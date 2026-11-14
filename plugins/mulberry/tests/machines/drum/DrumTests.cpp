#include "support/GateCases.h"

#include "dsp/machines/drum/DrumMachine.h"

using namespace mulberry;
using namespace mulberry::analysis;

namespace
{
    // Gates on head 4 alone without Swell: one echo at Time, fed back from itself.
    const float head4 = character::stepCentre (3, 15);

    gates::GateOptions drumGates()
    {
        gates::GateOptions o;
        o.paramA = head4;
        o.paramB = 0.0f;
        o.flutterTarget = 0.15;
        return o;
    }

    constexpr double fs = 48000.0;

    dsp::ChainSettings drum (float ms, float fb, float heads, float swell)
    {
        auto s = gates::machineAt (Machine::drum, ms, fb);
        s.paramA = heads;
        s.paramB = swell;
        return s;
    }

    double passDb (float ms, double hz)
    {
        dsp::DrumMachine m;
        dsp::SlotMemory memory;
        memory.allocate (fs);
        m.prepare (fs, memory);
        dsp::MachineSettings s;
        s.timeMs = { ms, ms };
        return toDb (m.predictedPassGain (hz, s));
    }

    /** RMS of the left output in a window around t seconds after the impulse. */
    double levelAt (const Stereo& out, size_t start, double t, double halfWidth = 0.004)
    {
        const auto at = start + (size_t) (t * fs);
        const auto w = (size_t) (halfWidth * fs);
        return rms (out.left, at - w, at + w);
    }
}

MULBERRY_MACHINE_GATES (Machine::drum, "Drum", "[drum]", drumGates())

TEST_CASE ("Drum: four heads at quarters of Time", "[drum]")
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    const auto n = (size_t) (1.2 * fs);
    const auto settle = (size_t) (0.5 * fs);
    const auto all = character::stepCentre (14, 15);
    const auto out = render (chain, drum (400.0f, 0.0f, all, 0.0f), impulse (n, fs, 0.2f, settle), n);
    for (int h = 1; h <= 4; ++h)
    {
        const auto at = settle + (size_t) (h * 0.1 * fs);
        const auto t = groupDelay (out.left, at - 200, at + 200, 1000.0, fs);
        INFO ("head " << h << ": " << (t - (double) settle) / fs * 1000.0 << " ms");
        CHECK (std::abs ((t - (double) settle) / fs * 1000.0 - 100.0 * h) < 0.1 * h);
    }
}

TEST_CASE ("Drum: Swell feeds back from every head, so the trail sustains longer", "[drum]")
{
    // Head 1 alone (100 ms of a 400 ms drum) at 80 %: without Swell each 100 ms step loses the
    // whole pass; with full Swell part of the feedback comes from the later heads, so per unit
    // of time the trail decays more slowly, while the shares still sum to 1.
    const auto decayDb = [] (float swell)
    {
        dsp::EchoChain chain;
        chain.prepare (fs, 256);
        const auto n = (size_t) (2.0 * fs);
        const auto settle = (size_t) (0.5 * fs);
        auto s = drum (400.0f, 0.8f, character::stepCentre (0, 15), swell);
        s.wobble = 0.0f;
        const auto out = render (chain, s, impulse (n, fs, 0.2f, settle), n);
        return std::pair { toDb (levelAt (out, settle, 1.1) / levelAt (out, settle, 0.2)), toDb (levelAt (out, settle, 1.4) / levelAt (out, settle, 1.1)) };
    };
    const auto [plain, plainLate] = decayDb (0.0f);
    const auto [swelled, swelledLate] = decayDb (1.0f);
    INFO ("200 -> 1100 ms: " << plain << " dB without Swell, " << swelled << " dB with full Swell");
    CHECK (swelled > plain + 5.0);
    CHECK (swelledLate < 0.0);
    CHECK (plainLate < 0.0);
}

TEST_CASE ("Drum: a slower drum gives darker repeats; losses hold beyond the physical range", "[drum]")
{
    INFO ("6 kHz per pass: " << passDb (250.0f, 6000.0) << " dB at 250 ms, " << passDb (900.0f, 6000.0) << " dB at 900 ms");
    CHECK (passDb (900.0f, 6000.0) < passDb (250.0f, 6000.0) - 4.0);
    CHECK (std::abs (passDb (1200.0f, 6000.0) - passDb (3000.0f, 6000.0)) < 1.0e-9);
    CHECK (std::abs (passDb (120.0f, 6000.0) - passDb (30.0f, 6000.0)) < 1.0e-9);
}

TEST_CASE ("Drum: a Time change glides the drum, bending the echoes", "[drum]")
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    const auto head4 = character::stepCentre (3, 15);
    const auto a = drum (300.0f, 0.0f, head4, 0.0f), b = drum (600.0f, 0.0f, head4, 0.0f);
    const auto in = sine (1000.0, fs, 3.0, 0.1);
    const auto change = (size_t) (1.5 * fs);
    const auto out = render (chain, [&] (size_t pos) { return pos >= change ? b : a; }, in, in.size());

    const auto pitchAt = [&] (size_t from)
    {
        double best = 0.0, bestHz = 0.0;
        for (double hz = 400.0; hz < 1100.0; hz += 1.0)
        {
            const auto amp = toneAmplitude (out.left, from, (size_t) (0.05 * fs), hz, fs);
            if (amp > best) { best = amp; bestHz = hz; }
        }
        return bestHz;
    };
    const auto before = pitchAt (change - (size_t) (0.1 * fs));
    const auto during = pitchAt (change + (size_t) (0.3 * fs));
    INFO ("pitch before " << before << " Hz, 0.3 s into the change " << during << " Hz");
    CHECK (std::abs (before - 1000.0) < 3.0);
    CHECK (during < 950.0);
}

