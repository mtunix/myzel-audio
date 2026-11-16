// The detector when its input or its settings change while it runs: bad key samples, a release
// changed mid-release, the stereo routing or key switched, Harsh switched on. These drive the
// Detector directly (the probe holds one setting for a whole run).

#include "Signals.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstring>
#include <limits>

using namespace halite;
using namespace halite::testing;
using Catch::Matchers::WithinAbs;
using dsp::Detector;
using dsp::DetectorFrame;

namespace
{
    constexpr double rate = 48000.0;

    struct Recorder final : dsp::DetectorObserver
    {
        std::vector<DetectorFrame> hops;
        std::vector<dsp::EssEvent> events;
        void hop (const DetectorFrame& f) noexcept override { hops.push_back (f); }
        void event (const dsp::EssEvent& e) noexcept override { events.push_back (e); }
    };

    /**
        Runs a fresh detector over the channels from `from` on, in 512-sample blocks, with `before`
        until `change` and `after` from there (a block boundary). Frames in detector time.
    */
    Recorder drive (const std::vector<const Samples*>& channels, const dsp::DetectorSettings& before, const dsp::DetectorSettings& after,
                    int change, int from = 0, const std::vector<const Samples*>& key = {})
    {
        const juce::ScopedNoDenormals noDenormals;
        Detector detector;
        detector.prepare (rate);
        Recorder rec;

        const auto n = (int) channels[0]->size();
        std::array<const float*, 2> in {}, side {};
        for (auto pos = from; pos < n;)
        {
            const auto limit = pos < change ? change : n;
            const auto block = std::min (512, limit - pos);
            for (size_t c = 0; c < channels.size(); ++c)
                in[c] = channels[c]->data() + pos;
            for (size_t c = 0; c < key.size(); ++c)
                side[c] = key[c]->data() + pos;
            detector.process (in.data(), (int) channels.size(), key.empty() ? nullptr : side.data(), (int) key.size(), block,
                              pos < change ? before : after, &rec);
            pos += block;
        }
        return rec;
    }

    bool finite (const DetectorFrame& f)
    {
        for (int p = 0; p < f.numPaths; ++p)
        {
            const auto& x = f.paths[(size_t) p];
            for (const auto v : { x.bandDb, x.bodyDb, x.relativeDb, x.noise, x.periodicity, x.sibilanceDb, x.harshDb, x.harshBodyDb,
                                  x.harshRelativeDb, x.harshNoise, x.harshPeriodicity })
                if (! std::isfinite (v))
                    return false;
        }
        for (int r = 0; r < f.numReductions; ++r)
            for (const auto v : { f.decisions[(size_t) r].sibilanceDb, f.decisions[(size_t) r].essTargetDb, f.decisions[(size_t) r].harshTargetDb,
                                  f.reductions[(size_t) r].essDb, f.reductions[(size_t) r].harshDb, f.reductions[(size_t) r].sibilanceDb })
                if (! std::isfinite (v))
                    return false;
        return true;
    }

    bool samePath (const DetectorFrame::Path& a, const DetectorFrame::Path& b)
    {
        const auto same = [] (float x, float y) { return std::memcmp (&x, &y, sizeof x) == 0 || (std::isnan (x) && std::isnan (y)); };
        return same (a.bandDb, b.bandDb) && same (a.bodyDb, b.bodyDb) && same (a.relativeDb, b.relativeDb) && same (a.noise, b.noise)
            && same (a.periodicity, b.periodicity) && same (a.sibilanceDb, b.sibilanceDb) && same (a.harshDb, b.harshDb)
            && same (a.harshBodyDb, b.harshBodyDb) && same (a.harshRelativeDb, b.harshRelativeDb) && a.candidate == b.candidate
            && a.bodyHeld == b.bodyHeld && a.voiceOnset == b.voiceOnset && a.jumpHeld == b.jumpHeld;
    }

    /** A vowel with an ess every 0.6 s from 0.4 s. */
    Samples voiceWithRegularEsses (double seconds)
    {
        const auto n = samplesFor (seconds, rate);
        auto x = vowel (180.0, n, rate, 0.1);
        for (auto t = 0.4; t + 0.12 < seconds; t += 0.6)
            x = sum (x, gated (bandNoise (4500.0, 12000.0, n, rate, (unsigned) (t * 1000.0), 0.03), samplesFor (t, rate),
                               samplesFor (t + 0.12, rate), rate));
        return x;
    }
}

//==============================================================================
TEST_CASE ("Detector: a NaN, Inf or huge key sample never stops detection", "[detector][robust]")
{
    const auto clean = voiceWithRegularEsses (6.0);
    auto s = voiceSettings();
    s.thresholdDb = -14.0f;
    s.harshOn = true;
    const auto reference = drive ({ &clean }, s, s, 0);
    REQUIRE (reference.events.size() >= 9);

    // In the vowel between two esses, 0.8 s in.
    const auto at = samplesFor (0.8, rate);
    constexpr auto huge = std::numeric_limits<float>::max();
    struct Damage
    {
        const char* name;
        float value;
        int length;
        double recoverSeconds;     // events after this are the clean run's
    };

    // NaN and Inf read as 0, so the run is the clean one but for a sample. A single sample at float
    // max is clamped to +24 dBFS and lifts V less than the gap hold's 20 dB over the voice, so V
    // lets go at its release (measured: 0.40 s). A 64-sample burst there lifts V further: the gap
    // hold keeps it up for its 1 s, then it falls to the voice (1.76 s; NOTES.md, Open problems).
    const Damage damages[] { { "NaN", std::numeric_limits<float>::quiet_NaN(), 1, 0.0 },
                             { "+Inf", std::numeric_limits<float>::infinity(), 1, 0.0 },
                             { "-Inf", -std::numeric_limits<float>::infinity(), 1, 0.0 },
                             { "Inf for 64 samples", std::numeric_limits<float>::infinity(), 64, 0.0 },
                             { "float max", huge, 1, 0.6 },
                             { "-float max", -huge, 1, 0.6 },
                             { "float max for 64 samples", huge, 64, 2.0 } };

    for (const auto& d : damages)
    {
        auto x = clean;
        for (int i = 0; i < d.length; ++i)
            x[(size_t) (at + i)] = d.value;
        const auto r = drive ({ &x }, s, s, 0);
        INFO (d.name);

        auto nonFinite = 0;
        for (const auto& h : r.hops)
            nonFinite += finite (h) ? 0 : 1;
        CHECK (nonFinite == 0);

        // How long V stays more than 1 dB from the clean run's.
        auto lastApart = -1;
        for (size_t i = 0; i < r.hops.size(); ++i)
            if (std::abs (r.hops[i].paths[0].bodyDb - reference.hops[i].paths[0].bodyDb) > 1.0f)
                lastApart = (int) i;
        const auto apartSeconds = lastApart < 0 ? 0.0 : ((lastApart + 1) * Detector::hopSize - at) / rate;
        INFO ("V more than 1 dB from the clean run until " << apartSeconds << " s after the damage");
        CHECK (apartSeconds <= d.recoverSeconds);

        // Every event that starts after the recovery is the clean run's, within a hop and 0.1 dB.
        const auto after = (std::int64_t) (at + samplesFor (d.recoverSeconds, rate));
        std::vector<dsp::EssEvent> expected, got;
        for (const auto& e : reference.events)
            if (e.start >= after)
                expected.push_back (e);
        for (const auto& e : r.events)
            if (e.start >= after)
                got.push_back (e);
        REQUIRE (got.size() == expected.size());
        for (size_t e = 0; e < got.size(); ++e)
        {
            CHECK (std::abs (got[e].start - expected[e].start) <= Detector::hopSize);
            CHECK (std::abs (got[e].end - expected[e].end) <= Detector::hopSize);
            CHECK_THAT (got[e].peakReductionDb, WithinAbs (expected[e].peakReductionDb, 0.1));
        }
    }
}

TEST_CASE ("Detector: Mid and Side clamp each channel before they are formed", "[detector][robust]")
{
    // L at float max and R at -float max would form Inf - Inf = NaN, or overflow, unclamped.
    const auto n = samplesFor (0.5, rate);
    Samples left ((size_t) n, std::numeric_limits<float>::max()), right ((size_t) n, -std::numeric_limits<float>::max());
    for (const auto mode : { StereoMode::mid, StereoMode::side, StereoMode::linked })
    {
        auto s = voiceSettings();
        s.stereo = mode;
        s.harshOn = true;
        const auto r = drive ({ &left, &right }, s, s, 0);
        auto nonFinite = 0;
        for (const auto& h : r.hops)
            nonFinite += finite (h) ? 0 : 1;
        INFO ("stereo mode " << (int) mode);
        CHECK (nonFinite == 0);
    }
}

//==============================================================================
TEST_CASE ("Detector: a fixed release changed while the reduction lets go applies at once", "[detector][release]")
{
    constexpr auto hopSeconds = Detector::hopSize / rate;
    const auto ratio = [] (double ms) { return std::exp (-hopSeconds * 1000.0 / ms); };

    dsp::ReductionShaper shaper;
    shaper.prepare (16);
    dsp::ReleaseRule slow { .automatic = false, .fixedMs = 200.0f }, fast { .automatic = false, .fixedMs = 5.0f };
    for (int i = 0; i < 20; ++i)
        shaper.push (8.0f, 0, slow, hopSeconds);

    auto previous = shaper.get();
    for (int i = 0; i < 5; ++i)
    {
        const auto now = shaper.push (0.0f, 0, slow, hopSeconds);
        CHECK_THAT (now / previous, WithinAbs (ratio (200.0), 1.0e-5));
        previous = now;
    }
    for (int i = 0; i < 5; ++i)
    {
        const auto now = shaper.push (0.0f, 0, fast, hopSeconds);
        CHECK_THAT (now / previous, WithinAbs (ratio (5.0), 1.0e-5));
        previous = now;
    }

    // Auto keeps the release it took from the last time over (0.4 s: its 80 ms limit), whatever
    // the fixed value says.
    dsp::ReleaseRule automatic { .automatic = true, .fixedMs = 5.0f, .autoMinMs = 15.0f, .autoMaxMs = 80.0f };
    for (int i = 0; i < 600; ++i)
        shaper.push (8.0f, 0, automatic, hopSeconds);
    previous = shaper.get();
    automatic.fixedMs = 200.0f;
    const auto now = shaper.push (0.0f, 0, automatic, hopSeconds);
    CHECK_THAT (now / previous, WithinAbs (ratio (80.0), 1.0e-5));
}

//==============================================================================
TEST_CASE ("Detector: a path fed another signal starts clean", "[detector][stereo]")
{
    // A stereo voice, mostly in the mid with esses on a quieter side. After a switch at a hop
    // boundary every path reads exactly what a detector started there in the new mode reads: no
    // V or hold of the old signal carries over.
    const auto n = samplesFor (3.0, rate);
    const auto mid = voiceWithRegularEsses (3.0);
    const auto side = sum (vowel (150.0, n, rate, 0.01, 3), gated (bandNoise (4500.0, 12000.0, n, rate, 9, 0.01), samplesFor (2.1, rate),
                                                                     samplesFor (2.25, rate), rate));
    Samples left ((size_t) n), right ((size_t) n);
    for (size_t i = 0; i < left.size(); ++i)
    {
        left[i] = mid[i] + side[i];
        right[i] = mid[i] - side[i];
    }

    const auto change = 512 * 188;     // 2.005 s, a block and hop boundary
    REQUIRE (change % Detector::hopSize == 0);

    struct Switch
    {
        StereoMode from, to;
        KeySource keyFrom = KeySource::internal, keyTo = KeySource::internal;
    };
    const Switch switches[] { { StereoMode::mid, StereoMode::side },       { StereoMode::side, StereoMode::mid },
                              { StereoMode::linked, StereoMode::mid },     { StereoMode::mid, StereoMode::linked },
                              { StereoMode::unlinked, StereoMode::side },  { StereoMode::linked, StereoMode::linked, KeySource::internal, KeySource::external },
                              { StereoMode::mid, StereoMode::mid, KeySource::external, KeySource::internal } };

    // The side chain carries the channels swapped and quieter: another signal on both paths.
    const auto keyLeft = scaled (right, 0.5), keyRight = scaled (left, 0.5);

    for (const auto& sw : switches)
    {
        auto before = voiceSettings(), after = voiceSettings();
        before.stereo = sw.from;
        after.stereo = sw.to;
        before.key = sw.keyFrom;
        after.key = sw.keyTo;
        before.harshOn = after.harshOn = true;

        const auto switched = drive ({ &left, &right }, before, after, change, 0, { &keyLeft, &keyRight });
        const auto fresh = drive ({ &left, &right }, after, after, 0, change, { &keyLeft, &keyRight });
        const auto first = (size_t) (change / Detector::hopSize);
        INFO ("stereo " << (int) sw.from << " -> " << (int) sw.to << ", key " << (int) sw.keyFrom << " -> " << (int) sw.keyTo);
        REQUIRE (switched.hops.size() == first + fresh.hops.size());

        auto differing = 0;
        for (size_t i = 0; i < fresh.hops.size(); ++i)
        {
            const auto& a = switched.hops[first + i];
            const auto& b = fresh.hops[i];
            REQUIRE (a.numPaths == b.numPaths);
            for (int p = 0; p < a.numPaths; ++p)
                differing += samePath (a.paths[(size_t) p], b.paths[(size_t) p]) ? 0 : 1;
        }
        CHECK (differing == 0);
    }
}

TEST_CASE ("Detector: Harsh holds for 50 ms after it is switched on", "[detector][harsh]")
{
    // Allround, so neither an ess nor noise in its band holds Harsh: only the warm-up does. The
    // band reads over its threshold from the first hop.
    const auto n = samplesFor (2.0, rate);
    const auto x = harmonic ({ .f0 = 180.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -12.0, .peakHz = 3200.0, .peakDb = 30.0,
                               .peakOctaves = 0.7, .rms = 0.1 },
                             n, rate);
    auto off = voiceSettings();
    off.detection = Detection::allround;
    off.thresholdAbsDb = 0.0f;
    off.harshThresholdAbsDb = -60.0f;
    auto on = off;
    on.harshOn = true;

    const auto warmUpHops = (int) std::ceil (0.050 * rate / Detector::hopSize);
    for (const auto change : { 0, 512 * 94 })
    {
        const auto r = drive ({ &x }, change == 0 ? on : off, on, change);
        const auto first = change / Detector::hopSize;
        INFO ("switched on at sample " << change);
        for (int i = first; i < first + warmUpHops + 20; ++i)
        {
            const auto& d = r.hops[(size_t) i].decisions[0];
            INFO ("hop " << i - first << " after");
            CHECK (d.harshHeld == (i < first + warmUpHops));
            if (i < first + warmUpHops)
                CHECK (myzel::dsp::isExactly (d.harshTargetDb, 0.0));
            else
                CHECK (d.harshTargetDb > 0.0f);
        }
    }
}

TEST_CASE ("Detector: a reset is safe before prepare() and leaves nothing of the signal before it", "[detector][robust]")
{
    // JUCE's VST3 wrapper can call AudioProcessor::reset() before prepareToPlay().
    dsp::Detector detector;
    detector.reset();
    detector.prepare (rate);

    // A vowel back after a closure, cut 10 ms into the jump's hold, inside the persistence window
    // and inside a hop; after a reset an ess over a vowel reads as on a fresh detector.
    const auto n = samplesFor (0.57, rate) + 7;
    const auto voice = vowel (180.0, n, rate, 0.1);
    const auto before = sum (gated (voice, 0, samplesFor (0.5, rate), rate), gated (voice, samplesFor (0.56, rate), n, rate));
    const auto after = sum (vowel (220.0, samplesFor (0.8, rate), rate, 0.1),
                            gated (bandNoise (4500.0, 12000.0, samplesFor (0.8, rate), rate, 41, 0.03), samplesFor (0.4, rate), samplesFor (0.55, rate), rate));
    const auto s = voiceSettings();
    const float* first[] { before.data() };
    detector.process (first, 1, nullptr, 0, (int) before.size(), s, nullptr);
    detector.reset();

    Recorder reset;
    const float* second[] { after.data() };
    for (int pos = 0; pos < (int) after.size(); pos += 512)
    {
        const float* block[] { second[0] + pos };
        detector.process (block, 1, nullptr, 0, std::min (512, (int) after.size() - pos), s, &reset);
    }
    const auto fresh = drive ({ &after }, s, s, 0);

    REQUIRE (reset.hops.size() == fresh.hops.size());
    for (size_t i = 0; i < fresh.hops.size(); ++i)
    {
        INFO ("hop " << i);
        REQUIRE (samePath (reset.hops[i].paths[0], fresh.hops[i].paths[0]));
        REQUIRE (juce::exactlyEqual (reset.hops[i].reductions[0].essDb, fresh.hops[i].reductions[0].essDb));
    }
    CHECK (reset.events.size() == fresh.events.size());
    CHECK_FALSE (fresh.events.empty());
}
