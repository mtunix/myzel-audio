#include "dsp/algorithms/rise/RiseEngine.h"
#include "support/EngineGates.h"

namespace
{
    using namespace cerulean;

    struct RiseRender
    {
        analysis::Stereo wet;
        std::vector<float> dryGain;
        int triggers = 0;
    };

    /** Renders like analysis::renderEngine, but also collects the dry gain and the trigger count. */
    RiseRender render (dsp::RiseEngine& e, const dsp::EngineParams& p, const analysis::Stereo& in, size_t total,
                       const std::function<dsp::EngineParams (size_t)>& paramsAt = {})
    {
        RiseRender r;
        r.wet.sampleRate = in.sampleRate;
        r.wet.left.assign (total, 0.0f);
        r.wet.right.assign (total, 0.0f);
        r.dryGain.assign (total, 1.0f);
        std::array<float, dsp::Engine::maxSubBlock> l {}, rr {};

        for (size_t pos = 0; pos < total; pos += dsp::Engine::maxSubBlock)
        {
            const auto n = (int) std::min<size_t> (dsp::Engine::maxSubBlock, total - pos);
            for (int k = 0; k < n; ++k)
            {
                const auto i = pos + (size_t) k;
                l[(size_t) k] = i < in.left.size() ? in.left[i] : 0.0f;
                rr[(size_t) k] = i < in.right.size() ? in.right[i] : 0.0f;
            }
            e.process (l.data(), rr.data(), r.wet.left.data() + pos, r.wet.right.data() + pos, n,
                       paramsAt ? paramsAt (pos) : p);
            const auto* g = e.getDryGain();
            if (g != nullptr)
                std::copy (g, g + n, r.dryGain.begin() + (std::ptrdiff_t) pos);
        }
        r.triggers = e.getTriggerCount();
        return r;
    }

    analysis::Stereo silence (double seconds, double fs = 48000.0)
    {
        analysis::Stereo s;
        s.sampleRate = fs;
        s.left.assign ((size_t) (seconds * fs), 0.0f);
        s.right = s.left;
        return s;
    }

    /** Adds `src` into `dst` starting at `atSeconds`, scaled by `gain`. */
    void place (analysis::Stereo& dst, const analysis::Stereo& src, double atSeconds, float gain = 1.0f)
    {
        const auto start = (size_t) (atSeconds * dst.sampleRate);
        for (size_t i = 0; i < src.size() && start + i < dst.size(); ++i)
        {
            dst.left[start + i] += gain * src.left[i];
            dst.right[start + i] += gain * src.right[i];
        }
    }

    /** A picked note: 196 Hz with a few harmonics, instant attack, exponential decay (dB/s). */
    analysis::Stereo pluck (double seconds, float amplitude, double decayDbPerSecond, double fs = 48000.0)
    {
        auto s = silence (seconds, fs);
        for (size_t i = 0; i < s.size(); ++i)
        {
            const auto t = (double) i / fs;
            const auto env = amplitude * std::pow (10.0, -decayDbPerSecond * t / 20.0);
            const auto w = 2.0 * 3.14159265358979323846 * 196.0 * t;
            s.left[i] = s.right[i] = (float) (env * (0.6 * std::sin (w) + 0.3 * std::sin (2.0 * w) + 0.15 * std::sin (3.0 * w)));
        }
        return s;
    }

    /** Wet energy (L + R) in dB over [t0, t1) seconds. */
    double wetDb (const analysis::Stereo& w, double t0, double t1)
    {
        const auto a = (size_t) (t0 * w.sampleRate), b = (size_t) (t1 * w.sampleRate);
        const auto l = analysis::rms (w.left, a, b), r = analysis::rms (w.right, a, b);
        return analysis::toDb (std::sqrt (0.5 * (l * l + r * r)));
    }

    /** First time (s) the wet's 50 ms energy reaches `dbBelowFinal` under its level over [tFinal, tFinal + 1 s). */
    double timeToReach (const analysis::Stereo& w, double dbBelowFinal, double tFinal)
    {
        const auto target = wetDb (w, tFinal, tFinal + 1.0) - dbBelowFinal;
        for (double t = 0.0; t < tFinal; t += 0.005)
            if (wetDb (w, t, t + 0.05) >= target)
                return t + 0.025;
        return -1.0;
    }

    float riseParamFor (float ms) { return std::log (ms / 50.0f) / std::log (80.0f); }

    cerulean::gates::Options riseOptions()
    {
        cerulean::gates::Options o;
        // The gates measure the tank, so they run at the shortest Rise time (50 ms): the swell is
        // complete before anything is measured. At the default Rise (447 ms) the first ~150 ms
        // of the wet are 30+ dB down, so echo density there is inaudible, and the steep swell
        // envelope biases the Abel-Huang measure right at its 0.9 crossing (it then differs by
        // 12 ms between 48 and 96 kHz). The swell itself has its own tests below.
        o.base.paramA = 0.0f;
        return o;
    }
}

CERULEAN_ENGINE_GATES ("[rise]", cerulean::dsp::RiseEngine, riseOptions())


TEST_CASE ("Rise: the swell curve is an S-curve from 0 to 1", "[rise]")
{
    using dsp::RiseEngine;
    CHECK (std::abs (RiseEngine::swellCurve (0.0f)) < 1.0e-9f);
    CHECK (std::abs (RiseEngine::swellCurve (1.0f) - 1.0f) < 1.0e-6f);
    CHECK (std::abs (RiseEngine::swellCurve (0.5f) - 0.25f) < 1.0e-6f);   // -12 dB half way: even in loudness, not in amplitude
    float last = 0.0f;
    for (int i = 1; i <= 100; ++i)
    {
        const auto v = RiseEngine::swellCurve ((float) i / 100.0f);
        CHECK (v >= last);
        last = v;
    }
    // Flat at both ends: no audible start or arrival.
    CHECK (RiseEngine::swellCurve (0.05f) < 1.0e-4f);
    CHECK (RiseEngine::swellCurve (0.95f) > 0.98f);
}

TEST_CASE ("Rise: the wet fades in over the Rise time, with no attack", "[rise]")
{
    auto e = gates::makeEngine<dsp::RiseEngine> (48000.0);
    const auto note = gates::noise (48000.0, 8.0);   // a held note from silence

    double previous = 0.0;
    for (const auto ms : { 200.0f, 1000.0f, 4000.0f })
    {
        auto p = dsp::EngineParams {};
        p.decaySeconds = 2.0f;
        p.paramA = riseParamFor (ms);
        REQUIRE (std::abs (dsp::maps::riseTimeMs (p.paramA) - ms) < 1.0e-3f * ms);
        e->reset();
        const auto out = render (*e, p, note, note.size());

        const auto rise = ms / 1000.0;
        const auto tFinal = rise + 2.0;
        const auto t6 = timeToReach (out.wet, 6.0, tFinal);
        const auto t1 = timeToReach (out.wet, 1.0, tFinal);
        const auto early = wetDb (out.wet, 0.0, 0.1 * rise + 0.01) - wetDb (out.wet, tFinal, tFinal + 1.0);
        INFO ("Rise " << ms << " ms: -6 dB at " << t6 * 1000.0 << " ms, -1 dB at " << t1 * 1000.0
              << " ms, first 10 % of the rise " << early << " dB under the held level");
        CHECK (out.triggers == 1);
        // The curve reaches -6 dB at 64 % and -1 dB at 83 % of the Rise time; the tank's own
        // build-up on held noise (Decay 2 s: within 1 dB after ~0.25 s) adds to short Rise times.
        CHECK (t6 > 0.55 * rise);
        CHECK (t6 < 0.75 * rise + 0.06);
        CHECK (t1 > 0.7 * rise);
        CHECK (t1 < 1.0 * rise + 0.25);
        CHECK (early < -30.0);
        CHECK (t6 > 3.0 * previous);
        previous = t6;
    }
}

TEST_CASE ("Rise: Blend swells the dry as well, continuously", "[rise]")
{
    auto e = gates::makeEngine<dsp::RiseEngine> (48000.0);
    const auto note = gates::noise (48000.0, 3.0);
    auto p = dsp::EngineParams {};
    p.paramA = riseParamFor (1000.0f);

    auto halfway = [] (const std::vector<float>& g, float level)
    {
        for (size_t i = 0; i < g.size(); ++i)
            if (g[i] >= level)
                return (double) i / 48000.0;
        return -1.0;
    };

    {
        p.paramB = 0.0f;
        e->reset();
        const auto out = render (*e, p, note, note.size());
        const auto lowest = *std::min_element (out.dryGain.begin(), out.dryGain.end());
        INFO ("Blend 0: lowest dry gain " << lowest);
        CHECK (lowest > 0.999999f);   // only the wet swells
    }
    {
        p.paramB = 1.0f;
        e->reset();
        const auto out = render (*e, p, note, note.size());
        const auto t = halfway (out.dryGain, 0.25f);   // the curve is at 0.25 half way through
        INFO ("Blend 1: dry gain starts at " << out.dryGain[1] << ", reaches 0.25 at " << t * 1000.0
              << " ms, 0.999 at " << halfway (out.dryGain, 0.999f) * 1000.0 << " ms");
        CHECK (out.dryGain[1] < 1.0e-3f);           // the pick is silent
        CHECK (std::abs (t - 0.5) < 0.01);          // half way through the 1 s Rise
        CHECK (halfway (out.dryGain, 0.999f) < 1.0);
        CHECK (std::abs (out.dryGain.back() - 1.0f) < 1.0e-4f);
    }
    {
        p.paramB = 0.5f;
        e->reset();
        const auto out = render (*e, p, note, note.size());
        const auto lowest = *std::min_element (out.dryGain.begin(), out.dryGain.end());
        INFO ("Blend 0.5: lowest dry gain " << lowest);
        CHECK (std::abs (lowest - 0.5f) < 0.01f);
    }
}

TEST_CASE ("Rise: the dry closes after a note dies, and is left alone while frozen", "[rise]")
{
    auto e = gates::makeEngine<dsp::RiseEngine> (48000.0);
    auto in = silence (4.0);
    place (in, pluck (1.0, 0.5f, 6.0), 0.0);   // a note held for 1 s, then cut (muted)
    auto p = dsp::EngineParams {};
    p.paramA = riseParamFor (200.0f);
    p.paramB = 1.0f;

    const auto out = render (*e, p, in, in.size(), [&] (size_t pos)
    {
        auto q = p;
        q.freeze = pos >= (size_t) (3.0 * 48000.0) ? 1.0f : 0.0f;
        return q;
    });

    const auto at = [&] (double t) { return out.dryGain[(size_t) (t * 48000.0)]; };
    INFO ("dry gain: 0.9 s " << at (0.9) << ", 2.9 s " << at (2.9) << ", frozen 3.5 s " << at (3.5));
    CHECK (at (0.9) > 0.999f);    // swelled in
    CHECK (at (2.9) < 0.02f);     // closed after the note, ready to hide the next pick
    CHECK (at (3.5) > 0.999999f);     // frozen: the engine can't hear the input, so the dry plays normally
}

TEST_CASE ("Rise: retriggers on new picked notes, not during legato or held notes", "[rise]")
{
    auto e = gates::makeEngine<dsp::RiseEngine> (48000.0);
    auto p = dsp::EngineParams {};

    SECTION ("held noise: one swell")
    {
        const auto in = gates::noise (48000.0, 6.0);
        CHECK (render (*e, p, in, in.size()).triggers == 1);
    }

    SECTION ("legato melody with tremolo: one swell")
    {
        // Eight phase-continuous notes, 250 ms each, with a +-2 dB, 5 Hz tremolo on top.
        auto in = silence (2.5);
        double phase = 0.0;
        const double notes[] { 220.0, 247.0, 262.0, 294.0, 330.0, 294.0, 262.0, 247.0 };
        for (size_t i = 0; i < in.size(); ++i)
        {
            const auto t = (double) i / 48000.0;
            phase += 2.0 * 3.14159265358979323846 * notes[std::min<size_t> (7, (size_t) (t / 0.25))] / 48000.0;
            const auto trem = std::pow (10.0, 2.0 * std::sin (2.0 * 3.14159265358979323846 * 5.0 * t) / 20.0);
            in.left[i] = in.right[i] = (float) (0.3 * trem * std::sin (phase));
        }
        CHECK (render (*e, p, in, in.size()).triggers == 1);
    }

    SECTION ("picked notes after silence: one swell each")
    {
        auto in = silence (5.0);
        for (int k = 0; k < 4; ++k)
            place (in, pluck (0.6, 0.5f, 20.0), 1.0 * k + 0.2);
        CHECK (render (*e, p, in, in.size()).triggers == 4);
    }

    SECTION ("a new pick once the last note has died away (still ringing 20 dB down): a new swell")
    {
        auto in = silence (3.0);
        place (in, pluck (1.2, 0.5f, 20.0), 0.0);    // -24 dB when the next note comes
        place (in, pluck (1.5, 0.5f, 20.0), 1.2);
        CHECK (render (*e, p, in, in.size()).triggers == 2);
    }

    SECTION ("fast picking over a ringing note: no retrigger on every pick")
    {
        auto in = silence (2.5);
        for (int k = 0; k < 8; ++k)   // eighth notes at 120 bpm; each one only 5 dB down when the next comes
            place (in, pluck (2.5 - 0.25 * k, 0.4f, 20.0), 0.25 * k);
        CHECK (render (*e, p, in, in.size()).triggers == 1);
    }
}

TEST_CASE ("Rise: a new note swells in on top of the old tail, which keeps ringing", "[rise]")
{
    auto e = gates::makeEngine<dsp::RiseEngine> (48000.0);
    auto p = dsp::EngineParams {};
    p.decaySeconds = 4.0f;
    p.paramA = riseParamFor (1000.0f);

    auto first = silence (6.0);
    place (first, gates::noise (48000.0, 1.5), 0.0);
    auto both = first;
    place (both, gates::noise (48000.0, 2.0, 7), 2.5);

    const auto a = render (*e, p, first, first.size());
    e->reset();
    const auto b = render (*e, p, both, both.size());
    REQUIRE (b.triggers == 2);

    const auto onsetOld = wetDb (a.wet, 2.5, 2.6), onsetBoth = wetDb (b.wet, 2.5, 2.6);
    const auto laterOld = wetDb (a.wet, 3.6, 4.4), laterBoth = wetDb (b.wet, 3.6, 4.4);
    INFO ("first 100 ms of the new note: old tail alone " << onsetOld << " dB, with the new note "
          << onsetBoth << " dB; 1.1-1.9 s later: " << laterOld << " vs " << laterBoth << " dB");
    CHECK (std::abs (onsetBoth - onsetOld) < 1.0);   // no attack, and the old tail isn't ducked
    CHECK (laterBoth > laterOld + 10.0);             // the new note has swelled in
}

TEST_CASE ("Rise: a short note blooms up after it ends instead of choking", "[rise]")
{
    auto e = gates::makeEngine<dsp::RiseEngine> (48000.0);
    auto p = dsp::EngineParams {};
    p.decaySeconds = 6.0f;
    p.paramA = 1.0f;   // 4 s

    auto in = silence (4.0);
    place (in, gates::noise (48000.0, 0.2), 0.0);
    const auto out = render (*e, p, in, in.size());

    // The tail behind the note is at full level within ~0.6 s of the note dying, then decays at
    // the set Decay (10 dB/s).
    const auto at1 = wetDb (out.wet, 1.0, 1.2), at2 = wetDb (out.wet, 2.0, 2.2);
    INFO ("short note, Rise 4 s: wet " << at1 << " dB at 1 s, " << at2 << " dB at 2 s");
    CHECK (at2 < at1 - 7.0);
    CHECK (at2 > at1 - 13.0);
}
