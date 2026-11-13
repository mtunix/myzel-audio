/*
    The chain: a constant latency (the 2x pair's), the six modules in their order, Damage 0 clean
    to the bit with every module on, the same render for any block size and from bar 17, Mix,
    Output and Bypass without steps, and what it publishes for the display.
*/

#include "dsp/chain/InkcapChain.h"

#include "common/dsp/FloatCompare.h"
#include "common/dsp/Oversampler2x.h"
#include "support/ChainGates.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <tuple>
#include <utility>
#include <vector>

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::gates;
using myzel::dsp::isExactly;

namespace
{
    constexpr double fs = 48000.0;
}

TEST_CASE ("Chain: the latency is the 2x pair's group delay and Warble's rest, at every rate", "[chain]")
{
    const myzel::dsp::Oversampler2x pair;
    CHECK (InkcapChain::pairLatencySamples() == (int) std::lround (pair.latencySamples()));
    CHECK (InkcapChain::pairLatencySamples() == 3);
    CHECK (WarbleModule::restSamples == 6);
    CHECK (InkcapChain::fixedLatencySamples() == 9);

    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        InkcapChain chain;
        chain.prepare (rate, 512, 2);
        CHECK (chain.getLatencySamples() == 9);
    }
    CHECK (InkcapChain::maxTailSeconds >= GlitchModule::maxTailSeconds);
}

TEST_CASE ("Chain: Damage 0 is the aligned dry to the bit, every module on at any Amount, Type, Chaos and Mix", "[chain][gate]")
{
    for (const auto amount : { 0.0f, 0.5f, 1.0f })
        for (int type = 0; type < 5; ++type)
            for (const auto chaos : { 0.0f, 1.0f })
            {
                auto s = everyModule (0.0f, chaos, amount, type);
                s.mix = type % 2 == 0 ? 1.0f : 0.37f;
                const auto db = damageZeroNullDb (s, fs, type == 0 ? 1 : 512);
                INFO ("amount " << amount << ", type " << type << ", chaos " << chaos << ": " << db << " dB");
                CHECK (db <= -300.0);
            }
}

TEST_CASE ("Chain: every sample rate: Damage 0 exact, full Damage finite and the same for any block size", "[chain][gate]")
{
    for (const auto rate : { 44100.0, 96000.0, 192000.0 })
    {
        INFO (rate << " Hz");
        CHECK (damageZeroNullDb (everyModule (0.0f, 0.5f, 1.0f, 4), rate) <= -300.0);
        CHECK (sameForEveryBlockSize (everyModule (1.0f, 0.8f, 1.0f, 4), rate, 1.0));

        const auto in = analysis::programme (rate, 1.0, 7);
        const auto out = render (everyModule (1.0f, 1.0f, 1.0f, 4), in, 512, rate);
        bool finite = true;
        for (const auto x : out.left)
            finite = finite && std::isfinite (x) && std::abs (x) < 4.0f;
        CHECK (finite);
        CHECK (analysis::nullDb (out.left, delayed (in.left, InkcapChain::fixedLatencySamples())) > -40.0);
    }
}

TEST_CASE ("Chain: Mix 0 is the aligned dry; Mix blends linearly", "[chain]")
{
    const auto in = analysis::programme (fs, 2.0, 3);
    auto s = everyModule (1.0f, 0.5f);
    s.mix = 0.0f;

    // Where Crush and Bits rest, Mix's dry is the input delayed, to the bit.
    auto resting = s;
    resting.crush.on = resting.bits.on = false;
    CHECK (analysis::identical (render (resting, in, 512, fs).left, delayed (in.left, InkcapChain::fixedLatencySamples())));

    // With them on it takes their phase (Crush's pair, flat to 0.003 dB; Bits' delay, through the
    // windowed sinc, flat to 0.35 fs and 1.7 dB down at 0.4 fs): the input's level.
    const auto dryOnly = render (s, in, 512, fs);
    CHECK (std::abs (analysis::toDb (analysis::rms (dryOnly.left, (size_t) (0.1 * fs)) / analysis::rms (in.left, (size_t) (0.1 * fs)))) < 0.05);

    s.mix = 1.0f;
    const auto wet = render (s, in, 512, fs);
    s.mix = 0.25f;
    const auto quarter = render (s, in, 512, fs);
    double worst = 0.0;
    for (size_t i = 0; i < in.size(); ++i)
        worst = std::max (worst, (double) std::abs (quarter.left[i] - (0.75f * dryOnly.left[i] + 0.25f * wet.left[i])));
    CHECK (worst < 1.0e-6);
}

TEST_CASE ("Chain: a partial Mix does not comb against Crush's pair or Bits' converter", "[chain][gate][mix]")
{
    // A light touch is where the wet is closest to the dry, so where a misaligned dry combs most.
    // InkcapMeasure runs every type at 44.1 and 48 kHz; here one of each kind.
    auto alone = [] (float damage)
    {
        auto s = everyModule (damage, 0.0f, 1.0f, 0);
        s.glitch.on = s.warble.on = s.crush.on = s.bits.on = s.hiss.on = s.room.on = false;
        return s;
    };
    for (const auto& [type, damage] : { std::pair { 0, 0.05f }, { 1, 0.5f }, { 2, 0.05f }, { 3, 0.05f } })
    {
        auto s = alone (damage);
        s.crush = { true, 1.0f, (CrushType) type, 0.0f, 0.0f };
        const auto comb = mixCombDb (s, fs, 100.0, 24);
        INFO ("Crush type " << type << ", Damage " << damage << ": " << comb << " dB");
        CHECK (comb > -1.0);
    }
    for (const auto& [type, damage] : { std::pair { 0, 0.1f }, { 1, 0.4f }, { 2, 0.1f } })
    {
        auto s = alone (damage);
        s.bits = { true, 1.0f, (BitsType) type, 0.5f, 1.0f, 0.0f };
        // Phone's 300 Hz low cut leads the dry below its band: a parallel filter's own phase.
        const auto comb = mixCombDb (s, fs, type == 2 ? 1000.0 : 100.0, 24);
        INFO ("Bits type " << type << ", Damage " << damage << ": " << comb << " dB");
        CHECK (comb > -1.0);
    }
}

TEST_CASE ("Chain: Output and Mix follow automation at any block size", "[chain]")
{
    // One new value per block, as a host sends automation: the gain follows the ramp at a
    // constant lag (the glides' 30 ms), whatever the block size, and lands when it stops.
    const auto seconds = 2.5;
    const auto n = (size_t) (seconds * fs);
    const Stereo dc { std::vector<float> (n, 0.5f), std::vector<float> (n, 0.5f) };
    for (const auto block : { 64, 512 })
    {
        ChainSettings s;
        s.damage = 0.0f;
        const auto out = render (s, dc, block, fs, {}, [] (int64_t start, ChainSettings& c)
        {
            c.outputDb = (float) (-24.0 * std::min (1.0, (double) start / (2.0 * fs)));
        });
        auto gainAt = [&] (double t) { return analysis::toDb (out.left[(size_t) (t * fs)] / 0.5); };
        INFO ("block " << block << ": " << gainAt (1.0) << " dB at 1 s, " << gainAt (2.0) << " dB at 2 s");
        CHECK (std::abs (gainAt (1.0) - (-12.0 + 0.4)) < 0.5);
        CHECK (std::abs (gainAt (2.0) - (-24.0 + 0.4)) < 0.5);
        CHECK (std::abs (gainAt (2.4) + 24.0) < 1.0e-3);
    }

    // Mix from 100 to 0 % over 2 s, the wet a hum over silence: at the ramp's end the hum is
    // where the ramp was 30 ms before, not where it began.
    for (const auto block : { 64, 512 })
    {
        auto s = everyModule (1.0f, 0.0f, 1.0f, 2);
        s.glitch.on = s.warble.on = s.crush.on = s.bits.on = s.room.on = false;
        const Stereo silence { std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f) };
        const auto wet = render (s, silence, block, fs);
        const auto out = render (s, silence, block, fs, {}, [] (int64_t start, ChainSettings& c)
        {
            c.mix = (float) std::max (0.0, 1.0 - (double) start / (2.0 * fs));
        });
        auto windowRms = [&] (const std::vector<float>& x, double t)
        {
            return analysis::rms (x, (size_t) ((t - 0.05) * fs), (size_t) (t * fs));
        };
        const auto ratio = windowRms (out.left, 2.0) / windowRms (wet.left, 2.0);
        INFO ("block " << block << ": the hum at the ramp's end is " << ratio << " of the wet");
        CHECK (ratio < 0.06);
        CHECK (isExactly (windowRms (out.left, 2.5), 0.0));
    }
}

TEST_CASE ("Chain: the same render for any block size, to the bit", "[chain][gate][determinism]")
{
    for (int type = 0; type < 5; ++type)
    {
        INFO ("type " << type);
        CHECK (sameForEveryBlockSize (everyModule (0.85f, 0.6f, 1.0f, type), fs));
    }

    // Changing settings at fixed song positions: still the same for any block size, as long as the
    // changes land on block boundaries common to all of them (49152 = 12 x 4096).
    const auto in = analysis::programme (fs, 2.0, 4);
    auto change = [] (int64_t start, ChainSettings& s) { s.damage = start < 49152 ? 0.4f : 0.9f; };
    const auto reference = render (everyModule (0.4f, 0.3f), in, 4096, fs, {}, change);
    for (const auto block : { 1, 64, 1024 })
        CHECK (analysis::identical (render (everyModule (0.4f, 0.3f), in, block, fs, {}, change).left, reference.left));
}

TEST_CASE ("Chain: a render started at bar 17 matches bar 17 of a render from bar 1", "[chain][gate][determinism]")
{
    // The plan's gate at the defaults (worn tape, Glitch on): within -80 dB after 500 ms.
    auto s = everyModule (0.5f, 0.25f, 0.5f);
    s.glitch.type = GlitchType::skip;
    const auto db = barSeventeenDb (s, fs);
    INFO ("difference after 500 ms: " << db << " dB");
    CHECK (db < -80.0);
}

TEST_CASE ("Chain: a block longer than prepared is processed in pieces, the same to the bit", "[chain]")
{
    const auto in = analysis::programme (fs, 1.0, 5);
    const auto s = everyModule (0.9f, 0.5f, 1.0f, 4);
    const auto reference = render (s, in, 64, fs);

    InkcapChain chain;
    chain.prepare (fs, 64, 2);     // prepared for 64, called with 6000
    const auto big = render (s, in, 6000, fs, {}, {}, &chain);
    CHECK (analysis::identical (big.left, reference.left));
    CHECK (analysis::identical (big.right, reference.right));
}

TEST_CASE ("Chain: Output fades and arrives exactly; Bypass fades to the aligned dry", "[chain]")
{
    constexpr int n = 12000;
    const auto sine = analysis::sine (440.0, fs, (double) n / fs, 0.5);
    const Stereo in { sine, sine };

    InkcapChain chain;
    chain.prepare (fs, 256, 2);
    const auto latency = chain.getLatencySamples();

    // The first block jumps to its settings: -12 dB from the start (Damage 0: the wet is the dry).
    ChainSettings s;
    s.damage = 0.0f;
    s.outputDb = -12.0f;
    auto out = render (s, in, 256, fs, {}, {}, &chain);
    const auto gain = std::pow (10.0f, -12.0f / 20.0f);
    CHECK (std::abs (out.left[6000] - gain * in.left[(size_t) (6000 - latency)]) < 1.0e-6f);

    // Bypass: a 20 ms fade (no step larger than the sine's own), then exactly the aligned dry.
    s.bypass = true;
    out = render (s, in, 256, fs, {}, {}, &chain);
    float largestStep = 0.0f;
    for (int i = 1; i < n; ++i)
        largestStep = std::max (largestStep, std::abs (out.left[(size_t) i] - out.left[(size_t) i - 1]));
    const auto sineStep = 0.5f * 2.0f * 3.14159265f * 440.0f / (float) fs;
    CHECK (largestStep < sineStep * 1.05f);

    bool exact = true;
    for (int i = (int) (InkcapChain::bypassSeconds * fs) + 1; i < n; ++i)
        exact = exact && isExactly (out.left[(size_t) i], in.left[(size_t) (i - latency)]);
    CHECK (exact);

    // Back on at 0 dB: the 50 ms Output fade ends on exactly 1, so the wet is the dry again to the bit.
    s.bypass = false;
    s.outputDb = 0.0f;
    render (s, in, 256, fs, {}, {}, &chain);
    out = render (s, in, 256, fs, {}, {}, &chain);
    exact = true;
    for (int i = 0; i < n; ++i)
        exact = exact && isExactly (out.right[(size_t) i], in.right[(size_t) ((i - latency + n) % n)]);
    CHECK (exact);
}

TEST_CASE ("Chain: no clicks from Bypass, Mix, Output or the module keys (3rd-difference crest on a sine)", "[chain][gate]")
{
    const auto sine = analysis::sine (330.0, fs, 4.0, 0.5);
    const Stereo in { sine, sine };
    auto s = everyModule (0.7f, 0.3f);
    s.glitch.on = false;     // its events are the sound; their edges have their own gate
    s.hiss.on = false;       // noise and crackle are broadband by design; its fades have their own gate (on Hum)
    s.bits.on = false;       // its steps are the sound: the plan's click gate excepts Bits

    const auto out = render (s, in, 256, fs, {}, [] (int64_t start, ChainSettings& c)
    {
        const auto t = (double) start / fs;
        c.bypass = t > 0.5 && t < 1.0;
        c.mix = t < 1.5 ? 1.0f : 0.4f;
        c.outputDb = t < 2.0 ? 0.0f : -9.0f;
        c.warble.on = ! (t > 2.5 && t < 3.0);
        c.damage = t < 3.3 ? 0.7f : 0.0f;
    });
    const auto crest = analysis::clickCrestDb (out.left, (size_t) (0.1 * fs));
    INFO ("crest " << crest << " dB");
    CHECK (crest <= -60.0);
}

TEST_CASE ("Chain: mono, odd channel counts and empty blocks are safe", "[chain]")
{
    InkcapChain chain;
    chain.prepare (44100.0, 128, 1);
    const auto latency = chain.getLatencySamples();

    std::vector<float> mono (300);
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = (float) i;
    auto copy = mono;
    float* channels[] = { copy.data() };

    ChainSettings s;
    s.damage = 0.0f;
    chain.process (channels, 1, 0, s);
    chain.process (channels, 1, (int) copy.size(), s);
    for (size_t i = (size_t) latency; i < copy.size(); ++i)
        CHECK (isExactly (copy[i], mono[i - (size_t) latency]));

    // More channels than the chain carries: the extra ones are left alone.
    std::vector<float> a (64, 1.0f), b (64, 1.0f), c (64, 0.25f);
    float* three[] = { a.data(), b.data(), c.data() };
    chain.reset();
    chain.process (three, 3, 64, everyModule (1.0f, 1.0f));
    CHECK (isExactly (c[10], 0.25f));
}

TEST_CASE ("Chain: the display's telemetry follows the audio thread", "[chain][telemetry]")
{
    InkcapChain chain;
    chain.prepare (fs, 512, 2);
    auto s = everyModule (0.6f, 0.0f, 0.8f);
    s.bits.on = false;
    s.seed = 0xC0FFEEu;

    const auto in = analysis::programme (fs, 4.0, 6);
    const Transport t { true, 16.0, 100.0, 7, 8 };
    auto maxActivity = 0.0f;
    bool sawEvent = false;
    auto out = in;
    for (int64_t start = 0; start + 512 <= (int64_t) in.size(); start += 512)
    {
        float* ch[] = { out.left.data() + start, out.right.data() + start };
        chain.process (ch, 2, 512, s, t.at (start, fs));
        maxActivity = std::max (maxActivity, chain.telemetry().modules[(size_t) Module::glitch].activity.load());
        sawEvent = sawEvent || chain.telemetry().glitchEventType.load() >= 0;
    }

    const auto& m = chain.telemetry();
    float wear = 0.0f;
    for (const auto module : { Module::glitch, Module::warble, Module::crush, Module::hiss, Module::room })
    {
        const auto expected = moduleIntensity (module, 0.8f, 0.6f);
        CHECK (std::abs (m.modules[(size_t) module].intensity.load() - expected) < 1.0e-6f);
        wear += expected / 5.0f;
    }
    CHECK (m.modules[(size_t) Module::warble].on.load());
    CHECK_FALSE (m.modules[(size_t) Module::bits].on.load());
    CHECK (isExactly (m.modules[(size_t) Module::bits].intensity.load(), 0.0f));
    CHECK (std::abs (m.wear.load() - wear) < 1.0e-6f);       // the five modules that are on
    CHECK (m.modules[(size_t) Module::warble].activity.load() > 0.0f);
    CHECK (maxActivity > 0.0f);
    CHECK (sawEvent);
    CHECK (m.glitchSeed.load() == 0xC0FFEEu);
    CHECK (isExactly (m.bpm.load(), 100.0));
    CHECK (m.followingHost.load());
    CHECK (m.timeSigNumerator.load() == 7);
    CHECK (m.timeSigDenominator.load() == 8);
    const auto blocks = (int64_t) m.blocks.load();
    CHECK (std::abs (m.ppq.load() - (16.0 + (double) blocks * 512.0 * 100.0 / 60.0 / fs)) < 1.0e-9);

    // Peaks: max since the last read; reading takes them.
    auto& meters = const_cast<Telemetry&> (m);
    CHECK (meters.inputPeak[0].exchange (0.0f) > 0.3f);
    CHECK (meters.outputPeak[1].exchange (0.0f) > 0.1f);
    CHECK (isExactly (meters.inputPeak[0].load(), 0.0f));
}

TEST_CASE ("Chain: a host loop that wraps inside a block repeats its glitches on every pass", "[chain][determinism]")
{
    // One bar looped at 120 BPM (2 s), the host reporting only each block's start (no split at the
    // loop's end): 512-sample blocks wrap inside a block on every pass, at a different offset.
    const auto loopLength = (int64_t) (2.0 * fs);
    const auto bar = analysis::programme (fs, 2.0, 43);
    const int passes = 4;
    Stereo in;
    for (int p = 0; p < passes; ++p)
    {
        in.left.insert (in.left.end(), bar.left.begin(), bar.left.end());
        in.right.insert (in.right.end(), bar.right.begin(), bar.right.end());
    }

    struct Log final : GlitchModule::EventLog
    {
        std::vector<std::tuple<int64_t, int64_t, bool>> events;     // pass offset, slot, played
        void fired (const glitch::Event& e, int64_t sample, bool played) override { events.emplace_back (sample, e.slot, played); }
    } log;

    auto s = everyModule (0.9f, 0.4f, 1.0f, 0);
    s.warble.on = s.crush.on = s.bits.on = s.hiss.on = s.room.on = false;
    s.glitch.type = GlitchType::stall;

    InkcapChain chain;
    chain.prepare (fs, 512, 2);
    chain.glitchModule().setEventLog (&log);
    auto out = in;
    for (int64_t start = 0; start < (int64_t) in.size(); start += 512)
    {
        HostPosition h;
        h.playing = h.hasPpq = h.looping = true;
        h.bpm = 120.0;
        h.loopStartPpq = 8.0;
        h.loopEndPpq = 12.0;
        h.ppq = 8.0 + (double) (start % loopLength) * 2.0 / fs;
        const auto len = (int) std::min<int64_t> (512, (int64_t) in.size() - start);
        float* ch[] = { out.left.data() + start, out.right.data() + start };
        chain.process (ch, 2, len, s, h);
    }

    // The chain's processing index runs on; each pass starts where the loop wrapped.
    std::vector<std::vector<std::tuple<int64_t, int64_t, bool>>> perPass ((size_t) passes);
    for (const auto& [sample, slot, played] : log.events)
        perPass[(size_t) (sample / loopLength)].emplace_back (sample % loopLength, slot, played);
    CHECK_FALSE (perPass[1].empty());
    for (int p = 2; p < passes; ++p)
    {
        INFO ("pass " << p);
        CHECK (perPass[(size_t) p] == perPass[1]);
    }
    for (const auto& [offset, slot, played] : perPass[1])
        CHECK (slot < 48);      // nothing past the loop's end: 1/16 slots 32 .. 47

    // The audio repeats too: pass 3 is pass 2 to the bit.
    const auto from = (std::ptrdiff_t) (2 * loopLength), length = (std::ptrdiff_t) loopLength;
    const std::vector<float> two (out.left.begin() + from, out.left.begin() + from + length);
    const std::vector<float> three (out.left.begin() + from + length, out.left.begin() + from + 2 * length);
    CHECK (analysis::identical (two, three));
}
