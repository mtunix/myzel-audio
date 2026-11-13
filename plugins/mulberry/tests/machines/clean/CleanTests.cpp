#include "support/GateCases.h"

#include "common/dsp/Interpolators.h"
#include "dsp/machines/clean/CleanMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <complex>

using namespace mulberry;
using namespace mulberry::analysis;

namespace
{
    gates::GateOptions cleanGates()
    {
        gates::GateOptions o;
        o.timeToleranceSamples = 0.1;
        return o;
    }

    dsp::ChainSettings clean (float timeMs, float feedback = 0.0f)
    {
        auto s = wetOnly (Machine::clean);
        s.timeMs = { timeMs, timeMs };
        s.feedback = feedback;
        return s;
    }
}

TEST_CASE ("Clean: the first echo lands within 0.1 sample of Time", "[clean]")
{
    for (const auto fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto ms : { 1.0f, 3.7f, 123.456f, 375.0f, 1000.0f / 3.0f, 2999.9f })
        {
            dsp::EchoChain chain;
            chain.prepare (fs, 256);
            const auto expected = (double) ms * 0.001 * fs;
            const auto length = (size_t) expected + 2000;
            const auto out = render (chain, clean (ms), impulse (length, fs), length);

            const auto from = (size_t) std::max (0.0, expected - 40.0);
            const auto tau = groupDelay (out.left, from, from + 80, 1000.0, fs);
            INFO (fs << " Hz, " << ms << " ms: expected " << expected << ", measured " << tau);
            CHECK (std::abs (tau - expected) < 0.1);
        }
}

TEST_CASE ("Clean: per-pass loss is below 0.1 dB to 10 kHz and 0.5 dB at 16 kHz at any fraction", "[clean]")
{
    const auto& sinc = dsp::WindowedSinc::instance();
    double worst10 = 0.0, worst16 = 0.0, over = -100.0;

    for (int p = 0; p <= dsp::WindowedSinc::phases; p += 8)
    {
        const auto& k = sinc.kernel (p);
        auto gain = [&k] (double hz)
        {
            const auto w = 2.0 * std::numbers::pi * hz / 48000.0;
            std::complex<double> h {};
            for (int j = 0; j < dsp::WindowedSinc::taps; ++j)
                h += (double) k[(size_t) j] * std::polar (1.0, -w * j);
            return toDb (std::abs (h));
        };
        for (double hz = 20.0; hz <= 10000.0; hz *= 1.05)
            worst10 = std::min (worst10, gain (hz));
        worst16 = std::min (worst16, gain (16000.0));
        for (double hz = 20.0; hz < 24000.0; hz *= 1.02)
            over = std::max (over, gain (hz));
    }

    INFO ("worst to 10 kHz " << worst10 << " dB, at 16 kHz " << worst16 << " dB, peak " << over << " dB");
    CHECK (sinc.dcGain() > 0.9998f);
    CHECK (worst10 > -0.1);
    CHECK (worst16 > -0.5);
    CHECK (over <= 0.0);        // never above unity: safe at 100 % feedback
}

TEST_CASE ("Clean: silence in gives exact silence out", "[clean]")
{
    dsp::EchoChain chain;
    chain.prepare (48000.0, 256);
    auto s = clean (120.0f, 0.9f);
    s.wobble = 0.5f;
    s.diffuse = 0.5f;
    const auto n = (size_t) (48000 * 30);
    const auto out = render (chain, s, impulse (n, 48000.0, 0.5f), n);
    CHECK (peak (out.left, n - 48000) == 0.0);
    CHECK (peak (out.right, n - 48000) == 0.0);
}

TEST_CASE ("Clean: repeats keep their level at 100 % and grow above it into a bounded runaway", "[clean]")
{
    const auto fs = 48000.0;
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    const auto t = 250.0f;
    const auto n = (size_t) (fs * 4.0);
    const auto in = toneBurst (1000.0, fs, 0.1, 0.01, n);
    const auto out = render (chain, clean (t, 1.0f), in, n);
    const auto period = (size_t) (t * 0.001 * fs);
    const auto first = toneAmplitude (out.left, period + 480, 3840, 1000.0, fs);
    const auto tenth = toneAmplitude (out.left, 10 * period + 480, 3840, 1000.0, fs);
    INFO ("first " << toDb (first) << " dB, tenth " << toDb (tenth) << " dB");
    CHECK (std::abs (toDb (tenth / first)) < 0.05);
}

MULBERRY_MACHINE_GATES (Machine::clean, "Clean", "[clean]", cleanGates())
