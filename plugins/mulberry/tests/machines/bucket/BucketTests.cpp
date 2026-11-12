#include "support/GateCases.h"

#include "dsp/common/ClockedDomain.h"
#include "dsp/machines/bucket/BucketMachine.h"

#include <cstdlib>
#include <fstream>

using namespace mulberry;
using namespace mulberry::analysis;

namespace
{
    // Gates on the longest chip without the compander: the compander's broadband detectors make
    // a tone's level depend on the burst, and short chips at 300 ms alias by design.
    gates::GateOptions bucketGates()
    {
        gates::GateOptions o;
        o.paramA = 1.0f;
        o.paramB = 0.0f;
        return o;
    }

    constexpr double fs = 48000.0;

    /**
        The clocked domain alone with an ideal device (the held value is the sample K clocks ago),
        driven by a test signal: the configuration the Python reference recomputes.
    */
    struct Case
    {
        double cutoff = 3500.0, clock = 18750.0;
        int delay = 32;
        size_t length = 4800;
    };

    std::vector<double> testInput (size_t n)
    {
        std::vector<double> u (n);
        uint32_t state = 77u;
        for (size_t i = 0; i < n; ++i)
        {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            const auto noise = (double) state / 4294967296.0 - 0.5;
            u[i] = 0.5 * std::sin (2.0 * std::numbers::pi * 997.0 * (double) i / fs) + 0.3 * noise;
        }
        return u;
    }

    std::vector<double> runDomain (const Case& c, const std::vector<double>& u)
    {
        dsp::ClockedDomain<2, 1> d;
        const auto f = dsp::ModalFilter<2>::butterworth (c.cutoff);
        d.prepare (fs, f, f);
        d.setClock (c.clock);
        std::vector<double> memory (1 << 16, 0.0), out (u.size());
        size_t written = 0;
        for (size_t n = 0; n < u.size(); ++n)
        {
            const auto events = d.beginSample();
            for (int e = 0; e < events; ++e)
            {
                memory[written % memory.size()] = d.sampleInput();
                ++written;
                const auto held = written > (size_t) c.delay ? memory[(written - 1 - (size_t) c.delay) % memory.size()] : 0.0;
                d.pushOutput (0, held);
                d.nextEvent();
            }
            out[n] = d.output (0);
            d.endSample (u[n]);
        }
        return out;
    }
}

MULBERRY_MACHINE_GATES (Machine::bucket, "Bucket", "[bucket]", bucketGates())

TEST_CASE ("Bucket: the clocked domain matches the Python reference sample for sample", "[bucket]")
{
    // tests/machines/bucket/validation/hp_reference.py recomputes this case independently and
    // compares against the dump (MULBERRY_DUMP_DIR). Here: the linear chain's small-signal
    // response at 997 Hz against the analog prototype (filters, hold, delay).
    const Case c;
    const auto u = testInput (c.length);
    const auto y = runDomain (c, u);
    REQUIRE (std::isfinite (y.back()));

    if (const auto* dir = std::getenv ("MULBERRY_DUMP_DIR"))
    {
        std::ofstream in (std::string (dir) + "/hp_input.f64", std::ios::binary), out (std::string (dir) + "/hp_output.f64", std::ios::binary);
        in.write (reinterpret_cast<const char*> (u.data()), (std::streamsize) (u.size() * sizeof (double)));
        out.write (reinterpret_cast<const char*> (y.data()), (std::streamsize) (y.size() * sizeof (double)));
    }

    const auto f = dsp::ModalFilter<2>::butterworth (c.cutoff);
    const auto x = std::numbers::pi * 997.0 / c.clock;
    const auto expected = 0.5 * std::abs (f.response (997.0) * f.response (997.0)) * std::sin (x) / x;
    std::vector<float> yf (y.begin(), y.end());
    const auto measured = toneAmplitude (yf, 2400, 2400, 997.0, fs);
    INFO ("997 Hz: measured " << measured << ", expected " << expected);
    CHECK (std::abs (toDb (measured / expected)) < 0.05);
}

TEST_CASE ("Bucket: aliasing and clock images match a brute-force simulation at 64x", "[bucket]")
{
    // The same chain simulated directly at 64x the host rate: the filters as exact modal steps at
    // 64x, the device sampling at the nearest 64x instant, its staircase held at 64x, the host
    // reading every 64th sample. Its spectrum and the clocked domain's must agree within 1 dB at
    // the tone, its alias (clock - f) and an image, where the reference is above -80 dB.
    Case c;
    c.clock = 9000.0;     // a long time on a short chip: the alias of 3 kHz lands at 6 kHz
    c.cutoff = 5000.0;
    c.length = 24000;
    std::vector<double> u (c.length);
    for (size_t i = 0; i < u.size(); ++i)
        u[i] = 0.5 * std::sin (2.0 * std::numbers::pi * 3000.0 * (double) i / fs);
    const auto fast = runDomain (c, u);

    // Brute force.
    constexpr int over = 64;
    const auto f = dsp::ModalFilter<2>::butterworth (c.cutoff);
    const auto dt = 1.0 / (fs * over);
    std::array<std::complex<double>, 2> xin {}, yout {};
    std::array<std::complex<double>, 2> stepIn {}, stepOut {};
    for (size_t i = 0; i < 2; ++i)
    {
        stepIn[i] = std::exp (f.poles[i] * dt);
        stepOut[i] = std::exp (f.poles[i] * dt);
    }
    std::vector<double> memory (1 << 16, 0.0), slow (c.length);
    size_t written = 0;
    double phase = 0.0, held = 0.0;
    for (size_t n = 0; n < c.length; ++n)
        for (int j = 1; j <= over; ++j)
        {
            for (size_t i = 0; i < 2; ++i)
                xin[i] *= stepIn[i];
            phase += c.clock * dt;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                double v = 0.0;
                for (size_t i = 0; i < 2; ++i)
                    v += 2.0 * (f.residues[i] * xin[i]).real();
                memory[written % memory.size()] = v;
                ++written;
                held = written > (size_t) c.delay ? memory[(written - 1 - (size_t) c.delay) % memory.size()] : 0.0;
            }
            double y = 0.0;
            for (size_t i = 0; i < 2; ++i)
            {
                // Exact step response of the output mode over dt with the held value.
                const auto target = -f.residues[i] / f.poles[i] * held;
                yout[i] = target + (yout[i] - target) * stepOut[i];
                y += 2.0 * yout[i].real();
            }
            if (j == over)
            {
                slow[n] = y;
                for (size_t i = 0; i < 2; ++i)
                    xin[i] += u[n] / fs;
            }
        }

    std::vector<float> a (fast.begin(), fast.end()), b (slow.begin(), slow.end());
    for (const auto hz : { 3000.0, 6000.0, 12000.0 })
    {
        const auto la = toDb (toneAmplitude (a, 4800, 16384, hz, fs));
        const auto lb = toDb (toneAmplitude (b, 4800, 16384, hz, fs));
        INFO (hz << " Hz: clocked domain " << la << " dB, 64x reference " << lb << " dB");
        if (lb > -80.0)
            CHECK (std::abs (la - lb) < 1.0);
    }
}

TEST_CASE ("Bucket: shorter chips and longer times are darker", "[bucket]")
{
    dsp::BucketMachine m;
    dsp::SlotMemory memory;
    memory.allocate (fs);
    m.prepare (fs, memory);
    dsp::MachineSettings s;
    s.paramB = 0.0f;
    const auto at = [&] (float chip, float ms, double hz)
    {
        s.paramA = chip;
        s.timeMs = { ms, ms };
        return toDb (m.predictedPassGain (hz, s));
    };
    CHECK (at (0.0f, 400.0f, 2000.0) < at (1.0f, 400.0f, 2000.0) - 3.0);
    CHECK (at (1.0f, 1200.0f, 1500.0) < at (1.0f, 200.0f, 1500.0) - 1.0);
}

TEST_CASE ("Bucket: the compander breathes (the loss between its halves mistracks)", "[bucket]")
{
    dsp::BucketMachine m;
    dsp::SlotMemory memory;
    memory.allocate (fs);
    m.prepare (fs, memory);
    dsp::MachineSettings s;
    s.paramA = 1.0f;
    s.timeMs = { 300.0f, 300.0f };
    s.paramB = 0.0f;
    const auto plain = toDb (m.predictedPassGain (1000.0, s));
    s.paramB = 1.0f;
    const auto companded = toDb (m.predictedPassGain (1000.0, s));
    INFO ("pass at 1 kHz: " << plain << " dB plain, " << companded << " dB through the compander");
    CHECK (std::abs (companded - 2.0 * plain) < 1.0e-9);
}
