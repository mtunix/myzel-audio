#include "NetworkTestSupport.h"
#include "dsp/common/Network.h"
#include "network/generated/BaxandallShelf.h"
#include "network/generated/BaxandallShelfVectors.h"
#include "network/generated/GyratorBell.h"
#include "network/generated/GyratorBellVectors.h"
#include "network/generated/Hpf18.h"
#include "network/generated/Hpf18Vectors.h"
#include "network/generated/Ladder3.h"
#include "network/generated/Ladder3Vectors.h"
#include "network/generated/RcLowpass.h"
#include "network/generated/RcLowpassVectors.h"
#include "network/generated/RlcBell.h"
#include "network/generated/RlcBellVectors.h"
#include "network/generated/SteppedBell.h"
#include "network/generated/SteppedBellVectors.h"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <type_traits>

using namespace viridian;
using namespace viridian::dsp;
using namespace viridian::testing;
namespace gen = viridian::dsp::generated;

namespace
{
    template <typename Topology>
    struct VectorsOf;
    template <>
    struct VectorsOf<gen::RcLowpass> { using type = gen::RcLowpassVectors; };
    template <>
    struct VectorsOf<gen::RlcBell> { using type = gen::RlcBellVectors; };
    template <>
    struct VectorsOf<gen::GyratorBell> { using type = gen::GyratorBellVectors; };
    template <>
    struct VectorsOf<gen::BaxandallShelf> { using type = gen::BaxandallShelfVectors; };
    template <>
    struct VectorsOf<gen::Ladder3> { using type = gen::Ladder3Vectors; };
    template <>
    struct VectorsOf<gen::Hpf18> { using type = gen::Hpf18Vectors; };

    template <typename Topology>
    typename Network<Topology>::Controls controlsOf (const double* values)
    {
        typename Network<Topology>::Controls c {};
        for (int i = 0; i < Topology::numControls; ++i)
            c[(size_t) i] = values[i];
        return c;
    }

    /** A prepared network at `values`, designed from scratch. */
    template <typename Topology>
    std::unique_ptr<Network<Topology>> networkAt (double fs, const typename Network<Topology>::Controls& values)
    {
        auto n = std::make_unique<Network<Topology>>();
        n->setTargets (values);
        n->prepare (fs);
        return n;
    }

    template <typename Topology>
    std::vector<typename Network<Topology>::Controls> controlGrid (int random, unsigned seed)
    {
        std::vector<typename Network<Topology>::Controls> out;
        std::mt19937 rng (seed);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        for (int k = 0; k < random + 2; ++k)
        {
            typename Network<Topology>::Controls c {};
            for (int i = 0; i < Topology::numControls; ++i)
            {
                const auto& info = Topology::controls[(size_t) i];
                if (info.kind == NetworkControl::Kind::selector)
                    c[(size_t) i] = (double) (k % info.positions);
                else
                    c[(size_t) i] = k == 0 ? 0.0 : (k == 1 ? 1.0 : u (rng));
            }
            out.push_back (c);
        }
        return out;
    }

    /** The worst |dB| between the realised network and its analog H(s), 10 Hz ... 20 kHz. */
    template <typename Topology>
    double worstDb (const Network<Topology>& n)
    {
        double worst = 0.0, peak = 0.0;
        const auto f = logFrequencies (20000.0, 24);
        for (double x : f)
            peak = std::max (peak, std::abs (n.analogResponse (x)));
        for (double x : f)
        {
            const auto a = std::abs (n.analogResponse (x));
            if (a < 1.0e-5 * peak)
                continue;   // a pot at its end shorting the path: no level to compare in dB
            worst = std::max (worst, std::abs (toDb (std::abs (n.response (x))) - toDb (a)));
        }
        return worst;
    }

    double gateDb (double fs) { return fs < 47000.0 ? 0.012 : (fs < 60000.0 ? 0.003 : 0.001); }

    /**
        One pot moving a high-pass from 300 Hz to 10 Hz, optionally times a fixed first-order
        low-pass (degree 3: a root cascade). `Scaled` writes the same H(s) with the block's
        polynomials and the gain scaled against each other by factors in the pot, as a generated
        evaluate() may.
    */
    template <bool Scaled, bool Cascade>
    struct MovingHighPass
    {
        static constexpr int numControls = 1;
        static constexpr std::array<NetworkControl, 1> controls { { { "freq", NetworkControl::Kind::pot, 0, 0.0 } } };
        static constexpr int maxBlocks = 1;
        static constexpr int maxDegree = 3;
        static constexpr int maxSections = 1;
        static constexpr double omegaRef = twoPi * 1000.0;
        using Polynomials = NetworkPolynomials<maxBlocks, maxDegree>;

        static void evaluate (const double* c, Polynomials& out) noexcept
        {
            const auto r = 0.3 * std::pow (10.0 / 300.0, c[0]), q = 0.7, corner = 5.0;
            const auto numScale = Scaled ? (1.0 + 20.0 * c[0]) * (1.0 + 20.0 * c[0]) : 1.0;
            const auto denScale = Scaled ? 1.0 + 20.0 * c[0] : 1.0;
            std::array<double, 4> num { 0.0, 0.0, 1.0, 0.0 }, den { r * r, r / q, 1.0, 0.0 };
            if constexpr (Cascade)
            {
                num = { 0.0, 0.0, corner, 0.0 };
                den = { corner * r * r, corner * r / q + r * r, corner + r / q, 1.0 };
            }
            out.numBlocks = 1;
            auto& b = out.blocks[0];
            b.numDegree = 2;
            b.denDegree = Cascade ? 3 : 2;
            b.controls = 1u;
            for (size_t k = 0; k < 4; ++k)
            {
                b.num[k] = num[k] * numScale;
                b.den[k] = den[k] * denScale;
            }
            out.gain = denScale / numScale;
        }
    };

    /** A 330 Hz sine through a network whose pot jumps 0 -> 1 halfway, at 32-sample blocks. */
    template <typename Topology>
    std::vector<double> potJump (double fs)
    {
        Network<Topology> n;
        n.setTargets ({ 0.0 });
        n.prepare (fs);
        const auto total = (size_t) (0.6 * fs);
        std::vector<double> x (total);
        for (size_t i = 0; i < total; ++i)
            x[i] = 0.01 * std::sin (twoPi * 330.0 * (double) i / fs);
        for (size_t i = 0; i < total; i += 32)
        {
            n.setTargets ({ i < total / 2 ? 0.0 : 1.0 });
            double* ch[] { x.data() + i };
            n.process (ch, 1, (int) std::min<size_t> (32, total - i));
        }
        return x;
    }
} // namespace

TEMPLATE_TEST_CASE ("Network: the output does not depend on how evaluate() scales a block against the gain", "[network][modulation]",
                    std::true_type, std::false_type)
{
    // evaluate()'s split between a block and the gain is arbitrary; designed normalised, a block's
    // level follows its roots alone, so two scalings of the same H(s) sound the same.
    constexpr bool cascade = TestType::value;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const auto plain = potJump<MovingHighPass<false, cascade>> (fs);
        const auto scaled = potJump<MovingHighPass<true, cascade>> (fs);
        double peak = 0.0, difference = 0.0;
        for (size_t i = 0; i < plain.size(); ++i)
        {
            peak = std::max (peak, std::abs (plain[i]));
            difference = std::max (difference, std::abs (scaled[i] - plain[i]));
        }
        const auto crest = std::max (clickCrestDb (plain, (size_t) (0.05 * fs)), clickCrestDb (scaled, (size_t) (0.05 * fs)));
        INFO ((cascade ? "root cascade" : "matched section") << ", " << fs << " Hz: difference " << toDb (difference / peak)
                                                             << " dB re peak, crest " << crest << " dB");
        CHECK (difference <= 1.0e-9 * peak);
        CHECK (crest <= -60.0);
    }
}

TEMPLATE_TEST_CASE ("Network: the generated code reproduces the netlist's exact H(s)", "[network][generated]",
                    gen::RcLowpass, gen::RlcBell, gen::GyratorBell, gen::BaxandallShelf, gen::Ladder3, gen::Hpf18)
{
    using Topology = TestType;
    using Vectors = typename VectorsOf<TestType>::type;
    double worst = 0.0;
    for (const auto& p : Vectors::points)
    {
        Network<Topology> n;
        n.setTargets (controlsOf<Topology> (p.controls.data()));
        n.prepare (48000.0);
        const std::complex<double> exact { p.re, p.im };
        const auto h = n.analogResponse (p.frequency);
        worst = std::max (worst, std::abs (h - exact) / std::max (std::abs (exact), 1.0e-12));
    }
    INFO ("worst relative error against the netlist's exact response: " << worst);
    CHECK (worst < 1.0e-10);
}

TEMPLATE_TEST_CASE ("Network: digital response matches the analog network to 20 kHz", "[network]",
                    gen::RcLowpass, gen::RlcBell, gen::GyratorBell, gen::BaxandallShelf, gen::Ladder3, gen::Hpf18, gen::SteppedBell)
{
    for (double fs : rates)
    {
        double worst = 0.0;
        for (const auto& c : controlGrid<TestType> (12, 3))
            worst = std::max (worst, worstDb (*networkAt<TestType> (fs, c)));
        INFO (fs << " Hz: worst " << worst << " dB");
        CHECK (worst <= gateDb (fs));
    }
}

TEMPLATE_TEST_CASE ("Network: display = audio (impulse response DFT equals response())", "[network]",
                    gen::GyratorBell, gen::Ladder3, gen::Hpf18, gen::SteppedBell)
{
    for (double fs : { 44100.0, 96000.0 })
    {
        auto n = networkAt<TestType> (fs, controlGrid<TestType> (1, 9).back());
        std::vector<double> ir ((size_t) fs * 2, 0.0);
        ir[0] = 1.0;
        double* ch[] { ir.data() };
        n->process (ch, 1, (int) ir.size());
        double worst = 0.0;
        for (double f : logFrequencies (fs / 2 * 0.95, 4))
            if (f >= 25.0)
                worst = std::max (worst, std::abs (toDb (std::abs (dft (ir, f, fs))) - toDb (std::abs (n->response (f)))));
        INFO (fs << " Hz: worst " << worst << " dB");
        CHECK (worst < 1.0e-6);
    }
}

TEST_CASE ("Network: a block of degree 3 runs as a root cascade: a pair of poles and one alone (Ladder3)", "[network]")
{
    auto n = networkAt<gen::Ladder3> (48000.0, { 0.3, 0.7 });
    REQUIRE (n->numCascadeBlocks() == 1);
    CHECK (n->cascade (0).numPoles() == 3);
    CHECK (n->cascade (0).numStages() == 2);
    CHECK (worstDb (*n) <= gateDb (48000.0));
}

TEMPLATE_TEST_CASE ("Network: sweeps are smooth (3rd-difference crest)", "[network][modulation]",
                    gen::GyratorBell, gen::Ladder3, gen::Hpf18, gen::BaxandallShelf)
{
    for (double fs : { 44100.0, 96000.0 })
    {
        // A 330 Hz sine while every pot glides 0 -> 1 -> 0 over 2 s (targets set every 32 samples).
        Network<TestType> n;
        typename Network<TestType>::Controls c {};
        for (int i = 0; i < TestType::numControls; ++i)
            c[(size_t) i] = TestType::controls[(size_t) i].kind == NetworkControl::Kind::pot ? 0.0 : TestType::controls[(size_t) i].defaultValue;
        n.setTargets (c);
        n.prepare (fs);
        const auto total = (size_t) (2.0 * fs);
        std::vector<double> x (total);
        for (size_t i = 0; i < total; ++i)
            x[i] = 0.25 * std::sin (twoPi * 330.0 * (double) i / fs);
        for (size_t i = 0; i < total; i += 32)
        {
            const auto p = (double) i / (double) total;
            const auto v = p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p;
            for (int k = 0; k < TestType::numControls; ++k)
                if (TestType::controls[(size_t) k].kind == NetworkControl::Kind::pot)
                    c[(size_t) k] = v;
            n.setTargets (c);
            double* ch[] { x.data() + i };
            n.process (ch, 1, (int) std::min<size_t> (32, total - i));
        }
        const auto crest = clickCrestDb (x, (size_t) (0.05 * fs));
        INFO (fs << " Hz: " << crest << " dB");
        CHECK (crest <= -60.0);
    }
}

TEST_CASE ("Network: block sizes, channels and silence", "[network]")
{
    const double fs = 48000.0;
    const Network<gen::GyratorBell>::Controls a { 0.8, 0.3, 0.0 }, b { 0.2, 0.9, 1.0 };
    std::vector<float> input (24000);
    std::mt19937 rng (11);
    std::normal_distribution<float> noise (0.0f, 0.1f);
    for (auto& v : input)
        v = noise (rng);

    // Targets change at fixed sample positions; the output must not depend on the host blocks.
    auto render = [&] (const std::vector<int>& blocks)
    {
        Network<gen::GyratorBell> n;
        n.setTargets (a);
        n.prepare (fs);
        std::vector<float> l = input, r = input;
        size_t pos = 0, k = 0;
        while (pos < input.size())
        {
            const auto len = std::min<size_t> ((size_t) blocks[k++ % blocks.size()], input.size() - pos);
            if (pos >= 9600)
                n.setTargets (b);
            float* ch[] { l.data() + pos, r.data() + pos };
            n.process (ch, 2, (int) len);
            pos += len;
        }
        return std::make_pair (l, r);
    };
    const auto reference = render ({ 32 });
    CHECK (reference.first == reference.second);
    for (const auto& blocks : { std::vector<int> { 1 }, std::vector<int> { 4096 }, std::vector<int> { 7, 129, 1, 32, 500 } })
    {
        // 9600 is a multiple of every block pattern's period only for 32 and 1; align the change.
        const auto out = render (blocks);
        if (blocks.size() == 1 && (9600 % blocks[0]) == 0)
            CHECK (out.first == reference.first);
        CHECK (out.first == out.second);
    }

    Network<gen::GyratorBell> n;
    n.setTargets (b);
    n.prepare (fs);
    std::vector<float> l (48000, 0.0f);
    for (size_t i = 0; i < 4800; ++i)
        l[i] = std::sin ((float) (twoPi * 200.0 * (double) i / fs));
    float* ch[] { l.data() };
    n.process (ch, 1, (int) l.size());
    std::vector<float> silence (48000 * 20, 0.0f);
    float* sc[] { silence.data() };
    n.process (sc, 1, (int) silence.size());
    CHECK (std::all_of (silence.end() - 48000, silence.end(), [] (float v) { return myzel::dsp::isExactly (v, 0.0f); }));
}

TEST_CASE ("Network: CPU of matched sections, at rest and moving", "[network][.perf]")
{
    // Eight second-order sections (a four-band EQ with filters), 48 kHz stereo, 10 s.
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        FrequencyMap map;
        map.prepare (fs);
        std::array<MatchedSection, 8> sections;
        auto designAll = [&] (double p, int ramp)
        {
            for (size_t i = 0; i < sections.size(); ++i)
                sections[i].design (map, bell (60.0 * std::pow (2.0, (double) i * 1.3 + p), 1.0, 6.0 + 3.0 * p), ramp);
        };
        designAll (0.0, 0);
        const int n = (int) (10.0 * fs);
        std::vector<double> l ((size_t) n, 0.1), r ((size_t) n, 0.1);
        for (bool moving : { false, true })
        {
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < n; i += 32)
            {
                if (moving)
                    designAll ((double) i / n, 32);
                double* ch[] { l.data() + i, r.data() + i };
                for (auto& s : sections)
                    s.process (ch, 2, std::min (32, n - i));
            }
            const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            std::printf ("%6.1f kHz, D = %d, 8 sections stereo, %s: %.2f %% of one core\n", fs / 1000.0, map.degree(),
                         moving ? "every section re-designed every 32 samples" : "at rest", 100.0 * seconds / 10.0);
        }
    }
}

TEST_CASE ("RootCascade: a pair near the negative real axis turning real while two real roots meet keeps its labels", "[network][cascade]")
{
    // Pair A, -10 +- 0.4j, splits into -10.8 and -9.0 in the same design in which -1.0 and -1.6
    // meet as -1.25 +- 0.2j. The warm roots come back out of their labels' order, so they are
    // matched by distance, whose angle wraps at the negative real axis (-pi is +pi): otherwise
    // A's lower root goes to the new complex pair and the stages re-group.
    using Complex = std::complex<double>;
    auto expand = [] (const std::vector<Complex>& roots)
    {
        std::vector<Complex> p { 1.0 };
        for (const auto r : roots)
        {
            p.push_back (0.0);
            for (size_t k = p.size() - 1; k > 0; --k)
                p[k] = p[k - 1] - r * p[k];
            p[0] *= -r;
        }
        std::vector<double> c;
        for (const auto x : p)
            c.push_back (x.real());
        return c;
    };
    FrequencyMap map;
    map.prepare (48000.0);
    const double num[] { 1.0 };
    const auto before = expand ({ { -10.0, 0.4 }, { -10.0, -0.4 }, -1.0, -1.6 });
    const auto after = expand ({ -10.8, -9.0, { -1.25, 0.2 }, { -1.25, -0.2 } });

    RootCascade cascade;
    cascade.design (map, num, 0, before.data(), 4, omegaScale, 0, true);
    REQUIRE (cascade.numStages() == 2);
    std::array<int, 2> pairA {}, pairB {};
    for (int s = 0; s < 2; ++s)
    {
        const auto labels = cascade.stagePoles (s);
        (std::abs (cascade.pole (labels[0]).real() + 10.0) < 1.0e-6 ? pairA : pairB) = labels;
    }
    cascade.design (map, num, 0, after.data(), 4, omegaScale, 32, false);

    for (const auto label : pairA)
    {
        INFO ("pair A's label " << label << " holds " << cascade.pole (label));
        CHECK (std::abs (cascade.pole (label).imag()) < 1.0e-9);
        CHECK (cascade.pole (label).real() < -8.0);
    }
    for (const auto label : pairB)
    {
        INFO ("pair B's label " << label << " holds " << cascade.pole (label));
        CHECK (std::abs (cascade.pole (label) - Complex { -1.25, std::copysign (0.2, cascade.pole (label).imag()) }) < 1.0e-9);
    }
}

TEST_CASE ("RootFinder: the same roots however the sweeps are split", "[network][cascade]")
{
    // A cold design spread over callbacks must equal one designed at once, to the bit.
    std::mt19937 rng (21);
    std::uniform_real_distribution<double> u (-1.0, 1.0);
    for (int trial = 0; trial < 200; ++trial)
    {
        const int degree = 3 + trial % 22;
        std::vector<std::complex<double>> c ((size_t) degree + 1);
        for (auto& x : c)
            x = std::pow (10.0, 3.0 * u (rng)) * u (rng);
        std::array<std::complex<double>, poly::maxDegree> once {}, split {};
        const int n = poly::roots (c, once, false);
        poly::RootFinder finder;
        finder.begin (c, {}, false);
        while (! finder.step (1 + trial % 3))
        {
        }
        INFO ("degree " << degree);
        REQUIRE (finder.result (split) == n);
        CHECK (std::memcmp (once.data(), split.data(), sizeof (once)) == 0);
    }
}

TEST_CASE ("RootFinder: two real roots that met and turned complex are found from their real warm starts", "[network][cascade]")
{
    // A tracked pair that met within a design comes back to the finder with real warm starts.
    // From real starts every iterate of a real polynomial stays real, so without a start off the
    // axis the pair bounced on it until the 200-sweep cold restart (0.1 ms at degree 21).
    using Complex = std::complex<double>;
    for (const int degree : { 4, 12, 21 })
    {
        // The pair among conjugate pairs on a circle of radius 3 in the left half-plane (and one
        // real root for an odd degree).
        std::vector<Complex> roots { { -1.0, 0.02 }, { -1.0, -0.02 } };
        const int pairs = (degree - 2) / 2;
        for (int k = 0; k < pairs; ++k)
        {
            const auto r = std::polar (3.0, std::numbers::pi * (0.55 + 0.4 * (k + 0.5) / pairs));
            roots.push_back (r);
            roots.push_back (std::conj (r));
        }
        if ((degree - 2) % 2 == 1)
            roots.push_back (-5.0);
        std::vector<Complex> c { 1.0 };
        for (const auto r : roots)
        {
            c.push_back (0.0);
            for (size_t k = c.size() - 1; k > 0; --k)
                c[k] = c[k - 1] - r * c[k];
            c[0] *= -r;
        }
        std::vector<Complex> start (roots);
        start[0] = -1.03;
        start[1] = -0.97;
        poly::RootFinder finder;
        finder.begin (c, start, true);
        int sweeps = 0;
        while (! finder.step (1))
            ++sweeps;
        std::array<Complex, poly::maxDegree> found {};
        REQUIRE (finder.result (found) == degree);
        poly::symmetrise ({ found.data(), (size_t) degree });
        INFO ("degree " << degree << ", " << sweeps << " sweeps");
        CHECK (sweeps < 30);
        for (const auto r : roots)
        {
            double nearest = 1.0e300;
            for (int k = 0; k < degree; ++k)
                nearest = std::min (nearest, std::abs (found[(size_t) k] - r));
            CHECK (nearest < 1.0e-6 * std::abs (r));
        }
    }
}

TEST_CASE ("RootFinder: a finite root over a tiny leading coefficient is kept by its size, not dropped by the coefficient's", "[network][cascade]")
{
    // The later card's high-pass front (degree 22, sigma in kHz) at 192 kHz, 43.3 ms into a jump
    // of every knob: its roots span eight decades and the leading coefficient is 9.3e-15 of the
    // largest. Dropped for its size, the real root at -292.6 kHz goes to infinity and the pair at
    // -141.8 +- 17.1i kHz comes out at -69.3 +- 63.2i kHz; the coefficients of two cascade stages
    // jump (console NOTES.md, "Measured").
    using Complex = std::complex<double>;
    const std::vector<Complex> c { -2.2110791000011382e-43, -1.4547003365228401e-37, -3.2171600314101634e-32, -2.7992700082377304e-27,
                                   -1.0507365164961678e-22, -1.6457987020872287e-18, -9.7032827569548735e-15, -2.5814446364256832e-11,
                                   -3.2006051688135875e-08, -1.6702559714484864e-05, -0.0028583619549929682, -0.037476829568098974,
                                   -0.24609600640555498, -0.85051947300079234, -0.45449880083767819, -0.089450071791969871,
                                   -0.0083701073063794859, -0.00040533199579127798, -1.0692168117738449e-05, -1.6522184281990654e-07,
                                   -1.3655161077897479e-09, -5.4242766289225535e-12, -7.9015189096380257e-15 };
    // Its roots by mpmath at 60 digits.
    const std::vector<Complex> exact { { -292.60232394112269, 0.0 }, { -141.78120633958957, -17.113216934071577 },
                                       { -141.78120633958957, 17.113216934071577 }, { -34.176230317766611, -35.19047675245797 },
                                       { -34.176230317766611, 35.19047675245797 }, { -17.73942304803889, 0.0 },
                                       { -10.330536778008732, -1.4073910740340301 }, { -10.330536778008732, 1.4073910740340301 },
                                       { -3.2524159346316752, 0.0 }, { -0.078925529053405753, -0.13508725189108574 },
                                       { -0.078925529053405753, 0.13508725189108574 }, { -0.15116749018514434, 0.0 },
                                       { -0.0033921627733869931, 0.0 }, { -0.0014377002659242916, 0.0 },
                                       { -0.00053051498246562284, 0.0 }, { -0.0004110495110235532, 0.0 },
                                       { -0.000311877188987412, 0.0 }, { -4.8671763046866753e-5, 0.0 },
                                       { -2.3050497733521005e-5, 0.0 }, { -1.5228553237169571e-5, 0.0 },
                                       { -3.9656477663122168e-6, 0.0 }, { -3.7365647426868467e-6, 0.0 } };
    auto nearest = [] (const std::array<Complex, poly::maxDegree>& found, int count, Complex r)
    {
        double d = 1.0e300;
        for (int k = 0; k < count; ++k)
            d = std::min (d, std::abs (found[(size_t) k] - r));
        return d / std::abs (r);
    };

    // RootCascade's radius at 192 kHz: a thousand times the rate, in sigma.
    const double radius = 1000.0 * 192000.0 / 1000.0;
    std::array<Complex, poly::maxDegree> found {};
    const int kept = poly::roots (c, found, false, radius);
    REQUIRE (kept == 22);
    double worst = 0.0;
    for (const auto r : exact)
        worst = std::max (worst, nearest (found, kept, r));
    INFO ("every root within " << worst << " of its size");
    CHECK (worst < 1.0e-6);

    std::array<Complex, poly::maxDegree> bySize {};
    const int dropped = poly::roots (c, bySize, false);
    REQUIRE (dropped == 21);
    CHECK (nearest (bySize, dropped, exact[0]) > 0.5);
    CHECK (nearest (bySize, dropped, exact[2]) > 0.3);

    // A noise-level leading coefficient still drops (its "root" lies near 8e15), and a root beyond
    // the radius goes to infinity even where its coefficient is far above the noise: (x + 1)
    // (x + 2) (1 + x / 1e6) keeps two roots, where the old rule keeps the one at -1e6 too.
    auto noisy = c;
    noisy.push_back (1.0e-30);
    std::array<Complex, poly::maxDegree> denoised {};
    const int finite = poly::roots (noisy, denoised, false, radius);
    REQUIRE (finite == 22);
    double worstNoisy = 0.0;
    for (const auto r : exact)
        worstNoisy = std::max (worstNoisy, nearest (denoised, finite, r));
    INFO ("with a noise-level leading coefficient: every root within " << worstNoisy << " of its size");
    CHECK (worstNoisy < 1.0e-6);

    // The two left move by the dropped root's pull, their size over its.
    const std::vector<Complex> far { 2.0, 3.0 + 2.0e-6, 1.0 + 3.0e-6, 1.0e-6 };
    std::array<Complex, poly::maxDegree> near {};
    REQUIRE (poly::roots (far, near, false, radius) == 2);
    CHECK (nearest (near, 2, -1.0) < 2.0e-6);
    CHECK (nearest (near, 2, -2.0) < 5.0e-6);
    CHECK (poly::roots (far, near, false) == 3);
}

TEMPLATE_TEST_CASE ("Network: at rest after a glide every block runs its own design, to the bit", "[network][modulation]",
                    gen::GyratorBell, gen::Ladder3, gen::Hpf18)
{
    // The coefficients follow a spline through the designs; once it has settled, the network must
    // run the design itself (what a fresh design at those settings runs), not a weighted sum of
    // copies that differs by an ulp. A copy of the settled network with its path restarted at the
    // design is the reference.
    using Topology = TestType;
    const auto grid = controlGrid<Topology> (4, 31);
    for (const double fs : { 44100.0, 192000.0 })
        for (size_t k = 0; k + 1 < grid.size(); ++k)
        {
            auto n = networkAt<Topology> (fs, grid[k]);
            n->setTargets (grid[k + 1]);
            const auto total = (size_t) (12.0 * fs);
            std::vector<double> x (total, 0.0);
            for (size_t i = 0; i < (size_t) fs; ++i)
                x[i] = 0.25 * std::sin (twoPi * 330.0 * (double) i / fs);
            double* ch[] { x.data() };
            n->process (ch, 1, (int) total);
            REQUIRE (! n->isSmoothing());
            // Eleven seconds of silence have flushed every state to exactly 0.
            REQUIRE (std::all_of (x.end() - 4096, x.end(), [] (double v) { return myzel::dsp::isExactly (v, 0.0); }));

            auto reference = std::make_unique<Network<Topology>> (*n);
            reference->clear();
            std::vector<double> a ((size_t) (0.1 * fs)), b (a.size());
            std::mt19937 rng (5);
            std::uniform_real_distribution<double> u (-0.5, 0.5);
            for (auto& v : a)
                v = u (rng);
            b = a;
            double* chA[] { a.data() };
            double* chB[] { b.data() };
            n->process (chA, 1, (int) a.size());
            reference->process (chB, 1, (int) b.size());
            INFO (fs << " Hz, from setting " << k << " to " << k + 1);
            CHECK (std::memcmp (a.data(), b.data(), a.size() * sizeof (double)) == 0);
        }
}

TEMPLATE_TEST_CASE ("Network: a reset in steps designs what reset() does, to the bit", "[network][cascade]", gen::Ladder3, gen::Hpf18)
{
    for (const auto& values : controlGrid<TestType> (4, 23))
    {
        auto once = networkAt<TestType> (48000.0, values);
        auto stepped = networkAt<TestType> (48000.0, controlsOf<TestType> (std::array<double, 8> {}.data()));
        stepped->setTargets (values);
        stepped->beginReset();
        int steps = 1;
        while (! stepped->resetStep())
            ++steps;
        CHECK (steps > 1);
        for (const auto hz : { 50.0, 1000.0, 12000.0 })
            CHECK (stepped->response (hz) == once->response (hz));
    }
}

namespace
{
    /** The stepped reference's closed form (tools/netlist/validate.py) at a component value. */
    std::complex<double> steppedBell (double depth, double cap, double hz)
    {
        const std::complex<double> s { 0.0, twoPi * hz };
        const auto r1 = 3300.0, r2 = 47.0 + depth * 10000.0, l = 0.1;
        return (s * s * l * cap + s * r2 * cap + 1.0) / (s * s * l * cap + s * (r1 + r2) * cap + 1.0);
    }
} // namespace

TEST_CASE ("Network: a stepped control's generated code reproduces the netlist's exact H(s)", "[network][generated][stepped]")
{
    // evaluate() takes the step's coordinate over its values' range, as the vectors do.
    double worst = 0.0;
    for (const auto& p : gen::SteppedBellVectors::points)
    {
        gen::SteppedBell::Polynomials polys;
        gen::SteppedBell::evaluate (p.controls.data(), polys);
        const std::complex<double> sigma { 0.0, twoPi * p.frequency / gen::SteppedBell::omegaRef };
        std::complex<double> h { polys.gain, 0.0 };
        for (int b = 0; b < polys.numBlocks; ++b)
        {
            std::complex<double> n {}, d {};
            for (int k = polys.blocks[(size_t) b].numDegree; k >= 0; --k)
                n = n * sigma + polys.blocks[(size_t) b].num[(size_t) k];
            for (int k = polys.blocks[(size_t) b].denDegree; k >= 0; --k)
                d = d * sigma + polys.blocks[(size_t) b].den[(size_t) k];
            h *= n / d;
        }
        const std::complex<double> exact { p.re, p.im };
        worst = std::max (worst, std::abs (h - exact) / std::max (std::abs (exact), 1.0e-12));
    }
    INFO ("worst relative error: " << worst);
    CHECK (worst < 1.0e-10);
}

TEST_CASE ("Network: a stepped control sits on its detents' component values", "[network][stepped]")
{
    for (int position = 0; position < 5; ++position)
        for (const double depth : { 0.0, 0.5, 1.0 })
        {
            auto n = networkAt<gen::SteppedBell> (48000.0, { depth, (double) position });
            const auto cap = gen::SteppedBell::capSteps[(size_t) position];
            for (const double hz : { 100.0, 500.0, 1000.0, 2300.0, 10000.0 })
            {
                INFO ("position " << position << ", depth " << depth << ", " << hz << " Hz");
                CHECK (std::abs (n->analogResponse (hz) - steppedBell (depth, cap, hz)) <= 1.0e-12 * std::abs (steppedBell (depth, cap, hz)));
            }
            CHECK (worstDb (*n) <= gateDb (48000.0));
        }
}

TEST_CASE ("Network: a detent step glides its component value over about 10 ms", "[network][stepped]")
{
    // In the log of the value, as a pot glides in its segments' ratio: within 1 % of the step
    // 10 ms after it, and not at once (the hardware's switch clicks there).
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto& [from, to] : { std::pair { 0, 1 }, std::pair { 4, 0 }, std::pair { 2, 3 } })
        {
            auto n = networkAt<gen::SteppedBell> (fs, { 0.5, (double) from });
            n->setTargets ({ 0.5, (double) to });
            const auto& steps = gen::SteppedBell::capSteps;
            const auto low = steps.front(), span = steps.back() - steps.front();
            auto progress = [&]
            {
                const auto cap = low + n->currentControls()[1] * span;
                return std::log (cap / steps[(size_t) from]) / std::log (steps[(size_t) to] / steps[(size_t) from]);
            };
            std::vector<double> x ((size_t) (0.012 * fs));
            double atOne = 0.0, atTen = 0.0;
            for (size_t i = 0; i < x.size(); i += 32)
            {
                double* ch[] { x.data() + i };
                n->process (ch, 1, (int) std::min<size_t> (32, x.size() - i));
                const auto t = (double) (i + 32) / fs;
                if (t <= 0.001)
                    atOne = progress();
                if (t <= 0.010)
                    atTen = progress();
            }
            INFO (fs << " Hz, position " << from << " -> " << to << ": " << atOne << " of the step after 1 ms, " << atTen << " after 10 ms");
            CHECK (atOne < 0.5);
            CHECK (atTen > 0.99);
            CHECK (atTen <= 1.0);
        }
}

TEST_CASE ("Network: detent steps glide without a click", "[network][stepped][modulation]")
{
    // A 330 Hz sine through the stepped bell (full depth) while its switch steps a detent every
    // 50 ms up and down, then jumps end to end every 100 ms.
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const bool jumps : { false, true })
        {
            auto n = networkAt<gen::SteppedBell> (fs, { 0.0, 0.0 });
            const auto total = (size_t) (2.0 * fs);
            std::vector<double> x (total);
            for (size_t i = 0; i < total; ++i)
                x[i] = 0.25 * std::sin (twoPi * 330.0 * (double) i / fs);
            for (size_t i = 0; i < total; i += 32)
            {
                const auto t = (double) i / fs;
                double position;
                if (jumps)
                    position = ((int) (t / 0.1)) % 2 == 0 ? 0.0 : 4.0;
                else
                {
                    const auto k = (int) (t / 0.05) % 8;
                    position = k < 4 ? k : 8 - k;
                }
                n->setTargets ({ 0.0, position });
                double* ch[] { x.data() + i };
                n->process (ch, 1, (int) std::min<size_t> (32, total - i));
            }
            const auto crest = clickCrestDb (x, (size_t) (0.05 * fs));
            INFO (fs << " Hz, " << (jumps ? "jumps end to end" : "a detent at a time") << ": " << crest << " dB");
            CHECK (crest <= -60.0);
        }
}
