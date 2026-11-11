#include "dsp/algorithms/halo/HaloEngine.h"
#include "support/EngineGates.h"
#include "HaloSpectrum.h"

#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace
{
    cerulean::gates::Options haloOptions()
    {
        cerulean::gates::Options o;

        // Base interval +12 (A = 0.75), the signature shimmer, instead of the panel default +7:
        // every gate is then checked on the sound Halo exists for. It also matters for the click
        // gate: its 3rd-difference detector cannot tell the climb's legitimate high partials from
        // clicks (a static +12 tail at Amount 0.5 reads -32 dB, +7 reads -58 dB), so the static
        // reference must already contain them. Interval switching itself is checked for clicks
        // separately below, against static renders of every interval.
        o.base.paramA = 0.75f;

        // PLAN.md budget for Halo/Choir (pitch shifting): 7 %.
        o.cpuBudgetPercent = 7.0;
        return o;
    }
}

CERULEAN_ENGINE_GATES ("[halo]", cerulean::dsp::HaloEngine, haloOptions())

using namespace cerulean;

namespace
{
    dsp::EngineParams haloParams (int interval, float amount, float decay = 6.0f)
    {
        dsp::EngineParams p;
        p.paramA = (float) interval / 4.0f;
        p.paramB = amount;
        p.decaySeconds = decay;
        return p;
    }

    /** A 60 ms Hann-windowed tone burst (both inputs). */
    analysis::Stereo toneBurst (double fs, double hz)
    {
        auto s = gates::sine (fs, 0.06, hz, 0.5f);
        for (size_t i = 0; i < s.size(); ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) s.size());
            s.left[i] *= (float) w;
            s.right[i] *= (float) w;
        }
        return s;
    }

    /** Time (s) at which a band's energy envelope (50 ms windows) peaks. */
    double bandPeakTime (const analysis::Stereo& s, double fc)
    {
        double best = -1.0e9, at = 0.0;
        for (double t = 0.0; t + 0.05 < (double) s.size() / s.sampleRate; t += 0.05)
        {
            const auto db = halotest::bandDb (s, fc, t, t + 0.05);
            if (db > best) { best = db; at = t; }
        }
        return at;
    }

    /** Peak of the output above 14 kHz relative to its RMS (dB): broadband discontinuities. */
    double highBandPeakDb (const analysis::Stereo& s)
    {
        auto f1 = analysis::Biquad::highpass (14000.0, 0.5412, s.sampleRate);
        auto f2 = analysis::Biquad::highpass (14000.0, 1.3066, s.sampleRate);
        double pk = 0.0;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const auto y = f2.process (f1.process (s.left[i]));
            if (i > 48000)
                pk = std::max (pk, std::abs (y));
        }
        return analysis::toDb (pk / analysis::rms (s.left, 48000));
    }
}

TEST_CASE ("Halo: +12 climbs, octave by octave, starting from an unshifted first arrival", "[halo]")
{
    auto e = gates::makeEngine<dsp::HaloEngine> (48000.0);
    const auto out = analysis::renderEngine (*e, haloParams (3, 0.6f), toneBurst (48000.0, 220.0), 48000 * 5);

    // The first arrival is the plain reverb: no octave yet.
    const auto early220 = halotest::bandDb (out, 220.0, 0.0, 0.09);
    const auto early440 = halotest::bandDb (out, 440.0, 0.0, 0.09);
    INFO ("first 90 ms: 220 Hz " << early220 << " dB, 440 Hz " << early440 << " dB");
    CHECK (early440 < early220 - 30.0);

    // Then each octave peaks later than the one below it.
    const auto t440 = bandPeakTime (out, 440.0), t880 = bandPeakTime (out, 880.0), t1760 = bandPeakTime (out, 1760.0);
    INFO ("band energy peaks: 440 Hz at " << t440 << " s, 880 Hz at " << t880 << " s, 1760 Hz at " << t1760 << " s");
    CHECK (t440 > 0.1);
    CHECK (t880 > t440);
    CHECK (t1760 > t880);

    // And the spectral centroid keeps rising through the tail.
    const auto c1 = halotest::centroidHz (out.left, 48000.0, 24000, 72000);
    const auto c2 = halotest::centroidHz (out.left, 48000.0, 72000, 120000);
    const auto c3 = halotest::centroidHz (out.left, 48000.0, 120000, 168000);
    INFO ("centroid 0.5-1.5 s " << c1 << " Hz, 1.5-2.5 s " << c2 << " Hz, 2.5-3.5 s " << c3 << " Hz");
    CHECK (c2 > 1.3 * c1);
    CHECK (c3 > c2);
}

TEST_CASE ("Halo: energy appears an octave above a held note with +12, an octave below with -12", "[halo]")
{
    auto e = gates::makeEngine<dsp::HaloEngine> (48000.0);
    const auto note = gates::sine (48000.0, 2.0, 440.0, 0.25f);

    auto render = [&] (int interval, float amount)
    {
        e->reset();
        return analysis::renderEngine (*e, haloParams (interval, amount, 4.0f), note, 48000 * 3);
    };

    const auto plain = render (3, 0.0f);
    const auto up = render (3, 0.5f);
    const auto down = render (0, 0.5f);

    const auto plain880 = halotest::bandDb (plain, 880.0, 1.0, 2.5) - halotest::bandDb (plain, 440.0, 1.0, 2.5);
    const auto up880 = halotest::bandDb (up, 880.0, 1.0, 2.5) - halotest::bandDb (up, 440.0, 1.0, 2.5);
    const auto plain220 = halotest::bandDb (plain, 220.0, 1.0, 2.5) - halotest::bandDb (plain, 440.0, 1.0, 2.5);
    const auto down220 = halotest::bandDb (down, 220.0, 1.0, 2.5) - halotest::bandDb (down, 440.0, 1.0, 2.5);
    INFO ("880 Hz re 440 Hz: Amount 0 " << plain880 << " dB, +12 " << up880 << " dB; 220 Hz re 440 Hz: Amount 0 "
          << plain220 << " dB, -12 " << down220 << " dB");

    // Amount 0: no shifted energy at all (only the filters' skirts around 440 Hz).
    CHECK (plain880 < -40.0);
    CHECK (plain220 < -40.0);
    // Shifted: the octave is a real part of the tail.
    CHECK (up880 > -10.0);
    CHECK (down220 > -10.0);

    // -12 pulls the centroid down, +12 pushes it up.
    const auto cPlain = halotest::centroidHz (plain.left, 48000.0, 48000, 120000);
    const auto cUp = halotest::centroidHz (up.left, 48000.0, 48000, 120000);
    const auto cDown = halotest::centroidHz (down.left, 48000.0, 48000, 120000);
    INFO ("centroid: Amount 0 " << cPlain << " Hz, +12 " << cUp << " Hz, -12 " << cDown << " Hz");
    CHECK (cUp > 1.4 * cPlain);
    CHECK (cDown < 0.8 * cPlain);
}

TEST_CASE ("Halo: each interval shifts by its own ratio", "[halo]")
{
    auto e = gates::makeEngine<dsp::HaloEngine> (48000.0);
    const auto note = gates::sine (48000.0, 2.0, 440.0, 0.25f);

    auto shiftedDb = [&] (int interval, double hz)
    {
        e->reset();
        const auto out = analysis::renderEngine (*e, haloParams (interval, 0.5f, 4.0f), note, 48000 * 3);
        return halotest::bandDb (out, hz, 1.0, 2.5, 16.0) - halotest::bandDb (out, 440.0, 1.0, 2.5, 16.0);
    };

    const auto fourth = 440.0 * std::exp2 (5.0 / 12.0), fifth = 440.0 * std::exp2 (7.0 / 12.0);
    const auto twelfth = 440.0 * std::exp2 (19.0 / 12.0);

    const auto p5 = shiftedDb (1, fourth), p5wrong = shiftedDb (1, fifth);
    const auto p7 = shiftedDb (2, fifth), p7wrong = shiftedDb (2, fourth);
    const auto p12only = shiftedDb (3, twelfth), p1219 = shiftedDb (4, twelfth), p1219oct = shiftedDb (4, 880.0);
    INFO ("+5: fourth " << p5 << " dB, fifth " << p5wrong << " dB; +7: fifth " << p7 << " dB, fourth " << p7wrong
          << " dB; twelfth: +12 " << p12only << " dB, +12/+19 " << p1219 << " dB (octave " << p1219oct << " dB)");
    CHECK (p5 > p5wrong + 15.0);
    CHECK (p7 > p7wrong + 15.0);
    CHECK (p1219 > p12only + 15.0);   // the second voice adds the twelfth (+19)...
    CHECK (p1219oct > -15.0);         // ...next to the octave
}

TEST_CASE ("Halo: no grain-rate flutter on a held note's shifted partial", "[halo]")
{
    // RMS envelope of the shifted partial (1 kHz rate), split into a slow part (< ~8 Hz: the
    // tank's own chorus) and a fast part (where a granular shifter's flutter sits).
    auto e = gates::makeEngine<dsp::HaloEngine> (48000.0);
    const auto note = gates::sine (48000.0, 6.0, 440.0, 0.25f);
    const auto out = analysis::renderEngine (*e, haloParams (3, 0.5f, 4.0f), note, 48000 * 6);

    auto f1 = analysis::Biquad::bandpass (880.0, 6.0, 48000.0);
    auto f2 = f1;
    auto lp1 = analysis::Biquad::lowpass (150.0, 0.7071, 48000.0);
    auto lp2 = lp1;
    std::vector<double> env;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const auto y = f2.process (f1.process (out.left[i]));
        const auto en = lp2.process (lp1.process (y * y));
        if (i >= 96000 && i % 48 == 0)
            env.push_back (std::sqrt (std::max (en, 0.0)));
    }

    double mean = 0.0;
    for (auto v : env) mean += v;
    mean /= (double) env.size();
    double fast = 0.0;
    const size_t w = 62;
    for (size_t i = w; i + w < env.size(); ++i)
    {
        double m = 0.0;
        for (size_t k = i - w; k <= i + w; ++k) m += env[k];
        m /= (double) (2 * w + 1);
        fast += (env[i] - m) * (env[i] - m);
    }
    const auto fastIndex = std::sqrt (fast / (double) (env.size() - 2 * w)) / mean;
    INFO ("fast (> ~8 Hz) envelope modulation of the octave partial: " << 100.0 * fastIndex << " % RMS");
    CHECK (fastIndex < 0.08);   // measured ~4 %; unaligned (jitter-only) grains measured ~39 % here
}

TEST_CASE ("Halo: interval changes crossfade without clicks", "[halo]")
{
    // Flip between -12 and +5 (both dark: nothing of the tail reaches 14 kHz), so any
    // discontinuity at a switch would show up as broadband energy above 14 kHz.
    const double fs = 48000.0;
    const auto in = gates::sine (fs, 6.0, 220.0, 0.25f);
    auto e = gates::makeEngine<dsp::HaloEngine> (fs);

    auto still = haloParams (0, 1.0f, 3.0f);
    const auto stillDb = highBandPeakDb (analysis::renderEngine (*e, still, in, in.size()));

    e->reset();
    gates::SmoothedAutomation automation (still, fs);
    auto target = still;
    const auto moving = analysis::renderEngine (*e, still, in, in.size(), [&] (size_t pos)
    {
        target.paramA = ((pos / 9600) % 2) ? 0.25f : 0.0f;
        return automation.step (target);
    });
    const auto movingDb = highBandPeakDb (moving);
    INFO ("peak above 14 kHz re RMS: static -12 " << stillDb << " dB, switching -12/+5 every 200 ms " << movingDb << " dB");
    // Switching must add nothing over the static sound. (The static floor itself, ~-83 dB, is the
    // modulated delay lines' interpolation residue, the same in Hall at full Mod: inaudible.)
    CHECK (movingDb < stillDb + 6.0);
    CHECK (movingDb < -70.0);
}

TEST_CASE ("Halo: Freeze stops the climb but keeps the cloud", "[halo]")
{
    const double fs = 48000.0;
    auto e = gates::makeEngine<dsp::HaloEngine> (fs);
    auto running = haloParams (3, 1.0f, 6.0f);
    auto frozen = running;
    frozen.freeze = 1.0f;
    gates::SmoothedAutomation automation (running, fs);
    const auto out = analysis::renderEngine (*e, running, gates::sine (fs, 1.0, 330.0, 0.25f), (size_t) (fs * 12.0),
                                             [&] (size_t pos) { return automation.step (pos < (size_t) (fs * 1.5) ? running : frozen); });

    const auto c1 = halotest::centroidHz (out.left, fs, (size_t) (fs * 3.0), (size_t) (fs * 4.0));
    const auto c2 = halotest::centroidHz (out.left, fs, (size_t) (fs * 10.0), (size_t) (fs * 11.0));
    const auto l1 = analysis::rms (out.left, (size_t) (fs * 3.0), (size_t) (fs * 4.0));
    const auto l2 = analysis::rms (out.left, (size_t) (fs * 10.0), (size_t) (fs * 11.0));
    INFO ("frozen: centroid " << c1 << " Hz at 3 s, " << c2 << " Hz at 10 s; level " << analysis::toDb (l1)
          << " dB -> " << analysis::toDb (l2) << " dB");
    CHECK (std::abs (std::log2 (c2 / c1)) < 0.05);
    CHECK (std::abs (analysis::toDb (l2 / l1)) < 1.0);
}

TEST_CASE ("Halo: the RT60 stretch is bounded and only ever lengthens", "[halo]")
{
    for (int interval = 0; interval < 5; ++interval)
        for (float amount : { 0.0f, 0.3f, 1.0f })
            for (float decay : { 0.1f, 2.0f, 60.0f })
                for (float size : { 0.3f, 1.0f, 2.0f })
                {
                    const auto rt = dsp::HaloEngine::compensatedRt60 (decay, interval, amount, size);
                    CHECK (rt >= decay * 0.999f);
                    CHECK (rt <= decay * 6.001f);
                }
    CHECK_THAT (dsp::HaloEngine::compensatedRt60 (3.0f, 3, 0.0f, 1.0f), Catch::Matchers::WithinRel (3.0f, 1.0e-6f));
}
