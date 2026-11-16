// The two noise-cue candidates (PLAN.md, Detection 3; NOTES.md, "A candidate cue that does not
// need resolved harmonics", "On onsets, rises and decays"), through the detector, on NOTES.md's
// synthetic signals. Run "[.report]" for the raw values the M1 measurement starts from.

#include "Signals.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using namespace halite;
using namespace halite::testing;
using dsp::NoiseCueKind;

namespace
{
    constexpr double rate = 48000.0;

    const char* nameOf (NoiseCueKind cue) { return cue == NoiseCueKind::periodicity ? "periodicity" : "flatness"; }

    /** N (noise-likeness) per hop from `fromSeconds` on. */
    std::vector<double> noiseOf (const Samples& x, NoiseCueKind cue, double fromSeconds = 0.1, double toSeconds = 1.0e9)
    {
        const auto r = probeOf (x, rate, voiceSettings (cue));
        std::vector<double> n;
        for (int i = hopAt (fromSeconds, rate); i < std::min ((int) r.hops.size(), hopAt (std::min (toSeconds, 1.0e4), rate)); ++i)
            n.push_back (r.hops[(size_t) i].paths[0].noise);
        return n;
    }

    double quantile (std::vector<double> v, double q)
    {
        std::sort (v.begin(), v.end());
        return v[(size_t) std::clamp ((int) (q * (double) (v.size() - 1)), 0, (int) v.size() - 1)];
    }

    Samples modulated (Samples x, double hz, double index)
    {
        for (size_t i = 0; i < x.size(); ++i)
            x[i] *= (float) (1.0 + index * std::sin (2.0 * std::numbers::pi * hz * (double) i / rate));
        return x;
    }
}

TEST_CASE ("Noise cues: band noise reads as noise, harmonic sounds as harmonic", "[detector][noise]")
{
    const auto n = samplesFor (2.0, rate);

    for (const auto cue : { NoiseCueKind::periodicity, NoiseCueKind::flatness })
    {
        INFO (nameOf (cue));

        // Noise, and noise modulated at f0 like a voiced fricative (/z/, index 0.5, Pincas and Jackson).
        CHECK (quantile (noiseOf (bandNoise (4500.0, 12000.0, n, rate, 1, 0.1), cue), 0.1) >= 0.9);
        CHECK (quantile (noiseOf (bandNoise (1000.0, 20000.0, n, rate, 2, 0.1), cue), 0.1) >= 0.9);
        CHECK (quantile (noiseOf (modulated (bandNoise (4500.0, 12000.0, n, rate, 3, 0.1), 120.0, 0.5), cue), 0.5) >= 0.8);

        // Bright harmonics up to 16 kHz, as NOTES.md measured them.
        for (const auto f0 : { 110.0, 220.0, 440.0 })
        {
            INFO ("f0 " << f0 << " Hz, steady");
            CHECK (quantile (noiseOf (harmonic ({ .f0 = f0 }, n, rate), cue), 0.9) <= 0.1);
        }
    }

    // With vibrato only periodicity holds at every f0: flatness smears the comb once the harmonics
    // move by a bin within its 43 ms frame (NOTES.md; the values are in "[.report]").
    for (const auto f0 : { 110.0, 220.0, 440.0 })
        for (const auto cents : { 20.0, 50.0 })
        {
            INFO ("periodicity, f0 " << f0 << " Hz, vibrato ±" << cents << " cents");
            CHECK (quantile (noiseOf (harmonic ({ .f0 = f0, .vibratoCents = cents }, n, rate), NoiseCueKind::periodicity), 0.9) <= 0.1);
        }

    for (const auto& [f0, cents] : std::vector<std::pair<double, double>> { { 220.0, 20.0 }, { 440.0, 20.0 }, { 440.0, 50.0 } })
    {
        INFO ("flatness, f0 " << f0 << " Hz, vibrato ±" << cents << " cents");
        CHECK (quantile (noiseOf (harmonic ({ .f0 = f0, .vibratoCents = cents }, n, rate), NoiseCueKind::flatness), 0.5) <= 0.1);
    }
}

TEST_CASE ("Noise cues: onsets, rises and decays", "[detector][noise]")
{
    // A bright vowel whose top end lies 20 dB under the ess, NOTES.md's onset case, both ways round.
    const auto n = samplesFor (1.0, rate);
    const auto change = samplesFor (0.5, rate);
    const auto vowel = harmonic ({ .f0 = 220.0, .rms = 0.01 }, n, rate);
    const auto ess = bandNoise (1000.0, 20000.0, n, rate, 9, 0.1);
    const auto vowelThenEss = sum (gated (vowel, 0, change, rate), gated (ess, change, n, rate));
    const auto essThenVowel = sum (gated (ess, 0, change, rate), gated (vowel, change, n, rate));

    // Milliseconds after the change from which N stays on the given side of 0.5.
    const auto settledAfter = [] (const std::vector<double>& noise, bool towardsNoise)
    {
        auto last = -1;
        for (int i = 0; i < (int) noise.size(); ++i)
            if ((noise[(size_t) i] >= 0.5) != towardsNoise)
                last = i;
        return 1000.0 * (last + 1) * dsp::Detector::hopSize / rate;
    };

    for (const auto cue : { NoiseCueKind::periodicity, NoiseCueKind::flatness })
    {
        const auto toEss = settledAfter (noiseOf (vowelThenEss, cue, 0.5, 0.95), true);
        const auto toVowel = settledAfter (noiseOf (essThenVowel, cue, 0.5, 0.95), false);
        INFO (nameOf (cue) << ": an ess reads as noise " << toEss << " ms after it starts, a vowel after it as harmonic after " << toVowel << " ms");

        // An ess after a vowel must count at once: periodicity within NOTES.md's ≈ 2 ms, flatness
        // within its hop and window taper. A vowel after an ess may read as noise for a while: that
        // engages Focus late, the safe way round.
        CHECK (toEss <= (cue == NoiseCueKind::periodicity ? 4.0 : 16.0));
        CHECK (toVowel <= 50.0);
    }

    // Noise that rises linearly across a window (21.3 ms) and decays by 12 dB across one, with
    // steady stretches between: with only the mean removed NOTES.md measured these as periodic as
    // a /z/ (0.55 … 0.64); the detrended cue reads them as noise.
    const auto window = samplesFor (0.0213, rate), hold = samplesFor (0.05, rate);
    const auto period = 2 * (window + hold);
    const auto burst = bandNoise (4500.0, 12000.0, samplesFor (1.0, rate), rate, 13, 0.1);
    Samples shaped (burst.size());
    for (int i = 0; i < (int) shaped.size(); ++i)
    {
        const auto t = i % period;
        const auto low = std::pow (10.0, -12.0 / 20.0);
        double g;
        if (t < hold)
            g = 1.0;
        else if (t < hold + window)
            g = std::pow (low, (double) (t - hold) / window);
        else if (t < 2 * hold + window)
            g = low;
        else
            g = low + (1.0 - low) * (double) (t - 2 * hold - window) / window;
        shaped[(size_t) i] = (float) (burst[(size_t) i] * g);
    }
    for (const auto cue : { NoiseCueKind::periodicity, NoiseCueKind::flatness })
    {
        INFO (nameOf (cue) << ", noise rising and decaying across the window");
        CHECK (quantile (noiseOf (shaped, cue, 0.1), 0.1) >= 0.9);
    }
}

TEST_CASE ("Noise cues: periodicity is exact again soon after a loud passage", "[detector][noise]")
{
    // +80 dB of noise, then a quiet vowel's top end. The envelope squares the band, so the loud
    // passage leaves rounding residue in the running sums far above the quiet signal; the
    // round-robin recompute removes it. 300 ms on, the filters' memory of the passage has died away
    // too (at 150 ms the detrending high-pass still differs by about 1e-6), so the cue must agree
    // with one that heard silence instead.
    const auto loud = scaled (bandNoise (4500.0, 12000.0, samplesFor (0.5, rate), rate, 31, 0.1), 1.0e4);
    const auto quiet = harmonic ({ .f0 = 220.0, .topHz = 12000.0, .rms = 0.01 }, samplesFor (0.6, rate), rate);

    for (const auto longestLagMs : { 12.5, 25.0 })
    {
        dsp::PeriodicityCue heard, spared;
        heard.prepare (rate, longestLagMs);
        spared.prepare (rate, longestLagMs);
        for (const auto x : loud)
        {
            heard.push (x);
            spared.push (0.0);
        }

        auto worst = 0.0;
        for (size_t i = 0; i < quiet.size(); ++i)
        {
            heard.push (quiet[i]);
            spared.push (quiet[i]);
            if ((int) i >= samplesFor (0.3, rate) && i % dsp::Detector::hopSize == 0)
                worst = std::max (worst, std::abs (heard.value() - spared.value()));
        }
        INFO ("lags to " << longestLagMs << " ms: largest difference from 300 ms after the loud passage " << worst);
        CHECK (worst < 1.0e-9);
        CHECK (spared.value() > 0.9);
    }
}

TEST_CASE ("Noise cues: the longest lag sets the lowest f0 the periodicity cue hears", "[detector][noise]")
{
    // 55 Hz has an 18 ms period: past the default 12.5 ms, inside 25 ms (DetectorTuning's
    // periodicLongestLagMs, measured in M1b and left at 12.5).
    const auto low = harmonic ({ .f0 = 55.0, .topHz = 12000.0, .rms = 0.1 }, samplesFor (1.0, rate), rate);
    const auto rhoOf = [&low] (double longestLagMs)
    {
        dsp::PeriodicityCue cue;
        cue.prepare (rate, longestLagMs);
        std::vector<double> rho;
        for (size_t i = 0; i < low.size(); ++i)
        {
            cue.push (low[i]);
            if ((int) i >= samplesFor (0.2, rate) && i % dsp::Detector::hopSize == 0)
                rho.push_back (cue.value());
        }
        return quantile (rho, 0.5);
    };
    INFO ("median rho at 55 Hz: lags to 12.5 ms " << rhoOf (12.5) << ", to 25 ms " << rhoOf (25.0));
    CHECK (rhoOf (25.0) > 0.9);
    CHECK (rhoOf (25.0) > rhoOf (12.5) + 0.1);
}

TEST_CASE ("Noise cues: report the raw values on the NOTES signals", "[.report]")
{
    const auto n = samplesFor (3.0, rate);
    const auto row = [n] (const char* name, const Samples& x)
    {
        auto s = voiceSettings (NoiseCueKind::blend);
        s.harshOn = true;
        const auto r = probeOf (x, rate, s);
        std::vector<double> p, f, hp, hf;
        for (auto i = (size_t) hopAt (0.1, rate); i < r.hops.size(); ++i)
        {
            const auto& h = r.hops[i].paths[0];
            p.push_back (h.periodicity);
            f.push_back (h.flatnessDb);
            hp.push_back (h.harshPeriodicity);
            hf.push_back (h.harshFlatnessDb);
        }
        const auto cell = [] (const std::vector<double>& v, const char* format)
        {
            static char text[64];
            std::snprintf (text, sizeof text, format, quantile (v, 0.5), quantile (v, 0.1), quantile (v, 0.9));
            return std::string (text);
        };
        std::printf ("| %-28s | %-20s | %-24s | %-20s | %-24s |\n", name, cell (p, "%.2f (%.2f, %.2f)").c_str(),
                     cell (f, "%.1f (%.1f, %.1f)").c_str(), cell (hp, "%.2f (%.2f, %.2f)").c_str(), cell (hf, "%.1f (%.1f, %.1f)").c_str());
    };

    std::printf ("\nRaw noise cues at 48 kHz, defaults (Detect 4.5–12 kHz, Harsh 3.2 kHz, 1 octave): median (p10, p90) over 2.9 s\n");
    std::printf ("| %-28s | %-20s | %-24s | %-20s | %-24s |\n", "signal", "periodicity", "flatness (dB)", "Harsh periodicity", "Harsh flatness (dB)");
    row ("band noise 4.5–12 kHz", bandNoise (4500.0, 12000.0, n, rate, 3, 0.1));
    row ("band noise 1–20 kHz", bandNoise (1000.0, 20000.0, n, rate, 3, 0.1));
    row ("band noise 2–5 kHz", bandNoise (2000.0, 5000.0, n, rate, 4, 0.1));
    row ("noise AM 120 Hz, index 0.5", modulated (bandNoise (4500.0, 12000.0, n, rate, 5, 0.1), 120.0, 0.5));
    for (const auto f0 : { 110.0, 220.0, 440.0 })
        for (const auto cents : { 0.0, 20.0, 50.0 })
        {
            char name[64];
            std::snprintf (name, sizeof name, "harmonic %g Hz, vibrato %g c", f0, cents);
            row (name, harmonic ({ .f0 = f0, .vibratoCents = cents }, n, rate));
        }
    const auto h = harmonic ({ .f0 = 220.0 }, n, rate);
    row ("harmonic 220 Hz + noise, 10 dB", sum (h, bandNoise (1000.0, 20000.0, n, rate, 6, rmsOf (h) / std::sqrt (10.0))));
    row ("harmonic 220 Hz + noise, 0 dB", sum (h, bandNoise (1000.0, 20000.0, n, rate, 6, rmsOf (h))));
}
