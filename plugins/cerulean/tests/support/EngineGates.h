#pragma once

/**
    The standard quality gates every algorithm must pass (see Engine.h and PLAN.md).

    Usage, in tests/algorithms/<name>/<Name>Tests.cpp:

        #include "support/EngineGates.h"
        #include "dsp/algorithms/hall/HallEngine.h"

        static cerulean::gates::Options hallOptions() { cerulean::gates::Options o; return o; }
        CERULEAN_ENGINE_GATES ("[hall]", cerulean::dsp::HallEngine, hallOptions())

    Each gate is its own TEST_CASE (tags: "[gates]" + the algorithm tag), so failures are named.
    Deviations from the defaults go in Options with a comment saying why.
*/

#include "analysis/ReverbAnalysis.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstring>

namespace cerulean::gates
{

struct Options
{
    dsp::EngineParams base {};             // the nominal settings the checks start from

    bool checkRt60 = true;
    double rt60Tolerance = 0.10;           // relative, at the mid band
    std::vector<float> rt60Decays { 1.0f, 2.5f, 6.0f };

    bool checkToneAndLowEnd = true;

    bool checkEchoDensity = true;
    double echoDensityTargetS = 0.15;      // normalised echo density reaches 0.9 by then

    double wetLevelDb = -3.0;              // steady pink noise at Decay 2 s
    double wetLevelToleranceDb = 4.0;

    double maxTailCorrelation = 0.6;       // |L/R correlation| of the tail
    double minMonoSumDb = -4.0;            // (L+R)/2 energy vs mean channel energy

    double sampleRateRt60Tolerance = 0.05;
    double sampleRateEchoDensityTolerance = 0.15;   // relative (or 5 ms, whichever is larger)

    // Clicks: crest of the 3rd difference (a broadband discontinuity detector) of a 220 Hz sine
    // through the engine while every control jumps around (with the chain's smoothing). It must
    // stay below maxClickCrestDb, or within clickHeadroomDb of the static render if that is
    // already higher (nonlinear algorithms make harmonics of their own).
    double maxClickCrestDb = -50.0;
    double clickHeadroomDb = 12.0;

    double cpuBudgetPercent = 3.0;         // one core, 48 kHz stereo ([.perf] only)

    /** Extra decay time to render beyond the nominal tail (e.g. for long pre-diffusion). */
    double extraTailSeconds = 0.0;
};

template <typename EngineType>
std::unique_ptr<EngineType> makeEngine (double sampleRate, uint32_t seed = 1)
{
    auto e = std::make_unique<EngineType> (seed);
    e->prepare (sampleRate, dsp::Engine::maxSubBlock);
    e->reset();
    return e;
}

inline analysis::Stereo noise (double sampleRate, double seconds, uint32_t seed = 99)
{
    analysis::Stereo s;
    s.sampleRate = sampleRate;
    s.left = analysis::pinkNoise ((size_t) (seconds * sampleRate), seed);
    s.right = analysis::pinkNoise ((size_t) (seconds * sampleRate), seed + 1);
    return s;
}

inline analysis::Stereo sine (double sampleRate, double seconds, double hz, float amplitude)
{
    analysis::Stereo s;
    s.sampleRate = sampleRate;
    const auto n = (size_t) (seconds * sampleRate);
    s.left.resize (n);
    s.right.resize (n);
    for (size_t i = 0; i < n; ++i)
        s.left[i] = s.right[i] = amplitude * (float) std::sin (2.0 * 3.14159265358979323846 * hz * (double) i / sampleRate);
    return s;
}

inline std::vector<float> monoSum (const analysis::Stereo& s)
{
    std::vector<float> m (s.size());
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = 0.5f * (s.left[i] + s.right[i]);
    return m;
}

/** Simulates the chain's 50 ms parameter smoothing (per sub-block) towards jumping targets. */
class SmoothedAutomation
{
public:
    SmoothedAutomation (dsp::EngineParams start, double sampleRate) : current (start), fs (sampleRate) {}

    dsp::EngineParams step (const dsp::EngineParams& t)
    {
        const auto k = 1.0f - std::exp (-(float) dsp::Engine::maxSubBlock / (0.05f * (float) fs));
        auto s = [k] (float& c, float target) { c += (target - c) * k; };
        current.decaySeconds = std::exp (std::log (current.decaySeconds) + (std::log (t.decaySeconds) - std::log (current.decaySeconds)) * k);   // log domain, like the chain
        s (current.sizeScale, t.sizeScale);
        s (current.mod, t.mod);
        s (current.modRate, t.modRate);
        s (current.tone, t.tone);
        s (current.lowEnd, t.lowEnd);
        s (current.diffusion, t.diffusion);
        s (current.paramA, t.paramA);
        s (current.paramB, t.paramB);
        s (current.echoTimeMs, t.echoTimeMs);
        s (current.freeze, t.freeze);
        return current;
    }

private:
    dsp::EngineParams current;
    double fs;
};

/** Random extreme corner of every control (seeded). */
inline dsp::EngineParams cornerParams (dsp::EngineParams base, dsp::Rng& rng)
{
    auto pick = [&rng] (float lo, float hi) { return rng.uniform() < 0.5f ? lo : hi; };
    base.sizeScale = pick (0.3f, 2.0f);
    base.mod = pick (0.0f, 1.0f);
    base.modRate = pick (0.25f, 4.0f);
    base.tone = pick (-1.0f, 1.0f);
    base.lowEnd = pick (0.5f, 2.0f);
    base.diffusion = pick (0.0f, 1.0f);
    base.paramA = pick (0.0f, 1.0f);
    base.paramB = pick (0.0f, 1.0f);
    base.echoTimeMs = pick (20.0f, 1500.0f);
    return base;
}

/** Peak of the 3rd difference (a broadband discontinuity detector) after `start`. */
inline double thirdDifferencePeak (const std::vector<float>& x, size_t start)
{
    double p = 0.0;
    for (size_t i = std::max<size_t> (start, 3); i < x.size(); ++i)
        p = std::max (p, std::abs ((double) x[i] - 3.0 * x[i - 1] + 3.0 * x[i - 2] - x[i - 3]));
    return p;
}

//==============================================================================
template <typename E>
void deterministic (const Options& o)
{
    auto e = makeEngine<E> (48000.0);
    const auto in = noise (48000.0, 0.5);
    const auto a = analysis::renderEngine (*e, o.base, in, 96000);
    e->reset();
    const auto b = analysis::renderEngine (*e, o.base, in, 96000);

    REQUIRE (std::memcmp (a.left.data(), b.left.data(), a.left.size() * sizeof (float)) == 0);
    REQUIRE (std::memcmp (a.right.data(), b.right.data(), a.right.size() * sizeof (float)) == 0);

    // A second instance with the same seed behaves identically too.
    auto e2 = makeEngine<E> (48000.0);
    const auto c = analysis::renderEngine (*e2, o.base, in, 96000);
    REQUIRE (std::memcmp (a.left.data(), c.left.data(), a.left.size() * sizeof (float)) == 0);
}

template <typename E>
void rt60MatchesDecay (const Options& o)
{
    if (! o.checkRt60)
        return;

    auto e = makeEngine<E> (48000.0);
    for (const auto decay : o.rt60Decays)
    {
        auto p = o.base;
        p.decaySeconds = decay;
        const auto ir = analysis::impulseResponse (*e, p, 48000.0, decay * 1.3 + 0.5 + o.extraTailSeconds);
        const auto measured = analysis::midBandRt60 (ir);
        INFO ("Decay " << decay << " s -> measured mid-band RT60 " << measured << " s");
        CHECK (std::abs (measured - decay) <= o.rt60Tolerance * decay);
    }
}

template <typename E>
void toneAndLowEndShapeBands (const Options& o)
{
    if (! o.checkToneAndLowEnd)
        return;

    auto e = makeEngine<E> (48000.0);
    auto ir = [&] (float lowEnd, float tone)
    {
        auto p = o.base;
        p.decaySeconds = 3.0f;
        p.lowEnd = lowEnd;
        p.tone = tone;
        return analysis::impulseResponse (*e, p, 48000.0, 4.4 + o.extraTailSeconds);
    };

    {
        const auto longLows = ir (2.0f, 0.0f);
        const auto shortLows = ir (0.5f, 0.0f);
        const auto ratioLong = analysis::bandRt60 (longLows, 125.0) / analysis::midBandRt60 (longLows);
        const auto ratioShort = analysis::bandRt60 (shortLows, 125.0) / analysis::midBandRt60 (shortLows);
        INFO ("RT60(125 Hz)/RT60(mid): Low End 2x -> " << ratioLong << ", 0.5x -> " << ratioShort);
        CHECK (ratioLong > 1.25);
        CHECK (ratioShort < 0.85);
    }
    {
        const auto dark = ir (1.0f, -1.0f);
        const auto bright = ir (1.0f, 1.0f);
        const auto darkHigh = analysis::bandRt60 (dark, 4000.0);
        const auto brightHigh = analysis::bandRt60 (bright, 4000.0);
        INFO ("RT60(4 kHz): dark " << darkHigh << " s, bright " << brightHigh << " s");
        CHECK (darkHigh < 0.6 * analysis::midBandRt60 (dark));
        CHECK (brightHigh > 1.5 * darkHigh);
    }
}

template <typename E>
void tailAlwaysDecays (const Options& o)
{
    auto e = makeEngine<E> (48000.0);
    dsp::Rng rng (2024);
    const auto in = noise (48000.0, 0.2);

    for (int trial = 0; trial < 16; ++trial)
    {
        auto p = cornerParams (o.base, rng);
        p.decaySeconds = 6.0f;
        e->reset();
        const auto out = analysis::renderEngine (*e, p, in, (size_t) (48000.0 * (9.0 + o.extraTailSeconds)));
        REQUIRE (analysis::allFinite (out));

        const auto early = analysis::rms (out.left, 48000, 96000) + analysis::rms (out.right, 48000, 96000);
        const auto lateStart = (size_t) (48000.0 * (8.0 + o.extraTailSeconds));
        const auto late = analysis::rms (out.left, lateStart) + analysis::rms (out.right, lateStart);
        INFO ("trial " << trial << ": size " << p.sizeScale << " mod " << p.mod << " rate " << p.modRate
              << " tone " << p.tone << " lowEnd " << p.lowEnd << " diff " << p.diffusion
              << " A " << p.paramA << " B " << p.paramB << " -> early " << analysis::toDb (early)
              << " dB, late " << analysis::toDb (late) << " dB");
        CHECK (analysis::toDb (late) < analysis::toDb (early) - 20.0);
    }
}

template <typename E>
void freezeHoldsTheTail (const Options& o)
{
    const double fs = 48000.0;
    auto e = makeEngine<E> (fs);
    const auto in = noise (fs, 1.0);
    SmoothedAutomation automation (o.base, fs);

    auto frozen = o.base;
    frozen.decaySeconds = 3.0f;
    frozen.freeze = 1.0f;
    auto running = frozen;
    running.freeze = 0.0f;

    const auto out = analysis::renderEngine (*e, running, in, (size_t) (fs * 62.0), [&] (size_t pos)
    {
        return automation.step (pos < (size_t) fs ? running : frozen);
    });

    REQUIRE (analysis::allFinite (out));

    auto windowDb = [&] (double t0)
    {
        const auto a = (size_t) (t0 * fs), b = (size_t) ((t0 + 1.0) * fs);
        return analysis::toDb (analysis::rms (out.left, a, b) + analysis::rms (out.right, a, b));
    };

    const auto reference = windowDb (3.0);
    double loudest = reference, quietest = reference;
    for (double t = 3.0; t <= 60.0; t += 1.0)
    {
        loudest = std::max (loudest, windowDb (t));
        quietest = std::min (quietest, windowDb (t));
    }

    // Per band too: a broadband check can't see the highs dying while the mids hold.
    auto bandDb = [&] (const std::vector<double>& band, double t0)
    {
        double sum = 0.0;
        const auto a = (size_t) (t0 * fs), b = (size_t) ((t0 + 1.0) * fs);
        for (size_t i = a; i < b; ++i)
            sum += band[i] * band[i];
        return 10.0 * std::log10 (std::max (sum / (double) (b - a), 1.0e-30));
    };
    for (const auto hz : { 250.0, 1000.0, 4000.0 })
    {
        const auto band = analysis::octaveBand (out.left, fs, hz);
        const auto drift = bandDb (band, 59.0) - bandDb (band, 3.0);
        INFO ("frozen " << hz << " Hz band drifts " << drift << " dB from 3 s to 60 s");
        CHECK (std::abs (drift) < 1.5);
    }

    INFO ("frozen level at 3 s " << reference << " dB, at 60 s " << windowDb (60.0) << " dB, range "
          << quietest << " .. " << loudest << " dB");
    CHECK (reference > -60.0);
    CHECK (std::abs (windowDb (60.0) - reference) < 1.0);
    CHECK (loudest - reference < 1.0);
    CHECK (reference - quietest < 1.0);
}

template <typename E>
void partialFreezeStillDecays (const Options& o)
{
    auto e = makeEngine<E> (48000.0);
    auto p = o.base;
    p.decaySeconds = 2.0f;
    p.freeze = 0.5f;
    const auto out = analysis::renderEngine (*e, p, noise (48000.0, 0.3), (size_t) (48000 * 12));
    REQUIRE (analysis::allFinite (out));
    const auto early = analysis::rms (out.left, 24000, 72000);
    const auto late = analysis::rms (out.left, 48000 * 11);
    INFO ("freeze 0.5: early " << analysis::toDb (early) << " dB, late " << analysis::toDb (late) << " dB");
    CHECK (late < early);
}

template <typename E>
void wetLevelIsComparable (const Options& o)
{
    auto e = makeEngine<E> (48000.0);
    auto p = o.base;
    p.decaySeconds = 2.0f;
    const auto in = noise (48000.0, 6.0);
    const auto out = analysis::renderEngine (*e, p, in, in.size());
    const auto inDb = analysis::toDb (0.5 * (analysis::rms (in.left, 48000 * 3) + analysis::rms (in.right, 48000 * 3)));
    const auto outDb = analysis::toDb (0.5 * (analysis::rms (out.left, 48000 * 3) + analysis::rms (out.right, 48000 * 3)));
    INFO ("wet level relative to input: " << (outDb - inDb) << " dB (target " << o.wetLevelDb << " +- "
          << o.wetLevelToleranceDb << ")");
    CHECK (std::abs ((outDb - inDb) - o.wetLevelDb) <= o.wetLevelToleranceDb);
}

template <typename E>
void stereoImage (const Options& o)
{
    auto e = makeEngine<E> (48000.0);
    auto p = o.base;
    p.decaySeconds = 3.0f;
    const auto ir = analysis::impulseResponse (*e, p, 48000.0, 2.5 + o.extraTailSeconds);
    const auto start = (size_t) (48000.0 * (0.2 + o.extraTailSeconds)), end = (size_t) (48000.0 * (2.0 + o.extraTailSeconds));

    const auto rho = analysis::correlation (ir.left, ir.right, start, end);
    const auto mono = monoSum (ir);
    const auto monoDb = analysis::toDb (analysis::rms (mono, start, end))
                      - analysis::toDb (std::sqrt (0.5 * (std::pow (analysis::rms (ir.left, start, end), 2.0)
                                                         + std::pow (analysis::rms (ir.right, start, end), 2.0))));
    INFO ("tail L/R correlation " << rho << ", mono sum " << monoDb << " dB");
    CHECK (std::abs (rho) < o.maxTailCorrelation);
    CHECK (monoDb > o.minMonoSumDb);
}

template <typename E>
void echoDensityBuildsUp (const Options& o)
{
    if (! o.checkEchoDensity)
        return;

    auto e = makeEngine<E> (48000.0);
    auto p = o.base;
    p.decaySeconds = 3.0f;
    const auto ir = analysis::impulseResponse (*e, p, 48000.0, 1.5 + o.extraTailSeconds);
    const auto t = analysis::echoDensityTime (ir.left, 48000.0);
    INFO ("echo density reaches 0.9 at " << t << " s (target <= " << o.echoDensityTargetS << " s)");
    CHECK (t > 0.0);
    CHECK (t <= o.echoDensityTargetS);
}

template <typename E>
void sampleRateInvariant (const Options& o)
{
    auto measure = [&] (double fs)
    {
        auto e = makeEngine<E> (fs);
        auto p = o.base;
        p.decaySeconds = 2.5f;
        const auto ir = analysis::impulseResponse (*e, p, fs, 3.6 + o.extraTailSeconds);
        const auto band = analysis::decimateTo48k (ir.left, fs);
        return std::pair { analysis::midBandRt60 (ir), analysis::echoDensityTime (band, fs > 48000.0 && band.size() < ir.size() ? 48000.0 : fs) };
    };

    const auto [rtRef, edRef] = measure (48000.0);
    for (const auto fs : { 44100.0, 96000.0, 192000.0 })
    {
        const auto [rt, ed] = measure (fs);
        INFO (fs << " Hz: RT60 " << rt << " s (48 kHz: " << rtRef << "), echo density 0.9 at " << ed
              << " s (48 kHz: " << edRef << ")");
        if (o.checkRt60)
            CHECK (std::abs (rt - rtRef) <= o.sampleRateRt60Tolerance * rtRef);
        if (o.checkEchoDensity)
            CHECK (std::abs (ed - edRef) <= std::max (0.005, o.sampleRateEchoDensityTolerance * edRef));
    }
}

template <typename E>
void survivesSquareWaves (const Options& o)
{
    const double fs = 48000.0;
    auto e = makeEngine<E> (fs);
    dsp::Rng rng (7);

    analysis::Stereo square;
    square.sampleRate = fs;
    square.left.resize ((size_t) (fs * 2.0));
    for (size_t i = 0; i < square.left.size(); ++i)
        square.left[i] = ((i / 240) % 2) ? 1.0f : -1.0f;   // 100 Hz, 0 dBFS
    square.right = square.left;

    for (int trial = 0; trial < 8; ++trial)
    {
        auto p = cornerParams (o.base, rng);
        p.decaySeconds = trial % 2 ? 60.0f : 0.1f;
        auto frozen = p;
        frozen.freeze = 1.0f;
        SmoothedAutomation automation (p, fs);
        e->reset();

        const auto out = analysis::renderEngine (*e, p, square, (size_t) (fs * 3.0), [&] (size_t pos)
        {
            // Toggle freeze every half second while slamming the input.
            return automation.step (((pos / 24000) % 2) ? frozen : p);
        });

        INFO ("trial " << trial << " peak " << analysis::peak (out));
        REQUIRE (analysis::allFinite (out));
        CHECK (analysis::peak (out) < 64.0);
    }
}

template <typename E>
void automationIsClickFree (const Options& o)
{
    const double fs = 48000.0;
    const auto in = sine (fs, 6.0, 220.0, 0.25f);

    auto e = makeEngine<E> (fs);
    const auto still = analysis::renderEngine (*e, o.base, in, in.size());

    e->reset();
    dsp::Rng rng (31);
    dsp::EngineParams target = o.base;
    SmoothedAutomation automation (o.base, fs);
    size_t nextJump = 0;

    const auto moving = analysis::renderEngine (*e, o.base, in, in.size(), [&] (size_t pos)
    {
        if (pos >= nextJump)
        {
            target = o.base;
            target.decaySeconds = 0.3f + 8.0f * rng.uniform();
            target.sizeScale = 0.3f + 1.7f * rng.uniform();
            target.mod = rng.uniform();
            target.tone = rng.bipolar();
            target.lowEnd = 0.5f + 1.5f * rng.uniform();
            target.diffusion = rng.uniform();
            target.paramA = rng.uniform();
            target.paramB = rng.uniform();
            target.echoTimeMs = 50.0f + 900.0f * rng.uniform();
            nextJump += 12000;
        }
        return automation.step (target);
    });

    REQUIRE (analysis::allFinite (moving));
    const auto start = (size_t) fs;   // skip the onset
    const auto stillPeak = std::max (thirdDifferencePeak (still.left, start), 1.0e-6)
                         / std::max (analysis::rms (still.left, start), 1.0e-9);
    const auto movingPeak = thirdDifferencePeak (moving.left, start)
                          / std::max (analysis::rms (moving.left, start), 1.0e-9);
    INFO ("3rd-difference crest: static " << analysis::toDb (stillPeak) << " dB, automated "
          << analysis::toDb (movingPeak) << " dB");
    CHECK (analysis::toDb (movingPeak) < std::max (o.maxClickCrestDb, analysis::toDb (stillPeak) + o.clickHeadroomDb));
}

template <typename E>
void cpuReport (const Options& o, const char* name)
{
    const double fs = 48000.0;
    auto e = makeEngine<E> (fs);
    const auto in = noise (fs, 20.0);
    auto p = o.base;
    p.decaySeconds = 4.0f;

    const auto t0 = std::chrono::steady_clock::now();
    const auto out = analysis::renderEngine (*e, p, in, in.size());
    const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const auto percent = 100.0 * seconds / 20.0;

    WARN (name << ": " << percent << " % of one core (48 kHz stereo, budget " << o.cpuBudgetPercent << " %)");
    CHECK (analysis::allFinite (out));
    CHECK (percent <= o.cpuBudgetPercent);
}

} // namespace cerulean::gates

#define CERULEAN_ENGINE_GATES(TAG, ENGINE, OPTIONS)                                                              \
    TEST_CASE (#ENGINE ": deterministic after reset", "[gates]" TAG) { cerulean::gates::deterministic<ENGINE> (OPTIONS); } \
    TEST_CASE (#ENGINE ": RT60 matches Decay", "[gates]" TAG) { cerulean::gates::rt60MatchesDecay<ENGINE> (OPTIONS); } \
    TEST_CASE (#ENGINE ": Low End and Tone shape the bands", "[gates]" TAG) { cerulean::gates::toneAndLowEndShapeBands<ENGINE> (OPTIONS); } \
    TEST_CASE (#ENGINE ": tail always decays", "[gates]" TAG) { cerulean::gates::tailAlwaysDecays<ENGINE> (OPTIONS); } \
    TEST_CASE (#ENGINE ": freeze holds the tail", "[gates]" TAG) { cerulean::gates::freezeHoldsTheTail<ENGINE> (OPTIONS); } \
    TEST_CASE (#ENGINE ": partial freeze still decays", "[gates]" TAG) { cerulean::gates::partialFreezeStillDecays<ENGINE> (OPTIONS); } \
    TEST_CASE (#ENGINE ": wet level is comparable", "[gates]" TAG) { cerulean::gates::wetLevelIsComparable<ENGINE> (OPTIONS); } \
    TEST_CASE (#ENGINE ": stereo image", "[gates]" TAG) { cerulean::gates::stereoImage<ENGINE> (OPTIONS); } \
    TEST_CASE (#ENGINE ": echo density builds up", "[gates]" TAG) { cerulean::gates::echoDensityBuildsUp<ENGINE> (OPTIONS); } \
    TEST_CASE (#ENGINE ": sample-rate invariant", "[gates]" TAG) { cerulean::gates::sampleRateInvariant<ENGINE> (OPTIONS); } \
    TEST_CASE (#ENGINE ": survives square waves", "[gates]" TAG) { cerulean::gates::survivesSquareWaves<ENGINE> (OPTIONS); } \
    TEST_CASE (#ENGINE ": automation is click-free", "[gates]" TAG) { cerulean::gates::automationIsClickFree<ENGINE> (OPTIONS); } \
    TEST_CASE (#ENGINE ": CPU", "[.perf]" TAG) { cerulean::gates::cpuReport<ENGINE> (OPTIONS, #ENGINE); }
