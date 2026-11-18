// The measurements checked against known answers: a branching one-pole (the ballistics method
// and the trace's alignment), BasaltMeasure's transcription of the NOTES (the static and
// ballistics gates on a correct engine, h(ρ) and the crest detector against M0's tables), and
// synthetic signals (the click measure, the identity comparison, the pink noise).

#include "support/GateKit.h"

#include "support/Reference.h"

#include <cmath>
#include <complex>
#include <cstring>

namespace basalt::measure
{

using namespace kit;

namespace
{
    const double e = std::exp (1.0);

    /**
        The interpolated crossing of a sampled one-pole, from its closed form (dynamics/NOTES.md,
        Ballistics). Rising from rest for `heldSamples` samples: r_j = 1 − a^(j+1) to (1 − 1/e) of
        the value reached, 1 − a^held. Falling: r_j = a^(j+1) to 1/e.
    */
    double analyticCrossing (double tauSeconds, double rate, bool rising, int heldSamples = 0)
    {
        const auto a = std::exp (-1.0 / (tauSeconds * rate));
        const auto remaining = rising ? 1.0 / e + std::pow (a, heldSamples) * (1.0 - 1.0 / e) : 1.0 / e;
        for (int j = 0;; ++j)
        {
            const auto before = std::pow (a, j), after = std::pow (a, j + 1);
            if (after <= remaining)
                return (j + (before - remaining) / (before - after)) / rate;
        }
    }

    std::vector<Row> onePoleRows (const Options& o)
    {
        std::vector<Row> rows;
        const auto rate = o.rate;
        for (const auto latency : { LatencyMode::normal, LatencyMode::live })
            for (const auto attackMs : { 0.02, 0.05, 0.1, 1.0, 10.0, 300.0 })
                for (const auto releaseMs : { 5.0, 150.0, 5000.0 })
                {
                    if (o.quick && attackMs > 1.0 && releaseMs > 1000.0)
                        continue;
                    auto s = measurementBase (Model::clean);
                    s.latency = latency;
                    s.thresholdDb = -30.0;
                    s.ratio = 4.0;
                    s.attackMs = attackMs;
                    s.releaseMs = releaseMs;
                    const auto level = -30.0 + 10.0 / 0.75;

                    const auto pre = samplesFor (0.05, rate), hold = samplesFor (std::max (0.25, 12.0 * attackMs / 1000.0), rate);
                    Samples key = silence (pre);
                    append (key, carrier (hold, gainOf (level)));
                    append (key, silence (samplesFor (8.0 * releaseMs / 1000.0 + 0.05, rate)));
                    const auto r = renderKey (DeviceKind::onePole, s, { key }, Probe::external, { rate, { 512 }, {}, {} });
                    const auto& trace = r.traces[0].reductionDb;
                    const auto settled = trace[(size_t) (pre + hold - 1)];
                    const auto attack = crossingSeconds (trace, pre, (1.0 - 1.0 / e) * settled, true, rate);
                    const auto release = crossingSeconds (trace, pre + hold, settled / e, false, rate);

                    const auto expectedAttack = analyticCrossing (attackMs / 1000.0, rate, true, hold);
                    const auto expectedRelease = analyticCrossing (releaseMs / 1000.0, rate, false);
                    const auto group = format ("one-pole, %s (latency %d)", latencyName (latency), r.latency);
                    const auto tolA = std::max (0.1 / rate, 1.0e-6 * expectedAttack), tolR = std::max (0.1 / rate, 1.0e-6 * expectedRelease);
                    rows.push_back (gated ("selftest", group, format ("tau %g ms rising: the method reads the sampled exponential's crossing", attackMs),
                                           1000.0 * attack, 1000.0 * expectedAttack, 1000.0 * (expectedAttack - tolA), 1000.0 * (expectedAttack + tolA), "ms",
                                           format ("%.4f x tau", attack / (attackMs / 1000.0))));
                    rows.push_back (gated ("selftest", group, format ("tau %g ms falling (after tau %g ms rising)", releaseMs, attackMs),
                                           1000.0 * release, 1000.0 * expectedRelease, 1000.0 * (expectedRelease - tolR), 1000.0 * (expectedRelease + tolR), "ms",
                                           format ("%.4f x tau", release / (releaseMs / 1000.0))));
                }
        return rows;
    }

    std::vector<Row> releaseTableRows()
    {
        // dynamics/NOTES.md, Feed-forward ballistics: h(ρ) as M0 tabulated it.
        constexpr std::pair<double, double> table[] { { 1.01, 0.0100 }, { 1.1, 0.0952 }, { 1.25, 0.2235 }, { 1.5, 0.4287 }, { 2.0, 0.8652 },
                                                      { 3.0, 1.8294 }, { 5.0, 3.8676 }, { 10.0, 8.9397 }, { 30.0, 28.982 }, { 100.0, 98.995 } };
        std::vector<Row> rows;
        for (const auto& [rho, h] : table)
        {
            const auto tol = h < 10.0 ? 6.0e-5 : 6.0e-4;   // half a unit of the printed digit
            rows.push_back (gated ("selftest", "h(rho), the transcription against M0's table", format ("rho %g", rho), reference::releaseRatio (rho), h,
                                   h - tol, h + tol, ""));
        }
        return rows;
    }

    std::vector<Row> crestRows (double rate)
    {
        // clean/NOTES.md, Auto release: C² medians over 1 s after 1 s of settling.
        struct Case
        {
            const char* name;
            Samples x;
            double expected, tolerance;
        };
        const auto n = samplesFor (2.0, rate);
        const Case cases[] {
            { "sine 1 kHz", sine (1000.0, n, rate, 0.5), 2.00, 0.02 },
            { "sine 100 Hz", sine (100.0, n, rate, 0.5), 1.99, 0.02 },
            { "sine 50 Hz", sine (50.0, n, rate, 0.5), 1.98, 0.02 },
            { "carrier", carrier (n, 0.5), 1.00, 0.01 },
            { "white noise", gaussianNoise (n, 0.1, 3u), 12.6, 1.3 },
            { "pink noise", pink (n, rate, 4u, -20.0), 11.3, 1.2 },
        };
        std::vector<Row> rows;
        for (const auto& c : cases)
        {
            std::vector<double> c2;
            for (int at = samplesFor (1.0, rate); at < n; at += 480)
                c2.push_back (reference::crestSquared ({ c.x }, rate, false, at));
            rows.push_back (gated ("selftest", "C2 of the crest transcription against clean/NOTES.md", c.name, percentile (c2, 50.0), c.expected,
                                   c.expected - c.tolerance, c.expected + c.tolerance, ""));
        }
        return rows;
    }

    std::vector<Row> clickRows (double rate)
    {
        std::vector<Row> rows;
        const auto n = samplesFor (1.0, rate);
        const auto plain = sine (330.0, n, rate, 0.25);
        const auto own = 20.0 * std::log10 (std::pow (2.0 * std::sin (std::numbers::pi * 330.0 / rate), 3.0) * std::sqrt (2.0));
        rows.push_back (gated ("selftest", "the click measure", "a steady 330 Hz sine reads its own 3rd difference", clickCrestDb (plain, 0, n), own,
                               own - 0.1, own + 0.1, "dB"));

        auto stepped = plain;
        for (int i = n / 2; i < n; ++i)
            stepped[(size_t) i] = (float) (stepped[(size_t) i] * gainOf (-0.5));
        rows.push_back (gated ("selftest", "the click measure", "a 0.5 dB gain step reads above -60 dB", clickCrestDb (stepped, 0, n), -60.0, -60.0,
                               std::numeric_limits<double>::infinity(), "dB"));

        auto faded = plain;
        const auto length = samplesFor (0.02, rate);
        for (int i = n / 2; i < n; ++i)
        {
            const auto u = std::min (1.0, (double) (i - n / 2) / length);
            faded[(size_t) i] = (float) (faded[(size_t) i] * gainOf (-20.0 * 0.5 * (1.0 - std::cos (std::numbers::pi * u))));
        }
        rows.push_back (gated ("selftest", "the click measure", "a 20 dB raised-cosine fade over 20 ms reads at or under -60 dB", clickCrestDb (faded, 0, n),
                               -60.0, -std::numeric_limits<double>::infinity(), -60.0, "dB"));
        return rows;
    }

    /** The distortion measure on sines with known harmonics and noise, and on a sine with a known gain. */
    std::vector<Row> distortionRows (double rate)
    {
        std::vector<Row> rows;
        const auto length = 50 * (int) std::lround (rate / 100.0);
        const auto n = length + 1000;
        constexpr double a = 0.5;

        // −60 dBc of 3rd harmonic and −70 dBc of 7th: THD and THD+N 10·log10(10⁻⁶ + 10⁻⁷) = −59.59 dBc.
        Samples x ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const auto w = 2.0 * std::numbers::pi * 1000.0 * i / rate;
            x[(size_t) i] = (float) (a * (std::sin (w + 0.3) + 1.0e-3 * std::sin (3.0 * w + 1.0) + std::sqrt (1.0e-7) * std::sin (7.0 * w)));
        }
        const auto expected = 10.0 * std::log10 (1.0e-6 + 1.0e-7);
        const auto d = distortionOf (x, 1000, length, rate, 1000.0, a);
        rows.push_back (gated ("selftest", "the distortion measure", "harmonics 3 and 7 at -60 and -70 dBc: THD", d.thd, expected, expected - 0.01, expected + 0.01, "dBc"));
        rows.push_back (gated ("selftest", "the distortion measure", "the same: THD+N (float samples add their own floor)", d.thdN, expected, expected - 0.05,
                               expected + 0.05, "dBc"));
        rows.push_back (gated ("selftest", "the distortion measure", "the same: the fundamental's level", d.reductionDb, 0.0, -1.0e-4, 1.0e-4, "dB"));

        // White noise at −80 dB of the sine's power: THD+N reads it, THD does not.
        auto noisy = sine (1000.0, n, rate, a);
        const auto noise = whiteNoise (n, a * std::sqrt (3.0 * 0.5e-8), 7u);
        for (int i = 0; i < n; ++i)
            noisy[(size_t) i] += noise[(size_t) i];
        const auto dn = distortionOf (noisy, 1000, length, rate, 1000.0, a);
        rows.push_back (gated ("selftest", "the distortion measure", "noise at -80 dB: THD+N", dn.thdN, -80.0, -80.5, -79.5, "dBc"));
        rows.push_back (gated ("selftest", "the distortion measure", "noise at -80 dB: THD stays far below", dn.thd, -100.0, -std::numeric_limits<double>::infinity(),
                               -90.0, "dBc"));
        return rows;
    }

    std::vector<Row> identityRows()
    {
        const auto in = whiteNoise (4096, 0.9, 5u);
        Samples delayed = silence (7);
        append (delayed, Samples (in.begin(), in.end() - 7));
        auto flipped = delayed;
        uint32_t bits;
        std::memcpy (&bits, &flipped[1000], sizeof (bits));
        bits ^= 1u;
        std::memcpy (&flipped[1000], &bits, sizeof (bits));
        return {
            gated ("selftest", "the identity comparison", "the input delayed by 7 samples", (double) delayedMismatches (delayed, in, 7), 0.0, 0.0, 0.0, "samples"),
            gated ("selftest", "the identity comparison", "one bit flipped in one sample", (double) delayedMismatches (flipped, in, 7), 1.0, 1.0, 1.0, "samples"),
        };
    }

    std::vector<Row> pinkRows (double rate)
    {
        const auto n = 1 << 18;
        const auto x = pink (n, rate, 9u, -20.0);
        std::vector<std::complex<double>> spectrum ((size_t) n);
        auto power = 0.0;
        for (int i = 0; i < n; ++i)
        {
            spectrum[(size_t) i] = x[(size_t) i];
            power += (double) x[(size_t) i] * x[(size_t) i];
        }
        fft (spectrum, -1);

        // The density per Hz in octave bands from 62.5 Hz to 8 kHz: −3.01 dB per octave by least squares.
        auto sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        auto count = 0;
        for (auto centre = 62.5; centre <= 8000.0; centre *= 2.0)
        {
            auto band = 0.0;
            const auto from = (int) (centre / std::sqrt (2.0) * n / rate), to = (int) (centre * std::sqrt (2.0) * n / rate);
            for (int k = from; k < to; ++k)
                band += std::norm (spectrum[(size_t) k]);
            const auto xo = std::log2 (centre), yo = 10.0 * std::log10 (band / (to - from));
            sx += xo;
            sy += yo;
            sxx += xo * xo;
            sxy += xo * yo;
            ++count;
        }
        const auto slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);
        const auto rmsDb = 10.0 * std::log10 (power / n);
        return {
            gated ("selftest", "the pink noise", "density slope per octave, 62.5 Hz - 8 kHz", slope, -3.0103, -3.0103 - 0.1, -3.0103 + 0.1, "dB"),
            gated ("selftest", "the pink noise", "RMS", rmsDb, -20.0, -20.01, -19.99, "dBFS"),
        };
    }

    /**
        The processor's plumbing: controls set through the parameters read back as set, and the
        probe's trace through the real processBlock reads Output −6 dB as a 6 dB gain drop.
    */
    std::vector<Row> processorRows (const Options& o)
    {
        std::vector<Row> rows;
        Settings s;
        s.thresholdDb = -30.0;
        s.ratio = infiniteRatio;
        s.attackMs = 0.02;
        s.releaseMs = 5000.0;
        s.kneeDb = 24.0;
        s.lookaheadMs = 3.0;
        s.linkPercent = 50.0;
        s.outputDb = -6.0;

        const auto n = samplesFor (0.5, o.rate);
        const auto r = renderKey (DeviceKind::processor, s, { silence (n) }, Probe::external, { o.rate, { 512 }, {}, {} });
        const auto& a = r.applied;
        const std::pair<double, double> pairs[] { { a.thresholdDb, -30.0 }, { a.attackMs, 0.02 }, { a.releaseMs, 5000.0 }, { a.kneeDb, 24.0 },
                                                   { a.lookaheadMs, 3.0 }, { a.linkPercent, 50.0 }, { a.outputDb, -6.0 } };
        auto worst = std::isinf (a.ratio) && a.key == KeySource::external ? 0.0 : 1.0;
        for (const auto& [got, set] : pairs)
            worst = std::max (worst, std::abs (got - set) / std::abs (set));
        rows.push_back (gated ("selftest", "the processor", "controls set through the parameters read back as set (largest relative error)", worst, 0.0, 0.0,
                               1.0e-5, ""));

        const auto settled = r.traces[0].reductionDb[(size_t) n - 1];
        rows.push_back (gated ("selftest", "the processor", "Output -6 dB reads as a 6 dB drop through the probe's trace", settled, 6.0, 6.0 - 1.0e-5,
                               6.0 + 1.0e-5, "dB", format ("latency %d", r.latency)));
        return rows;
    }

    /** The probe paths agree: the same curve read through the side chain and through the main input. */
    std::vector<Row> probeRows (const Options& o)
    {
        auto external = o, internal = o;
        external.device = internal.device = DeviceKind::reference;
        external.probe = Probe::external;
        internal.probe = Probe::internal;
        std::vector<Row> rows;
        for (const auto signal : { Signal::sine, Signal::carrier })
        {
            const auto a = staticCurveRow (external, Model::clean, -30.0, 4.0, 6.0, 40.0, 10.0, signal, 1000.0, true);
            const auto b = staticCurveRow (internal, Model::clean, -30.0, 4.0, 6.0, 40.0, 10.0, signal, 1000.0, true);
            rows.push_back (gated ("selftest", "the probe paths", format ("%s curve: internal against external, largest error", signal == Signal::sine ? "sine" : "carrier"),
                                   b.measured - a.measured, 0.0, -0.001, 0.001, "dB", format ("external %.4f, internal %.4f dB", a.measured, b.measured)));
        }
        return rows;
    }

    /** A gate run on the transcription: every row must pass, as on a correct engine. */
    std::vector<Row> onReference (std::vector<Row> rows, const char* what)
    {
        for (auto& r : rows)
        {
            r.group = std::string ("reference ") + what + ": " + r.group;
            r.gate = "selftest";
        }
        return rows;
    }
}

std::vector<Row> selfTest (const Options& o)
{
    auto ref = o;
    ref.device = DeviceKind::reference;
    ref.probe = Probe::external;
    ref.model = Model::clean;
    ref.quick = true;
    ref.latencies = { LatencyMode::normal, LatencyMode::live };
    ref.jobs = 1;   // the outer jobs already run in parallel

    std::vector<Job> jobs {
        [&o] { return onePoleRows (o); },
        [] { return releaseTableRows(); },
        [&o] { return crestRows (o.rate); },
        [&o] { return clickRows (o.rate); },
        [&o] { return distortionRows (o.rate); },
        [] { return identityRows(); },
        [&o] { return pinkRows (o.rate); },
        [&o] { return probeRows (o); },
        [&o] { return processorRows (o); },
        [ref] { return onReference (staticCurve (ref), "static"); },
        [ref] { return onReference (ballistics (ref), "ballistics"); },
        [ref] { return onReference (stereo (ref), "stereo"); },
        [ref] { return onReference (identity (ref), "identity"); },
    };
    return runJobs (jobs, o.jobs);
}

} // namespace basalt::measure
