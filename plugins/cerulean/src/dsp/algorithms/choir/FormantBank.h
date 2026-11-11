#pragma once

#include "dsp/common/ReverbBlocks.h"

namespace cerulean::dsp::choir
{

inline constexpr int numFormants = 4;
inline constexpr int numVowels = 5;

/** One vowel: formant centre frequencies (Hz), bandwidths (Hz) and relative levels (dB). */
struct Vowel
{
    std::array<float, numFormants> hz, bandwidth, db;
};

/**
    A-E-I-O-U for an adult (tenor-range) voice. F1-F3 are the Peterson & Barney (1952) male
    averages for /a/ (father), /e/ (bet), /i/ (beet), /o/ (bought, raised towards /o/), /u/ (boot);
    F4, the bandwidths and the levels follow the usual singing-voice formant tables (F1 strongest,
    higher formants falling off, /i/ with a strong F2-F3 cluster, /u/ almost F1 alone).
*/
inline constexpr std::array<Vowel, numVowels> vowelTable {{
    { { 730.0f, 1090.0f, 2440.0f, 3400.0f }, { 80.0f,  90.0f, 120.0f, 130.0f }, { 0.0f,  -4.0f, -16.0f, -24.0f } },  // A
    { { 530.0f, 1840.0f, 2480.0f, 3500.0f }, { 60.0f, 100.0f, 120.0f, 150.0f }, { 0.0f, -10.0f, -14.0f, -24.0f } },  // E
    { { 270.0f, 2290.0f, 3010.0f, 3600.0f }, { 50.0f, 100.0f, 150.0f, 180.0f }, { 0.0f, -14.0f, -18.0f, -26.0f } },  // I
    { { 480.0f,  820.0f, 2450.0f, 3300.0f }, { 70.0f,  80.0f, 120.0f, 130.0f }, { 0.0f,  -6.0f, -24.0f, -30.0f } },  // O
    { { 300.0f,  870.0f, 2240.0f, 3300.0f }, { 50.0f,  70.0f, 120.0f, 130.0f }, { 0.0f, -12.0f, -28.0f, -34.0f } },  // U
}};

/**
    Vowel at a continuous position 0..4 (A, E, I, O, U): frequencies and bandwidths interpolated
    in log frequency, levels in dB, so a morph glides like a tongue does rather than
    cross-fading two filter sets.
*/
inline Vowel vowelAt (float position) noexcept
{
    const auto x = std::clamp (position, 0.0f, (float) (numVowels - 1));
    const auto i = std::min ((int) x, numVowels - 2);
    const auto t = x - (float) i;
    const auto& a = vowelTable[(size_t) i];
    const auto& b = vowelTable[(size_t) i + 1];

    Vowel v {};
    for (size_t f = 0; f < (size_t) numFormants; ++f)
    {
        v.hz[f] = a.hz[f] * std::pow (b.hz[f] / a.hz[f], t);
        v.bandwidth[f] = a.bandwidth[f] * std::pow (b.bandwidth[f] / a.bandwidth[f], t);
        v.db[f] = a.db[f] + (b.db[f] - a.db[f]) * t;
    }
    return v;
}

/**
    Four parallel band-passes (Simper SVF, unity peak gain) for a stereo voice: both channels share
    the coefficients. Every coefficient (g, k and each formant's gain) ramps per sample from one
    sub-block's target to the next, so vowel morphs never step. The levels include a make-up gain
    that holds the bank's power gain on pink noise at 1 whatever the vowel, so morphing changes the
    colour, not the loudness.
*/
class FormantBank
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = (float) sampleRate;
        smoothing = 1.0f - std::exp (-1.0f / (0.002f * fs));   // 2 ms, after the linear ramps
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : bands)
            b = {};
        primed = false;
    }

    /** New targets: vowel scaled by `scale` (vocal-tract length), reached in numSamples. */
    void setTarget (const Vowel& v, float scale, int numSamples) noexcept
    {
        std::array<float, numFormants> g {}, k {}, level {};
        float pinkPower = 0.0f;
        for (size_t f = 0; f < (size_t) numFormants; ++f)
        {
            const auto hz = std::min (v.hz[f] * scale, 0.45f * fs);
            const auto bw = v.bandwidth[f] * scale;
            g[f] = std::tan (std::numbers::pi_v<float> * hz / fs);
            k[f] = bw / hz;
            level[f] = dbToGain (v.db[f]);
            // Pink noise through a unity-peak band-pass: power ~ (pi/2) (bw / hz) / ln (1000).
            pinkPower += level[f] * level[f] * k[f] * 0.2274f;
        }
        const auto makeup = 1.0f / std::sqrt (std::max (pinkPower, 1.0e-6f));

        const auto n = (float) std::max (numSamples, 1);
        for (size_t f = 0; f < (size_t) numFormants; ++f)
        {
            auto& b = bands[f];
            const auto gain = level[f] * makeup * k[f];   // k: unity peak gain from the BP output
            if (! primed)
            {
                b.g = b.sg = g[f];
                b.k = b.sk = k[f];
                b.gain = b.sgain = gain;
                b.dg = b.dk = b.dgain = 0.0f;
            }
            else
            {
                b.dg = (g[f] - b.g) / n;
                b.dk = (k[f] - b.k) / n;
                b.dgain = (gain - b.gain) / n;
            }
        }
        primed = true;
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        float l = 0.0f, r = 0.0f;
        for (auto& b : bands)
        {
            // Linear ramp per sub-block, then a short one-pole: the coefficient trajectory has
            // no corners at the sub-block boundaries (corners modulate the resonators audibly
            // when the vowel moves fast).
            b.g += b.dg;
            b.k += b.dk;
            b.gain += b.dgain;
            b.sg += (b.g - b.sg) * smoothing;
            b.sk += (b.k - b.sk) * smoothing;
            b.sgain += (b.gain - b.sgain) * smoothing;

            const auto a1 = 1.0f / (1.0f + b.sg * (b.sg + b.sk));
            const auto a2 = b.sg * a1;
            const auto a3 = b.sg * a2;

            l += b.sgain * tick (inL, b.l1, b.l2, a1, a2, a3);
            r += b.sgain * tick (inR, b.r1, b.r2, a1, a2, a3);
        }
        outL = l;
        outR = r;
    }

private:
    /** One SVF sample; returns the band-pass output. */
    static float tick (float x, float& ic1, float& ic2, float a1, float a2, float a3) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushTiny (2.0f * v1 - ic1);
        ic2 = flushTiny (2.0f * v2 - ic2);
        return v1;
    }

    struct Band
    {
        float g = 0.0f, k = 1.0f, gain = 0.0f, dg = 0.0f, dk = 0.0f, dgain = 0.0f;
        float sg = 0.0f, sk = 1.0f, sgain = 0.0f;   // smoothed
        float l1 = 0.0f, l2 = 0.0f, r1 = 0.0f, r2 = 0.0f;
    };

    float fs = 48000.0f, smoothing = 1.0f;
    std::array<Band, (size_t) numFormants> bands {};
    bool primed = false;
};

} // namespace cerulean::dsp::choir
