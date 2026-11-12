#include "support/MachineGates.h"

#include <catch2/catch_test_macros.hpp>

using namespace mulberry;
using namespace mulberry::analysis;

namespace
{
    constexpr double fs = 48000.0;

    dsp::ChainSettings clean (float ms, float fb = 0.0f)
    {
        return gates::machineAt (Machine::clean, ms, fb);
    }

    /** Sample index of the strongest absolute value in [from, to). */
    size_t loudest (const std::vector<float>& x, size_t from, size_t to)
    {
        size_t at = from;
        for (size_t i = from; i < std::min (to, x.size()); ++i)
            if (std::abs (x[i]) > std::abs (x[at]))
                at = i;
        return at;
    }
}

TEST_CASE ("Chain: Ping-Pong bounces L, R, L at Time", "[chain]")
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = clean (100.0f, 0.5f);
    s.plan.mode = Mode::pingPong;
    const auto n = (size_t) (0.5 * fs);
    const auto out = render (chain, s, impulse (n, fs), n);
    const auto t = (size_t) (0.1 * fs);

    CHECK (std::abs (out.left[t]) > 0.4f);
    CHECK (std::abs (out.right[t]) < 1.0e-4f);
    CHECK (std::abs (out.right[2 * t]) > 0.2f);
    CHECK (std::abs (out.left[2 * t]) < 1.0e-4f);
    CHECK (std::abs (out.left[3 * t]) > 0.1f);
}

TEST_CASE ("Chain: Dual runs two times, Pattern places its steps", "[chain]")
{
    {
        dsp::EchoChain chain;
        chain.prepare (fs, 256);
        auto s = clean (100.0f);
        s.plan.mode = Mode::dual;
        s.timeMs = { 100.0f, 160.0f };
        const auto n = (size_t) (0.3 * fs);
        const auto out = render (chain, s, impulse (n, fs), n);
        CHECK (loudest (out.left, 10, n) == (size_t) (0.1 * fs));
        CHECK (loudest (out.right, 10, n) == (size_t) (0.16 * fs));
    }
    {
        dsp::EchoChain chain;
        chain.prepare (fs, 256);
        auto s = clean (400.0f);
        s.plan.mode = Mode::pattern;
        s.plan.steps = 4;
        s.plan.stepLevels = { 1.0f, 0.0f, 0.5f, 1.0f };
        s.plan.panRule = PanRule::centre;
        const auto n = (size_t) (0.45 * fs);
        const auto out = render (chain, s, impulse (n, fs), n);
        CHECK (std::abs (out.left[(size_t) (0.1 * fs)]) > 0.9f);
        CHECK (std::abs (out.left[(size_t) (0.2 * fs)]) < 1.0e-4f);
        CHECK (std::abs (out.left[(size_t) (0.3 * fs)]) > 0.45f);
        CHECK (std::abs (out.left[(size_t) (0.4 * fs)]) > 0.9f);
    }
}

TEST_CASE ("Chain: Groove swings the odd echo, Feel moves every echo", "[chain]")
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = clean (200.0f, 0.6f);
    s.plan.groove = maps::grooveOffset (0.75f);
    s.plan.feelMs = 10.0f;
    const auto n = (size_t) (0.9 * fs);
    const auto out = render (chain, s, impulse (n, fs), n);
    // Echoes at 1.5 T, 2 T, 3.5 T, 4 T, each 10 ms late.
    const auto at = [] (double ms) { return (size_t) std::lround ((ms + 10.0) * 0.001 * fs); };
    CHECK (loudest (out.left, 10, at (350.0)) == at (300.0));
    CHECK (loudest (out.left, at (300.0) + 10, at (450.0)) == at (400.0));
    CHECK (loudest (out.left, at (400.0) + 10, at (750.0)) == at (700.0));
}

TEST_CASE ("Chain: pedal-law Mix and Wet", "[chain]")
{
    CHECK (maps::dryGain (0.3f) == 1.0f);
    CHECK (maps::wetGain (0.5f) == 1.0f);
    CHECK (std::abs (maps::dryGain (0.75f) - 0.5f) < 1.0e-6f);
    CHECK (std::abs (maps::wetGain (0.25f) - 0.5f) < 1.0e-6f);

    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = clean (100.0f);
    s.wetOnly = false;
    s.mix = 0.25f;
    const auto n = (size_t) (0.2 * fs);
    const auto out = render (chain, s, impulse (n, fs, 0.1f), n);
    CHECK (std::abs (out.left[0] - 0.1f) < 1.0e-5f);
    CHECK (std::abs (std::abs (out.left[(size_t) (0.1 * fs)]) - 0.05f) < 1.0e-4f);
}

TEST_CASE ("Chain: changing machine lets the old echoes ring out (Trails)", "[chain]")
{
    for (const auto trails : { true, false })
    {
        dsp::EchoChain chain;
        chain.prepare (fs, 256);
        auto s = clean (300.0f, 0.6f);
        s.trails = trails;
        auto other = s;
        other.machine = Machine::reel;
        const auto change = (size_t) (0.1 * fs);
        const auto n = (size_t) (1.0 * fs);
        const auto out = render (chain, [&] (size_t pos) { return pos >= change ? other : s; }, impulse (n, fs), n);
        const auto echo = std::abs (out.left[(size_t) (0.3 * fs)]) + std::abs (out.left[(size_t) (0.6 * fs)]);
        INFO ("trails " << trails << ": echoes after the change " << echo);
        if (trails)
            CHECK (echo > 0.5);
        else
            CHECK (echo < 1.0e-3);
    }
}

TEST_CASE ("Chain: a ringing slot's runaway is capped and dies out", "[chain]")
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = clean (200.0f, 1.2f);
    auto other = s;
    other.machine = Machine::reel;
    other.feedback = 0.0f;
    const auto change = (size_t) (8.0 * fs);
    const auto n = (size_t) (30.0 * fs);
    const auto out = render (chain, [&] (size_t pos) { return pos >= change ? other : s; }, impulse (n, fs, 0.5f), n);
    const auto running = rms (out.left, change - (size_t) fs, change);
    const auto left = rms (out.left, n - (size_t) fs, n);
    INFO ("runaway " << toDb (running) << " dBFS, 21 s after the change " << toDb (left) << " dBFS");
    CHECK (running > 0.01);
    CHECK (toDb (left / running) < -40.0);
}

TEST_CASE ("Chain: Bypass with trails passes the dry and lets the echoes ring", "[chain]")
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = clean (200.0f, 0.5f);
    s.wetOnly = false;
    s.mix = 0.5f;
    auto bypassed = s;
    bypassed.bypass = true;
    const auto change = (size_t) (0.1 * fs);
    const auto n = (size_t) (0.7 * fs);
    auto in = impulse (n, fs);
    in.left[(size_t) (0.3 * fs)] = in.right[(size_t) (0.3 * fs)] = 1.0f;     // played while bypassed
    const auto out = render (chain, [&] (size_t pos) { return pos >= change ? bypassed : s; }, in, n);
    CHECK (std::abs (out.left[(size_t) (0.2 * fs)]) > 0.4f);                 // the old echo rings on
    CHECK (std::abs (out.left[(size_t) (0.3 * fs)] - 1.0f) < 1.0e-3f);       // dry at unity
    CHECK (std::abs (out.left[(size_t) (0.5 * fs)]) < 1.0e-3f);              // nothing new enters
}

TEST_CASE ("Chain: Duck pulls the wet under the playing and lets it bloom back", "[chain]")
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = clean (50.0f, 0.9f);
    s.duck = true;
    s.duckAmountDb = 18.0f;
    s.duckThresholdDb = -40.0f;
    const auto n = (size_t) (3.0 * fs);
    auto in = sine (440.0, fs, 3.0, 0.3);
    std::fill (in.left.begin() + (long) fs, in.left.end(), 0.0f);
    std::fill (in.right.begin() + (long) fs, in.right.end(), 0.0f);
    const auto out = render (chain, s, in, n);
    auto noDuck = s;
    noDuck.duck = false;
    dsp::EchoChain plain;
    plain.prepare (fs, 256);
    const auto ref = render (plain, noDuck, in, n);
    const auto playing = toDb (rms (out.left, (size_t) (0.6 * fs), (size_t) fs) / rms (ref.left, (size_t) (0.6 * fs), (size_t) fs));
    const auto after = toDb (rms (out.left, (size_t) (2.0 * fs), (size_t) (2.2 * fs)) / rms (ref.left, (size_t) (2.0 * fs), (size_t) (2.2 * fs)));
    INFO ("while playing " << playing << " dB, a second later " << after << " dB");
    CHECK (playing < -15.0);
    CHECK (after > -1.0);
}

TEST_CASE ("Chain: block sizes and settings changes never click or differ", "[chain]")
{
    const auto n = (size_t) (2.0 * fs);
    const auto in = sine (220.0, fs, 2.0, 0.25);
    auto s = clean (230.0f, 0.6f);

    // Steady settings: identical for every block size. With Wobble the control-rate modulation
    // is interpolated per sub-block, so block sizes differ far below audibility.
    for (const auto wobble : { 0.0f, 0.5f })
    {
        s.wobble = wobble;
        dsp::EchoChain a;
        a.prepare (fs, 4096);
        const auto reference = render (a, s, in, n, 512);
        for (const auto block : { 1, 7, 64, 333, 4096 })
        {
            dsp::EchoChain b;
            b.prepare (fs, 4096);
            const auto out = render (b, s, in, n, block);
            double diff = 0.0;
            for (size_t i = 0; i < n; ++i)
                diff = std::max (diff, (double) std::abs (out.left[i] - reference.left[i]));
            INFO ("wobble " << wobble << ", block " << block << ": " << diff);
            CHECK (diff < (wobble > 0.0f ? 1.0e-3 : 1.0e-6));
        }
    }

    // Mode, feel, groove, width, machine and hold changes: no clicks on a low tone.
    dsp::EchoChain c;
    c.prepare (fs, 256);
    const auto steady = sine (220.0, fs, 8.0, 0.25);
    const auto changes = [&] (size_t pos)
    {
        auto t = s;
        const auto k = (int) (pos / (size_t) (0.4 * fs));
        t.plan.mode = static_cast<Mode> (k % numModes);
        t.plan.feelMs = (k % 3) * 7.0f;
        t.plan.groove = (k % 2) * 0.3f;
        t.plan.width = (k % 4) * 0.3f;
        t.hold = (k % 5) == 4;
        return t;
    };
    const auto out = render (c, changes, steady, steady.size());
    const auto crest = clickCrestDb (out.left, (size_t) fs);
    INFO ("3rd-difference crest under changes: " << crest << " dB");
    CHECK (crest < -50.0);
}

TEST_CASE ("Chain: renders are deterministic", "[chain]")
{
    auto s = gates::machineAt (Machine::clean, 180.0f, 0.7f);
    s.wobble = 0.8f;
    s.noise = 0.5f;
    s.paramB = 0.8f;
    const auto n = (size_t) (2.0 * fs);
    const auto in = sine (330.0, fs, 2.0, 0.2);
    dsp::EchoChain a, b;
    a.prepare (fs, 256);
    b.prepare (fs, 256);
    const auto x = render (a, s, in, n);
    const auto y = render (b, s, in, n);
    CHECK (x.left == y.left);
    a.reset();
    const auto z = render (a, s, in, n);
    CHECK (x.left == z.left);
}
