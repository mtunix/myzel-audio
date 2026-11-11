#include "dsp/Mappings.h"
#include "dsp/algorithms/spring/SpringEngine.h"
#include "support/EngineGates.h"

#include <complex>
#include <iostream>

namespace
{
    using namespace cerulean;

    gates::Options springOptions()
    {
        // No deviations from the defaults. In particular the echo-density target holds although a
        // spring is not diffuse like a hall: Abel-Huang counts samples outside one standard
        // deviation, and a dispersed chirp (a swept sinusoid) has *more* of those than Gaussian
        // noise, so each chirp reads as "dense" (0.9 at ~33 ms, i.e. on the first arrival).
        // The springs' sparse echo *pattern* is measured separately below.
        gates::Options o;
        return o;
    }

    constexpr double fs = 48000.0;

    dsp::EngineParams springParams (float springs, float drip)
    {
        auto p = dsp::EngineParams {};
        p.paramA = springs;
        p.paramB = drip;
        p.decaySeconds = 3.0f;
        return p;
    }

    /** Energy-weighted arrival time (s) of the band around hz within [t0, t1) s. */
    double bandArrival (const std::vector<float>& x, double hz, double t0, double t1, double q = 4.0)
    {
        auto f1 = analysis::Biquad::bandpass (hz, q, fs);
        auto f2 = f1;
        double energy = 0.0, moment = 0.0;
        const auto end = std::min (x.size(), (size_t) (t1 * fs));
        for (size_t i = 0; i < end; ++i)
        {
            const auto y = f2.process (f1.process (x[i]));
            if (i >= (size_t) (t0 * fs))
            {
                energy += y * y;
                moment += y * y * (double) i;
            }
        }
        return energy > 0.0 ? moment / energy / fs : 0.0;
    }

    double bandEnergyDb (const std::vector<float>& x, double hz, size_t start, size_t end)
    {
        const auto band = analysis::octaveBand (x, fs, hz);
        double e = 0.0;
        for (size_t i = start; i < std::min (end, band.size()); ++i)
            e += band[i] * band[i];
        return 10.0 * std::log10 (std::max (e, 1.0e-30));
    }

    std::vector<float> mono (const analysis::Stereo& s)
    {
        std::vector<float> m (s.size());
        for (size_t i = 0; i < m.size(); ++i)
            m[i] = s.left[i] + s.right[i];
        return m;
    }

    /** Energy arriving 35.5-46 ms (springs 2 and 3) relative to 29-35 ms (spring 1), in dB. */
    double laterArrivalsDb (const analysis::Stereo& ir)
    {
        const auto m = mono (ir);
        return analysis::toDb (analysis::rms (m, (size_t) (0.0355 * fs), (size_t) (0.046 * fs)))
             - analysis::toDb (analysis::rms (m, (size_t) (0.029 * fs), (size_t) (0.035 * fs)));
    }
}

CERULEAN_ENGINE_GATES ("[spring]", cerulean::dsp::SpringEngine, springOptions())

//==============================================================================
TEST_CASE ("Spring: the dispersion cascade is lossless and its group delay matches the model", "[spring]")
{
    // Two groups of 5 + 3 stages with different coefficients per lane.
    using Cascade = dsp::DispersionCascade<8, 5>;
    Cascade cascade;
    cascade.prepare (5);
    const Cascade::Lanes a { 0.6f, 0.3f, 0.0f, -0.4f }, b { 0.2f, 0.55f, 0.5f, 0.0f };

    constexpr size_t n = 1 << 14;
    std::array<std::vector<float>, 4> h;
    for (auto& v : h)
        v.resize (n);
    for (size_t i = 0; i < n; ++i)
    {
        Cascade::Lanes x {};
        if (i == 0)
            x = { 1.0f, 1.0f, 1.0f, 1.0f };
        cascade.process (x, a, b);
        for (size_t l = 0; l < 4; ++l)
            h[l][i] = x[l];
    }

    for (size_t l = 0; l < 4; ++l)
    {
        double energy = 0.0;
        for (const auto v : h[l])
            energy += (double) v * v;
        INFO ("lane " << l << " (a = " << a[l] << "): impulse energy " << energy);
        CHECK (std::abs (energy - 1.0) < 1.0e-4);

        // Group delay from the phase slope of the DFT, at 1, 2.5 and 4 kHz.
        auto response = [&] (double w)
        {
            std::complex<double> s {};
            for (size_t i = 0; i < n; ++i)
                s += (double) h[l][i] * std::polar (1.0, -w * (double) i);
            return s;
        };
        for (const auto hz : { 1000.0, 2500.0, 4000.0 })
        {
            const auto w = 2.0 * 3.14159265358979 * hz / fs, dw = 1.0e-4;
            const auto measured = -std::arg (response (w + dw) / response (w - dw)) / (2.0 * dw);
            const auto model = (double) Cascade::groupDelay (a[l], b[l], 5, (float) w);
            INFO ("  " << hz << " Hz: measured " << measured << " samples, model " << model);
            CHECK (std::abs (measured - model) < 0.01 * model + 0.05);
        }
    }
}

TEST_CASE ("Spring: block-interleaved dispersion equals the per-sample recursion", "[spring]")
{
    using Cascade = dsp::SpringEngine::Cascade;
    Cascade a, b;
    a.prepare (5);
    b.prepare (5);
    dsp::Rng rng (5);

    std::array<Cascade::Lanes, 32> x {}, c1 {}, c2 {};
    double worst = 0.0;
    for (int block = 0; block < 200; ++block)
    {
        const auto n = 1 + (int) (rng.uniform() * 32.0f);   // odd sizes exercise the remainder path
        for (int i = 0; i < n; ++i)
            for (size_t l = 0; l < 4; ++l)
            {
                x[(size_t) i][l] = rng.bipolar();
                c1[(size_t) i][l] = 0.5f + 0.05f * std::sin (0.01f * (float) (block * 32 + i) + (float) l);
                c2[(size_t) i][l] = 0.3f * (float) l / 3.0f;
            }
        auto y = x;
        a.processBlock (y.data(), c1.data(), c2.data(), n);
        for (int i = 0; i < n; ++i)
        {
            auto z = x[(size_t) i];
            b.process (z, c1[(size_t) i], c2[(size_t) i]);
            for (size_t l = 0; l < 4; ++l)
                worst = std::max (worst, (double) std::abs (z[l] - y[(size_t) i][l]));
        }
    }
    INFO ("largest difference " << worst);
    CHECK (worst < 1.0e-5);
}

TEST_CASE ("Spring: the sound does not depend on the sub-block size", "[spring]")
{
    // The springs run in passes over each sub-block with deferred writes to the back transits;
    // any block size (1 .. 32) must give the same impulse response (Mod off: no control-rate LFO).
    auto render = [] (int block)
    {
        auto e = gates::makeEngine<dsp::SpringEngine> (fs);
        auto p = springParams (1.0f, 0.7f);
        p.mod = 0.0f;
        std::vector<float> l (48000), r (48000), in (48000, 0.0f);
        in[0] = 1.0f;
        for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
        {
            const auto n = (int) std::min<size_t> ((size_t) block, l.size() - pos);
            e->process (in.data() + pos, in.data() + pos, l.data() + pos, r.data() + pos, n, p);
        }
        return l;
    };

    const auto reference = render (32);
    for (const int block : { 1, 7, 16 })
    {
        const auto other = render (block);
        double worst = 0.0;
        for (size_t i = 0; i < reference.size(); ++i)
            worst = std::max (worst, (double) std::abs (other[i] - reference[i]));
        INFO ("block " << block << ": largest difference to 32-sample blocks " << worst
              << " (IR rms " << analysis::rms (reference) << ")");
        CHECK (worst < 1.0e-5);
    }
}

TEST_CASE ("Spring: Drip delays the top of each chirp (frequency-dependent group delay)", "[spring]")
{
    auto e = gates::makeEngine<dsp::SpringEngine> (fs);

    // One spring; the first arrival at the far pickup (left) lies in 20-58 ms. The spread is the
    // arrival of the 3.8 kHz band minus that of the 700 Hz band (the band filters' own delays
    // cancel when comparing Drip settings).
    struct Arrival { double low, high; };
    auto arrival = [&] (float drip)
    {
        const auto ir = analysis::impulseResponse (*e, springParams (0.0f, drip), fs, 0.1);
        return Arrival { bandArrival (ir.left, 700.0, 0.020, 0.058), bandArrival (ir.left, 3800.0, 0.020, 0.058) };
    };

    const auto smooth = arrival (0.0f), mid = arrival (0.5f), drippy = arrival (1.0f);
    auto spreadMs = [] (Arrival a) { return 1000.0 * (a.high - a.low); };
    INFO ("3.8 kHz - 700 Hz arrival: Drip 0 " << spreadMs (smooth) << " ms, 0.5 " << spreadMs (mid)
          << " ms, 1 " << spreadMs (drippy) << " ms; 700 Hz arrives at " << 1000.0 * smooth.low << " / "
          << 1000.0 * mid.low << " / " << 1000.0 * drippy.low << " ms");

    CHECK (spreadMs (mid) > spreadMs (smooth) + 2.0);
    CHECK (spreadMs (drippy) > spreadMs (mid) + 2.0);
    CHECK (spreadMs (drippy) > spreadMs (smooth) + 5.0);

    // The low end keeps its timing: Drip changes the chirp, not the spring's length.
    CHECK (std::abs (drippy.low - smooth.low) < 0.0015);
}

TEST_CASE ("Spring: chirps lengthen with every trip along the spring", "[spring]")
{
    // Dispersion accumulates: the 2nd far-end arrival (3 transits) is more spread out than the
    // 1st (1 transit). Drip 1, one spring; the near-end arrivals only reach the left at -10 dB.
    auto e = gates::makeEngine<dsp::SpringEngine> (fs);
    auto p = springParams (0.0f, 1.0f);
    p.diffusion = 0.0f;   // no coil smear, so the arrivals stay separable
    const auto ir = analysis::impulseResponse (*e, p, fs, 0.2);

    const auto first = bandArrival (ir.left, 3800.0, 0.020, 0.058) - bandArrival (ir.left, 700.0, 0.020, 0.058);
    const auto third = bandArrival (ir.left, 3800.0, 0.090, 0.150) - bandArrival (ir.left, 700.0, 0.090, 0.150);
    INFO ("3.8 kHz lag behind 700 Hz: 1st arrival " << first * 1000.0 << " ms, after 3 transits " << third * 1000.0 << " ms");
    CHECK (third > first + 0.005);
}

TEST_CASE ("Spring: Drip emphasises the treble into the tank", "[spring]")
{
    auto e = gates::makeEngine<dsp::SpringEngine> (fs);
    auto tilt = [&] (float drip)
    {
        const auto ir = analysis::impulseResponse (*e, springParams (0.5f, drip), fs, 1.0);
        return bandEnergyDb (ir.left, 3000.0, 0, 48000) - bandEnergyDb (ir.left, 500.0, 0, 48000);
    };
    const auto smooth = tilt (0.0f), mid = tilt (0.5f), drippy = tilt (1.0f);
    INFO ("3 kHz vs 500 Hz band energy: Drip 0 " << smooth << " dB, 0.5 " << mid << " dB, 1 " << drippy << " dB");
    CHECK (mid > smooth + 2.0);
    CHECK (drippy > mid + 2.0);
}

TEST_CASE ("Spring: Springs adds arrivals to the echo pattern", "[spring]")
{
    auto e = gates::makeEngine<dsp::SpringEngine> (fs);

    for (const float drip : { 0.0f, 0.5f })
    {
        std::array<double, 3> later {};
        for (int count = 1; count <= 3; ++count)
        {
            const auto a = 0.5f * (float) (count - 1);
            REQUIRE (dsp::maps::springCount (a) == count);
            later[(size_t) (count - 1)] = laterArrivalsDb (analysis::impulseResponse (*e, springParams (a, drip), fs, 0.1));
        }
        INFO ("Drip " << drip << ": energy at 35-46 ms vs spring 1's arrival: 1 spring " << later[0]
              << " dB, 2 springs " << later[1] << " dB, 3 springs " << later[2] << " dB");
        CHECK (later[1] > later[0] + 2.0);
        CHECK (later[2] > later[1] + 1.0);
        CHECK (later[2] > later[0] + 5.0);
    }
}

TEST_CASE ("Spring: more springs widen the image, mono-compatible at every count", "[spring]")
{
    auto e = gates::makeEngine<dsp::SpringEngine> (fs);
    std::array<double, 3> rho {};
    for (int count = 1; count <= 3; ++count)
    {
        const auto ir = analysis::impulseResponse (*e, springParams (0.5f * (float) (count - 1), 0.5f), fs, 2.0);
        const size_t start = 9600, end = 96000;
        rho[(size_t) (count - 1)] = analysis::correlation (ir.left, ir.right, start, end);
        const auto m = gates::monoSum (ir);
        const auto monoDb = analysis::toDb (analysis::rms (m, start, end))
                          - analysis::toDb (std::sqrt (0.5 * (std::pow (analysis::rms (ir.left, start, end), 2.0)
                                                             + std::pow (analysis::rms (ir.right, start, end), 2.0))));
        INFO (count << " spring(s): L/R correlation " << rho[(size_t) (count - 1)] << ", mono sum " << monoDb << " dB");
        CHECK (monoDb > -3.0);
        CHECK (rho[(size_t) (count - 1)] < 0.6);
        CHECK (rho[(size_t) (count - 1)] > 0.2);   // a tank is narrow: never fully decorrelated
    }
    CHECK (rho[0] > rho[1] + 0.05);
    CHECK (rho[1] > rho[2] + 0.03);
}

TEST_CASE ("Spring: Size scales the spring length", "[spring]")
{
    auto e = gates::makeEngine<dsp::SpringEngine> (fs);
    auto firstArrival = [&] (float size)
    {
        auto p = springParams (0.0f, 0.5f);
        p.sizeScale = size;
        const auto ir = analysis::impulseResponse (*e, p, fs, 0.3);
        // 1 kHz band of the left (far) pickup: the sizzle loop has nothing down there.
        auto f1 = analysis::Biquad::bandpass (1000.0, 1.0, fs);
        auto f2 = f1;
        std::vector<double> band (ir.size());
        double peak = 0.0;
        for (size_t i = 0; i < band.size(); ++i)
            peak = std::max (peak, std::abs (band[i] = f2.process (f1.process (ir.left[i]))));
        for (size_t i = 0; i < band.size(); ++i)
            if (std::abs (band[i]) > 0.1 * peak)
                return (double) i / fs;
        return 0.0;
    };

    const auto half = firstArrival (0.5f), nominal = firstArrival (1.0f), twice = firstArrival (2.0f);
    INFO ("first arrival: Size 0.5x " << half * 1000.0 << " ms, 1x " << nominal * 1000.0 << " ms, 2x " << twice * 1000.0 << " ms");
    // Spring 1's transit is 28.7 ms at 1x: arrivals move by transit x (scale change).
    CHECK (std::abs ((twice - nominal) - 0.0287) < 0.002);
    CHECK (std::abs ((nominal - half) - 0.01435) < 0.002);
}

TEST_CASE ("Spring: the drive is soft, and bypassed (exactly linear) when frozen", "[spring]")
{
    auto e = gates::makeEngine<dsp::SpringEngine> (fs);
    const auto quiet = gates::noise (fs, 0.5);
    auto loud = quiet;
    for (auto* ch : { &loud.left, &loud.right })
        for (auto& v : *ch)
            v *= 8.0f;   // ~ +6 dBFS peaks: deep into the drive

    auto nonlinearity = [&] (float freeze)
    {
        auto p = springParams (0.5f, 1.0f);
        p.freeze = freeze;
        e->reset();
        const auto a = analysis::renderEngine (*e, p, quiet, 48000);
        e->reset();
        const auto b = analysis::renderEngine (*e, p, loud, 48000);
        double err = 0.0, ref = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const auto d = (double) b.left[i] - 8.0 * a.left[i];
            err += d * d;
            ref += 64.0 * (double) a.left[i] * a.left[i];
        }
        return analysis::toDb (std::sqrt (err / ref));
    };

    const auto running = nonlinearity (0.0f), frozen = nonlinearity (1.0f);
    INFO ("deviation from linear scaling (input x8): running " << running << " dB, frozen " << frozen << " dB");
    CHECK (running > -30.0);   // it does drive
    CHECK (frozen < -100.0);   // and is out of the way when frozen

    // Soft: a -20 dBFS tone at full Drip comes out clean (3rd harmonic < -40 dB).
    const auto tone = gates::sine (fs, 2.0, 440.0, 0.1f);
    e->reset();
    const auto out = analysis::renderEngine (*e, springParams (0.5f, 1.0f), tone, tone.size());
    auto goertzel = [&] (double hz)
    {
        const auto c = 2.0 * std::cos (2.0 * 3.14159265358979 * hz / fs);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = 48000; i < out.size(); ++i)
        {
            const auto s0 = (double) out.left[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return std::sqrt (std::max (s1 * s1 + s2 * s2 - c * s1 * s2, 0.0));
    };
    const auto h3 = analysis::toDb (goertzel (1320.0) / goertzel (440.0));
    INFO ("-20 dBFS 440 Hz at Drip 1: 3rd harmonic " << h3 << " dB");
    CHECK (h3 < -40.0);
}

TEST_CASE ("Spring: freeze keeps the top end (integer, exactly-read delays)", "[spring]")
{
    // Stricter than the shared gate on the band that a fractional delay read would erode first:
    // the 4 kHz octave must hold within +-0.5 dB over 30 s frozen.
    auto e = gates::makeEngine<dsp::SpringEngine> (fs);
    auto running = springParams (0.5f, 0.5f);
    auto frozen = running;
    frozen.freeze = 1.0f;
    gates::SmoothedAutomation automation (running, fs);
    const auto out = analysis::renderEngine (*e, running, gates::noise (fs, 1.0), (size_t) (fs * 34.0), [&] (size_t pos)
    {
        return automation.step (pos < (size_t) fs ? running : frozen);
    });

    const auto at3 = bandEnergyDb (out.left, 4000.0, (size_t) (3.0 * fs), (size_t) (4.0 * fs));
    const auto at33 = bandEnergyDb (out.left, 4000.0, (size_t) (33.0 * fs), (size_t) (34.0 * fs));
    INFO ("4 kHz band, frozen: change from 3 s to 33 s " << at33 - at3 << " dB");
    CHECK (std::abs (at33 - at3) < 0.5);
}

//==============================================================================
TEST_CASE ("Spring: measurements for NOTES.md", "[.measure]")
{
    auto e = gates::makeEngine<dsp::SpringEngine> (fs);
    std::cout << "stretch K = " << e->getStretch() << " (fc " << fs / (2.0 * e->getStretch()) << " Hz)\n";

    for (const float decay : { 1.0f, 3.0f })
        for (const float a : { 0.0f, 0.5f, 1.0f })
            for (const float b : { 0.0f, 0.5f, 1.0f })
            {
                auto p = springParams (a, b);
                p.decaySeconds = decay;
                const auto ir = analysis::impulseResponse (*e, p, fs, decay * 1.5 + 0.5);
                std::cout << "Decay " << decay << " springs " << dsp::maps::springCount (a) << " drip " << b << ": RT";
                for (const auto f : { 125.0, 250.0, 1000.0, 4000.0 })
                    std::cout << " " << f << "=" << analysis::bandRt60 (ir, f);
                const auto ir2 = analysis::impulseResponse (*e, p, fs, 2.0);
                std::cout << " mid=" << analysis::midBandRt60 (ir) << " | ED0.9 " << analysis::echoDensityTime (ir.left, fs)
                          << " s | rho " << analysis::correlation (ir2.left, ir2.right, 9600, 96000)
                          << " | later arrivals " << laterArrivalsDb (ir2)
                          << " dB | 3k-700 lag " << 1000.0 * (bandArrival (ir2.left, 3800.0, 0.020, 0.058) - bandArrival (ir2.left, 700.0, 0.020, 0.058))
                          << " ms\n";
            }

    const auto in = gates::noise (fs, 6.0);
    for (const float a : { 0.0f, 0.5f, 1.0f })
        for (const float b : { 0.0f, 0.5f, 1.0f })
        {
            auto p = springParams (a, b);
            p.decaySeconds = 2.0f;
            e->reset();
            const auto out = analysis::renderEngine (*e, p, in, in.size());
            std::cout << "wet level springs " << dsp::maps::springCount (a) << " drip " << b << ": "
                      << analysis::toDb (0.5 * (analysis::rms (out.left, 144000) + analysis::rms (out.right, 144000)))
                         - analysis::toDb (0.5 * (analysis::rms (in.left, 144000) + analysis::rms (in.right, 144000))) << " dB\n";
        }

    auto p = springParams (0.5f, 0.5f);
    p.decaySeconds = 2.0f;
    e->reset();
    const auto out = analysis::renderEngine (*e, p, in, in.size());
    std::cout << "pink-noise response (out - in, dB):";
    for (const auto f : { 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 })
        std::cout << " " << f << ":" << bandEnergyDb (out.left, f, 144000, out.size()) - bandEnergyDb (in.left, f, 144000, in.size());
    std::cout << "\n";
}
