#include "dsp/algorithms/echo/EchoEngine.h"
#include "support/EngineGates.h"

namespace
{
    using namespace cerulean;

    constexpr double fs = 48000.0;
    constexpr double pi = 3.14159265358979323846;

    analysis::Stereo silence (double seconds)
    {
        analysis::Stereo s;
        s.sampleRate = fs;
        s.left.assign ((size_t) (seconds * fs), 0.0f);
        s.right = s.left;
        return s;
    }

    /** A Hann-windowed tone burst at `at` seconds. */
    void addBurst (analysis::Stereo& s, double at, double hz, double ms, float amplitude)
    {
        const auto start = (size_t) (at * fs), len = (size_t) (ms * 0.001 * fs);
        for (size_t i = 0; i < len && start + i < s.size(); ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) len);
            const auto v = (float) (amplitude * w * std::sin (2.0 * pi * hz * (double) i / fs));
            s.left[start + i] += v;
            s.right[start + i] += v;
        }
    }

    /** Mid signal (L + R) / 2. */
    std::vector<float> mid (const analysis::Stereo& s)
    {
        std::vector<float> m (s.size());
        for (size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (s.left[i] + s.right[i]);
        return m;
    }

    double energyDb (const std::vector<float>& x, double t0, double t1)
    {
        return analysis::toDb (analysis::rms (x, (size_t) (t0 * fs), (size_t) (t1 * fs)));
    }

    /** Time (s) of the largest |x| in [t0, t1), refined by a parabola through the peak. */
    double peakTime (const std::vector<float>& x, double t0, double t1)
    {
        size_t best = (size_t) (t0 * fs);
        for (size_t i = best; i < (size_t) (t1 * fs) && i < x.size(); ++i)
            if (std::abs (x[i]) > std::abs (x[best]))
                best = i;
        const auto a = std::abs ((double) x[best - 1]), b = std::abs ((double) x[best]), c = std::abs ((double) x[best + 1]);
        const auto offset = 0.5 * (a - c) / std::max (a - 2.0 * b + c, -1.0e30);
        return ((double) best + (std::abs (a - 2.0 * b + c) > 1.0e-12 ? offset : 0.0)) / fs;
    }

    /** Parameters that keep the plate short and the tape steady, to look at the echoes alone. */
    dsp::EngineParams dryEchoes()
    {
        auto p = dsp::EngineParams {};
        p.decaySeconds = 0.1f;
        p.mod = 0.0f;
        p.echoTimeMs = 400.0f;
        return p;
    }

    cerulean::gates::Options echoOptions()
    {
        cerulean::gates::Options o;
        // Repeats is a second decay, deliberately independent of Decay (the echo trail; Decay
        // sets the plate). At the default 50 % the repeats of a 300 ms echo ring for ~3 s and
        // would dominate the measured RT60, so the gates run with Repeats at 0 and measure the
        // plate (plus the three head taps). The repeats have their own tests below; the
        // "tail always decays" corners still include Repeats 100 %.
        o.base.paramB = 0.0f;
        o.cpuBudgetPercent = 5.0;   // PLAN.md: Echo (tape + plate) gets the 5 % budget
        return o;
    }
}

CERULEAN_ENGINE_GATES ("[echo]", cerulean::dsp::EchoEngine, echoOptions())

TEST_CASE ("Echo: the tape saturation is soft and can never add gain", "[echo]")
{
    using dsp::EchoEngine;
    float last = -2.0f;
    for (int i = -400; i <= 400; ++i)
    {
        const auto x = (float) i / 100.0f;
        const auto y = EchoEngine::saturate (x);
        CHECK (std::abs (y) <= std::abs (x) + 1.0e-6f);
        CHECK (std::abs (y) <= 1.0f + 1.0e-6f);
        CHECK (y >= last - 1.0e-6f);   // monotonic
        last = y;
    }
    CHECK (std::abs (EchoEngine::saturate (0.25f) - 0.25f) < 0.01f);   // ~linear at playing level
}

TEST_CASE ("Echo: repeats land on the echo time and its multiples, heads on the triplet grid", "[echo]")
{
    auto e = gates::makeEngine<dsp::EchoEngine> (fs);
    for (const auto ms : { 400.0f, 375.0f /* 1/8 dotted at 120 bpm */, 90.0f })
    {
        auto p = dryEchoes();
        p.echoTimeMs = ms;
        p.paramB = 0.6f;
        auto in = silence (2.5);
        in.left[0] = in.right[0] = 1.0f;
        e->reset();
        const auto out = mid (analysis::renderEngine (*e, p, in, in.size()));
        const auto T = ms * 0.001;
        const auto window = std::min (0.004, T / 8.0);

        for (int k = 1; k <= 3; ++k)
        {
            const auto t = peakTime (out, k * T - window, k * T + window);
            INFO (ms << " ms: repeat " << k << " at " << t * 1000.0 << " ms (expected " << k * ms << ")");
            CHECK (std::abs (t - k * T) < 0.0003);
        }
        for (const auto ratio : { 1.0 / 3.0, 2.0 / 3.0 })
        {
            const auto t = peakTime (out, ratio * T - window, ratio * T + window);
            INFO (ms << " ms: head at " << t * 1000.0 << " ms (expected " << ratio * ms << ")");
            CHECK (std::abs (t - ratio * T) < 0.0003);
        }
    }
}

TEST_CASE ("Echo: Repeats sets the per-repeat decay", "[echo]")
{
    auto e = gates::makeEngine<dsp::EchoEngine> (fs);
    auto in = silence (3.5);
    addBurst (in, 0.0, 1000.0, 20.0, 0.25f);   // 1 kHz: the loop's tone loss is small and known

    // Energy of the k-th main-head repeat (the side heads fall between, at thirds of T).
    auto repeat = [] (const std::vector<float>& x, int k) { return energyDb (x, 0.4 * k - 0.01, 0.4 * k + 0.04); };

    double previousStep = -1000.0;
    for (const auto b : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        auto p = dryEchoes();
        p.paramB = b;
        e->reset();
        const auto out = mid (analysis::renderEngine (*e, p, in, in.size()));

        if (b <= 0.0f)
        {
            INFO ("Repeats 0: first echo " << repeat (out, 1) << " dB, second " << repeat (out, 2) << " dB");
            CHECK (repeat (out, 2) < repeat (out, 1) - 60.0);   // one echo (three heads), no repeats
            continue;
        }

        const auto step1 = repeat (out, 2) - repeat (out, 1);
        const auto step3 = repeat (out, 4) - repeat (out, 3);
        // Loop loss at 1 kHz: head/tape LP at 3.6 kHz (-0.32 dB) and the 110 Hz low cut (-0.05 dB).
        const auto expected = 20.0 * std::log10 (dsp::EchoEngine::feedbackFor (b, 400.0f)) - 0.37;
        INFO ("Repeats " << b << ": per repeat " << step1 << " dB, then " << step3 << " dB (expected " << expected << ")");
        CHECK (std::abs (step1 - expected) < 0.6);
        CHECK (std::abs (step3 - expected) < 0.6);   // exponential: the same step every repeat
        CHECK (step1 > previousStep + 1.0);          // more Repeats, slower decay
        previousStep = step1;
    }
    CHECK (dsp::EchoEngine::feedbackFor (1.0f, 150.0f) > 0.949f);   // up to 95 % per repeat
}

TEST_CASE ("Echo: every repeat is darker and a little thinner", "[echo]")
{
    auto e = gates::makeEngine<dsp::EchoEngine> (fs);
    auto in = silence (3.0);
    const auto burst = analysis::pinkNoise ((size_t) (0.03 * fs), 5);
    for (size_t i = 0; i < burst.size(); ++i)
        in.left[i] = in.right[i] = 2.0f * burst[i];

    auto p = dryEchoes();
    p.paramB = 0.8f;
    const auto out = mid (analysis::renderEngine (*e, p, in, in.size()));

    auto bandShare = [&] (int k, bool high)
    {
        const auto a = (size_t) ((0.4 * k - 0.005) * fs), b = (size_t) ((0.4 * k + 0.045) * fs);
        std::vector<float> seg (out.begin() + (std::ptrdiff_t) a, out.begin() + (std::ptrdiff_t) b);
        const auto total = analysis::rms (seg);
        double band = 0.0;
        if (high)
            band = analysis::highBandRms (seg, fs, 3000.0, 0, seg.size());
        else
        {
            // Below 150 Hz: total minus a 150 Hz high-pass.
            const auto hp = analysis::highBandRms (seg, fs, 150.0, 0, seg.size());
            band = std::sqrt (std::max (total * total - hp * hp, 1.0e-30));
        }
        return analysis::toDb (band) - analysis::toDb (total);
    };

    double lastHigh = 1000.0, lastLow = 1000.0;
    for (int k = 1; k <= 5; ++k)
    {
        const auto h = bandShare (k, true), l = bandShare (k, false);
        INFO ("repeat " << k << ": share above 3 kHz " << h << " dB, below 150 Hz " << l << " dB");
        CHECK (h < lastHigh - 1.0);
        CHECK (l < lastLow);
        lastHigh = h;
        lastLow = l;
    }
    CHECK (bandShare (5, true) < bandShare (1, true) - 8.0);
}

TEST_CASE ("Echo: an echo time change glides like tape, without clicks", "[echo]")
{
    auto e = gates::makeEngine<dsp::EchoEngine> (fs);
    const auto in = gates::sine (fs, 6.0, 330.0, 0.25f);
    auto p = dsp::EngineParams {};
    p.paramB = 0.6f;
    p.mod = 0.0f;
    p.echoTimeMs = 300.0f;

    const auto still = analysis::renderEngine (*e, p, in, in.size());
    e->reset();

    std::vector<float> times;
    auto moved = p;
    moved.echoTimeMs = 450.0f;   // an abrupt jump (the chain would smooth it; this is harder)
    const auto change = (size_t) (2.0 * fs);
    const auto moving = analysis::renderEngine (*e, p, in, in.size(), [&] (size_t pos)
    {
        times.push_back (e->getCurrentEchoMs());
        return pos >= change ? moved : p;
    });

    const auto start = (size_t) fs;
    const auto crest = [&] (const analysis::Stereo& s)
    {
        return analysis::toDb (gates::thirdDifferencePeak (s.left, start) / analysis::rms (s.left, start));
    };
    INFO ("3rd-difference crest: static " << crest (still) << " dB, with the jump " << crest (moving) << " dB");
    CHECK (crest (moving) < -50.0);

    // The tape time moves continuously: no step bigger than the glide's slew limit, at most a
    // pitch glide of an octave, and it arrives within a second and a half.
    double biggestStep = 0.0;
    size_t arrived = 0;
    for (size_t i = 1; i < times.size(); ++i)
    {
        biggestStep = std::max (biggestStep, (double) std::abs (times[i] - times[i - 1]));
        if (arrived == 0 && i * dsp::Engine::maxSubBlock > change && std::abs (times[i] - 450.0f) < 0.5f)
            arrived = i * dsp::Engine::maxSubBlock;
    }
    const auto glideSeconds = (double) (arrived - change) / fs;
    const auto maxStepMs = 0.5 * dsp::Engine::maxSubBlock / fs * 1000.0;   // 0.5 samples per sample
    INFO ("largest change per sub-block " << biggestStep << " ms (limit " << maxStepMs << "), glide " << glideSeconds << " s");
    CHECK (biggestStep <= maxStepMs * 1.001);
    CHECK (biggestStep > 0.0);
    CHECK (glideSeconds > 0.2);
    CHECK (glideSeconds < 1.5);
}

TEST_CASE ("Echo: Mod adds wow, so the echoes wander in time", "[echo]")
{
    auto spread = [] (float mod)
    {
        auto e = gates::makeEngine<dsp::EchoEngine> (fs);
        auto p = dryEchoes();
        p.mod = mod;
        p.echoTimeMs = 300.0f;
        auto in = silence (8.0);
        for (int k = 0; k < 8; ++k)
            in.left[(size_t) (k * 0.9 * fs)] = in.right[(size_t) (k * 0.9 * fs)] = 1.0f;
        const auto out = mid (analysis::renderEngine (*e, p, in, in.size()));
        double lo = 1.0, hi = -1.0;
        for (int k = 0; k < 8; ++k)
        {
            const auto t = peakTime (out, k * 0.9 + 0.29, k * 0.9 + 0.31) - k * 0.9;
            lo = std::min (lo, t);
            hi = std::max (hi, t);
        }
        return (hi - lo) * 1000.0;
    };

    const auto steady = spread (0.0f), wobbly = spread (1.0f);
    INFO ("echo arrival spread: Mod 0 " << steady << " ms, Mod 1 " << wobbly << " ms");
    CHECK (steady < 0.1);
    CHECK (wobbly > 0.5);
}

TEST_CASE ("Echo: freeze holds the plate and stops the echoes", "[echo]")
{
    auto e = gates::makeEngine<dsp::EchoEngine> (fs);
    auto p = dsp::EngineParams {};
    p.paramB = 0.9f;
    p.echoTimeMs = 300.0f;
    p.decaySeconds = 3.0f;
    const auto in = gates::noise (fs, 1.0);
    gates::SmoothedAutomation automation (p, fs);
    auto frozen = p;
    frozen.freeze = 1.0f;

    const auto out = mid (analysis::renderEngine (*e, p, in, (size_t) (10.0 * fs), [&] (size_t pos)
    {
        return automation.step (pos < (size_t) fs ? p : frozen);
    }));

    // Without freeze the repeats would pulse every 300 ms; frozen, 150 ms windows stay level.
    double lo = 1000.0, hi = -1000.0;
    for (double t = 3.0; t < 9.8; t += 0.15)
    {
        lo = std::min (lo, energyDb (out, t, t + 0.15));
        hi = std::max (hi, energyDb (out, t, t + 0.15));
    }
    INFO ("frozen, 150 ms windows from 3 to 10 s: " << lo << " .. " << hi << " dB");
    CHECK (hi - lo < 2.0);
    CHECK (lo > -60.0);
}

TEST_CASE ("Echo: full Repeats on the shortest echo, slammed, never runs away", "[echo]")
{
    auto e = gates::makeEngine<dsp::EchoEngine> (fs);
    auto p = dsp::EngineParams {};
    p.paramB = 1.0f;
    p.echoTimeMs = 20.0f;
    p.tone = 1.0f;
    p.decaySeconds = 0.5f;
    auto in = gates::noise (fs, 2.0);
    for (auto* ch : { &in.left, &in.right })
        for (auto& v : *ch)
            v *= 8.0f;   // ~+6 dBFS peaks
    const auto out = analysis::renderEngine (*e, p, in, (size_t) (6.0 * fs));
    REQUIRE (analysis::allFinite (out));
    const auto loud = energyDb (mid (out), 1.0, 2.0), after = energyDb (mid (out), 5.0, 6.0);
    INFO ("peak " << analysis::peak (out) << ", while playing " << loud << " dB, 3 s after " << after << " dB");
    CHECK (analysis::peak (out) < 8.0);
    CHECK (after < loud - 60.0);
}
