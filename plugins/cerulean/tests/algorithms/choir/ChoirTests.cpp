#include "dsp/algorithms/choir/ChoirEngine.h"
#include "support/EngineGates.h"
#include "ChoirSpectrum.h"

namespace
{
    cerulean::gates::Options choirOptions()
    {
        cerulean::gates::Options o;

        // PLAN.md budget for Halo/Choir (formant banks): 7 %.
        o.cpuBudgetPercent = 7.0;
        return o;
    }
}

CERULEAN_ENGINE_GATES ("[choir]", cerulean::dsp::ChoirEngine, choirOptions())

using namespace cerulean;

namespace
{
    dsp::EngineParams choirParams (float vowel01, float voices01, float mod = 0.0f)
    {
        dsp::EngineParams p;
        p.paramA = vowel01;
        p.paramB = voices01;
        p.mod = mod;
        p.decaySeconds = 2.0f;
        return p;
    }

    /** 1/12-octave spectrum (dB, both channels) of a render over [t0, t1). */
    std::vector<std::pair<double, double>> spectrum (const analysis::Stereo& s, double t0, double t1,
                                                     double lo = 150.0, double hi = 4000.0)
    {
        std::vector<std::pair<double, double>> out;
        for (double f = lo; f < hi; f *= std::exp2 (1.0 / 12.0))
            out.push_back ({ f, choirtest::bandDb (s, f, t0, t1, 17.0) });
        return out;
    }

    /** Frequency of the strongest bin within [lo, hi). */
    double peakIn (const std::vector<std::pair<double, double>>& sp, double lo, double hi)
    {
        double best = -1.0e9, at = 0.0;
        for (const auto& [f, db] : sp)
            if (f >= lo && f < hi && db > best) { best = db; at = f; }
        return at;
    }

    analysis::Stereo steadyRender (dsp::Engine& e, const dsp::EngineParams& p, double seconds)
    {
        e.reset();
        const auto in = gates::noise (48000.0, seconds);
        return analysis::renderEngine (e, p, in, in.size());
    }

    /**
        Vowel colour over time: per 1-s window, the level at 300 Hz (F1 of I) re 530 Hz (F1 of E),
        in dB. Moves by ~10 dB between E and I.
    */
    std::vector<double> vowelTrack (const analysis::Stereo& s, double t0, double t1)
    {
        std::vector<double> c;
        for (double t = t0; t + 1.0 <= t1; t += 1.0)
            c.push_back (choirtest::bandDb (s, 300.0, t, t + 1.0, 5.0) - choirtest::bandDb (s, 530.0, t, t + 1.0, 5.0));
        return c;
    }

    double standardDeviation (const std::vector<double>& x)
    {
        double m = 0.0, v = 0.0;
        for (auto a : x) m += a;
        m /= (double) x.size();
        for (auto a : x) v += (a - m) * (a - m);
        return std::sqrt (v / (double) x.size());
    }
}

TEST_CASE ("Choir: the tail's formant peaks follow the Vowel", "[choir]")
{
    auto e = gates::makeEngine<dsp::ChoirEngine> (48000.0);

    // One voice, no drift: the formants of voice 1 (tract scale 1) sit where the table puts them.
    struct Expect { float a; double f1, f2; };
    const Expect expected[] = { { 0.0f, 730.0, 1090.0 }, { 0.25f, 530.0, 1840.0 }, { 0.5f, 270.0, 2290.0 },
                                { 0.75f, 480.0, 820.0 }, { 1.0f, 300.0, 870.0 } };
    for (const auto& x : expected)
    {
        const auto out = steadyRender (*e, choirParams (x.a, 0.0f), 4.0);
        const auto sp = spectrum (out, 1.5, 4.0);
        const auto f1 = peakIn (sp, 0.8 * x.f1, 1.2 * x.f1);
        const auto f1Global = peakIn (sp, 150.0, 900.0);
        const auto f2 = peakIn (sp, std::max (0.8 * x.f2, 1.4 * x.f1), 1.2 * x.f2);
        INFO ("vowel " << x.a << ": strongest peak below 900 Hz at " << f1Global << " Hz (F1 " << x.f1
              << "), F2 peak at " << f2 << " Hz (" << x.f2 << ")");
        CHECK (std::abs (std::log2 (f1Global / x.f1)) < 0.2);   // F1 is the strongest low peak
        CHECK (std::abs (std::log2 (f1 / x.f1)) < 0.15);
        CHECK (std::abs (std::log2 (f2 / x.f2)) < 0.15);
    }

    // The I has its energy high (F2/F3 cluster), U and O low: compare 2.3 kHz against 0.9 kHz.
    auto tilt = [&] (float a)
    {
        const auto out = steadyRender (*e, choirParams (a, 0.0f), 3.0);
        return choirtest::bandDb (out, 2300.0, 1.5, 3.0) - choirtest::bandDb (out, 900.0, 1.5, 3.0);
    };
    // (The plain-tail blend fills the valleys, so the contrast is moderate by design.)
    const auto tiltI = tilt (0.5f), tiltU = tilt (1.0f);
    INFO ("2.3 kHz re 0.9 kHz: I " << tiltI << " dB, U " << tiltU << " dB");
    CHECK (tiltI > tiltU + 5.0);
}

TEST_CASE ("Choir: vowels morph continuously (F2 glides between A and E)", "[choir]")
{
    auto e = gates::makeEngine<dsp::ChoirEngine> (48000.0);
    double previous = 0.0;
    for (float a : { 0.0f, 0.0625f, 0.125f, 0.1875f, 0.25f })
    {
        const auto out = steadyRender (*e, choirParams (a, 0.0f), 3.0);
        const auto f2 = peakIn (spectrum (out, 1.5, 3.0, 900.0, 2200.0), 900.0, 2200.0);
        INFO ("vowel position " << a * 4.0f << ": F2 at " << f2 << " Hz");
        CHECK (f2 > previous);
        previous = f2;
    }
}

TEST_CASE ("Choir: more Voices = wider and thicker, at the same level", "[choir]")
{
    auto e = gates::makeEngine<dsp::ChoirEngine> (48000.0);
    double lastRho = 2.0, firstDb = 0.0, firstWidth = 0.0;
    for (int step = 0; step <= 6; ++step)
    {
        const auto b = (float) step / 6.0f;   // 1, 1.5, 2, ... 4 voices: fractional ones fade in
        const auto out = steadyRender (*e, choirParams (0.0f, b), 4.0);
        const auto rho = analysis::correlation (out.left, out.right, 72000, out.size());
        const auto db = analysis::toDb (analysis::rms (out.left, 72000) + analysis::rms (out.right, 72000));

        // Thickness: how many 1/12-octave bins lie within 6 dB of the strongest one (the voices'
        // different tract lengths spread each formant over a wider band).
        const auto sp = spectrum (out, 1.5, 4.0);
        double top = -1.0e9;
        for (auto& [f, d] : sp) top = std::max (top, d);
        int width = 0;
        for (auto& [f, d] : sp) width += d > top - 6.0 ? 1 : 0;

        INFO (1.0 + 0.5 * step << " voices: L/R correlation " << rho << ", level " << db << " dB, bins within 6 dB of the peak "
              << width);
        CHECK (rho < lastRho + 0.02);
        lastRho = rho;
        if (step == 0)
        {
            firstDb = db;
            firstWidth = width;
            CHECK (rho > 0.35);
        }
        else
        {
            CHECK (std::abs (db - firstDb) < 1.5);
        }
        if (step == 6)
        {
            CHECK (rho < 0.25);
            CHECK (width >= firstWidth + 3);
        }
    }
}

TEST_CASE ("Choir: with Mod up the vowel drifts on its own; Freeze holds it", "[choir]")
{
    auto e = gates::makeEngine<dsp::ChoirEngine> (48000.0);

    const auto still = steadyRender (*e, choirParams (0.375f, 0.0f, 0.0f), 24.0);
    const auto drifting = steadyRender (*e, choirParams (0.375f, 0.0f, 1.0f), 24.0);
    const auto sdStill = standardDeviation (vowelTrack (still, 2.0, 24.0));
    const auto sdDrift = standardDeviation (vowelTrack (drifting, 2.0, 24.0));

    // Frozen at Mod 1: after the freeze, the colour must stay put.
    auto running = choirParams (0.375f, 1.0f, 1.0f);
    auto frozen = running;
    frozen.freeze = 1.0f;
    e->reset();
    gates::SmoothedAutomation automation (running, 48000.0);
    const auto held = analysis::renderEngine (*e, running, gates::noise (48000.0, 3.0), 48000 * 16,
                                              [&] (size_t pos) { return automation.step (pos < 48000 * 3 ? running : frozen); });
    const auto sdFrozen = standardDeviation (vowelTrack (held, 4.0, 16.0));

    INFO ("std of the 300/530 Hz balance over 1-s windows: Mod 0 " << sdStill << " dB, Mod 1 " << sdDrift
          << " dB, Mod 1 frozen " << sdFrozen << " dB");
    CHECK (sdStill < 1.0);
    CHECK (sdDrift > 3.0 * sdStill + 1.0);
    CHECK (sdFrozen < 1.0);
}

TEST_CASE ("Choir: Vowel keeps the level; vowel sweeps are click-free", "[choir]")
{
    auto e = gates::makeEngine<dsp::ChoirEngine> (48000.0);
    double lo = 1.0e9, hi = -1.0e9;
    for (float a : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const auto out = steadyRender (*e, choirParams (a, 0.5f), 3.0);
        const auto db = analysis::toDb (analysis::rms (out.left, 72000) + analysis::rms (out.right, 72000));
        lo = std::min (lo, db);
        hi = std::max (hi, db);
    }
    INFO ("level range over the vowels: " << lo << " .. " << hi << " dB");
    CHECK (hi - lo < 2.0);

    // A sine through the choir while the vowel jumps (chain-smoothed) between A and U: any
    // step in the formant filters would show as broadband energy above 14 kHz.
    const double fs = 48000.0;
    const auto in = gates::sine (fs, 6.0, 220.0, 0.25f);
    auto hfPeakDb = [&] (const analysis::Stereo& s)
    {
        auto f1 = analysis::Biquad::highpass (14000.0, 0.5412, fs), f2 = analysis::Biquad::highpass (14000.0, 1.3066, fs);
        double pk = 0.0;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const auto y = f2.process (f1.process (s.left[i]));
            if (i > 48000) pk = std::max (pk, std::abs (y));
        }
        return analysis::toDb (pk / analysis::rms (s.left, 48000));
    };
    auto p = choirParams (0.0f, 1.0f, 0.35f);
    e->reset();
    const auto stillDb = hfPeakDb (analysis::renderEngine (*e, p, in, in.size()));
    e->reset();
    gates::SmoothedAutomation automation (p, fs);
    const auto moving = analysis::renderEngine (*e, p, in, in.size(), [&] (size_t pos)
    {
        auto t = p;
        t.paramA = ((pos / 7200) % 2) ? 1.0f : 0.0f;
        return automation.step (t);
    });
    const auto movingDb = hfPeakDb (moving);
    INFO ("peak above 14 kHz re RMS: static " << stillDb << " dB, vowel jumping A/U every 150 ms " << movingDb << " dB");
    CHECK (movingDb < stillDb + 3.0);
}
