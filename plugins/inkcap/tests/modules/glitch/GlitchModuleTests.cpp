/*
    The Glitch module: clean between events and at Damage 0 (to the bit), each failure doing
    what it says on a sine, events on the grid at any tempo, no clicks at their edges, and the
    determinism gates: any block size, a start at bar 17, loops, a new seed at the next slot.
*/

#include "dsp/modules/glitch/GlitchModule.h"

#include "common/dsp/FloatCompare.h"
#include "support/ModuleHarness.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::testing;
using analysis::Stereo;
using myzel::dsp::isExactly;

namespace
{
    constexpr double fs = 48000.0;

    struct Fired
    {
        glitch::Event event;
        int64_t sample;
        bool played;
    };

    struct Log final : GlitchModule::EventLog
    {
        std::vector<Fired> events;
        void fired (const glitch::Event& e, int64_t sample, bool played) override { events.push_back ({ e, sample, played }); }
    };

    GlitchSettings settings (GlitchType type, float length = 0.5f, int rate = defaultGlitchRate)
    {
        GlitchSettings s;
        s.on = true;
        s.amount = 1.0f;
        s.type = type;
        s.length = length;
        s.rate = rate;
        return s;
    }

    Stereo sineInput (double hz, double seconds)
    {
        const auto x = analysis::sine (hz, fs, seconds, 0.5);
        return { x, x };
    }

    struct Rendered
    {
        Stereo out;
        std::vector<Fired> events;
    };

    const Fired& firstPlayed (const std::vector<Fired>& events)
    {
        const auto i = std::find_if (events.begin(), events.end(), [] (const Fired& f) { return f.played; });
        REQUIRE (i != events.end());
        return *i;
    }

    Rendered render (const GlitchSettings& s, Macros m, const Stereo& in, int block = 512, Transport t = {})
    {
        GlitchModule module;
        module.prepare ({ fs, block, 2 });
        Log log;
        module.setEventLog (&log);
        auto out = renderModule (module, s, m, in, block, fs, t);
        return { std::move (out), std::move (log.events) };
    }
}

TEST_CASE ("Glitch: Damage 0, Off and the time between events are the input, to the bit", "[glitch]")
{
    const auto in = analysis::programme (fs, 3.0);

    for (int type = 0; type < 5; ++type)
        for (const auto chaos : { 0.0f, 1.0f })
        {
            INFO ("type " << type << ", chaos " << chaos);
            const auto atZero = render (settings ((GlitchType) type), { 0.0f, chaos }, in);
            CHECK (atZero.events.empty());
            CHECK (analysis::identical (atZero.out.left, in.left));
            CHECK (analysis::identical (atZero.out.right, in.right));

            auto off = settings ((GlitchType) type);
            off.on = false;
            const auto offRender = render (off, { 1.0f, chaos }, in);
            CHECK (analysis::identical (offRender.out.left, in.left));
        }

    // Full Damage: outside each event's span the output is the input.
    const auto r = render (settings (GlitchType::any), { 1.0f, 0.5f }, in);
    REQUIRE (r.events.size() > 4);
    std::vector<bool> busy (in.size(), false);
    for (const auto& f : r.events)
        if (f.played)
        {
            const auto end = std::min<int64_t> ((int64_t) in.size(), f.sample + (int64_t) std::ceil (glitch::durationSeconds (f.event) * fs) + 2);
            for (auto i = f.sample; i < end; ++i)
                busy[(size_t) i] = true;
        }
    bool clean = true;
    for (size_t i = 0; i < in.size(); ++i)
        clean = clean && (busy[i] || isExactly (in.left[i], r.out.left[i]));
    CHECK (clean);
}

TEST_CASE ("Glitch: Skip repeats the piece before the slot, then returns to the input", "[glitch]")
{
    const auto in = analysis::programme (fs, 4.0, 5);
    auto s = settings (GlitchType::skip, 0.4f);
    const auto r = render (s, { 1.0f, 0.0f }, in);
    REQUIRE_FALSE (r.events.empty());

    for (const auto& f : r.events)
    {
        if (! f.played)
            continue;
        const auto piece = (int64_t) std::lround (f.event.lengthSeconds * fs);
        const auto fade = (int64_t) std::lround (glitch::skipFadeSeconds * fs);
        const auto start = f.sample;
        if (start + piece * f.event.repeats >= (int64_t) in.size())
            continue;

        INFO ("event at " << start << ", piece " << piece << ", " << f.event.repeats << " repeats");
        bool repeats = true;
        for (int k = 0; k < f.event.repeats; ++k)
            for (int64_t q = fade; q < piece; ++q)
                repeats = repeats && isExactly (r.out.left[(size_t) (start + k * piece + q)], in.left[(size_t) (start - piece + q)]);
        CHECK (repeats);
    }
}

TEST_CASE ("Glitch: Stall slows to a stop and silence, Bend drops by its semitones, Chew takes the highs", "[glitch]")
{
    // One event per bar at 1/4 (period 4 at half density), Length long.
    const auto in = sineInput (440.0, 3.0);

    SECTION ("Stall")
    {
        const auto r = render (settings (GlitchType::stall, 1.0f, 1), { 0.71f, 0.0f }, in);
        REQUIRE_FALSE (r.events.empty());
        const auto& f = firstPlayed (r.events);
        const auto stop = (int64_t) (f.event.lengthSeconds * fs);
        const auto track = analysis::frequencyTrack (r.out.left, fs, (size_t) f.sample, (size_t) (f.sample + stop * 6 / 10));
        REQUIRE (track.hz.size() > 10);
        CHECK (track.hz.front() > 400.0);
        CHECK (track.hz.back() < 300.0);     // well on the way down
        // Near the stop the level is gone.
        CHECK (analysis::peak (r.out.left, (size_t) (f.sample + stop - 200), (size_t) (f.sample + stop)) < 0.01);
    }

    SECTION ("Bend")
    {
        const auto r = render (settings (GlitchType::bend, 1.0f, 1), { 1.0f, 0.0f }, in);
        REQUIRE_FALSE (r.events.empty());
        const auto& f = firstPlayed (r.events);
        const auto length = f.event.lengthSeconds * fs;
        const auto from = (size_t) (f.sample + (int64_t) (0.25 * length)), to = (size_t) (f.sample + (int64_t) (0.6 * length));
        const auto track = analysis::frequencyTrack (r.out.left, fs, from, to);
        REQUIRE (track.hz.size() > 10);
        const auto expected = 440.0 * std::exp2 (-(double) f.event.depth / 12.0);
        INFO ("bend " << f.event.depth << " semitones: expected " << expected << " Hz, measured " << track.hz[track.hz.size() / 2]);
        CHECK (std::abs (track.hz[track.hz.size() / 2] - expected) < 0.01 * expected);
    }

    SECTION ("Chew")
    {
        // 300 Hz and 10 kHz together: in the middle of the event 10 kHz loses the depth, 300 Hz only the dip.
        auto mixed = analysis::sine (300.0, fs, 3.0, 0.3);
        const auto high = analysis::sine (10000.0, fs, 3.0, 0.3);
        for (size_t i = 0; i < mixed.size(); ++i)
            mixed[i] += high[i];
        const Stereo two { mixed, mixed };

        const auto r = render (settings (GlitchType::chew, 1.0f, 1), { 1.0f, 0.0f }, two);
        REQUIRE_FALSE (r.events.empty());
        const auto& f = firstPlayed (r.events);
        const auto from = (size_t) (f.sample + (int64_t) (0.2 * f.event.lengthSeconds * fs));
        const size_t n = 4096;
        const auto lowDb = analysis::toDb (analysis::toneAmplitude (r.out.left, 300.0, fs, from, n) / 0.3);
        const auto highDb = analysis::toDb (analysis::toneAmplitude (r.out.left, 10000.0, fs, from, n) / 0.3);
        const auto dip = 2.0 + 6.0 * (f.event.depth - 6.0) / 24.0;
        INFO ("depth " << f.event.depth << " dB: 300 Hz " << lowDb << " dB, 10 kHz " << highDb << " dB");
        CHECK (std::abs (lowDb + dip) < 1.5);
        CHECK (std::abs (highDb + dip + f.event.depth) < 3.0);     // the bilinear low-pass falls a little faster than its analogue
    }
}

TEST_CASE ("Glitch: events start on the grid at 60 to 200 BPM, swing included", "[glitch]")
{
    for (const auto bpm : { 60.0, 97.0, 140.0, 200.0 })
        for (const auto swing : { 0.5f, 0.66f })
        {
            const Transport t { true, 3.3, bpm };
            const auto in = analysis::programme (fs, 4.0, 9);
            auto s = settings (GlitchType::skip);
            s.groove = swing;
            const auto r = render (s, { 1.0f, 1.0f }, in, 333, t);
            REQUIRE (r.events.size() > 3);

            const Grid grid { glitchRates[(size_t) s.rate].beats, swing };
            bool onGrid = true;
            for (const auto& f : r.events)
            {
                // The first sample at or after the slot's start (within Grid::epsilon).
                const auto exact = (grid.slotStart (f.event.slot) - Grid::epsilon - 3.3) * 60.0 / bpm * fs;
                onGrid = onGrid && f.sample == (int64_t) std::ceil (exact);
                const auto firstChange = [&]
                {
                    for (auto i = f.sample; i < (int64_t) in.size(); ++i)
                        if (! isExactly (r.out.left[(size_t) i], in.left[(size_t) i]))
                            return i;
                    return (int64_t) -1;
                }();
                onGrid = onGrid && (! f.played || firstChange == f.sample || firstChange == f.sample + 1);
            }
            INFO (bpm << " BPM, swing " << swing);
            CHECK (onGrid);
        }
}

TEST_CASE ("Glitch: a tempo change moves the events in time, never which slots fire", "[glitch]")
{
    // 100 BPM, then 150 BPM from sample 96 000 on (the host's position runs on continuously).
    const auto change = (int64_t) 96000;
    const auto in = analysis::programme (fs, 5.0, 12);
    const auto s = settings (GlitchType::chew);

    GlitchModule module;
    module.prepare ({ fs, 400, 2 });
    Log log;
    module.setEventLog (&log);
    GridClock clock;
    clock.prepare (fs);
    auto out = in;
    const auto ppqAtChange = (double) change * 100.0 / 60.0 / fs;
    for (int64_t start = 0; start < (int64_t) in.size(); start += 400)
    {
        HostPosition h;
        h.playing = h.hasPpq = true;
        h.bpm = start < change ? 100.0 : 150.0;
        h.ppq = start < change ? (double) start * 100.0 / 60.0 / fs : ppqAtChange + (double) (start - change) * 150.0 / 60.0 / fs;
        clock.advance (h, 120.0, 400);
        CHECK_FALSE ((start > 0 && clock.jumped()));
        float* ch[] = { out.left.data() + start, out.right.data() + start };
        module.process ({ ch, 2, 400, clock.blockStart() }, s, { clock, 1.0f, 0.0f, 3u });
    }

    const auto p = glitch::patternFor (s, 1.0f, 0.0f);
    REQUIRE (log.events.size() > 6);
    int before = 0, after = 0;
    for (const auto& f : log.events)
    {
        CHECK (glitch::eventAt (3u, p, f.event.slot).has_value());
        const auto beat = p.grid.slotStart (f.event.slot) - Grid::epsilon;
        const auto expected = beat < ppqAtChange ? (int64_t) std::ceil (beat * 60.0 / 100.0 * fs)
                                                 : change + (int64_t) std::ceil ((beat - ppqAtChange) * 60.0 / 150.0 * fs);
        CHECK (f.sample == expected);
        ++(f.sample < change ? before : after);
    }
    CHECK (before > 1);
    CHECK (after > 1);
}

TEST_CASE ("Glitch: no clicks at an event's edges (3rd-difference crest on a sine)", "[glitch]")
{
    const auto in = sineInput (330.0, 4.0);
    const auto steady = analysis::clickCrestDb (in.left, 100);

    for (int type = 0; type < 4; ++type)
        for (const auto length : { 0.0f, 1.0f })
        {
            const auto r = render (settings ((GlitchType) type, length), { 0.8f, 0.3f }, in);
            REQUIRE_FALSE (r.events.empty());
            const auto crest = analysis::clickCrestDb (r.out.left, 100);
            INFO ("type " << type << ", length " << length << ": crest " << crest << " dB (the sine's own " << steady << ")");
            CHECK (crest <= -60.0);
        }
}

TEST_CASE ("Glitch: the same render for any block size, to the bit", "[glitch][determinism]")
{
    const auto in = analysis::programme (fs, 3.0, 2);
    const auto reference = render (settings (GlitchType::any), { 0.9f, 0.6f }, in, 4096);
    REQUIRE (reference.events.size() > 4);

    for (const auto block : { 1, 7, 64, 511 })
    {
        const auto r = render (settings (GlitchType::any), { 0.9f, 0.6f }, in, block);
        INFO ("block " << block);
        CHECK (analysis::identical (r.out.left, reference.out.left));
        CHECK (analysis::identical (r.out.right, reference.out.right));
        CHECK (r.events.size() == reference.events.size());
    }
}

TEST_CASE ("Glitch: a render started at bar 17 has bar 17's events, and its audio once the start is behind it", "[glitch][determinism]")
{
    constexpr double bpm = 120.0;
    const auto bar17 = (int64_t) (64.0 * 60.0 / bpm * fs);    // 1 536 000
    const auto tail = (int64_t) (6.0 * fs);
    const auto in = analysis::programme (fs, (double) (bar17 + tail) / fs, 4);

    Stereo late;
    late.left.assign (in.left.begin() + bar17, in.left.end());
    late.right.assign (in.right.begin() + bar17, in.right.end());

    for (int type = 0; type < 5; ++type)
    {
        const auto s = settings ((GlitchType) type);
        const Macros m { 0.85f, 0.7f };
        const auto whole = render (s, m, in, 512, { true, 0.0, bpm });
        const auto part = render (s, m, late, 480, { true, 64.0, bpm });

        // The same decisions from bar 17 on.
        std::vector<Fired> wholeFromBar17;
        for (const auto& f : whole.events)
            if (f.sample >= bar17)
                wholeFromBar17.push_back (f);
        INFO ("type " << type);
        REQUIRE (part.events.size() == wholeFromBar17.size());
        bool same = true;
        for (size_t i = 0; i < part.events.size(); ++i)
            same = same && part.events[i].event.slot == wholeFromBar17[i].event.slot
                   && part.events[i].event.type == wholeFromBar17[i].event.type
                   && part.events[i].sample + bar17 == wholeFromBar17[i].sample;
        CHECK (same);

        // The audio: the same to the bit once the first event played in both has cut in.
        const auto first = std::find_if (part.events.begin(), part.events.end(), [] (const Fired& f) { return f.played; });
        REQUIRE (first != part.events.end());
        const auto from = (size_t) (first->sample + (int64_t) std::ceil (glitch::cutSeconds * fs) + 1);
        std::vector<float> wholeTail (whole.out.left.begin() + bar17, whole.out.left.end());
        CHECK (analysis::identical (part.out.left, wholeTail, from));
        CHECK (from < (size_t) (2.5 * fs));
    }

    // The plan's gate, at the defaults (1/16, Skip, Length 50 %): within -80 dB after 500 ms.
    const auto s = settings (GlitchType::skip);
    const Macros m { 0.5f, 0.25f };
    const auto whole = render (s, m, in, 512, { true, 0.0, bpm });
    const auto part = render (s, m, late, 512, { true, 64.0, bpm });
    std::vector<float> wholeTail (whole.out.left.begin() + bar17, whole.out.left.end());
    CHECK (analysis::nullDb (part.out.left, wholeTail, (size_t) (0.5 * fs)) < -80.0);
}

TEST_CASE ("Glitch: Chew, Stall and Bend play on the slot of a jump; Skip, whose piece lies before it, does not", "[glitch][determinism]")
{
    // A render started exactly on a slot that fires in a render from the beginning: the downbeat
    // after play, a locate or a loop. The event plays, and the audio is the full render's to the
    // bit once a cut of whatever the full render had under way (3 ms) is behind it.
    constexpr double bpm = 120.0;
    const auto in = analysis::programme (fs, 24.0, 41);
    for (const auto type : { GlitchType::chew, GlitchType::stall, GlitchType::bend, GlitchType::skip })
    {
        const auto s = settings (type);
        const Macros m { 1.0f, 0.0f };
        const auto whole = render (s, m, in, 512, { true, 0.0, bpm });

        const auto start = std::find_if (whole.events.begin(), whole.events.end(), [] (const Fired& f) { return f.sample >= (int64_t) (8.0 * fs); });
        REQUIRE (start != whole.events.end());
        const auto at = start->sample;
        const auto ppq = Grid { glitchRates[(size_t) defaultGlitchRate].beats, 0.5 }.slotStart (start->event.slot);

        Stereo late;
        late.left.assign (in.left.begin() + at, in.left.end());
        late.right.assign (in.right.begin() + at, in.right.end());
        const auto part = render (s, m, late, 480, { true, ppq, bpm });

        INFO ("type " << (int) type << ", slot " << start->event.slot);
        REQUIRE_FALSE (part.events.empty());
        CHECK (part.events.front().event.slot == start->event.slot);
        CHECK (part.events.front().sample == 0);
        CHECK (part.events.front().played == (type != GlitchType::skip));

        if (type != GlitchType::skip)
        {
            const std::vector<float> wholeTail (whole.out.left.begin() + at, whole.out.left.end());
            CHECK (analysis::identical (part.out.left, wholeTail, (size_t) std::ceil (glitch::cutSeconds * fs) + 1));
        }
    }
}

TEST_CASE ("Glitch: a loop repeats its glitches; an event under way at the jump is not carried over", "[glitch][determinism]")
{
    // Loop bars 3-4 at 120 BPM: two seconds, played three times.
    const auto loopLength = (int64_t) (4.0 * 60.0 / 120.0 * fs * 2.0);
    const auto in = analysis::programme (fs, 3.0 * (double) loopLength / fs, 6);

    GlitchModule module;
    module.prepare ({ fs, 256, 2 });
    Log log;
    module.setEventLog (&log);

    GridClock clock;
    clock.prepare (fs);
    auto out = in;
    const auto s = settings (GlitchType::any, 1.0f);
    for (int64_t start = 0; start < (int64_t) in.size(); start += 256)
    {
        const auto inLoop = start % loopLength;
        HostPosition h;
        h.playing = h.hasPpq = true;
        h.bpm = 120.0;
        h.ppq = 8.0 + (double) inLoop * 2.0 / fs;
        const auto len = (int) std::min<int64_t> (256, (int64_t) in.size() - start);
        clock.advance (h, 120.0, len);
        float* ch[] = { out.left.data() + start, out.right.data() + start };
        module.process ({ ch, 2, len, clock.blockStart() }, s, { clock, 0.9f, 0.4f, 7u });
    }

    std::vector<std::vector<int64_t>> slotsPerPass (3);
    for (const auto& f : log.events)
        slotsPerPass[(size_t) (f.sample / loopLength)].push_back (f.event.slot);
    CHECK (slotsPerPass[0] == slotsPerPass[1]);
    CHECK (slotsPerPass[1] == slotsPerPass[2]);
    CHECK_FALSE (slotsPerPass[0].empty());
}

TEST_CASE ("Glitch: a new seed takes effect at the next slot", "[glitch][determinism]")
{
    const auto in = analysis::programme (fs, 4.0, 8);
    const auto s = settings (GlitchType::any);
    const auto switchAt = (int64_t) (1.9 * fs);
    const auto slotSamples = (int64_t) (0.25 * 60.0 / 120.0 * fs);    // 1/16 at 120 BPM: 6000

    GlitchModule module;
    module.prepare ({ fs, 100, 2 });
    Log log;
    module.setEventLog (&log);
    renderModule<GlitchModule, GlitchSettings> (module, s, { 1.0f, 1.0f, 1u }, in, 100, fs, {},
                                                [&] (int64_t start, GlitchSettings&, Macros& m) { m.seed = start >= switchAt ? 2u : 1u; });

    const auto firstNewSlot = (switchAt + slotSamples - 1) / slotSamples;
    const auto p = glitch::patternFor (s, 1.0f, 1.0f);
    for (const auto& f : log.events)
    {
        const auto expected = glitch::eventAt (f.event.slot >= firstNewSlot ? 2u : 1u, p, f.event.slot);
        REQUIRE (expected.has_value());
        CHECK (expected->type == f.event.type);
        CHECK (std::abs (expected->lengthSeconds - f.event.lengthSeconds) < 1.0e-12);
    }
    CHECK (module.seedInUse() == 2u);
}

TEST_CASE ("Glitch: a stopped transport runs the grid free at the fallback tempo", "[glitch]")
{
    const auto in = analysis::programme (fs, 3.0, 10);
    const Transport stopped { false, 0.0, 0.0 };
    const auto r = render (settings (GlitchType::skip), { 1.0f, 0.0f }, in, 512, stopped);
    REQUIRE (r.events.size() > 2);
    // ModuleHarness feeds 120 BPM as the fallback: slots of 1/16 every 6000 samples from the first block.
    for (const auto& f : r.events)
        CHECK (f.sample == f.event.slot * 6000);
}
