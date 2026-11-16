// Reduction accuracy and collateral (PLAN.md, Measurement and quality gates; Settled 38–40). A
// synthetic ess X dB over the threshold: Split and Spectral are gated on the band's own power,
// which loses the detector's reduction within ±0.5 dB (Range permitting), and the landing on B
// (the ess as the detector hears the output, which also hears its skirts past the edges) is
// reported; Wide is gated on the landing, the threshold ± 0.5 dB. The Harsh band is gated on its
// band's power, H reported. Outside the band nothing changes by more than 0.1 dB during events.
// Every row that meets its gate is checked against it (since M3's Speed Auto, every row does); a row
// that does not would be held to its measured reading; a reported reading is held to its measured
// value. "[.report]" prints every table.

#include "ChainRun.h"
#include "SplitMeasure.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>

using namespace halite;
using namespace halite::testing;

namespace
{
    constexpr double rate = 48000.0;
    constexpr double essFrom = 0.6, essTo = 1.0;           // s
    constexpr double steadyFrom = 0.68, steadyTo = 0.97;   // after B's rise, before the release

    double median (std::vector<double> v)
    {
        std::sort (v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    }

    /** Hop values of the probe over [from, to) s: S, B, H − body, H of path 0, the shaped reductions. */
    struct Levels
    {
        std::vector<double> sibilance, band, harshRelative, harsh, reduction, harshReduction;
    };

    Levels levelsOf (const Samples& x, const dsp::DetectorSettings& s, double from, double to, double fs = rate)
    {
        const auto r = probeOf (x, fs, s);
        Levels l;
        for (auto h = hopAt (from, fs); h < hopAt (to, fs) && h < (int) r.hops.size(); ++h)
        {
            const auto& p = r.hops[(size_t) h].paths[0];
            l.sibilance.push_back (p.sibilanceDb);
            l.band.push_back (p.bandDb);
            l.harshRelative.push_back (p.harshRelativeDb);
            l.harsh.push_back (p.harshDb);
            l.reduction.push_back (r.hops[(size_t) h].reductions[0].essDb);
            l.harshReduction.push_back (r.hops[(size_t) h].reductions[0].harshDb);
        }
        return l;
    }

    double mean (const std::vector<double>& v)
    {
        auto sum = 0.0;
        for (const auto x : v)
            sum += x;
        return sum / (double) std::max<size_t> (1, v.size());
    }

    /** Per hop: `level` moved by what `after` lost against `before`. */
    std::vector<double> moved (const std::vector<double>& level, const std::vector<double>& after, const std::vector<double>& before)
    {
        std::vector<double> v;
        for (size_t i = 0; i < level.size(); ++i)
            v.push_back (level[i] + after[i] - before[i]);
        return v;
    }

    /** The output moved back by the latency, so its hops line up with the input's. */
    Samples aligned (const Samples& out, int latency)
    {
        Samples y (out.size(), 0.0f);
        for (size_t i = 0; i + (size_t) latency < out.size(); ++i)
            y[i] = out[i + (size_t) latency];
        return y;
    }

    /** A vowel with an ess (Detect-band noise) whose steady S is `sibilanceDb`, found by scaling the ess. */
    Samples voiceWithEss (const dsp::DetectorSettings& s, double sibilanceDb, int n, double fs = rate)
    {
        // vowel()'s harmonics, but none above 0.45 · 48 kHz: the same signal up to 48 kHz, and no cost for what no gate reads above.
        const auto voice = harmonic ({ .f0 = 150.0, .topHz = std::min (0.45 * fs, 0.45 * rate), .tiltDbPerOctave = -12.0, .rms = 0.1, .seed = 1 }, n, fs);
        const auto ess = gated (bandNoise (4500.0, 12000.0, n, fs, 3, 0.02), samplesFor (essFrom, fs), samplesFor (essTo, fs), fs);
        auto gain = 1.0;
        Samples x;
        for (int pass = 0; pass < 4; ++pass)
        {
            x = sum (voice, scaled (ess, gain));
            const auto now = median (levelsOf (x, s, steadyFrom, steadyTo, fs).sibilance);
            gain *= std::pow (10.0, (sibilanceDb - now) / 20.0);
        }
        return sum (voice, scaled (ess, gain));
    }

    /** A vowel with a group of equal harmonics around Harsh Freq whose steady H − body is `harshDb`. */
    Samples voiceWithHarshness (const dsp::DetectorSettings& s, double harshDb, int n)
    {
        const auto voice = vowel (150.0, n, rate, 0.1, 1);
        Samples peak ((size_t) n, 0.0f);
        for (auto k = (int) std::ceil (2600.0 / 150.0); k * 150.0 <= 3900.0; ++k)
            peak = sum (peak, sine (k * 150.0, n, rate, 0.01, 0.37 * k));
        auto gain = 1.0;
        for (int pass = 0; pass < 4; ++pass)
        {
            const auto x = sum (voice, scaled (peak, gain));
            const auto now = median (levelsOf (x, s, 0.5, 1.5).harshRelative);
            gain *= std::pow (10.0, (harshDb - now) / 20.0);
        }
        return sum (voice, scaled (peak, gain));
    }

    /**
        The share of the test ess's B (its noise as the detector's band filter weighs it, 512-point
        Hann frames at 48 kHz) that lies in bins outside 4.5 ... 12 kHz: what an in-band cut leaves.
    */
    double essShareOutside()
    {
        const auto n = samplesFor (1.4, rate);
        const auto ess = bandNoise (4500.0, 12000.0, n, rate, 3, 0.02);
        dsp::RealFft fft;
        fft.prepare (9);
        Samples frame (512), re (257), im (257);
        double inside = 0, outside = 0;
        const auto warp = [] (double f) { return std::tan (std::numbers::pi * f / rate); };
        for (int start = 0; start + 512 <= n; start += 128)
        {
            for (int j = 0; j < 512; ++j)
                frame[(size_t) j] = ess[(size_t) (start + j)] * (float) (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * j / 512));
            fft.forward (frame.data(), re.data(), im.data());
            for (int k = 1; k < 257; ++k)
            {
                const auto hz = k * rate / 512;
                const auto w = 1.0 / (1.0 + std::pow (warp (4500.0) / warp (hz), 4.0)) / (1.0 + std::pow (warp (hz) / warp (12000.0), 4.0));
                (k >= 48 && k <= 128 ? inside : outside) += w * ((double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k]);
            }
        }
        return outside / (inside + outside);
    }

    struct Accuracy
    {
        double inDb = 0.0, outDb = 0.0, expectedDb = 0.0;
        double appliedDb = 0.0, lostDb = 0.0;   // the detector's mean reduction, and what B lost on average
        double ownLostDb = 0.0;                 // what the band's own power lost (bins inside the band)
        double ownAskedDb = 0.0;                // what it loses under the detector's reduction applied exactly (Wide's)
        double powerLostDb = 0.0;               // Harsh: what the band's power lost (through the detector's band filter)
        double error() const { return outDb - expectedDb; }
        double ownError() const { return ownLostDb - ownAskedDb; }
    };

    /** The ess's S over the threshold before and after the chain, and where the gate wants it. */
    Accuracy essAccuracy (Shape shape, double overDb, float rangeDb, double fs = rate, dsp::DetectorSettings s = {})
    {
        s.rangeDb = rangeDb;
        const auto n = samplesFor (1.4, fs);
        const auto x = voiceWithEss (s, s.thresholdDb + overDb, n, fs);

        ChainRun run;
        run.rate = fs;
        run.settings.detector = s;
        run.settings.shape = shape;
        const auto latency = dsp::HaliteChain::latencySamples (LatencyMode::normal, fs);
        const auto out = aligned (runChain ({ x }, run)[0], latency);

        const auto in = levelsOf (x, s, steadyFrom, steadyTo, fs), after = levelsOf (out, s, steadyFrom, steadyTo, fs);
        Accuracy a;
        a.inDb = median (in.sibilance) - s.thresholdDb;
        // The ess as the detector hears it in the output, hop by hop: its S moved by what B lost
        // (V is held from the vowel before; the cues would read the output differently).
        a.outDb = median (moved (in.sibilance, after.band, in.band)) - s.thresholdDb;
        a.expectedDb = a.inDb - dsp::reductionDb ((float) a.inDb, rangeDb);
        a.appliedDb = mean (in.reduction);
        a.lostDb = mean (in.band) - mean (after.band);
        // The band's own power against what the detector's reduction takes off it, applied exactly
        // (Wide's gain is the reduction, sample by sample): the same window, the same averaging.
        const auto b = dsp::Detector::bandsFor (s, fs);
        const auto from = samplesFor (steadyFrom, fs), to = samplesFor (steadyTo, fs);
        a.ownLostDb = ownBandLostDb (x, out, from, to, b.essLow, b.essHigh, fs);
        auto exact = run;
        exact.settings.shape = Shape::wide;
        const auto asked = shape == Shape::wide ? out : aligned (runChain ({ x }, exact)[0], latency);
        a.ownAskedDb = ownBandLostDb (x, asked, from, to, b.essLow, b.essHigh, fs);
        return a;
    }

    Accuracy harshAccuracy (Shape shape, double overDb, float rangeDb)
    {
        dsp::DetectorSettings s;
        s.harshOn = true;
        s.harshRangeDb = rangeDb;
        const auto n = samplesFor (1.6, rate);
        const auto x = voiceWithHarshness (s, s.harshThresholdDb + overDb, n);

        ChainRun run;
        run.rate = rate;
        run.settings.detector = s;
        run.settings.shape = shape;
        const auto out = aligned (runChain ({ x }, run)[0], dsp::HaliteChain::latencySamples (LatencyMode::normal, rate));

        const auto in = levelsOf (x, s, 0.5, 1.5), after = levelsOf (out, s, 0.5, 1.5);
        Accuracy a;
        a.inDb = median (in.harshRelative) - s.harshThresholdDb;
        a.outDb = median (moved (in.harshRelative, after.harsh, in.harsh)) - s.harshThresholdDb;
        a.expectedDb = a.inDb - dsp::reductionDb ((float) a.inDb, rangeDb);
        a.appliedDb = mean (in.harshReduction);
        a.lostDb = mean (in.harsh) - mean (after.harsh);

        // The band's power, not the detector's reading of it (fast attack, slow release).
        dsp::BandFilter band;
        const auto b = dsp::Detector::bandsFor (s, rate);
        band.setBand (b.harshLow, b.harshHigh, rate);
        Samples bandIn (x.size()), bandOut (x.size());
        for (size_t i = 0; i < x.size(); ++i)
            bandIn[i] = (float) band.process ((double) x[i]);
        band.reset();
        for (size_t i = 0; i < x.size(); ++i)
            bandOut[i] = (float) band.process ((double) out[i]);
        a.powerLostDb = 20.0 * std::log10 (rmsOf (bandIn, samplesFor (0.5, rate), samplesFor (1.5, rate))
                                            / rmsOf (bandOut, samplesFor (0.5, rate), samplesFor (1.5, rate)));
        a.ownLostDb = ownBandLostDb (x, out, samplesFor (0.5, rate), samplesFor (1.5, rate), b.harshLow, b.harshHigh, rate);
        return a;
    }

    struct Collateral
    {
        double hz, changeDb;
    };

    /**
        Sines on the main input while the external key holds a steady reduction at Range: each
        sine's change in level, in dB. Ess (harsh = false) or Harsh. With `noiseRms`, noise fills the
        band on the main input too, so a shape that cuts by what the main signal holds has something
        to cut, and `bandLostDb` says how much the band lost.
    */
    std::vector<Collateral> collateral (Shape shape, bool harsh, const std::vector<double>& frequencies, double noiseRms = 0.0,
                                        double* bandLostDb = nullptr)
    {
        const auto n = samplesFor (1.6, rate);
        dsp::DetectorSettings s;
        s.key = KeySource::external;
        s.harshOn = harsh;
        if (harsh)
            s.rangeDb = 0.0f;

        const auto voice = vowel (150.0, n, rate, 0.1, 1);
        Channels key;
        if (harsh)
        {
            Samples peak ((size_t) n, 0.0f);
            for (auto k = 18; k * 150.0 <= 3900.0; ++k)
                peak = sum (peak, sine (k * 150.0, n, rate, 0.05, 0.37 * k));
            key = { sum (voice, peak) };
        }
        else
        {
            key = { sum (voice, gated (bandNoise (4500.0, 12000.0, n, rate, 3, 0.3), samplesFor (0.3, rate), n, rate)) };
        }

        const auto b = dsp::Detector::bandsFor (s, rate);
        const auto low = harsh ? b.harshLow : b.essLow, high = harsh ? b.harshHigh : b.essHigh;
        Samples x ((size_t) n, 0.0f);
        for (size_t f = 0; f < frequencies.size(); ++f)
            x = sum (x, sine (frequencies[f], n, rate, 0.01, 0.7 * (double) f));
        if (noiseRms > 0.0)
            x = sum (x, bandNoise (low, high, n, rate, 9, noiseRms));

        ChainRun run;
        run.rate = rate;
        run.settings.detector = s;
        run.settings.shape = shape;
        run.key = &key;
        dsp::HaliteChain chain;
        chain.prepare (rate, 512, 1, LatencyMode::normal);
        const auto out = aligned (runChain ({ x }, run, &chain)[0], dsp::HaliteChain::latencySamples (LatencyMode::normal, rate));
        const auto reduction = harsh ? chain.meters().harshReductionDb[0].load() : chain.meters().essReductionDb[0].load();
        REQUIRE (reduction > (harsh ? 3.9f : 7.9f));

        const auto from = samplesFor (0.8, rate), to = samplesFor (1.5, rate);
        if (bandLostDb != nullptr)
        {
            dsp::BandFilter band;
            band.setBand (low, high, rate);
            Samples bandIn (x.size()), bandOut (x.size());
            for (size_t i = 0; i < x.size(); ++i)
                bandIn[i] = (float) band.process ((double) x[i]);
            band.reset();
            for (size_t i = 0; i < x.size(); ++i)
                bandOut[i] = (float) band.process ((double) out[i]);
            *bandLostDb = 20.0 * std::log10 (rmsOf (bandIn, from, to) / rmsOf (bandOut, from, to));
        }

        std::vector<Collateral> result;
        for (const auto f : frequencies)
            result.push_back ({ f, 20.0 * std::log10 (amplitudeAt (out, f, rate, from, to) / amplitudeAt (x, f, rate, from, to)) });
        return result;
    }

    // Outside the Detect band: a third of an octave and more beyond either edge.
    const std::vector<double> essOutside { 250.0, 500.0, 1000.0, 1500.0, 2250.0, 3000.0, 3572.0, 15119.0, 19000.0 };
    const std::vector<double> harshOutside { 250.0, 500.0, 1131.0, 1796.0, 5701.0, 9051.0 };    // Harsh band 2263 … 4525 Hz

    const char* nameOf (Shape s) { return s == Shape::wide ? "Wide" : (s == Shape::spectral ? "Spectral" : "Split"); }

    /**
        A row's reading as measured (2026-11-14, GCC, x86-64) and whether it meets the plan's gate.
        A row that does not may move by `margin` for other compilers' rounding, never further.
    */
    struct Measured
    {
        double db;
        bool passes;
    };

    constexpr double margin = 0.1;

    void checkRow (double value, double gate, const Measured& m)
    {
        if (m.passes)
            CHECK (std::abs (value) <= gate);
        else
            CHECK (std::abs (value) <= std::abs (m.db) + margin);
    }

    /** A reported reading: held to its measured value, the same margin either way. */
    void checkReported (double value, double measured)
    {
        CHECK (std::abs (value - measured) <= margin);
    }

    struct EssRow
    {
        Shape shape;
        double over;
        float range;
        Measured own;           // the band's own power lost against the detector's reduction applied exactly (the gate for Split and Spectral)
        Measured landing;       // dB of S over where the gate wants it (the gate for Wide; reported for Split and Spectral)
    };

    // Measured 2026-11-14 (GCC, x86-64), M3's Speed Auto (15 ... 30 ms). Wide and Split land 0.25 ...
    // 0.43 dB under the threshold where nothing else binds: the shaped reduction follows the peaks of
    // S's ripple (instant attack, look-ahead maximum, a slower release; M2's 15 ... 80 ms landed
    // 0.51 ... 0.57 dB under, src/dsp/detector/NOTES.md, M3). Split's steep cut and Spectral's
    // water-fill take the reduction off the band's own power; B also holds the ess's skirts past the
    // edges (3.55 % of this ess's B), which both leave, so B loses less: a little for Split's
    // transition, more for Spectral, which cuts only the band's bins.
    const std::vector<EssRow> essRows {
        { Shape::wide, 3.0, 8.0f, { 0.00, true }, { -0.42, true } },
        { Shape::wide, 6.0, 8.0f, { 0.00, true }, { -0.43, true } },
        { Shape::wide, 12.0, 8.0f, { 0.00, true }, { 0.00, true } },
        { Shape::wide, 6.0, 24.0f, { 0.00, true }, { -0.43, true } },
        { Shape::wide, 12.0, 24.0f, { 0.00, true }, { -0.43, true } },
        { Shape::wide, 18.0, 24.0f, { 0.00, true }, { -0.43, true } },
        { Shape::split, 3.0, 8.0f, { -0.00, true }, { -0.39, true } },
        { Shape::split, 6.0, 8.0f, { -0.01, true }, { -0.36, true } },
        { Shape::split, 12.0, 8.0f, { -0.01, true }, { 0.08, true } },
        { Shape::split, 6.0, 24.0f, { -0.01, true }, { -0.36, true } },
        { Shape::split, 12.0, 24.0f, { -0.02, true }, { -0.25, true } },
        { Shape::split, 18.0, 24.0f, { -0.04, true }, { 0.07, true } },
        { Shape::spectral, 3.0, 8.0f, { -0.02, true }, { -0.19, true } },
        { Shape::spectral, 6.0, 8.0f, { -0.01, true }, { 0.12, true } },
        { Shape::spectral, 12.0, 8.0f, { -0.01, true }, { 0.74, false } },
        { Shape::spectral, 6.0, 24.0f, { -0.03, true }, { 0.15, true } },
        { Shape::spectral, 12.0, 24.0f, { -0.03, true }, { 1.62, false } },
        { Shape::spectral, 18.0, 24.0f, { -0.06, true }, { 4.96, false } },
    };

    struct HarshRow
    {
        Shape shape;
        double over;
        float range;
        Measured power;         // the Harsh band's power lost against the detector's reduction (the gate)
        double landing;         // H − body over where the gate wants it (reported)
    };

    // The steep bell takes the reduction off the band's power in Split and Wide; Spectral's H, fast
    // to rise and slow to fall, reads more loss than the band's power once the resonance is cut more
    // than the band's edges.
    const std::vector<HarshRow> harshRows {
        { Shape::split, 3.0, 4.0f, { -0.04, true }, -0.14 },
        { Shape::split, 6.0, 4.0f, { -0.03, true }, 0.02 },
        { Shape::split, 9.0, 12.0f, { -0.08, true }, -0.12 },
        { Shape::split, 15.0, 12.0f, { -0.06, true }, 0.05 },
        { Shape::wide, 3.0, 4.0f, { -0.04, true }, -0.14 },
        { Shape::wide, 6.0, 4.0f, { -0.03, true }, 0.02 },
        { Shape::wide, 9.0, 12.0f, { -0.08, true }, -0.12 },
        { Shape::wide, 15.0, 12.0f, { -0.06, true }, 0.05 },
        { Shape::spectral, 3.0, 4.0f, { 0.01, true }, -0.33 },
        { Shape::spectral, 6.0, 4.0f, { -0.02, true }, 0.01 },
        { Shape::spectral, 9.0, 12.0f, { 0.02, true }, -0.45 },
        { Shape::spectral, 15.0, 12.0f, { -0.02, true }, 0.01 },
    };

    // Split's steep cut and the Harsh bell are back within 0.1 dB a third of an octave past each edge.
    const std::vector<Measured> splitOutside { { 0.001, true }, { 0.005, true }, { 0.012, true }, { 0.007, true }, { -0.007, true },
                                               { -0.003, true }, { -0.012, true }, { -0.006, true }, { -0.000, true } };
    const std::vector<Measured> harshBellOutside { { -0.006, true }, { -0.015, true }, { 0.016, true },
                                                   { 0.002, true }, { -0.000, true }, { 0.006, true } };
}

TEST_CASE ("Accuracy: Wide takes off the band exactly the reduction the detector asks for", "[accuracy]")
{
    // What M2 owns: the gain the chain applies is the detector's shaped reduction, on time.
    for (const auto& [over, range] : std::vector<std::pair<double, float>> { { 3.0, 8.0f }, { 6.0, 8.0f }, { 12.0, 8.0f }, { 18.0, 24.0f } })
    {
        const auto a = essAccuracy (Shape::wide, over, range);
        INFO ("Wide: the detector's reduction " << a.appliedDb << " dB, B lost " << a.lostDb << " dB");
        CHECK (std::abs (a.lostDb - a.appliedDb) <= 0.1);
    }
}

TEST_CASE ("Accuracy: an ess comes down as its shape's gate reads it, and no further from it than measured elsewhere", "[accuracy]")
{
    for (const auto& row : essRows)
    {
        const auto a = essAccuracy (row.shape, row.over, row.range);
        INFO (nameOf (row.shape) << ", " << row.over << " dB over, Range " << row.range << ": the detector's reduction " << a.appliedDb
                                 << " dB; the band's own power lost " << a.ownLostDb << " (" << a.ownError() << ", measured " << row.own.db << "), B "
                                 << a.lostDb << "; out " << a.outDb << " dB over, expected " << a.expectedDb << " (" << a.error() << ", measured "
                                 << row.landing.db << ")");
        if (row.shape == Shape::wide)
        {
            checkRow (a.error(), 0.5, row.landing);
            checkReported (a.ownError(), row.own.db);
        }
        else
        {
            checkRow (a.ownError(), 0.5, row.own);
            checkReported (a.error(), row.landing.db);
        }
    }
}

TEST_CASE ("Accuracy: Spectral cuts the band's own bins and leaves what B holds past them", "[accuracy][spectral]")
{
    // Settled 44: the band's own power loses the reduction (the rows above) and the ess's skirts
    // past the edges keep all of theirs (gain exactly 1 outside the band), so B loses the band's
    // loss diluted by the share it holds outside: where Spectral lands on B, explained.
    const auto share = essShareOutside();
    for (const auto& [over, range] : std::vector<std::pair<double, float>> { { 3.0, 8.0f }, { 6.0, 8.0f }, { 12.0, 8.0f }, { 6.0, 24.0f }, { 12.0, 24.0f }, { 18.0, 24.0f } })
    {
        const auto a = essAccuracy (Shape::spectral, over, range);
        const auto expected = -10.0 * std::log10 (share + (1.0 - share) * std::pow (10.0, -a.ownLostDb / 10.0));
        INFO ("Spectral, " << over << " dB over, Range " << range << ": the band's own power lost " << a.ownLostDb << " dB, B " << a.lostDb
                           << " dB, with " << 100.0 * share << " % of B left alone " << expected << " dB");
        CHECK (std::abs (a.lostDb - expected) <= 0.1);
    }
}

TEST_CASE ("Accuracy: a harsh vowel's band loses the Harsh reduction, every shape", "[accuracy][harsh]")
{
    // Settled 40: the Harsh band's power (through the detector's Harsh band filter) loses the
    // detector's reduction within ±0.5 dB, Range permitting; where H lands is reported.
    for (const auto& row : harshRows)
    {
        const auto a = harshAccuracy (row.shape, row.over, row.range);
        INFO (nameOf (row.shape) << ", " << row.over << " dB over, Range " << row.range << ": the detector's reduction " << a.appliedDb
                                 << " dB, the band's power lost " << a.powerLostDb << " (measured " << row.power.db << " off); H − body out "
                                 << a.outDb << " dB over, expected " << a.expectedDb << " (measured " << row.landing << ")");
        checkRow (a.powerLostDb - a.appliedDb, 0.5, row.power);
        checkReported (a.error(), row.landing);
    }
}

TEST_CASE ("Collateral: Split's and the Harsh bell's outside within 0.1 dB during an event", "[accuracy][collateral]")
{
    const auto split = collateral (Shape::split, false, essOutside);
    for (size_t k = 0; k < split.size(); ++k)
    {
        INFO ("Split, " << split[k].hz << " Hz: " << split[k].changeDb << " dB at 8 dB of reduction (measured " << splitOutside[k].db << ")");
        checkRow (split[k].changeDb, 0.1, splitOutside[k]);
    }
    for (const auto shape : { Shape::split, Shape::wide })
    {
        const auto bell = collateral (shape, true, harshOutside);
        for (size_t k = 0; k < bell.size(); ++k)
        {
            INFO (nameOf (shape) << ", Harsh, " << bell[k].hz << " Hz: " << bell[k].changeDb << " dB at 4 dB of Harsh reduction (measured "
                                 << harshBellOutside[k].db << ")");
            checkRow (bell[k].changeDb, 0.1, harshBellOutside[k]);
        }
    }
}

TEST_CASE ("Collateral: Spectral leaves the band's outside within 0.1 dB during an event, both bands", "[accuracy][collateral][spectral]")
{
    // The band holds noise on the main input too, so the cut is real: Spectral cuts the bins that
    // the main signal holds, as the key asks.
    auto lost = 0.0;
    for (const auto& c : collateral (Shape::spectral, false, essOutside, 0.05, &lost))
    {
        INFO ("Ess, " << c.hz << " Hz: " << c.changeDb << " dB at 8 dB of reduction; the band lost " << lost << " dB");
        CHECK (std::abs (c.changeDb) <= 0.1);
    }
    CHECK (lost > 6.0);
    for (const auto& c : collateral (Shape::spectral, true, harshOutside, 0.05, &lost))
    {
        INFO ("Harsh, " << c.hz << " Hz: " << c.changeDb << " dB at 4 dB of Harsh reduction; the band lost " << lost << " dB");
        CHECK (std::abs (c.changeDb) <= 0.1);
    }
    CHECK (lost > 3.0);
}

TEST_CASE ("Collateral: Split's cut is its design: the steep bell over the band, depth = reduction", "[accuracy][collateral]")
{
    // What the chain measures is the design's response (the full depth from Detect Low to High,
    // next to none a third of an octave past them), within the near-Nyquist gate of its analog prototype.
    const auto lo = 4500.0, hi = 12000.0, gain = std::pow (10.0, -8.0 / 20.0);
    const auto& prototype = dsp::splitDesign::ess();
    const auto design = designFor (prototype, dsp::SplitKind::bell, lo, hi, rate, gain);

    const std::vector<double> frequencies { 500.0, 2250.0, lo, std::sqrt (lo * hi), hi, 16000.0, 20000.0 };
    for (const auto& c : collateral (Shape::split, false, frequencies))
    {
        const auto designed = designDb (prototype, dsp::SplitKind::bell, design, c.hz, rate);
        const auto analog = analogDb (prototype, dsp::SplitKind::bell, lo, hi, c.hz, gain);
        INFO (c.hz << " Hz: measured " << c.changeDb << " dB, the design " << designed << " dB, analog " << analog << " dB");
        CHECK (std::abs (c.changeDb - designed) <= 0.05);
        CHECK (std::abs (c.changeDb - analog) <= 0.5);
    }
}

TEST_CASE ("Accuracy: report", "[.report]")
{
    std::printf ("\nEss accuracy at 48 kHz: the band's own power lost against what the detector's reduction, applied exactly (Wide), takes\n"
                 "off it (Settled 39), and the landing on B (dB of S over the threshold, as the detector reads the output; error against\n"
                 "where the gate wants it). 'detector': the mean of its hop reductions; 'own ask': the band's own loss under them:\n");
    std::printf ("  %-8s %6s %6s %9s %8s %8s %8s %8s | %8s %8s %8s\n", "shape", "over", "range", "detector", "own ask", "own lost", "own err", "B lost", "out", "expected", "landing");
    for (const auto shape : { Shape::wide, Shape::split, Shape::spectral })
        for (const auto& [over, range] : std::vector<std::pair<double, float>> { { 3.0, 8.0f }, { 6.0, 8.0f }, { 12.0, 8.0f }, { 6.0, 24.0f }, { 12.0, 24.0f }, { 18.0, 24.0f }, { 24.0, 24.0f } })
        {
            const auto a = essAccuracy (shape, over, range);
            std::printf ("  %-8s %6.2f %6.1f %9.2f %8.2f %8.2f %+8.2f %8.2f | %8.2f %8.2f %+8.2f\n", nameOf (shape), a.inDb, range, a.appliedDb, a.ownAskedDb,
                         a.ownLostDb, a.ownError(), a.lostDb, a.outDb, a.expectedDb, a.error());
        }

    {
        const auto share = essShareOutside();
        std::printf ("\nThe test ess: %.2f %% of B outside the band's bins; the deepest cut Spectral can take off B: %.1f dB (Range 24: %.1f dB)\n",
                     100.0 * share, -10.0 * std::log10 (share), -10.0 * std::log10 (share + (1.0 - share) * std::pow (10.0, -2.4)));
    }

    std::printf ("\nHarsh accuracy at 48 kHz (dB of H - body over the Harsh threshold):\n");
    for (const auto shape : { Shape::split, Shape::wide, Shape::spectral })
        for (const auto& [over, range] : std::vector<std::pair<double, float>> { { 3.0, 4.0f }, { 6.0, 4.0f }, { 9.0, 12.0f }, { 15.0, 12.0f } })
        {
            const auto a = harshAccuracy (shape, over, range);
            std::printf ("  %-6s %6.2f %6.1f %8.2f %8.2f %+8.2f %9.2f %8.2f (power %.2f, %+.2f against the detector; own bins %.2f)\n", nameOf (shape), a.inDb, range,
                         a.outDb, a.expectedDb, a.error(), a.appliedDb, a.lostDb, a.powerLostDb, a.powerLostDb - a.appliedDb, a.ownLostDb);
        }

    std::printf ("\nCollateral at 48 kHz, Detect 4.5 ... 12 kHz, 8 dB of reduction (Split):\n");
    for (const auto& c : collateral (Shape::split, false, { 250.0, 500.0, 1000.0, 1500.0, 2250.0, 3000.0, 3572.0, 4500.0, 7348.0, 12000.0, 15119.0, 19000.0 }))
        std::printf ("  %8.0f Hz %+8.3f dB\n", c.hz, c.changeDb);

    auto lost = 0.0;
    const auto spectralEss = collateral (Shape::spectral, false, { 250.0, 500.0, 1000.0, 1500.0, 2250.0, 3000.0, 3572.0, 4000.0, 4350.0, 12150.0, 13000.0, 15119.0, 19000.0 }, 0.05, &lost);
    std::printf ("\nCollateral at 48 kHz, Detect 4.5 ... 12 kHz, 8 dB of reduction (Spectral, noise in the band: it lost %.2f dB):\n", lost);
    for (const auto& c : spectralEss)
        std::printf ("  %8.0f Hz %+8.3f dB\n", c.hz, c.changeDb);

    const auto spectralHarsh = collateral (Shape::spectral, true, { 250.0, 500.0, 1131.0, 1600.0, 1796.0, 2000.0, 5000.0, 5701.0, 9051.0, 16000.0 }, 0.05, &lost);
    std::printf ("\nHarsh collateral at 48 kHz, Harsh 3.2 kHz, 1 octave, 4 dB of reduction (Spectral, noise in the band: it lost %.2f dB):\n", lost);
    for (const auto& c : spectralHarsh)
        std::printf ("  %8.0f Hz %+8.3f dB\n", c.hz, c.changeDb);

    std::printf ("\nHarsh collateral at 48 kHz, Harsh 3.2 kHz, 1 octave, 4 dB of reduction (Split):\n");
    for (const auto& c : collateral (Shape::split, true, { 250.0, 500.0, 1131.0, 1600.0, 1796.0, 2263.0, 3200.0, 4525.0, 5701.0, 9051.0, 16000.0 }))
        std::printf ("  %8.0f Hz %+8.3f dB\n", c.hz, c.changeDb);
}

TEST_CASE ("Accuracy: the landing against Speed, report", "[.report]")
{
    // Where Wide and Split land on B at each fixed release and at Auto, and how much the reduction
    // itself moves in the ess's steady part (the std of the shaped reduction, dB): what a faster
    // release buys in accuracy and costs in gain modulation (NOTES.md, M3).
    std::printf ("\nThe landing against Speed (48 kHz, Range 8; dB of S over where the gate wants it; reduction's std over the steady ess):\n");
    std::printf ("  %-10s %-6s %8s %8s %8s\n", "speed", "shape", "3 over", "6 over", "std (6)");
    std::vector<std::pair<bool, float>> speeds { { true, 0.0f } };
    for (const auto ms : { 5.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f, 40.0f, 60.0f, 80.0f })
        speeds.emplace_back (false, ms);
    for (const auto& [automatic, ms] : speeds)
        for (const auto shape : { Shape::wide, Shape::split })
        {
            dsp::DetectorSettings s;
            s.speedAuto = automatic;
            s.releaseMs = automatic ? s.releaseMs : ms;
            const auto a3 = essAccuracy (shape, 3.0, 8.0f, rate, s), a6 = essAccuracy (shape, 6.0, 8.0f, rate, s);
            s.rangeDb = 8.0f;
            const auto x = voiceWithEss (s, s.thresholdDb + 6.0, samplesFor (1.4, rate));
            const auto r = levelsOf (x, s, steadyFrom, steadyTo).reduction;
            const auto m = mean (r);
            auto var = 0.0;
            for (const auto v : r)
                var += (v - m) * (v - m);
            std::printf ("  %-10s %-6s %+8.2f %+8.2f %8.2f\n", automatic ? "Auto" : juce::String (ms, 0).toRawUTF8(), nameOf (shape), a3.error(), a6.error(),
                         std::sqrt (var / (double) std::max<size_t> (1, r.size())));
        }
}

TEST_CASE ("Accuracy: the ess's onset against the look-ahead, report", "[.report]")
{
    // Wide, so the gain is the reduction: in 1 ms windows from the test ess's onset (a 2 ms fade
    // in), how much less the ess loses than in its steady part, and before it, what the vowel
    // already loses (the look-ahead's ramp). NOTES.md, M3.
    std::printf ("\nThe onset against the look-ahead (48 kHz, Wide, 6 dB over, Range 8; dB): the ess's loss short of its steady loss in\n"
                 "1 ms windows from its onset, the mean over its first 5 and 10 ms; the vowel's loss in the 10 ms before (mean, largest):\n");
    std::printf ("  %-10s %6s %6s %6s %6s %6s %6s %8s %8s | %8s %8s\n", "look-ahead", "0-1", "1-2", "2-3", "3-4", "4-5", "5-10", "mean 5", "mean 10", "before", "largest");
    for (const auto ms : { -1.0f, 0.0f, 2.5f, 5.0f, 7.5f, 10.0f })
    {
        dsp::DetectorSettings s;
        s.rangeDb = 8.0f;
        s.latency = ms < 0.0f ? LatencyMode::live : LatencyMode::normal;
        s.lookaheadMs = std::max (ms, 0.0f);
        const auto n = samplesFor (1.4, rate);
        const auto x = voiceWithEss (s, s.thresholdDb + 6.0, n);
        ChainRun run;
        run.rate = rate;
        run.settings.detector = s;
        run.settings.shape = Shape::wide;
        run.settings.detector.latency = s.latency;
        const auto out = aligned (runChain ({ x }, run)[0], dsp::HaliteChain::latencySamples (s.latency, rate));
        const auto onset = samplesFor (essFrom, rate), ms1 = samplesFor (0.001, rate);
        const auto lossDb = [&] (int from, int to) { return 20.0 * std::log10 (rmsOf (x, from, to) / std::max (1.0e-12, rmsOf (out, from, to))); };
        const auto steady = lossDb (samplesFor (steadyFrom, rate), samplesFor (steadyTo, rate));
        std::vector<double> shortBy;
        for (int k = 0; k < 10; ++k)
            shortBy.push_back (steady - lossDb (onset + k * ms1, onset + (k + 1) * ms1));
        auto before = 0.0, largest = 0.0;
        for (int k = 1; k <= 10; ++k)
        {
            const auto l = lossDb (onset - k * ms1, onset - (k - 1) * ms1);
            before += l / 10.0;
            largest = std::max (largest, l);
        }
        auto mean5 = 0.0, mean10 = 0.0, late = 0.0;
        for (int k = 0; k < 10; ++k)
        {
            mean10 += shortBy[(size_t) k] / 10.0;
            if (k < 5)
                mean5 += shortBy[(size_t) k] / 5.0;
            else
                late += shortBy[(size_t) k] / 5.0;
        }
        std::printf ("  %-10s %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f %8.2f %8.2f | %8.2f %8.2f\n", ms < 0.0f ? "Live" : juce::String (ms, 1).toRawUTF8(), shortBy[0],
                     shortBy[1], shortBy[2], shortBy[3], shortBy[4], late, mean5, mean10, before, largest);
    }
}

TEST_CASE ("Accuracy: every shape takes the same off the band at 44.1 to 192 kHz", "[accuracy][rates]")
{
    // The family gate on rates: the 48 kHz gates above, read at every rate from 44.1 to 192 kHz with
    // an ess 6 dB over. Wide takes off B what the detector asks and lands within the gate, where it
    // lands at 48 kHz; Split and Spectral take it off the band's own power, as at 48 kHz (their
    // landing on B: the case below).
    std::array<Accuracy, 3> at48 {};
    for (size_t k = 0; k < 3; ++k)
        at48[k] = essAccuracy (std::array { Shape::wide, Shape::spectral, Shape::split }[k], 6.0, 8.0f);

    for (const auto fs : { 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (size_t k = 0; k < 3; ++k)
        {
            const auto shape = std::array { Shape::wide, Shape::spectral, Shape::split }[k];
            const auto a = essAccuracy (shape, 6.0, 8.0f, fs);
            INFO (nameOf (shape) << " at " << fs << " Hz: the detector's reduction " << a.appliedDb << " dB, B lost " << a.lostDb << " dB, the band's own power "
                                 << a.ownLostDb << " dB, out " << a.outDb << " dB over; at 48 kHz " << at48[k].appliedDb << ", " << at48[k].lostDb << ", "
                                 << at48[k].ownLostDb << ", " << at48[k].outDb);
            if (shape == Shape::wide)
            {
                CHECK (std::abs (a.lostDb - a.appliedDb) <= 0.1);
                CHECK (std::abs (a.error()) <= 0.5);
                CHECK (std::abs (a.error() - at48[k].error()) <= 0.15);
                CHECK (std::abs ((a.lostDb - a.appliedDb) - (at48[k].lostDb - at48[k].appliedDb)) <= 0.1);
            }
            else
            {
                CHECK (std::abs (a.ownError()) <= 0.5);
                CHECK (std::abs (a.ownError() - at48[k].ownError()) <= 0.1);
            }
        }
}

TEST_CASE ("Accuracy: Split's and Spectral's landing on B across rates, as first written", "[accuracy][rates][!mayfail]")
{
    // The rates check as M2 wrote it for every shape: the ess lands within 0.15 dB of where it lands
    // at 48 kHz, and B loses what it loses there within 0.1 dB. Both shapes leave the test ess's
    // skirts past the band, whose share of B changes with the rate, so their landing moves with
    // them; their own band does not (above).
    for (const auto shape : { Shape::split, Shape::spectral })
    {
        const auto at48 = essAccuracy (shape, 6.0, 8.0f);
        for (const auto fs : { 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        {
            const auto a = essAccuracy (shape, 6.0, 8.0f, fs);
            INFO (nameOf (shape) << " at " << fs << " Hz: out " << a.error() << " dB from the gate's expectation, at 48 kHz " << at48.error());
            CHECK (std::abs (a.error() - at48.error()) <= 0.15);
            CHECK (std::abs ((a.lostDb - a.appliedDb) - (at48.lostDb - at48.appliedDb)) <= 0.1);
        }
    }
}

TEST_CASE ("Accuracy: across rates, report", "[.report]")
{
    std::printf ("\nEss accuracy, 6 dB over, Range 8, at each rate (dB; the detector's reduction, what the band's own power and B lost, S over the threshold out, error):\n");
    std::printf ("  %-9s %-8s %9s %8s %8s %8s %8s %8s %8s\n", "rate", "shape", "detector", "own ask", "own lost", "own err", "B lost", "out", "error");
    for (const auto fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (const auto shape : { Shape::wide, Shape::split, Shape::spectral })
        {
            const auto a = essAccuracy (shape, 6.0, 8.0f, fs);
            std::printf ("  %-9.1f %-8s %9.2f %8.2f %8.2f %+8.2f %8.2f %8.2f %+8.2f\n", fs / 1000.0, nameOf (shape), a.appliedDb, a.ownAskedDb, a.ownLostDb, a.ownError(),
                         a.lostDb, a.outDb, a.error());
        }
}
