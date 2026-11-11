#include "dsp/algorithms/room/RoomEngine.h"
#include "support/EngineGates.h"

#include <numbers>

namespace
{
    cerulean::gates::Options roomOptions()
    {
        cerulean::gates::Options o;
        // A room's reflections stay distinct for its first ~20 ms (that is its character), then
        // fuse. The perceptual mixing time of the 1x room (Lindau et al. 2012: 20 V/S + 12 ms) is
        // ~28 ms; the measured 0.9 crossing is ~32 ms. Hold it to 60 ms: slower than the plate
        // (instantly diffuse, <= 50 ms), never as slow as a smeary hall-style build.
        o.echoDensityTargetS = 0.06;
        return o;
    }

    using namespace cerulean;

    double energy (const analysis::Stereo& s)
    {
        double e = 0.0;
        for (size_t i = 0; i < s.size(); ++i)
            e += (double) s.left[i] * s.left[i] + (double) s.right[i] * s.right[i];
        return e;
    }

    /**
        Early and late energy, separated by superposition: Early/Late is an equal-power law between
        theta = 0.2 and pi/2 - 0.2 (NOTES.md), so the A = 0 and A = 1 renders are two known
        mixtures of the same two (nearly orthogonal) signals.
    */
    std::pair<double, double> earlyAndLate (dsp::RoomEngine& e, dsp::EngineParams p)
    {
        p.paramA = 0.0f;
        const auto e0 = energy (analysis::impulseResponse (e, p, 48000.0, p.decaySeconds * 1.5 + 0.3));
        p.paramA = 1.0f;
        const auto e1 = energy (analysis::impulseResponse (e, p, 48000.0, p.decaySeconds * 1.5 + 0.3));
        const auto c2 = std::pow (std::cos (0.2), 2.0), s2 = std::pow (std::sin (0.2), 2.0);
        const auto det = c2 * c2 - s2 * s2;
        return { (c2 * e0 - s2 * e1) / det, (c2 * e1 - s2 * e0) / det };
    }

    /**
        Flutter detector: the largest normalised autocorrelation, over lags of 8-80 ms (flutter
        periods of 1.4-14 m wall spacings), of the 1 ms RMS envelope in [t0, t1) after removing
        its decay (log-linear fit) and its slow shape (21 ms moving average). Periodic echoes stand
        out as a peak; a diffuse tail, however its envelope wanders, stays near 0.
    */
    double flutter (const std::vector<float>& x, double fs, double t0, double t1)
    {
        const auto hop = (size_t) (0.001 * fs);
        std::vector<double> env;
        for (auto i = (size_t) (t0 * fs); i + hop <= (size_t) (t1 * fs); i += hop)
            env.push_back (analysis::rms (x, i, i + hop));

        const auto n = (double) env.size();
        double st = 0, sy = 0, stt = 0, sty = 0;
        for (size_t i = 0; i < env.size(); ++i)
        {
            const auto y = std::log (std::max (env[i], 1.0e-12));
            st += (double) i; sy += y; stt += (double) i * (double) i; sty += (double) i * y;
        }
        const auto slope = (n * sty - st * sy) / (n * stt - st * st);
        const auto icpt = (sy - slope * st) / n;
        for (size_t i = 0; i < env.size(); ++i)
            env[i] /= std::exp (icpt + slope * (double) i);

        std::vector<double> v;
        for (size_t i = 10; i + 10 < env.size(); ++i)
        {
            double mean = 0.0;
            for (size_t k = i - 10; k <= i + 10; ++k)
                mean += env[k] / 21.0;
            v.push_back (env[i] - mean);
        }

        double var = 0.0;
        for (const auto a : v)
            var += a * a;
        double best = 0.0;
        for (size_t lag = 8; lag <= 80 && lag < v.size(); ++lag)
        {
            double r = 0.0;
            for (size_t i = lag; i < v.size(); ++i)
                r += v[i] * v[i - lag];
            best = std::max (best, r / var);
        }
        return best;
    }
}

CERULEAN_ENGINE_GATES ("[room]", cerulean::dsp::RoomEngine, roomOptions())

TEST_CASE ("Room: Early/Late moves the balance, and the middle is even at every size", "[room]")
{
    auto e = gates::makeEngine<dsp::RoomEngine> (48000.0);
    auto p = dsp::EngineParams {};
    p.decaySeconds = 1.0f;   // "Even" is defined at a typical room RT60

    for (const auto size : { 0.3f, 1.0f, 2.0f })
    {
        p.sizeScale = size;
        const auto [early, late] = earlyAndLate (*e, p);
        const auto c2 = std::pow (std::cos (0.2), 2.0), s2 = std::pow (std::sin (0.2), 2.0);
        const auto evenDb = analysis::toDb (std::sqrt (late / early));
        const auto earlyDominanceDb = 10.0 * std::log10 ((c2 * early) / (s2 * late));
        const auto lateDominanceDb = 10.0 * std::log10 ((c2 * late) / (s2 * early));
        INFO ("Size " << size << ": late re early at A = 0.5: " << evenDb << " dB; early over late at A = 0: "
              << earlyDominanceDb << " dB; late over early at A = 1: " << lateDominanceDb << " dB");
        CHECK (std::abs (evenDb) < 2.0);
        CHECK (earlyDominanceDb > 10.0);
        CHECK (lateDominanceDb > 10.0);
    }
}

TEST_CASE ("Room: hard walls are brighter, soft walls darker, at a similar level", "[room]")
{
    auto e = gates::makeEngine<dsp::RoomEngine> (48000.0);
    auto p = dsp::EngineParams {};
    p.decaySeconds = 2.0f;

    // The reflections (A = 0: early dominates): high band (> 4 kHz) re broadband, first 40 ms.
    auto reflectionBrightness = [&] (float walls)
    {
        p.paramA = 0.0f;
        p.paramB = walls;
        const auto ir = analysis::impulseResponse (*e, p, 48000.0, 0.1);
        return analysis::toDb (analysis::highBandRms (ir.left, 48000.0, 4000.0, 0, 1920))
             - analysis::toDb (analysis::rms (ir.left, 0, 1920));
    };
    const auto softEr = reflectionBrightness (0.0f), hardEr = reflectionBrightness (1.0f);
    INFO ("reflections > 4 kHz re broadband: soft " << softEr << " dB, hard " << hardEr << " dB");
    CHECK (hardEr > softEr + 6.0);

    // The tank: high-band decay.
    auto highRatio = [&] (float walls)
    {
        p.paramA = 0.5f;
        p.paramB = walls;
        const auto ir = analysis::impulseResponse (*e, p, 48000.0, 3.0);
        return analysis::bandRt60 (ir, 8000.0) / analysis::midBandRt60 (ir);
    };
    const auto softRt = highRatio (0.0f), hardRt = highRatio (1.0f);
    INFO ("RT60(8 kHz)/RT60(mid): soft " << softRt << ", hard " << hardRt);
    CHECK (hardRt > 1.8 * softRt);

    // Softer, not just quieter: steady pink noise within 3 dB across the whole travel.
    auto level = [&] (float walls)
    {
        auto q = p;
        q.paramA = 0.5f;
        q.paramB = walls;
        e->reset();
        const auto in = gates::noise (48000.0, 5.0);
        const auto out = analysis::renderEngine (*e, q, in, in.size());
        return analysis::toDb (analysis::rms (out.left, 96000) + analysis::rms (out.right, 96000));
    };
    const auto soft = level (0.0f), mid = level (0.5f), hard = level (1.0f);
    INFO ("pink noise level: soft " << soft << " dB, middle " << mid << " dB, hard " << hard << " dB");
    CHECK (std::abs (soft - mid) < 3.0);
    CHECK (std::abs (hard - mid) < 3.0);
}

TEST_CASE ("Room: hard walls do not flutter", "[room]")
{
    // The detector itself: a diffuse tail with periodic 17 ms echoes ~10 dB above it must trip it.
    {
        dsp::Rng rng (3);
        std::vector<float> x ((size_t) (0.6 * 48000.0));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = rng.bipolar() * std::exp (-3.45f * (float) i / 48000.0f);
        for (int k = 1; k * 816 + 24 < (int) x.size(); ++k)
            for (int j = 0; j < 24; ++j)
                x[(size_t) (k * 816 + j)] += 4.0f * rng.bipolar() * std::exp (-3.45f * (float) (k * 816) / 48000.0f);
        const auto synthetic = flutter (x, 48000.0, 0.01, 0.5);
        INFO ("synthetic 17 ms flutter: " << synthetic);
        REQUIRE (synthetic > 0.5);
    }

    auto e = gates::makeEngine<dsp::RoomEngine> (48000.0);
    auto p = dsp::EngineParams {};
    p.decaySeconds = 2.0f;
    p.paramB = 1.0f;

    for (const auto size : { 0.3f, 1.0f, 2.0f })
        for (const auto mod : { 0.0f, 0.35f })
        {
            p.sizeScale = size;
            p.mod = mod;
            const auto ir = analysis::impulseResponse (*e, p, 48000.0, 0.6);
            const auto f = std::max (flutter (ir.left, 48000.0, 0.01, 0.5), flutter (ir.right, 48000.0, 0.01, 0.5));
            INFO ("hard walls, Size " << size << ", Mod " << mod << ": flutter " << f);
            CHECK (f < 0.3);
        }
}

TEST_CASE ("Room: Size scales the reflection pattern from booth to live room", "[room]")
{
    auto e = gates::makeEngine<dsp::RoomEngine> (48000.0);
    auto p = dsp::EngineParams {};
    p.decaySeconds = 0.5f;
    p.paramA = 0.0f;

    // Time by which half of the first 150 ms' energy has arrived (reflections dominate at A = 0).
    auto centre = [&] (float size)
    {
        p.sizeScale = size;
        const auto ir = analysis::impulseResponse (*e, p, 48000.0, 0.15);
        double total = 0.0, acc = 0.0;
        for (size_t i = 0; i < ir.size(); ++i)
            total += (double) ir.left[i] * ir.left[i];
        for (size_t i = 0; i < ir.size(); ++i)
        {
            acc += (double) ir.left[i] * ir.left[i];
            if (acc >= 0.5 * total)
                return (double) i / 48000.0;
        }
        return 0.15;
    };
    const auto booth = centre (0.3f), medium = centre (1.0f), live = centre (2.0f);
    INFO ("half-energy time: booth " << booth * 1000.0 << " ms, medium " << medium * 1000.0 << " ms, live room "
          << live * 1000.0 << " ms");
    CHECK (booth < 0.5 * medium);
    CHECK (live > 1.5 * medium);
}
