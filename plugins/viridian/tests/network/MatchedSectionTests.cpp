#include "NetworkTestSupport.h"
#include "dsp/common/FrequencyMap.h"
#include "dsp/common/MatchedSection.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <map>
#include <random>

using namespace viridian;
using namespace viridian::dsp;
using namespace viridian::testing;

namespace
{
    /** Worst |dB| between the realised section and its analog prototype, 10 Hz ... top. */
    double worstErrorDb (const MatchedSection& m, double top)
    {
        double worst = 0.0;
        for (double f : logFrequencies (top))
            worst = std::max (worst, std::abs (toDb (std::abs (m.response (f))) - toDb (std::abs (m.analog().response (f)))));
        return worst;
    }

    /** The main stage is stable exactly when g > 0 (and k > 0 for the SVF); the correction when
        its poles lie inside the unit circle. */
    bool isStable (const MatchedSection& m)
    {
        const auto& st = m.stage();
        if (! (st.g > 0.0) || (st.order == 2 && ! (st.k > 0.0)))
            return false;
        const auto& c = m.correction();
        if (c.order == 0)
            return true;
        std::array<std::complex<double>, MatchedSection::maxCorrectionOrder + 1> p {};
        for (int k = 0; k <= c.order; ++k)
            p[(size_t) k] = c.a[(size_t) (c.order - k)];   // z^order a (1/z), ascending in z
        std::array<std::complex<double>, poly::maxDegree> r {};
        const int n = poly::roots ({ p.data(), (size_t) c.order + 1 }, r, false);
        for (int k = 0; k < n; ++k)
            if (! (std::abs (r[(size_t) k]) < 1.0))
                return false;
        return true;
    }

    const FrequencyMap& mapAt (double fs)
    {
        static std::map<double, FrequencyMap> maps;
        auto& m = maps[fs];
        if (! myzel::dsp::isExactly (m.sampleRate(), fs))
            m.prepare (fs);
        return m;
    }

    /** Analog-matched gates, measured 2026-11-12 (tools/netlist/README.md): a margin above the
        worst case of the grid, so a regression shows. */
    double gateDb (double fs)
    {
        return fs < 47000.0 ? 0.012 : (fs < 60000.0 ? 0.003 : 0.001);
    }
} // namespace

TEST_CASE ("MatchedSection: the frequency map's error, degree and shape per rate", "[network][map]")
{
    struct Expect
    {
        double fs;
        int degree;
        double bound;
    };
    for (const auto& e : { Expect { 44100.0, 3, 2.0e-4 }, Expect { 48000.0, 3, 4.0e-5 }, Expect { 88200.0, 3, 2.0e-5 },
                           Expect { 96000.0, 3, 2.0e-5 }, Expect { 176400.0, 2, 2.0e-5 }, Expect { 192000.0, 2, 2.0e-5 } })
    {
        const auto& m = mapAt (e.fs);
        // The error on a grid the fit never saw, against the exact axis asin^2 (sqrt t).
        double worst = 0.0, previous = 0.0;
        bool monotone = true;
        const auto top = std::sin (std::numbers::pi * m.bandTopHz() / e.fs);
        for (int k = 1; k <= 20000; ++k)
        {
            const auto t = top * top * k / 20000.0;
            const auto exact = std::pow (std::asin (std::sqrt (t)), 2.0);
            worst = std::max (worst, std::abs (m.map (t) / exact - 1.0));
        }
        for (int k = 1; k <= 20000; ++k)
        {
            const auto v = m.map (k / 20000.0);
            monotone = monotone && v > previous;
            previous = v;
        }
        INFO (e.fs << " Hz: degree " << m.degree() << ", relative error of Omega^2 " << worst << " (fit says "
                   << m.maxRelativeError() << "), Nyquist reads " << m.analogOmega (e.fs / 2) / twoPi << " Hz");
        CHECK (m.degree() == e.degree);
        CHECK (worst <= e.bound);
        CHECK (worst <= 1.05 * m.maxRelativeError() + 1.0e-12);
        CHECK (monotone);
        CHECK (myzel::dsp::isExactly (m.bandTopHz(), e.fs < 64000.0 ? 20000.0 : 32000.0));
    }
}

TEST_CASE ("MatchedSection: digital roots reproduce their analog roots' magnitude exactly", "[network][map]")
{
    // A zero pair over a pole pair (each root with its conjugate): the squared magnitude of their
    // digital roots is proportional to |(jO - rz)(jO - conj rz) / ((jO - rp)(jO - conj rp))|^2 at
    // O = Omega (w), the composition identity (the map's denominator cancels between them).
    std::mt19937 rng (5);
    std::uniform_real_distribution<double> logf (std::log (5.0), std::log (60000.0)), angle (0.5 * std::numbers::pi, std::numbers::pi);
    for (double fs : rates)
    {
        const auto& m = mapAt (fs);
        double worst = 0.0;
        for (int trial = 0; trial < 200; ++trial)
        {
            const auto rz = std::polar (twoPi * std::exp (logf (rng)), angle (rng));
            const auto rp = std::polar (twoPi * std::exp (logf (rng)), angle (rng));
            std::array<std::complex<double>, 3> zz {}, zp {};
            FrequencyMap::Track tz, tp;
            m.digitalRoots ({ rz, 1.0 }, false, zz, tz);
            m.digitalRoots ({ rp, 1.0 }, false, zp, tp);
            double lo = 1.0e300, hi = 0.0;
            for (double f : logFrequencies (m.bandTopHz(), 6))
            {
                const auto e = std::polar (1.0, -twoPi * f / fs);
                std::complex<double> d { 1.0, 0.0 };
                for (int k = 0; k < m.degree(); ++k)
                    d *= (1.0 - zz[(size_t) k] * e) * (1.0 - std::conj (zz[(size_t) k]) * e)
                         / ((1.0 - zp[(size_t) k] * e) * (1.0 - std::conj (zp[(size_t) k]) * e));
                const std::complex<double> jw { 0.0, m.analogOmega (f) };
                const auto ratio = std::norm (d) / std::norm ((jw - rz) * (jw - std::conj (rz)) / ((jw - rp) * (jw - std::conj (rp))));
                lo = std::min (lo, ratio);
                hi = std::max (hi, ratio);
            }
            worst = std::max (worst, hi / lo - 1.0);
        }
        INFO (fs << " Hz: worst relative spread " << worst);
        CHECK (worst < 1.0e-6);   // rounding near high-Q roots; 1e-6 is 4e-6 dB
    }
}

TEST_CASE ("MatchedSection: a fresh design's primary root is continuous along every ray", "[network][map]")
{
    // Constant-Q frequency sweeps restarted from scratch: the primary moves continuously (where a
    // 1 % step moves it far, no one of ten sub-steps carries most of it, as a jump would), and
    // agrees with a sweep that tracks it.
    for (double fs : rates)
    {
        const auto& m = mapAt (fs);
        auto primary = [&] (double hz, double angle)
        {
            std::array<std::complex<double>, 3> z {};
            FrequencyMap::Track none;
            m.digitalRoots ({ std::polar (twoPi * hz, angle), 1.0 }, false, z, none);
            return z[0];
        };
        double worstRatio = 0.0, worstTrack = 0.0;
        for (double angle = 0.5 * std::numbers::pi; angle <= std::numbers::pi + 1.0e-9; angle += std::numbers::pi / 36.0)
        {
            FrequencyMap::Track tracked;
            for (double hz = 10.0; hz < 100000.0; hz *= 1.01)
            {
                const auto a = primary (hz, angle), b = primary (hz * 1.01, angle);
                const auto step = std::abs (b - a);
                if (step > 0.01)
                {
                    double largest = 0.0;
                    auto previous = a;
                    for (int k = 1; k <= 10; ++k)
                    {
                        const auto next = primary (hz * std::pow (1.01, k / 10.0), angle);
                        largest = std::max (largest, std::abs (next - previous));
                        previous = next;
                    }
                    worstRatio = std::max (worstRatio, largest / step);
                }
                std::array<std::complex<double>, 3> followed {};
                m.digitalRoots ({ std::polar (twoPi * hz, angle), 1.0 }, false, followed, tracked);
                worstTrack = std::max (worstTrack, std::abs (followed[0] - a));
            }
        }
        INFO (fs << " Hz: largest sub-step over its 1 % step " << worstRatio << ", largest difference from a tracked sweep " << worstTrack);
        CHECK (worstRatio < 0.5);
        CHECK (worstTrack < 1.0e-9);
    }
}

TEST_CASE ("MatchedSection: magnitude matches the analog prototype to 20 kHz at every rate", "[network][matched]")
{
    const auto grid = sectionGrid();
    for (double fs : rates)
    {
        const auto& map = mapAt (fs);
        double worst = 0.0;
        std::string worstName;
        for (const auto& c : grid)
        {
            MatchedSection m;
            m.design (map, c.section);
            const auto e = worstErrorDb (m, 20000.0);
            if (e > worst)
            {
                worst = e;
                worstName = c.name;
            }
        }
        INFO (fs << " Hz: worst " << worst << " dB (" << worstName << ") over " << grid.size() << " sections");
        CHECK (worst <= gateDb (fs));
    }
}

TEST_CASE ("MatchedSection: error by octave, cramping region included", "[network][matched][.report]")
{
    const auto grid = sectionGrid();
    const double edges[] { 10.0, 160.0, 640.0, 2500.0, 5000.0, 10000.0, 14000.0, 17000.0, 20000.0 };
    std::printf ("\nworst |dB| between each realised section and its analog prototype, %zu sections\n", grid.size());
    std::printf ("   rate  map  |");
    for (size_t b = 0; b + 1 < std::size (edges); ++b)
        std::printf (" %5.0f-%-5.0f", edges[b], edges[b + 1]);
    std::printf (" | to Nyquist\n");
    for (double fs : rates)
    {
        const auto& map = mapAt (fs);
        std::array<double, std::size (edges)> worst {};
        for (const auto& c : grid)
        {
            MatchedSection m;
            m.design (map, c.section);
            for (double f : logFrequencies (fs / 2 * 0.999, 96))
            {
                const auto e = std::abs (toDb (std::abs (m.response (f))) - toDb (std::abs (m.analog().response (f))));
                size_t b = 0;
                while (b + 1 < std::size (edges) && f > edges[b + 1])
                    ++b;
                worst[b] = std::max (worst[b], e);
            }
        }
        std::printf (" %6.1f  D=%d  |", fs / 1000.0, map.degree());
        for (size_t b = 0; b + 1 < std::size (edges); ++b)
            std::printf ("  %9.5f  ", worst[b]);
        std::printf (" | %.3f\n", worst[std::size (edges) - 1]);
    }
}

TEST_CASE ("MatchedSection: sections near and above Nyquist", "[network][matched]")
{
    for (double fs : { 44100.0, 48000.0 })
    {
        const auto& map = mapAt (fs);
        for (double f0 : { 20000.0, 22000.0, 26000.0, 40000.0 })
            for (double q : { 0.4, 1.0, 4.0 })
                for (double g : { -15.0, 15.0 })
                {
                    MatchedSection m;
                    m.design (map, bell (f0, q, g));
                    INFO (fs << " Hz, bell " << f0 << " Hz Q " << q << " " << g << " dB");
                    CHECK (worstErrorDb (m, 20000.0) <= gateDb (fs));
                    CHECK (isStable (m));
                }
    }
}

TEST_CASE ("MatchedSection: stable for every section of the grid and for unstable prototypes", "[network][matched]")
{
    for (double fs : rates)
        for (const auto& c : sectionGrid())
        {
            MatchedSection m;
            m.design (mapAt (fs), c.section);
            INFO (fs << " Hz, " << c.name);
            REQUIRE (isStable (m));
        }

    // An analog pole in the right half-plane is reflected, never realised unstable.
    MatchedSection m;
    m.design (mapAt (48000.0), fromS (1.0, 0.0, 0.0, 1.0, -1.0 / (twoPi * 500.0), 0.0));
    CHECK (m.reflectedUnstablePole());
    CHECK (isStable (m));
}

TEST_CASE ("MatchedSection: the response function is the realised filter's (impulse response DFT)", "[network][matched]")
{
    for (double fs : { 44100.0, 192000.0 })
    {
        const auto& map = mapAt (fs);
        for (const auto& s : { bell (1000.0, 2.0, 12.0), bell (16000.0, 0.8, -15.0), lowShelf (80.0, 0.7, 12.0),
                               highpass2 (30.0, 0.7), shelf1 (3000.0, -9.0, true), lowpass1 (12000.0) })
        {
            MatchedSection m;
            m.design (map, s);
            std::vector<double> ir ((size_t) fs * 2, 0.0);
            ir[0] = 1.0;
            double* ch[] { ir.data() };
            m.process (ch, 1, (int) ir.size());
            double worst = 0.0;
            for (double f : logFrequencies (fs / 2 * 0.95, 4))
            {
                if (f < 25.0)
                    continue;   // the IR window resolves the lowest corners only to 1e-9 above here
                worst = std::max (worst, std::abs (toDb (std::abs (dft (ir, f, fs))) - toDb (std::abs (m.response (f)))));
            }
            INFO (fs << " Hz: worst " << worst << " dB");
            CHECK (worst < 1.0e-7);
        }
    }
}

TEST_CASE ("MatchedSection: phase of low and mid sections, right half-plane zeros", "[network][matched]")
{
    // A magnitude-matched section is minimum phase over 0 ... fs/2; the analog section's phase
    // also carries its magnitude above Nyquist, so the two part where a section acts near the
    // top (the report below). Sections that act below 2 kHz meet the plan's 2 degrees to 10 kHz.
    for (double fs : rates)
    {
        const auto& map = mapAt (fs);
        double worst = 0.0;
        for (const auto& s : { bell (1000.0, 1.0, 12.0), bell (300.0, 2.0, -15.0), lowShelf (100.0, 0.7, 15.0),
                               lowShelf (500.0, 0.5, -12.0), highpass2 (80.0, 0.7), shelf1 (200.0, 9.0, false) })
        {
            MatchedSection m;
            m.design (map, s);
            for (double f : logFrequencies (10000.0, 12))
                worst = std::max (worst, std::abs (std::arg (m.response (f) / s.response (f))) * 180.0 / std::numbers::pi);
        }
        INFO (fs << " Hz: phase within " << worst << " deg of the analog sections to 10 kHz");
        CHECK (worst < 2.0);

        // An analog all-pass: flat magnitude, all its phase from a zero in the right half-plane.
        // Reflected to minimum phase it would be 180 degrees off at high frequencies.
        MatchedSection allpass;
        const auto w = twoPi * 2000.0;
        allpass.design (map, fromS (-w, 1.0, 0.0, w, 1.0, 0.0));
        double flat = 0.0, phase = 0.0;
        for (double f : logFrequencies (10000.0, 12))
        {
            flat = std::max (flat, std::abs (toDb (std::abs (allpass.response (f)))));
            phase = std::max (phase, std::abs (std::arg (allpass.response (f) / allpass.analog().response (f))) * 180.0 / std::numbers::pi);
        }
        INFO (fs << " Hz all-pass at 2 kHz: " << flat << " dB from flat, phase within " << phase << " deg to 10 kHz");
        CHECK (flat < 1.0e-9);
        CHECK (phase < 5.0);
    }
}

TEST_CASE ("MatchedSection: phase error report", "[network][matched][.report]")
{
    std::printf ("\nworst phase difference (degrees) between section and analog prototype, 10 Hz ... 10 kHz\n");
    const Case cases[] { { "bell 1 kHz Q 1 +12", bell (1000.0, 1.0, 12.0) }, { "bell 8 kHz Q 0.8 -15", bell (8000.0, 0.8, -15.0) },
                         { "bell 16 kHz Q 0.8 +15", bell (16000.0, 0.8, 15.0) }, { "high shelf 6 kHz -12", highShelf (6000.0, 0.7, -12.0) },
                         { "low-pass 15 kHz", lowpass2 (15000.0, 0.707) }, { "low-pass 22 kHz", lowpass2 (22000.0, 0.707) },
                         { "high-pass 80 Hz", highpass2 (80.0, 0.707) }, { "all-pass 2 kHz", fromS (-twoPi * 2000.0, 1.0, 0.0, twoPi * 2000.0, 1.0, 0.0) } };
    for (const auto& c : cases)
    {
        std::printf ("  %-24s", c.name.c_str());
        for (double fs : rates)
        {
            MatchedSection m;
            m.design (mapAt (fs), c.section);
            double worst = 0.0;
            for (double f : logFrequencies (10000.0, 12))
                worst = std::max (worst, std::abs (std::arg (m.response (f) / c.section.response (f))) * 180.0 / std::numbers::pi);
            std::printf (" %6.1f k: %5.2f", fs / 1000.0, worst);
        }
        std::printf ("\n");
    }
}

TEST_CASE ("MatchedSection: sign follows the analog section", "[network][matched]")
{
    MatchedSection m;
    m.design (mapAt (48000.0), fromS (-1.0, 0.0, 0.0, 1.0, 1.0 / (twoPi * 1000.0), 0.0));
    CHECK (m.response (20.0).real() < 0.0);
    m.design (mapAt (48000.0), highpass2 (100.0, 0.7));
    CHECK (m.response (10000.0).real() > 0.0);
}

namespace
{
    /** A sine through a section re-designed every 32 samples along `at (progress 0 ... 1)`, each
        design ramped in over the next 32 samples as Network does. */
    template <typename Design>
    double sweepCrestDb (double fs, double seconds, double tone, const Design& at)
    {
        const auto& map = mapAt (fs);
        MatchedSection m;
        m.design (map, at (0.0));
        const auto n = (size_t) (fs * seconds);
        std::vector<double> x (n);
        for (size_t i = 0; i < n; ++i)
            x[i] = 0.25 * std::sin (twoPi * tone * (double) i / fs);
        for (size_t i = 0; i < n; i += 32)
        {
            m.design (map, at ((double) i / (double) n), 32);
            double* ch[] { x.data() + i };
            m.process (ch, 1, (int) std::min<size_t> (32, n - i));
        }
        return clickCrestDb (x, (size_t) (0.05 * fs));
    }
} // namespace

TEST_CASE ("MatchedSection: sweeps are smooth (3rd-difference crest)", "[network][matched][modulation]")
{
    // The plan's automation gate: -60 dB. A 330 Hz sine's own 3rd difference sits near -80 dB at 48 kHz.
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto steady = sweepCrestDb (fs, 1.0, 330.0, [] (double) { return bell (1000.0, 1.0, 0.0); });
        struct Sweep
        {
            const char* name;
            dsp::AnalogSection (*at) (double);
        };
        const Sweep sweeps[] {
            { "bell 50 Hz -> 30 kHz, Q 2, +15 dB", [] (double p) { return bell (50.0 * std::pow (600.0, p), 2.0, 15.0); } },
            { "bell 30 kHz -> 50 Hz, Q 0.4, -18 dB", [] (double p) { return bell (30000.0 * std::pow (600.0, -p), 0.4, -18.0); } },
            { "bell gain -18 -> +18 dB at 3 kHz, Q 0.7 (real <-> complex poles)", [] (double p) { return bell (3000.0, 0.7, -18.0 + 36.0 * p); } },
            { "bell Q 0.3 -> 4 at 15 kHz, -12 dB", [] (double p) { return bell (15000.0, 0.3 * std::pow (4.0 / 0.3, p), -12.0); } },
            { "low shelf 20 Hz -> 2 kHz, +12 dB", [] (double p) { return lowShelf (20.0 * std::pow (100.0, p), 0.7, 12.0); } },
            { "high shelf 25 kHz -> 1 kHz, -15 dB", [] (double p) { return highShelf (25000.0 * std::pow (25.0, -p), 1.0, -15.0); } },
            { "high-pass 10 Hz -> 1 kHz", [] (double p) { return highpass2 (10.0 * std::pow (100.0, p), 0.7); } },
            { "low-pass 22 kHz -> 3 kHz", [] (double p) { return lowpass2 (22000.0 * std::pow (22000.0 / 3000.0, -p), 0.7); } },
            { "1st-order high shelf 500 Hz -> 20 kHz, +9 dB", [] (double p) { return shelf1 (500.0 * std::pow (40.0, p), 9.0, true); } },
        };
        for (const auto& s : sweeps)
        {
            const auto crest = sweepCrestDb (fs, 1.0, 330.0, s.at);
            INFO (fs << " Hz, " << s.name << ": " << crest << " dB (steady " << steady << " dB)");
            CHECK (crest <= std::max (-60.0, steady + 3.0));
        }
    }
}

TEST_CASE ("MatchedSection: denormals are flushed, silence in gives silence out", "[network][matched]")
{
    MatchedSection m;
    m.design (mapAt (48000.0), lowShelf (40.0, 0.7, 12.0));
    std::vector<double> x (48000, 0.0);
    for (size_t i = 0; i < 4800; ++i)
        x[i] = std::sin (twoPi * 100.0 * (double) i / 48000.0);
    bool subnormal = false;
    for (size_t i = 0; i < x.size(); i += 32)
    {
        double* ch[] { x.data() + i };
        m.process (ch, 1, 32);
        m.flushDenormals();
        for (int k = 0; k < 32; ++k)
            subnormal = subnormal || std::fpclassify (x[i + (size_t) k]) == FP_SUBNORMAL;
    }
    std::vector<double> silence (48000 * 20, 0.0);
    for (size_t i = 0; i < silence.size(); i += 32)
    {
        double* ch[] { silence.data() + i };
        m.process (ch, 1, 32);
        m.flushDenormals();
        for (int k = 0; k < 32; ++k)
            subnormal = subnormal || std::fpclassify (silence[i + (size_t) k]) == FP_SUBNORMAL;
    }
    CHECK_FALSE (subnormal);
    CHECK (std::all_of (silence.end() - 48000, silence.end(), [] (double v) { return myzel::dsp::isExactly (v, 0.0); }));
}
