#include "dsp/modules/room/RoomSpaces.h"

#include "dsp/modules/room/RoomVoicing.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace inkcap::dsp::room
{

using namespace myzel::dsp;

namespace
{
    constexpr int closetType = 0, roomType = 1, hallType = 3;

    // Closet: 1.4 m at scale 1. First reflection 1 ms (0.35 m of extra path), the cluster over by
    // 9 ms; the smear makes it dense without a tail.
    constexpr EarlyReflections<16>::Setup closetEarly { .firstMs = 1.0f, .lastMs = 9.0f, .maxScale = 1.9f,
                                                        .decayShape = 2.0f, .crossAmount = 0.4f, .seed = 0x1C105E7u };
    constexpr std::array<float, 3> closetSmearL { 0.53f, 1.19f, 2.11f };
    constexpr std::array<float, 3> closetSmearR { 0.61f, 1.31f, 2.29f };
    constexpr float closetSmear = 0.5f;
    constexpr float closetMinScale = 0.55f, closetMaxScale = 1.85f;
}

const TankVoicing& roomVoicing() noexcept
{
    // 5 m at scale 1: reflections from 3 ms, the late field from an 8-line tank of 8.5-30 ms,
    // half of it made of the reflections.
    static const TankVoicing v {
        .type = roomType,
        .referenceMetres = 5.0f,
        .minScale = 0.5f,
        .maxScale = 2.0f,
        .early = { .firstMs = 3.0f, .lastMs = 32.0f, .maxScale = 2.05f, .decayShape = 2.4f, .crossAmount = 0.35f, .seed = 0x5E7700Du },
        .tankMs = { 8.53f, 10.91f, 13.37f, 16.13f, 19.07f, 22.31f, 25.79f, 29.63f },
        .diffuseLMs = { 1.13f, 2.27f, 3.71f, 5.39f },
        .diffuseRMs = { 1.37f, 2.53f, 4.07f, 5.83f },
        .diffusion = 0.6f,
        .earlyIntoTank = 1.0f,
        .lowEnd = 0.85f,
        .high = { 0.45f, 3500.0f },
        .bandwidthHz = 9000.0f,
        .lowCutHz = 110.0f,
        .modulationMs = 0.12f,
        .earlyGain = 1.0f,
        .lateGain = 1.0f,
    };
    return v;
}

const TankVoicing& hallVoicing() noexcept
{
    // 20 m at scale 1: an 8-line tank of 41-121 ms (Cerulean's hall span), reflections from 9 ms, darker.
    static const TankVoicing v {
        .type = hallType,
        .referenceMetres = 20.0f,
        .minScale = 0.5f,
        .maxScale = 2.0f,
        .early = { .firstMs = 9.0f, .lastMs = 85.0f, .maxScale = 2.05f, .decayShape = 2.2f, .crossAmount = 0.3f, .seed = 0x4A11u },
        .tankMs = { 41.27f, 48.71f, 57.13f, 66.19f, 76.93f, 89.29f, 103.67f, 121.07f },
        .diffuseLMs = { 3.07f, 5.27f, 8.93f, 13.69f },
        .diffuseRMs = { 3.71f, 6.13f, 9.67f, 14.87f },
        .diffusion = 0.65f,
        .earlyIntoTank = 0.5f,
        .lowEnd = 1.0f,
        .high = { 0.4f, 3000.0f },
        .bandwidthHz = 7500.0f,
        .lowCutHz = 90.0f,
        .modulationMs = 0.3f,
        .earlyGain = 0.7f,
        .lateGain = 1.0f,
    };
    return v;
}

//==============================================================================
void ClosetSpace::prepare (double sampleRate)
{
    fs = sampleRate;
    early.prepare (fs, closetEarly);
    smearL.prepare (fs, closetSmearL, closetMaxScale);
    smearR.prepare (fs, closetSmearR, closetMaxScale);
    bandwidthL.setCutoff (7000.0f, fs);
    bandwidthR.setCutoff (7000.0f, fs);
    lowCutL.setCoefficients (160.0f, 0.7071f, fs);
    lowCutR.setCoefficients (160.0f, 0.7071f, fs);
    reset();
}

void ClosetSpace::reset() noexcept
{
    early.reset();
    smearL.reset();
    smearR.reset();
    bandwidthL.reset();
    bandwidthR.reset();
    lowCutL.reset();
    lowCutR.reset();
}

void ClosetSpace::update (int numSamples, double metres, float, const Modulation&) noexcept
{
    const auto scale = std::clamp ((float) metres / referenceMetres, closetMinScale, closetMaxScale);
    early.update (numSamples, scale);
    smearL.setScale (scale);
    smearR.setScale (scale);
    smearL.advance (numSamples);
    smearR.advance (numSamples);
}

void ClosetSpace::process (float inL, float inR, float& outL, float& outR) noexcept
{
    float eL, eR;
    early.process (bandwidthL.lowpass (inL), bandwidthR.lowpass (inR), eL, eR);
    outL = lowCutL.highpass (0.55f * eL + 0.85f * smearL.process (eL, closetSmear));
    outR = lowCutR.highpass (0.55f * eR + 0.85f * smearR.process (eR, closetSmear));
}

//==============================================================================
void TankSpace::prepare (double sampleRate)
{
    fs = sampleRate;
    const auto& v = voicing;
    constexpr int rows[] = { 1, 2, 4, 7, 3, 5 };
    for (size_t r = 0; r < signs.size(); ++r)
        for (int i = 0; i < tankLines; ++i)
            signs[r][(size_t) i] = hadamardSign (rows[r], i);
    early.prepare (fs, v.early);
    diffuserL.prepare (fs, v.diffuseLMs, v.maxScale);
    diffuserR.prepare (fs, v.diffuseRMs, v.maxScale);
    tank.prepare (fs, { .delayMs = v.tankMs, .maxScale = v.maxScale, .maxModMs = 3.0f * v.modulationMs + 0.05f,
                        .nominalModHz = 0.5f, .seed = 1u, .mixing = Fdn<tankLines>::Mixing::hadamard });
    bandwidthL.setCutoff (v.bandwidthHz, fs);
    bandwidthR.setCutoff (v.bandwidthHz, fs);
    lowCutL.setCoefficients (v.lowCutHz, 0.7071f, fs);
    lowCutR.setCoefficients (v.lowCutHz, 0.7071f, fs);
    reset();
}

void TankSpace::reset() noexcept
{
    early.reset();
    diffuserL.reset();
    diffuserR.reset();
    tank.reset();
    bandwidthL.reset();
    bandwidthR.reset();
    lowCutL.reset();
    lowCutR.reset();
}

void TankSpace::update (int numSamples, double metres, float chaos, const Modulation& modulation) noexcept
{
    const auto& v = voicing;
    const auto scale = std::clamp ((float) metres / v.referenceMetres, v.minScale, v.maxScale);
    early.update (numSamples, scale);
    diffuserL.setScale (scale);
    diffuserR.setScale (scale);
    diffuserL.advance (numSamples);
    diffuserR.advance (numSamples);

    Fdn<tankLines>::Controls c;
    c.scale = scale;
    c.rt60 = (float) rt60Of (v.type, metres);
    c.lowEnd = v.lowEnd;
    c.high = v.high;
    c.modDepthMs = v.modulationMs * (1.0f + 2.0f * std::clamp (chaos, 0.0f, 1.0f));
    tank.update (numSamples, c, modulation);
}

void TankSpace::process (float inL, float inR, float& outL, float& outR) noexcept
{
    const auto& v = voicing;
    const auto l = bandwidthL.lowpass (inL), r = bandwidthR.lowpass (inR);

    float eL, eR;
    early.process (l, r, eL, eR);

    const auto inject = 1.0f / std::sqrt ((float) tankLines);
    const auto dl = diffuserL.process (l, v.diffusion) * inject;
    const auto dr = diffuserR.process (r, v.diffusion) * inject;
    const auto el = eL * inject * v.earlyIntoTank, er = eR * inject * v.earlyIntoTank;

    // Four orthogonal injection patterns (Hadamard rows): the late field gets the power sum of
    // the dry and the reflections, not one comb imprinted on the whole tail.
    std::array<float, (size_t) tankLines> in, y;
    for (size_t i = 0; i < (size_t) tankLines; ++i)
        in[i] = signs[0][i] * dl + signs[1][i] * dr + signs[2][i] * el + signs[3][i] * er;

    tank.process (in.data(), y.data());

    float sumL = 0.0f, sumR = 0.0f;
    for (size_t i = 0; i < (size_t) tankLines; ++i)
    {
        sumL += signs[4][i] * y[i];
        sumR += signs[5][i] * y[i];
    }

    outL = lowCutL.highpass (v.earlyGain * eL + v.lateGain * inject * sumL);
    outR = lowCutR.highpass (v.earlyGain * eR + v.lateGain * inject * sumR);
}

//==============================================================================
namespace
{
    constexpr double dispersionCornerHz = 4600.0;                       // where the cascade's chirp tops out
    constexpr std::array<double, SpringTank::springs> roundTripMs { 61.0, 71.0, 83.0 };
    constexpr std::array<float, SpringTank::springs> dispersion { 0.52f, 0.48f, 0.56f };
    constexpr float springLowEnd = 0.7f;                                // the bass dies first in a tank
    constexpr HighDecay springHigh { 0.5f, 2800.0f };

    float drive (float x) noexcept
    {
        const auto c = std::clamp (0.8f * x, -1.0f, 1.0f);
        return (1.5f * c - 0.5f * c * c * c) / 1.2f;     // unity for small signals, -2 dB at full scale
    }
}

void SpringTank::prepare (double sampleRate)
{
    fs = sampleRate;
    const auto stretch = std::max (1, (int) std::lround (fs / (2.0 * dispersionCornerHz)));
    cascade.prepare (stretch);
    coefficients = { dispersion[0], dispersion[1], dispersion[2], 0.0f };

    const auto omega1k = (float) (2.0 * std::numbers::pi * 1000.0 / fs);
    for (int s = 0; s < springs; ++s)
    {
        // The transit makes up the round trip the cascade's own delay leaves (measured at DC, where the bass echoes).
        const auto a = dispersion[(size_t) s];
        const auto cascadeDc = (double) stages * stretch * (1.0 - a) / (1.0 + a);
        transitSamples[(size_t) s] = std::max (1, (int) std::lround (roundTripMs[(size_t) s] * 0.001 * fs - cascadeDc));
        transit[(size_t) s].setMaxDelay (transitSamples[(size_t) s] + 2);
        loopSamples[(size_t) s] = (float) transitSamples[(size_t) s] + Cascade::groupDelay (a, a, stretch, omega1k);
        decay[(size_t) s].setCrossovers (decayLowCrossoverHz, springHigh.crossoverHz, fs);
    }

    const auto imageHz = (float) std::min (0.92 * fs / (2.0 * stretch), 0.45 * fs);
    inputLowCut.setCoefficients (120.0f, 0.7071f, fs);
    inputTop.setCoefficients (5000.0f, 0.7071f, fs);
    for (int c = 0; c < 2; ++c)
    {
        imageA[(size_t) c].setCoefficients (imageHz, 0.5412f, fs);      // 4th-order Butterworth
        imageB[(size_t) c].setCoefficients (imageHz, 1.3066f, fs);
        lowCut[(size_t) c].setCoefficients (140.0f, 0.7071f, fs);
    }
    reset();
}

void SpringTank::reset() noexcept
{
    cascade.reset();
    for (int s = 0; s < springs; ++s)
    {
        transit[(size_t) s].reset();
        decay[(size_t) s].reset();
    }
    inputLowCut.reset();
    inputTop.reset();
    for (int c = 0; c < 2; ++c)
    {
        imageA[(size_t) c].reset();
        imageB[(size_t) c].reset();
        lowCut[(size_t) c].reset();
    }
}

void SpringTank::update (int numSamples, double decaySeconds, float, const Modulation&) noexcept
{
    for (int s = 0; s < springs; ++s)
    {
        const auto g = computeBandGains (loopSamples[(size_t) s], (float) decaySeconds, springLowEnd, springHigh.ratio, 0.0f, (float) fs);
        decay[(size_t) s].setGains (g.low, g.mid, g.high, numSamples);
    }
}

void SpringTank::process (float inL, float inR, float& outL, float& outR) noexcept
{
    const auto x = drive (inputTop.lowpass (inputLowCut.highpass (0.5f * (inL + inR))));

    std::array<float, springs> near, far;
    Cascade::Lanes u { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int s = 0; s < springs; ++s)
    {
        const auto& line = transit[(size_t) s];
        near[(size_t) s] = line.read (transitSamples[(size_t) s]);
        far[(size_t) s] = line.read (std::max (1, transitSamples[(size_t) s] / 2));
        u[(size_t) s] = 0.6f * x + decay[(size_t) s].process (near[(size_t) s]);
    }

    cascade.process (u, coefficients, coefficients);
    for (int s = 0; s < springs; ++s)
        transit[(size_t) s].write (flushTiny (u[(size_t) s]));

    // Two pickups per spring: the far end first hears a wave half a round trip in (the tank's
    // pre-delay), the near end a whole one. The outer springs lean to their sides, the middle
    // one's far end is in both: a narrow image, as a tank's.
    const float side[] = { far[0] + 0.7f * far[1] + 0.35f * near[2], far[2] + 0.7f * far[1] + 0.35f * near[0] };
    float* out[] = { &outL, &outR };
    for (int c = 0; c < 2; ++c)
        *out[c] = lowCut[(size_t) c].highpass (imageB[(size_t) c].lowpass (imageA[(size_t) c].lowpass (side[c])));
}

} // namespace inkcap::dsp::room
