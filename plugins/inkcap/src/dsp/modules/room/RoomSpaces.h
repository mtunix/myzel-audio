#pragma once

#include "common/dsp/Blocks.h"
#include "common/dsp/DispersionCascade.h"
#include "common/dsp/EarlyReflections.h"
#include "common/dsp/Fdn.h"
#include "common/dsp/ReverbBlocks.h"

#include <array>
#include <cstdint>

/**
    Room's four spaces, built from the family's reverb blocks (common/dsp: EarlyReflections, Fdn,
    DispersionCascade, moved from Cerulean). Each is stereo in, stereo out (wet only), with the
    same small interface: prepare, reset, update once per sub-block (its size, Chaos and the
    tank's modulation), process per sample. See NOTES.md for the voicing.
*/
namespace inkcap::dsp::room
{

inline constexpr int tankLines = 8;      // 16 cost twice the CPU for no audible gain at these decays (NOTES.md)
using Modulation = std::array<float, (size_t) tankLines>;

/** Closet: a dense cluster of close reflections, smeared by three short allpasses; no tank. */
class ClosetSpace
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void update (int numSamples, double metres, float chaos, const Modulation&) noexcept;
    void process (float inL, float inR, float& outL, float& outR) noexcept;

    static constexpr float referenceMetres = 1.4f;

private:
    double fs = 48000.0;
    myzel::dsp::EarlyReflections<16> early;
    myzel::dsp::AllpassChain<3> smearL, smearR;
    myzel::dsp::OnePole bandwidthL, bandwidthR;
    myzel::dsp::Svf lowCutL, lowCutR;
};

/** How a tank space (Room, Hall) is built and voiced. */
struct TankVoicing
{
    int type = 1;                           // for rt60Of
    float referenceMetres = 5.0f;           // the size the lengths below are given at
    float minScale = 0.5f, maxScale = 2.0f;
    myzel::dsp::EarlyReflections<14>::Setup early;
    std::array<float, (size_t) tankLines> tankMs {};
    std::array<float, 4> diffuseLMs {}, diffuseRMs {};
    float diffusion = 0.6f;                 // the input allpasses' coefficient
    float earlyIntoTank = 1.0f;             // the reflections feed the late field (it grows out of them)
    float lowEnd = 1.0f;                    // RT60 (low) / RT60 (mid)
    myzel::dsp::HighDecay high { 0.45f, 3500.0f };
    float bandwidthHz = 9000.0f;            // into the space
    float lowCutHz = 110.0f;                // out of it (2nd order): small spaces add no boom
    float modulationMs = 0.12f;             // the tank's peak modulation at Chaos 0; three times that at Chaos 1
    float earlyGain = 1.0f, lateGain = 1.0f;
};

const TankVoicing& roomVoicing() noexcept;
const TankVoicing& hallVoicing() noexcept;

/** Room and Hall: early reflections, input diffusion and an 8-line FDN, modulated from the song position. */
class TankSpace
{
public:
    explicit TankSpace (const TankVoicing& v) : voicing (v) {}

    void prepare (double sampleRate);
    void reset() noexcept;
    void update (int numSamples, double metres, float chaos, const Modulation&) noexcept;
    void process (float inL, float inR, float& outL, float& outR) noexcept;

private:
    const TankVoicing& voicing;
    double fs = 48000.0;
    std::array<std::array<float, (size_t) tankLines>, 6> signs {};    // Hadamard rows 1, 2, 4, 7 (in), 3, 5 (out)
    myzel::dsp::EarlyReflections<14> early;
    myzel::dsp::AllpassChain<4> diffuserL, diffuserR;
    myzel::dsp::Fdn<tankLines> tank;
    myzel::dsp::OnePole bandwidthL, bandwidthR;
    myzel::dsp::Svf lowCutL, lowCutR;
};

/**
    Spring: three springs in a mono tank, each a loop of a stretched-allpass dispersion cascade
    (the chirp), a transit delay and a decay filter, after Cerulean's Spring, smaller. A gentle
    cubic drive into the tank (band-limited first, so it adds nothing above 3 x 5 kHz), the
    springs' pickups spread left and right, the cascade's images cut above its corner.
*/
class SpringTank
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void update (int numSamples, double decaySeconds, float chaos, const Modulation&) noexcept;
    void process (float inL, float inR, float& outL, float& outR) noexcept;

    static constexpr int springs = 3;
    static constexpr int stages = 64;

private:
    using Cascade = myzel::dsp::DispersionCascade<stages, stages>;

    double fs = 48000.0;
    Cascade cascade;
    Cascade::Lanes coefficients {};
    std::array<myzel::dsp::DelayLine, springs> transit;
    std::array<int, springs> transitSamples {};
    std::array<float, springs> loopSamples {};
    std::array<myzel::dsp::DecayFilter, springs> decay;
    myzel::dsp::Svf inputLowCut, inputTop;
    std::array<myzel::dsp::Svf, 2> imageA, imageB, lowCut;
};

} // namespace inkcap::dsp::room
