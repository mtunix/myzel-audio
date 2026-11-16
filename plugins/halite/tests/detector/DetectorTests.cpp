// The Ess and Harsh detectors on synthetic signals (PLAN.md, Detection, The Harsh band), through
// the offline probe, which runs the plugin's own Detector.

#include "Signals.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstring>
#include <functional>
#include <random>

using namespace halite;
using namespace halite::testing;
using Catch::Matchers::WithinAbs;
using myzel::dsp::isExactly;
using dsp::Detector;
using dsp::DetectorFrame;

namespace
{
    constexpr double rate = 48000.0;

    using Field = std::function<double (const DetectorFrame&)>;

    std::vector<double> series (const dsp::probe::Result& r, const Field& f, int fromHop, int toHop = -1)
    {
        toHop = toHop < 0 ? (int) r.hops.size() : std::min (toHop, (int) r.hops.size());
        std::vector<double> v;
        for (int i = fromHop; i < toHop; ++i)
            v.push_back (f (r.hops[(size_t) i]));
        return v;
    }

    double mean (const std::vector<double>& v)
    {
        auto s = 0.0;
        for (auto x : v)
            s += x;
        return s / (double) std::max<size_t> (v.size(), 1);
    }

    double stddev (const std::vector<double>& v)
    {
        const auto m = mean (v);
        auto s = 0.0;
        for (auto x : v)
            s += (x - m) * (x - m);
        return std::sqrt (s / (double) std::max<size_t> (v.size(), 1));
    }

    double median (std::vector<double> v)
    {
        std::sort (v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    }

    double largestDifference (const std::vector<double>& a, const std::vector<double>& b)
    {
        auto d = 0.0;
        for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            d = std::max (d, std::abs (a[i] - b[i]));
        return d;
    }

    Field band (int p = 0) { return [p] (const DetectorFrame& f) { return f.paths[(size_t) p].bandDb; }; }
    Field body (int p = 0) { return [p] (const DetectorFrame& f) { return f.paths[(size_t) p].bodyDb; }; }
    Field relative (int p = 0) { return [p] (const DetectorFrame& f) { return f.paths[(size_t) p].relativeDb; }; }
    Field sibilance (int p = 0) { return [p] (const DetectorFrame& f) { return f.paths[(size_t) p].sibilanceDb; }; }
    Field essTarget (int r = 0) { return [r] (const DetectorFrame& f) { return f.decisions[(size_t) r].essTargetDb; }; }
    Field essReduction (int r = 0) { return [r] (const DetectorFrame& f) { return f.reductions[(size_t) r].essDb; }; }
    Field harshTarget (int r = 0) { return [r] (const DetectorFrame& f) { return f.decisions[(size_t) r].harshTargetDb; }; }
    Field presence (int p = 0) { return [p] (const DetectorFrame& f) { return f.paths[(size_t) p].presenceDb; }; }

    /** What the shape cue takes off S on a hop (NOTES.md, M1b). */
    double shapeTermDb (const DetectorFrame::Path& p, float focus, const dsp::DetectorTuning& t = {})
    {
        return focus * std::clamp (t.presenceSlope * (t.presenceKneeDb - p.presenceDb), 0.0f, t.presenceRangeDb);
    }

    /** What the noise cue (and a jump's hold) takes off S on a hop. */
    double harmonicTermDb (const DetectorFrame::Path& p, float focus, const dsp::DetectorTuning& t = {})
    {
        const auto noise = p.voiceOnset ? 0.0f : p.noise;
        return focus * std::max (t.noiseRangeDb * (1.0f - noise), p.jumpHeld ? t.jumpHoldDb : 0.0f);
    }

    /** S as Voice composes it: B − V, less the noise cue's and the shape cue's terms. */
    double voiceSibilance (const DetectorFrame::Path& p, float focus, const dsp::DetectorTuning& t = {})
    {
        return p.relativeDb - harmonicTermDb (p, focus, t) - shapeTermDb (p, focus, t);
    }

    dsp::probe::Result probeTuned (const Samples& x, const dsp::DetectorSettings& s, const dsp::DetectorTuning& t)
    {
        const auto* in = x.data();
        return dsp::probe::run (&in, 1, (int) x.size(), rate, s, nullptr, 0, { 512 }, t);
    }

    /** A vowel throughout with esses on top: 4.5–12 kHz noise bursts at the given times (s). */
    Samples voiceWithEsses (double seconds, const std::vector<std::pair<double, double>>& esses,
                            double vowelRms = 0.1, double essRms = 0.03, double f0 = 180.0)
    {
        const auto n = samplesFor (seconds, rate);
        auto x = vowel (f0, n, rate, vowelRms);
        for (const auto& [from, to] : esses)
            x = sum (x, gated (bandNoise (4500.0, 12000.0, n, rate, (unsigned) (from * 1000.0), essRms),
                               samplesFor (from, rate), samplesFor (to, rate), rate));
        return x;
    }

    Samples sine (double hz, double amplitude, int n)
    {
        Samples x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = (float) (amplitude * std::sin (2.0 * std::numbers::pi * hz * i / rate));
        return x;
    }

    bool same (float a, float b) { return std::memcmp (&a, &b, sizeof a) == 0 || (std::isnan (a) && std::isnan (b)); }

    /** Every field of two hops, bit for bit (NaN equals NaN). */
    bool same (const DetectorFrame& a, const DetectorFrame& b)
    {
        if (a.position != b.position || a.reductionPosition != b.reductionPosition || a.numPaths != b.numPaths
            || a.numReductions != b.numReductions || a.eventActive != b.eventActive)
            return false;

        for (size_t p = 0; p < 2; ++p)
        {
            const auto &x = a.paths[p], &y = b.paths[p];
            if (! (same (x.bandDb, y.bandDb) && same (x.bodyDb, y.bodyDb) && same (x.relativeDb, y.relativeDb)
                   && same (x.noise, y.noise) && same (x.periodicity, y.periodicity) && same (x.flatnessDb, y.flatnessDb)
                   && same (x.sibilanceDb, y.sibilanceDb) && same (x.harshDb, y.harshDb) && same (x.harshBodyDb, y.harshBodyDb)
                   && same (x.harshRelativeDb, y.harshRelativeDb) && same (x.harshNoise, y.harshNoise)
                   && same (x.harshPeriodicity, y.harshPeriodicity) && same (x.harshFlatnessDb, y.harshFlatnessDb)
                   && same (x.presenceDb, y.presenceDb) && x.candidate == y.candidate && x.bodyHeld == y.bodyHeld
                   && x.bodyRising == y.bodyRising && x.bodyJumped == y.bodyJumped && x.voiceOnset == y.voiceOnset
                   && x.jumpHeld == y.jumpHeld))
                return false;

            const auto &d = a.decisions[p], &e = b.decisions[p];
            if (! (same (d.sibilanceDb, e.sibilanceDb) && same (d.essTargetDb, e.essTargetDb)
                   && same (d.harshTargetDb, e.harshTargetDb) && d.harshHeld == e.harshHeld))
                return false;

            const auto &r = a.reductions[p], &s = b.reductions[p];
            if (! (same (r.essDb, s.essDb) && same (r.harshDb, s.harshDb) && same (r.sibilanceDb, s.sibilanceDb)))
                return false;
        }
        return true;
    }

    /** The first hop where the two results differ, or -1. */
    int firstDifference (const dsp::probe::Result& a, const dsp::probe::Result& b)
    {
        if (a.hops.size() != b.hops.size())
            return 0;
        for (size_t i = 0; i < a.hops.size(); ++i)
            if (! same (a.hops[i], b.hops[i]))
                return (int) i;
        return -1;
    }

    bool sameEvents (const dsp::probe::Result& a, const dsp::probe::Result& b)
    {
        if (a.events.size() != b.events.size())
            return false;
        for (size_t i = 0; i < a.events.size(); ++i)
            if (a.events[i].start != b.events[i].start || a.events[i].end != b.events[i].end
                || ! same (a.events[i].peakReductionDb, b.events[i].peakReductionDb)
                || ! same (a.events[i].peakSibilanceDb, b.events[i].peakSibilanceDb))
                return false;
        return true;
    }
}

//==============================================================================
TEST_CASE ("Detector: B ripple on band noise is at most 0.5 dB, and B reads the RMS", "[detector][levels]")
{
    for (const auto r : { 44100.0, 48000.0, 96000.0 })
    {
        const auto noise = bandNoise (4500.0, 12000.0, samplesFor (4.0, r), r, 11, 0.1);
        const auto b = series (probeOf (noise, r, voiceSettings()), band(), hopAt (0.1, r));

        // The spread of the per-hop dB, as NOTES.md measured the M0 candidates.
        INFO ("rate " << r << ": B std " << stddev (b) << " dB, mean " << mean (b) << " dBFS");
        CHECK (stddev (b) <= 0.5);
        CHECK_THAT (mean (b), WithinAbs (-20.0, 0.5));

        // At the band's centre a sine reads its RMS: B is close to an RMS detector.
        Samples tone ((size_t) samplesFor (1.0, r));
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = (float) (0.1 * std::sin (2.0 * std::numbers::pi * std::sqrt (4500.0 * 12000.0) * (double) i / r));
        const auto t = series (probeOf (tone, r, voiceSettings()), band(), hopAt (0.1, r));
        CHECK_THAT (mean (t), WithinAbs (20.0 * std::log10 (0.1 / std::sqrt (2.0)), 0.2));
    }
}

TEST_CASE ("Detector: B reaches an ess within the default look-ahead", "[detector][levels]")
{
    // A tone at the band's centre switched on hard at 0.5 s: B's rise is its averaging and attack.
    const auto n = samplesFor (1.0, rate);
    const auto onset = samplesFor (0.5, rate);
    auto x = sine (std::sqrt (4500.0 * 12000.0), 0.1, n);
    std::fill (x.begin(), x.begin() + onset, 0.0f);
    const auto r = probeOf (x, rate, voiceSettings());
    const auto settled = median (series (r, band(), hopAt (onset + samplesFor (0.05, rate))));

    // A hop ends at (hop + 1) · hopSize: that is when the detector knows.
    const auto msUntil = [&] (double belowDb)
    {
        auto hop = hopAt (onset);
        while (r.hops[(size_t) hop].paths[0].bandDb < settled - belowDb)
            ++hop;
        return 1000.0 * ((hop + 1) * Detector::hopSize - onset) / rate;
    };

    // B's 4 ms averaging (its ripple) costs rise time: within 3 dB inside the 5 ms look-ahead.
    INFO ("B within 3 dB after " << msUntil (3.0) << " ms, within 1 dB after " << msUntil (1.0) << " ms");
    CHECK (msUntil (3.0) <= 4.5);
    CHECK (msUntil (1.0) <= 8.0);
}

TEST_CASE ("Detector: a voice starting does not read as an ess", "[detector][body]")
{
    // A vowel fading in over 5 ms, after silence and after a vowel 30 dB quieter (V low but above
    // the floor). While the body rises B is compared with the body's averaging, so B − V stays
    // within a few dB of its steady value; without that rule it overshoots by 9.5 dB here.
    const auto n = samplesFor (1.2, rate);
    const auto at = samplesFor (0.3, rate);
    auto loud = vowel (180.0, n, rate, 0.1);
    const auto edge = samplesFor (0.005, rate);
    for (int i = 0; i < n; ++i)
        loud[(size_t) i] *= (float) (i < at ? 0.0 : (i < at + edge ? 0.5 - 0.5 * std::cos (std::numbers::pi * (i - at) / edge) : 1.0));
    const auto quiet = gated (vowel (180.0, n, rate, 0.1 * std::pow (10.0, -30.0 / 20.0), 2), 0, at, rate);
    const auto afterSilence = loud;
    const auto afterQuiet = sum (loud, quiet);

    auto s = voiceSettings();
    s.focus = 0.0f;     // the noise cue needs ≈ 20 ms of a new vowel to call it harmonic: not tested here
    s.harshOn = true;

    for (const auto* x : { &afterSilence, &afterQuiet })
    {
        const auto r = probeOf (*x, rate, s);
        const auto steady = median (series (r, relative(), hopAt (0.6, rate), hopAt (0.9, rate)));
        const Field harshRelative = [] (const DetectorFrame& f) { return f.paths[0].harshRelativeDb; };
        const auto harshSteady = median (series (r, harshRelative, hopAt (0.6, rate), hopAt (0.9, rate)));
        auto peak = -300.0, harshPeak = -300.0;
        for (int i = hopAt (at); i < hopAt (at + samplesFor (0.1, rate)); ++i)
        {
            peak = std::max (peak, (double) r.hops[(size_t) i].paths[0].relativeDb);
            harshPeak = std::max (harshPeak, (double) r.hops[(size_t) i].paths[0].harshRelativeDb);
        }

        INFO ((x == &afterSilence ? "after silence" : "after a quieter vowel") << ": B − V steady " << steady << " dB, at most " << peak << " dB in the first 100 ms");
        CHECK (peak - steady < 7.0);

        // The Harsh band compares H with its body by the same rule.
        INFO ("H − body steady " << harshSteady << " dB, at most " << harshPeak << " dB in the first 100 ms");
        CHECK (harshPeak - harshSteady < 7.0);

        s.thresholdDb = (float) steady + 7.0f + 0.5f * dsp::kneeDb;
        CHECK (probeOf (*x, rate, s).events.empty());
    }
}

//==============================================================================
TEST_CASE ("Detector: S is level-independent above the body floor", "[detector][relative]")
{
    const auto x = voiceWithEsses (3.0, { { 1.0, 1.12 }, { 1.6, 1.75 }, { 2.2, 2.3 } });

    for (const auto cue : { dsp::NoiseCueKind::periodicity, dsp::NoiseCueKind::flatness })
    {
        auto s = voiceSettings (cue);
        s.thresholdDb = -14.0f;
        s.harshOn = true;
        s.harshThresholdDb = -30.0f;
        const auto reference = probeOf (x, rate, s);
        REQUIRE (reference.events.size() == 3);

        // The detector starts from silence, below the floor, so the first second is left out:
        // only then has every gain the same history relative to its level.
        const auto from = hopAt (0.9, rate);
        const Field fields[] = { relative(), sibilance(), essTarget(), essReduction(), harshTarget(), presence(),
                                 [] (const DetectorFrame& f) { return f.paths[0].noise; },
                                 [] (const DetectorFrame& f) { return f.paths[0].harshRelativeDb; },
                                 [] (const DetectorFrame& f) { return f.reductions[0].harshDb; } };

        for (const auto gainDb : { -7.3, -13.0, -21.7, -30.0 })
        {
            const auto quieter = probeOf (scaled (x, std::pow (10.0, gainDb / 20.0)), rate, s);
            INFO ("cue " << (int) cue << ", gain " << gainDb << " dB");

            for (const auto& f : fields)
                CHECK (largestDifference (series (reference, f, from), series (quieter, f, from)) < 1.0e-3);

            CHECK_THAT (mean (series (quieter, band(), from)) - mean (series (reference, band(), from)), WithinAbs (gainDb, 1.0e-3));

            REQUIRE (quieter.events.size() == reference.events.size());
            for (size_t e = 0; e < reference.events.size(); ++e)
            {
                CHECK (quieter.events[e].start == reference.events[e].start);
                CHECK (quieter.events[e].end == reference.events[e].end);
                CHECK_THAT (quieter.events[e].peakReductionDb, WithinAbs (reference.events[e].peakReductionDb, 1.0e-3));
                CHECK_THAT (quieter.events[e].peakSibilanceDb, WithinAbs (reference.events[e].peakSibilanceDb, 1.0e-3));
            }
        }
    }
}

TEST_CASE ("Detector: the body holds through an ess, and lets go of a long noise", "[detector][body]")
{
    // A vowel that drops by 15 dB at 0.5 s (less than the gap hold's 20 dB, so only the candidate
    // hold can keep V up) while a noise burst starts.
    const auto run = [] (double burstSeconds)
    {
        const auto n = samplesFor (1.6, rate);
        const auto at = samplesFor (0.5, rate);
        auto loud = gated (vowel (180.0, n, rate, 0.1), 0, at, rate);
        auto quiet = gated (vowel (180.0, n, rate, 0.1 * std::pow (10.0, -15.0 / 20.0)), at, n, rate);
        auto x = sum (sum (loud, quiet), gated (bandNoise (4500.0, 12000.0, n, rate, 7, 0.03), at, at + samplesFor (burstSeconds, rate), rate));
        auto s = voiceSettings();
        s.harshOn = true;
        return probeOf (x, rate, s);
    };

    const auto at = [] (const dsp::probe::Result& r, double seconds) { return r.hops[(size_t) hopAt (seconds, rate)]; };

    SECTION ("a 150 ms ess: V holds, so S does not rise through it")
    {
        const auto r = run (0.15);
        const auto before = at (r, 0.49).paths[0].bodyDb;
        const auto harshBefore = at (r, 0.49).paths[0].harshBodyDb;
        for (auto t = 0.505; t < 0.65; t += 0.005)
        {
            INFO ("t " << t << " s");
            CHECK (at (r, t).paths[0].bodyDb >= before - 0.5);
            CHECK (at (r, t).paths[0].bodyHeld);

            // The Harsh body holds with V, or the end of the ess would read as harshness.
            CHECK (at (r, t).paths[0].harshBodyDb >= harshBefore - 0.5);
        }

        const auto early = median (series (r, relative(), hopAt (0.51, rate), hopAt (0.53, rate)));
        const auto late = median (series (r, relative(), hopAt (0.62, rate), hopAt (0.64, rate)));
        INFO ("B − V early " << early << " dB, late " << late << " dB");
        CHECK_THAT (late - early, WithinAbs (0.0, 1.5));

        // After the ess V follows the quieter vowel.
        CHECK (at (r, 0.95).paths[0].bodyDb < before - 10.0);
    }

    SECTION ("a 600 ms noise: V holds for at most the hold time, then releases")
    {
        const auto r = run (0.6);
        const auto before = at (r, 0.49).paths[0].bodyDb;
        CHECK (at (r, 0.5 + 0.2).paths[0].bodyDb >= before - 0.5);
        CHECK (at (r, 0.5 + 0.45).paths[0].bodyDb < before - 6.0);
    }
}

TEST_CASE ("Detector: the body holds through a gap, at most the gap hold", "[detector][body]")
{
    // The voice stops for 0.6 s with room tone 40 dB down, then comes back 30 dB quieter. V keeps
    // the voice through the pause (room tone never becomes the reference), then follows the
    // quieter voice once the gap hold has run out.
    const auto tuning = dsp::DetectorTuning {};
    const auto n = samplesFor (3.2, rate);
    const auto stop = samplesFor (0.5, rate), back = samplesFor (1.1, rate);
    auto x = gated (vowel (180.0, n, rate, 0.1), 0, stop, rate);
    x = sum (x, gated (bandNoise (150.0, 8000.0, n, rate, 3, 0.001), stop, back, rate));
    x = sum (x, gated (vowel (180.0, n, rate, 0.1 * std::pow (10.0, -30.0 / 20.0)), back, n, rate));
    const auto r = probeOf (x, rate, voiceSettings());

    const auto at = [&r] (double seconds) { return r.hops[(size_t) hopAt (seconds, rate)].paths[0]; };
    // V releases until the body's 5 ms average is 20 dB under it, about 20 ms: about 1 dB.
    const auto before = at (0.49).bodyDb;
    for (auto t = 0.54; t < 1.1; t += 0.02)
    {
        INFO ("t " << t << " s");
        CHECK (at (t).bodyDb >= before - 1.5);
        CHECK (at (t).bodyHeld);
    }

    // The quiet voice is 30 dB under V, past the gap too: the hold counts from the pause, so V
    // follows the quieter voice once the gap hold has run out.
    const auto held = 0.5 + tuning.bodyGapHoldMs * 0.001;
    CHECK (at (held - 0.1).bodyDb >= before - 1.5);
    CHECK (at (held + 0.8).bodyDb < before - 25.0);
}

TEST_CASE ("Detector: the body reaches up to 2 kHz, or Detect Low / 2 when that is lower", "[detector][body]")
{
    // A sine at the body's upper edge reads 3 dB under one at its centre (Butterworth edges).
    const auto bodyOf = [] (double hz, const dsp::DetectorSettings& s)
    {
        return median (series (probeOf (sine (hz, 0.1, samplesFor (0.6, rate)), rate, s), body(), hopAt (0.3, rate)));
    };

    for (const auto detectLow : { 4500.0f, 3000.0f })
    {
        auto s = voiceSettings();
        s.detectLowHz = detectLow;
        const auto cap = std::min (2000.0, detectLow / 2.0);
        const auto edge = bodyOf (cap, s) - bodyOf (std::sqrt (120.0 * cap), s);
        INFO ("Detect Low " << detectLow << " Hz: V at " << cap << " Hz reads " << edge << " dB against the body's centre");
        CHECK_THAT (edge, WithinAbs (-3.0, 0.2));
    }
}

TEST_CASE ("Detector: the onset holds last 30 ms after the body rises or jumps", "[detector][body]")
{
    // A vowel fading in after silence: the body rises over a few hops.
    const auto n = samplesFor (0.8, rate);
    const auto at = samplesFor (0.3, rate);
    auto x = vowel (180.0, n, rate, 0.1);
    const auto edge = samplesFor (0.01, rate);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] *= (float) (i < at ? 0.0 : (i < at + edge ? 0.5 - 0.5 * std::cos (std::numbers::pi * (i - at) / edge) : 1.0));
    const auto r = probeOf (x, rate, voiceSettings());

    // voiceOnset on a hop exactly when the body rose on it or on one of the hops within 30 ms
    // before; jumpHeld the same for a jump.
    const auto holdHops = (int) std::ceil (0.030 * rate / Detector::hopSize);
    auto rising = 0, jumped = 0;
    for (int i = 0; i < (int) r.hops.size(); ++i)
    {
        auto rose = false, jump = false;
        for (int j = std::max (0, i - holdHops + 1); j <= i; ++j)
        {
            rose = rose || r.hops[(size_t) j].paths[0].bodyRising;
            jump = jump || r.hops[(size_t) j].paths[0].bodyJumped;
        }
        rising += r.hops[(size_t) i].paths[0].bodyRising ? 1 : 0;
        jumped += r.hops[(size_t) i].paths[0].bodyJumped ? 1 : 0;
        INFO ("hop " << i);
        REQUIRE (r.hops[(size_t) i].paths[0].voiceOnset == rose);
        REQUIRE (r.hops[(size_t) i].paths[0].jumpHeld == jump);
    }
    CHECK (rising > 0);
    CHECK (jumped > 0);
}

TEST_CASE ("Detector: a voice coming back after a closure is an onset, though V is held", "[detector][body]")
{
    // A vowel, a 60 ms closure 40 dB down, the vowel again. The gap hold keeps V through the
    // closure, so V does not rise when the vowel returns; the body's averaged level jumps.
    const auto n = samplesFor (1.0, rate);
    const auto stop = samplesFor (0.5, rate), back = samplesFor (0.56, rate);
    const auto voice = vowel (180.0, n, rate, 0.1);
    const auto x = sum (sum (gated (voice, 0, stop, rate), gated (scaled (voice, 0.01), stop, back, rate)), gated (voice, back, n, rate));

    const auto window = [&] (const dsp::probe::Result& r, const std::function<bool (const DetectorFrame::Path&)>& f)
    {
        auto count = 0;
        for (int i = hopAt (back); i < hopAt (back + samplesFor (0.025, rate)); ++i)
            count += f (r.hops[(size_t) i].paths[0]) ? 1 : 0;
        return count;
    };

    const auto r = probeOf (x, rate, voiceSettings());
    CHECK (r.hops[(size_t) hopAt (back) - 1].paths[0].bodyHeld);
    CHECK (window (r, [] (const auto& p) { return p.bodyRising; }) == 0);
    CHECK (window (r, [] (const auto& p) { return p.bodyJumped; }) > 0);
    CHECK (window (r, [] (const auto& p) { return p.jumpHeld; }) >= hopAt (samplesFor (0.020, rate)));

    // Without the jump rule nothing marks the onset.
    auto tuning = dsp::DetectorTuning {};
    tuning.jumpDb = 0.0f;
    const auto without = probeTuned (x, voiceSettings(), tuning);
    CHECK (window (without, [] (const auto& p) { return p.voiceOnset || p.jumpHeld || p.bodyJumped; }) == 0);
}

TEST_CASE ("Detector: the body jumps when its averaged level rises 10 dB within 5 ms, not more slowly", "[detector][body]")
{
    // After a closure (V held), the body comes back 30 dB up, linear in dB: over 4 ms its averaged
    // level rises more than 10 dB within 5 ms; over 30 ms it rises 5 dB per 5 ms.
    const auto n = samplesFor (1.0, rate);
    const auto stop = samplesFor (0.5, rate), back = samplesFor (0.56, rate);
    const auto voice = vowel (180.0, n, rate, 0.1);
    const auto jumpsWithin = [&] (double rampSeconds)
    {
        auto x = voice;
        const auto ramp = samplesFor (rampSeconds, rate);
        for (int i = stop; i < n; ++i)
            x[(size_t) i] *= (float) (i < back ? 0.0316 : std::pow (10.0, -1.5 * std::max (0.0, 1.0 - (double) (i - back) / ramp)));
        const auto r = probeOf (x, rate, voiceSettings());
        auto jumps = 0;
        for (int i = hopAt (back); i < hopAt (back + ramp) + hopAt (samplesFor (0.01, rate)); ++i)
            jumps += r.hops[(size_t) i].paths[0].bodyJumped ? 1 : 0;
        return jumps;
    };
    const auto fast = jumpsWithin (0.004), slow = jumpsWithin (0.030);
    INFO ("hops that jump: 30 dB over 4 ms " << fast << ", over 30 ms " << slow);
    CHECK (fast > 0);
    CHECK (slow == 0);
}

TEST_CASE ("Detector: an ess after a stop's burst is caught at once, a marginal one within the hold", "[detector][body]")
{
    // A vowel, a 60 ms closure, a 10 ms burst under 3 kHz, then 150 ms of frication in
    // 2.5 … 12 kHz (an affricate, or an /s/ after a stop), then the vowel again. The burst lifts
    // the body's averaged level by far more than 10 dB, so the jump's hold covers the frication's
    // first 20 ms; it asks the ess for jumpHoldDb × Focus more, not for the whole noise range. The
    // clear frication reads about 11 dB over the threshold, the marginal one about 5 dB.
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto n = samplesFor (1.0, sampleRate);
        const auto stop = samplesFor (0.5, sampleRate), burst = samplesFor (0.56, sampleRate);
        const auto frication = samplesFor (0.57, sampleRate), end = samplesFor (0.72, sampleRate);
        const auto voice = vowel (180.0, n, sampleRate, 0.1);
        const auto run = [&] (double fricationRms, float jumpHoldDb)
        {
            auto x = sum (gated (voice, 0, stop, sampleRate), gated (voice, end, n, sampleRate));
            x = sum (x, gated (bandNoise (100.0, 3000.0, n, sampleRate, 21, 0.1), burst, frication, sampleRate));
            x = sum (x, gated (bandNoise (2500.0, 12000.0, n, sampleRate, 22, fricationRms), frication, end, sampleRate));
            auto tuning = dsp::DetectorTuning {};
            tuning.jumpHoldDb = jumpHoldDb;
            const auto* in = x.data();
            const auto r = dsp::probe::run (&in, 1, n, sampleRate, voiceSettings(), nullptr, 0, { 512 }, tuning);

            // In ms after the frication starts: the end of the jump's hold and the first decision.
            const auto ms = [&] (int hop) { return 1000.0 * ((hop + 1) * Detector::hopSize - frication) / sampleRate; };
            auto holdEnd = -1000.0, first = -1.0;
            for (int i = hopAt (burst); i < hopAt (end); ++i)
            {
                if (r.hops[(size_t) i].paths[0].jumpHeld)
                    holdEnd = ms (i);
                if (i >= hopAt (frication) && first < 0.0 && r.hops[(size_t) i].decisions[0].essTargetDb > 0.5f)
                    first = ms (i);
            }
            return std::make_pair (holdEnd, first);
        };

        const auto t = dsp::DetectorTuning {};
        const auto [holdEnd, clear] = run (0.05, t.jumpHoldDb);
        const auto unheld = run (0.05, 0.0f).second;
        const auto marginal = run (0.025, t.jumpHoldDb).second;
        const auto fullHold = run (0.05, t.noiseRangeDb).second;
        INFO (sampleRate << " Hz, after the frication starts: the jump's hold ends at " << holdEnd << " ms; first decision: clear " << clear
                         << " ms (without the hold " << unheld << " ms), marginal " << marginal << " ms, clear with N = 0 through the hold " << fullHold << " ms");
        const auto hopMs = 1000.0 * Detector::hopSize / sampleRate;
        CHECK (holdEnd > 15.0);
        CHECK (unheld >= 0.0);
        CHECK (clear <= unheld + hopMs);
        CHECK (clear <= 8.0);
        CHECK (marginal > clear + 10.0);
        CHECK (marginal <= holdEnd + 2.0 * hopMs);
        CHECK (fullHold >= holdEnd - 2.0 * hopMs);
    }
}

//==============================================================================
TEST_CASE ("Detector: the shape cue lowers S where the spectrum falls, and never raises it", "[detector][shape]")
{
    const auto t = dsp::DetectorTuning {};
    const auto n = samplesFor (1.0, rate);
    const auto from = hopAt (0.2, rate);
    const auto s = voiceSettings();

    // Noise in the Detect band alone rises towards it, as an ess does; with 1–3 kHz noise 12 dB
    // stronger beside it the spectrum falls, as a vowel's top end or a stop's aspiration does.
    const auto ess = bandNoise (4500.0, 12000.0, n, rate, 3, 0.02);
    const auto falling = sum (ess, bandNoise (1000.0, 3000.0, n, rate, 4, 0.08));
    const auto rising = probeOf (ess, rate, s), fall = probeOf (falling, rate, s);
    INFO ("presence over the band: rising " << median (series (rising, presence(), from)) << " dB, falling " << median (series (fall, presence(), from)) << " dB");
    CHECK (median (series (rising, presence(), from)) > 6.0);
    CHECK (median (series (fall, presence(), from)) < t.presenceKneeDb);

    auto risingTaken = 0.0, fallingTaken = 0.0;
    for (int i = from; i < (int) rising.hops.size(); ++i)
    {
        risingTaken = std::max (risingTaken, shapeTermDb (rising.hops[(size_t) i].paths[0], s.focus));
        fallingTaken += shapeTermDb (fall.hops[(size_t) i].paths[0], s.focus) / (double) (rising.hops.size() - (size_t) from);
    }
    CHECK (isExactly (risingTaken, 0.0));
    CHECK (fallingTaken > 10.0);

    // Harmonic spectra: equal amplitudes rise in band power, -12 dB per octave falls far under the
    // knee; the term never passes its range.
    const auto flat = probeOf (harmonic ({ .f0 = 220.0, .topHz = 12000.0, .rms = 0.1 }, n, rate), rate, s);
    const auto dull = probeOf (vowel (220.0, n, rate, 0.1), rate, s);
    CHECK (median (series (flat, presence(), from)) > t.presenceKneeDb);
    CHECK (median (series (dull, presence(), from)) < t.presenceKneeDb - 15.0);

    for (const auto* r : { &rising, &fall, &flat, &dull })
        for (int i = from; i < (int) r->hops.size(); ++i)
        {
            const auto& p = r->hops[(size_t) i].paths[0];
            REQUIRE_THAT (p.sibilanceDb, WithinAbs (voiceSibilance (p, s.focus), 1.0e-4));
            REQUIRE (p.sibilanceDb <= p.relativeDb - harmonicTermDb (p, s.focus) + 1.0e-4);
            REQUIRE (shapeTermDb (p, s.focus) <= t.presenceRangeDb * s.focus + 1.0e-4);
        }

    // An ess over a vowel keeps its S: the shape term is zero through the burst.
    const auto voiced = probeOf (voiceWithEsses (2.0, { { 1.0, 1.15 } }), rate, s);
    for (int i = hopAt (1.03, rate); i < hopAt (1.13, rate); ++i)
        CHECK (isExactly (shapeTermDb (voiced.hops[(size_t) i].paths[0], s.focus), 0.0));
}

TEST_CASE ("Detector: the shape cue's two levels share B's averaging", "[detector][shape]")
{
    // Broadband noise switched 30 dB down and up every 30 ms: both bands carry the same envelope,
    // so their ratio stays put through the steps when both levels are averaged alike (B's 4 ms
    // power average, no ballistics), and moves by several dB when either follows faster or slower.
    // Measured: ripple 0.62 dB steady, 0.68 dB through the steps; with B's ballistic level in place
    // of its average 4.1 dB, with a 1 ms presence average 7.3 dB.
    const auto n = samplesFor (2.0, rate);
    auto x = bandNoise (300.0, 16000.0, n, rate, 31, 0.1);
    const auto step = samplesFor (0.03, rate);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] *= (i / step) % 2 == 0 ? 1.0f : 0.0316f;
    const auto steady = probeOf (bandNoise (300.0, 16000.0, n, rate, 31, 0.1), rate, voiceSettings());
    const auto stepped = probeOf (x, rate, voiceSettings());

    const auto from = hopAt (0.2, rate);
    const auto still = series (steady, presence(), from), moving = series (stepped, presence(), from);
    auto worst = 0.0;
    for (const auto v : moving)
        worst = std::max (worst, std::abs (v - median (still)));
    INFO ("presence on steady noise: median " << median (still) << " dB, ripple " << stddev (still) << " dB; through the steps: ripple "
                                              << stddev (moving) << " dB, largest distance from the steady median " << worst << " dB");
    CHECK (stddev (still) < 0.8);
    CHECK (stddev (moving) < 1.0);
    CHECK (worst < 8.0);
}

TEST_CASE ("Detector: a voiced /z/ over a vowel is caught", "[detector][shape]")
{
    // A /z/: frication in the Detect band, amplitude-modulated by the voicing at f0 with index 0.5
    // (the saturation value of Pincas and Jackson), while the vowel's body goes on.
    const auto n = samplesFor (2.0, rate);
    const auto from = samplesFor (1.0, rate), to = samplesFor (1.12, rate);
    auto frication = bandNoise (4500.0, 12000.0, n, rate, 9, 0.04);
    for (int i = 0; i < n; ++i)
        frication[(size_t) i] *= (float) (1.0 + 0.5 * std::sin (2.0 * std::numbers::pi * 180.0 * i / rate));
    const auto x = sum (vowel (180.0, n, rate, 0.1), gated (frication, from, to, rate));

    const auto r = probeOf (x, rate, voiceSettings());
    auto tuning = dsp::DetectorTuning {};
    tuning.persistMs = 0.0;
    const auto unpersisted = probeTuned (x, voiceSettings(), tuning);

    // The cue reads the modulated frication between noise and harmonic (ρ 0.3 … 0.75), under the
    // persistence level, so persistence leaves it alone; the shape cue sees it rise.
    std::vector<double> shape;
    auto largest = -300.0f;
    for (int i = hopAt (from) + 2; i < hopAt (to); ++i)
    {
        const auto& p = r.hops[(size_t) i].paths[0];
        shape.push_back (p.presenceDb);
        largest = std::max (largest, p.sibilanceDb);
        REQUIRE (isExactly (p.noise, unpersisted.hops[(size_t) i].paths[0].noise));
    }
    INFO ("presence " << median (shape) << " dB, largest S " << largest << " dB");
    CHECK (median (shape) > dsp::DetectorTuning{}.presenceKneeDb);
    CHECK (largest > dsp::DetectorSettings{}.thresholdDb + 3.0f);
    CHECK (std::any_of (r.events.begin(), r.events.end(), [&] (const dsp::EssEvent& e) { return e.start < to && e.end > from; }));
}

TEST_CASE ("Detector: a harmonic reading persists through a glide while B stays steady", "[detector][noise]")
{
    // Notes a fifth apart joined by 30 ms glides, harmonics falling 6 dB per octave: the
    // periodicity cue loses the pitch inside a glide; persistence keeps the reading from before it.
    constexpr double note = 0.3, glide = 0.03;
    const std::vector<double> pitches { 110.0, 165.0, 220.0, 165.0, 110.0, 165.0 };
    const auto n = samplesFor (note * (double) pitches.size(), rate);
    Samples x ((size_t) n);
    auto phase = std::vector<double> (200, 0.0);
    for (int i = 0; i < n; ++i)
    {
        const auto t = i / rate;
        const auto k = std::min ((size_t) (t / note), pitches.size() - 1);
        const auto into = t - (double) k * note;
        auto f0 = pitches[k];
        if (k > 0 && into < glide)
            f0 = pitches[k - 1] * std::pow (pitches[k] / pitches[k - 1], into / glide);
        auto v = 0.0;
        for (size_t h = 0; h < phase.size() && (double) (h + 1) * f0 < 12000.0; ++h)
        {
            v += std::pow (10.0, -6.0 * std::log2 ((double) h + 1.0) / 20.0) * std::sin (phase[h]);
            phase[h] = std::fmod (phase[h] + 2.0 * std::numbers::pi * f0 * (double) (h + 1) / rate, 2.0 * std::numbers::pi);
        }
        x[(size_t) i] = (float) (0.05 * v);
    }

    auto tuning = dsp::DetectorTuning {};
    const auto with = probeTuned (x, voiceSettings(), tuning);
    tuning.persistMs = 0.0;
    const auto without = probeTuned (x, voiceSettings(), tuning);

    // Persistence only ever calls the band more harmonic; here it does so inside the glides.
    auto helped = 0;
    auto worstWith = 0.0f, worstWithout = 0.0f;
    for (int i = hopAt (0.2, rate); i < (int) with.hops.size(); ++i)
    {
        const auto& a = with.hops[(size_t) i].paths[0];
        const auto& b = without.hops[(size_t) i].paths[0];
        REQUIRE (a.noise <= b.noise);
        helped += a.noise < b.noise ? 1 : 0;
        worstWith = std::max (worstWith, a.noise);
        worstWithout = std::max (worstWithout, b.noise);
    }
    INFO ("hops called more harmonic " << helped << "; largest N with persistence " << worstWith << ", without " << worstWithout);
    CHECK (helped > 0);
    CHECK (worstWith < worstWithout);
}

TEST_CASE ("Detector: a sliding extreme is the extreme of the last span values", "[detector][levels]")
{
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> value (-50.0f, 50.0f);
    for (const auto span : { 1, 2, 7, 60 })
    {
        dsp::SlidingExtreme<true> largest;
        dsp::SlidingExtreme<false> smallest;
        largest.prepare (span, -100.0f);
        smallest.prepare (span, 100.0f);
        std::vector<float> fed ((size_t) span, -100.0f), low ((size_t) span, 100.0f);
        for (int i = 0; i < 500; ++i)
        {
            const auto v = value (rng);
            largest.push (v);
            smallest.push (v);
            fed.push_back (v);
            low.push_back (v);
            INFO ("span " << span << ", value " << i);
            REQUIRE (isExactly (largest.get(), *std::max_element (fed.end() - span, fed.end())));
            REQUIRE (isExactly (smallest.get(), *std::min_element (low.end() - span, low.end())));
        }
        largest.reset();
        CHECK (isExactly (largest.get(), -100.0f));
    }
}

//==============================================================================
TEST_CASE ("Detector: the soft knee and Range", "[detector][knee]")
{
    using dsp::reductionDb;
    constexpr auto knee = dsp::kneeDb;

    CHECK (isExactly (reductionDb (-10.0f, 8.0f), 0.0));
    CHECK (isExactly (reductionDb (-0.5f * knee, 8.0f), 0.0));
    CHECK_THAT (reductionDb (0.0f, 8.0f), WithinAbs (knee / 8.0, 1.0e-6));
    CHECK_THAT (reductionDb (0.5f * knee, 8.0f), WithinAbs (0.5 * knee, 1.0e-6));
    CHECK_THAT (reductionDb (5.0f, 8.0f), WithinAbs (5.0, 1.0e-6));     // brought down to the threshold
    CHECK (isExactly (reductionDb (30.0f, 8.0f), 8.0));                          // never more than Range
    CHECK (isExactly (reductionDb (30.0f, 0.0f), 0.0));

    // The plan's knee is 3 dB wide: at the threshold an ess is reduced by 3/8 dB, and the knee
    // ends 1.5 dB either side of it.
    CHECK_THAT (reductionDb (0.0f, 24.0f), WithinAbs (0.375, 1.0e-6));
    CHECK (isExactly (reductionDb (-1.5f, 24.0f), 0.0));
    CHECK_THAT (reductionDb (1.5f, 24.0f), WithinAbs (1.5, 1.0e-6));

    // Continuous, with a continuous slope from 0 to 1, and never decreasing.
    auto previous = 0.0f;
    for (auto over = -3.0f; over <= 3.0f; over += 0.001f)
    {
        const auto r = reductionDb (over, 24.0f);
        CHECK (r >= previous);
        CHECK (r - previous <= 0.001f * 1.0001f + 1.0e-6f);
        previous = r;
    }

    // Through the detector: a steady tone at the band's centre in Allround, so S is B.
    const auto tone = sine (std::sqrt (4500.0 * 12000.0), 0.1, samplesFor (1.0, rate));
    auto s = voiceSettings();
    s.detection = Detection::allround;
    s.rangeDb = 8.0f;
    const auto level = mean (series (probeOf (tone, rate, s), band(), hopAt (0.3, rate)));

    for (const auto over : { -3.0, -1.0, 0.0, 1.0, 3.0, 6.0, 12.0 })
    {
        s.thresholdAbsDb = (float) (level - over);
        const auto r = probeOf (tone, rate, s);
        INFO ("S − threshold " << over << " dB");

        for (int i = hopAt (0.3, rate); i < (int) r.hops.size(); ++i)
            REQUIRE (isExactly (r.hops[(size_t) i].decisions[0].essTargetDb, reductionDb (r.hops[(size_t) i].paths[0].sibilanceDb - s.thresholdAbsDb, s.rangeDb)));

        CHECK_THAT (median (series (r, essReduction(), hopAt (0.3, rate))), WithinAbs (reductionDb ((float) over, 8.0f), 0.05));
    }
}

TEST_CASE ("Detector: Relative follows the voice, Absolute the level", "[detector][relative]")
{
    const auto x = voiceWithEsses (2.0, { { 1.0, 1.15 } });
    const auto from = hopAt (0.9, rate);

    auto voice = voiceSettings();
    auto allround = voiceSettings();
    allround.detection = Detection::allround;

    const auto v0 = probeOf (x, rate, voice), a0 = probeOf (x, rate, allround);
    const auto v1 = probeOf (scaled (x, 0.25), rate, voice), a1 = probeOf (scaled (x, 0.25), rate, allround);

    // Allround: S is B in dBFS, so it moves with the gain; Voice: it does not.
    CHECK (isExactly (largestDifference (series (a0, sibilance(), from), series (a0, band(), from)), 0.0));
    CHECK_THAT (mean (series (a1, sibilance(), from)) - mean (series (a0, sibilance(), from)), WithinAbs (20.0 * std::log10 (0.25), 1.0e-3));
    CHECK (largestDifference (series (v1, sibilance(), from), series (v0, sibilance(), from)) < 1.0e-3);

    // Voice: S = B − V, lowered by up to the noise range as N falls (N counts as 0 during a voice
    // onset) and by the shape cue where the spectrum falls, both scaled by Focus.
    for (int i = from; i < (int) v0.hops.size(); ++i)
        REQUIRE_THAT (v0.hops[(size_t) i].paths[0].sibilanceDb, WithinAbs (voiceSibilance (v0.hops[(size_t) i].paths[0], voice.focus), 1.0e-4));

    // Focus 0 is a pure relative detector: neither cue takes anything off.
    voice.focus = 0.0f;
    const auto plain = probeOf (x, rate, voice);
    for (int i = from; i < (int) plain.hops.size(); ++i)
        REQUIRE (isExactly (plain.hops[(size_t) i].paths[0].sibilanceDb, plain.hops[(size_t) i].paths[0].relativeDb));
}

TEST_CASE ("Detector: Allround ignores the noise cue, Voice applies it with Focus", "[detector][relative]")
{
    // Bright harmonics: the noise cue calls them harmonic (N near 0).
    const auto bright = harmonic ({ .f0 = 220.0, .rms = 0.1 }, samplesFor (1.0, rate), rate);
    const auto from = hopAt (0.2, rate);

    for (const auto cue : { dsp::NoiseCueKind::periodicity, dsp::NoiseCueKind::flatness })
    {
        auto s = voiceSettings (cue);
        s.detection = Detection::allround;
        s.focus = 0.0f;
        const auto a0 = probeOf (bright, rate, s);
        s.focus = 1.0f;
        const auto a1 = probeOf (bright, rate, s);
        CHECK (isExactly (largestDifference (series (a0, sibilance(), from), series (a1, sibilance(), from)), 0.0));

        s.detection = Detection::voice;
        s.focus = 0.0f;
        const auto v0 = probeOf (bright, rate, s);
        s.focus = 1.0f;
        const auto v1 = probeOf (bright, rate, s);
        INFO ("cue " << (int) cue);
        CHECK_THAT (median (series (v0, sibilance(), from)) - median (series (v1, sibilance(), from)), WithinAbs (dsp::DetectorTuning{}.noiseRangeDb, 0.5));
    }
}

//==============================================================================
TEST_CASE ("Detector: events start over 0.5 dB and end 20 ms after it falls back", "[detector][events]")
{
    // Steady tone bursts at the band's centre, Allround, so the reductions are clean. The last
    // two bursts are 10 ms apart: the release keeps the reduction up, so they are one ess.
    const auto n = samplesFor (1.6, rate);
    const std::vector<std::pair<double, double>> bursts { { 0.2, 0.3 }, { 0.6, 0.7 }, { 1.0, 1.1 }, { 1.11, 1.2 } };
    Samples x ((size_t) n, 0.0f);
    for (const auto& [from, to] : bursts)
        x = sum (x, gated (sine (std::sqrt (4500.0 * 12000.0), 0.1, n), samplesFor (from, rate), samplesFor (to, rate), rate));

    for (const auto latency : { LatencyMode::normal, LatencyMode::live })
    {
        auto s = voiceSettings();
        s.detection = Detection::allround;
        s.thresholdAbsDb = -33.0f;
        s.latency = latency;
        s.speedAuto = false;
        s.releaseMs = 20.0f;
        const auto r = probeOf (x, rate, s);
        INFO ((latency == LatencyMode::normal ? "Normal" : "Live"));

        REQUIRE (r.events.size() == 3);
        CHECK (r.lookaheadSamples == (latency == LatencyMode::normal ? 8 * Detector::hopSize : 0));

        // The rule, hop by hop, against the shaped reductions.
        const auto hangover = (int) std::lround (Detector::eventHangoverMs * 0.001 * rate / Detector::hopSize);
        std::vector<dsp::EssEvent> expected;
        auto quiet = 0;
        auto open = false;
        for (int i = 0; i < (int) r.hops.size(); ++i)
        {
            const auto& h = r.hops[(size_t) i];
            const auto over = h.reductions[0].essDb > 0.5f;
            if (! open && over)
            {
                open = true;
                quiet = 0;
                expected.push_back ({ (std::int64_t) i * Detector::hopSize, 0, h.reductions[0].essDb, h.reductions[0].sibilanceDb });
            }
            else if (open)
            {
                auto& e = expected.back();
                e.peakReductionDb = std::max (e.peakReductionDb, h.reductions[0].essDb);
                e.peakSibilanceDb = std::max (e.peakSibilanceDb, h.reductions[0].sibilanceDb);
                quiet = over ? 0 : quiet + 1;
                if (quiet == hangover)
                {
                    e.end = (std::int64_t) (i + 1) * Detector::hopSize;
                    open = false;
                }
            }
            CHECK (h.eventActive == open);
        }

        REQUIRE (expected.size() == r.events.size());
        for (size_t e = 0; e < expected.size(); ++e)
        {
            CHECK (r.events[e].start == expected[e].start);
            CHECK (r.events[e].end == expected[e].end);
            CHECK (isExactly (r.events[e].peakReductionDb, expected[e].peakReductionDb));
            CHECK (isExactly (r.events[e].peakSibilanceDb, expected[e].peakSibilanceDb));
            CHECK_THAT (r.events[e].peakReductionDb, WithinAbs (s.rangeDb, 0.01));
        }

        // Starts: with look-ahead the reduction leads the burst; Live reacts within three hops (B's
        // 4 ms averaging).
        const std::vector<double> onsets { 0.2, 0.6, 1.0 };
        for (size_t e = 0; e < onsets.size(); ++e)
        {
            const auto onset = samplesFor (onsets[e], rate);
            INFO ("event " << e << " starts " << 1000.0 * (double) (r.events[e].start - onset) / rate << " ms from the burst");
            if (latency == LatencyMode::normal)
            {
                CHECK (r.events[e].start <= onset);
                CHECK (r.events[e].start >= onset - r.lookaheadSamples - Detector::hopSize);
            }
            else
            {
                CHECK (r.events[e].start >= onset - Detector::hopSize);
                CHECK (r.events[e].start <= onset + 3 * Detector::hopSize);
            }
        }
    }
}

TEST_CASE ("Detector: Speed Auto's constants are the measured ones", "[detector][events]")
{
    // Chosen in M3 (NOTES.md, M3: Speed; PLAN.md, Settled 48 and 50): the burst cases below read them.
    const dsp::DetectorTuning t;
    CHECK (isExactly (t.essAutoShare, 0.25f));
    CHECK (isExactly (t.essAutoMinMs, 15.0f));
    CHECK (isExactly (t.essAutoMaxMs, 30.0f));
    CHECK (isExactly (t.harshAutoShare, 0.5f));
    CHECK (isExactly (t.harshAutoMinMs, 30.0f));
    CHECK (isExactly (t.harshAutoMaxMs, 150.0f));
}

TEST_CASE ("Detector: Speed Auto releases over a quarter of the time over threshold, 15 to 30 ms", "[detector][events]")
{
    // Live and Allround, so the reduction is the target and it lets go as a one-pole once the
    // target is 0: each hop keeps exp (-hop / release) of the one before.
    const auto hopMs = 1000.0 * Detector::hopSize / rate;
    auto s = voiceSettings();
    s.detection = Detection::allround;
    s.thresholdAbsDb = -33.0f;
    s.latency = LatencyMode::live;

    for (const auto burst : { 0.01, 0.08, 0.5 })
    {
        const auto n = samplesFor (burst + 0.5, rate);
        const auto x = gated (sine (std::sqrt (4500.0 * 12000.0), 0.1, n), samplesFor (0.1, rate), samplesFor (0.1 + burst, rate), rate);
        const auto r = probeOf (x, rate, s);

        auto over = 0, end = 0;
        for (int i = 0; i < (int) r.hops.size(); ++i)
        {
            if (r.hops[(size_t) i].decisions[0].essTargetDb > 0.0f)
                ++over;
            else if (over > 0)
            {
                end = i;
                break;
            }
        }
        REQUIRE (end > 0);

        const dsp::DetectorTuning t;
        const auto releaseMs = std::clamp (t.essAutoShare * over * hopMs, (double) t.essAutoMinMs, (double) t.essAutoMaxMs);
        const auto ratio = r.hops[(size_t) end].reductions[0].essDb / r.hops[(size_t) end - 1].reductions[0].essDb;
        INFO ("burst " << burst << " s: over threshold for " << over * hopMs << " ms, release " << releaseMs << " ms");
        CHECK_THAT (ratio, WithinAbs (std::exp (-hopMs / releaseMs), 1.0e-4));
    }
}

TEST_CASE ("Detector: Harsh Speed Auto releases over half the time over threshold, 30 to 150 ms", "[detector][harsh]")
{
    // As the Ess case: Live and Allround, a tone in the Harsh band, the Ess band out of it.
    const auto hopMs = 1000.0 * Detector::hopSize / rate;
    auto s = voiceSettings();
    s.detection = Detection::allround;
    s.thresholdAbsDb = 0.0f;
    s.harshOn = true;
    s.harshThresholdAbsDb = -25.0f;             // 2 dB under the tone, so H's 40 ms release leaves it soon
    s.latency = LatencyMode::live;

    const dsp::DetectorTuning t;
    // The floor binds, the middle, the limit binds.
    for (const auto burst : { 0.01, 0.15, 0.5 })
    {
        const auto n = samplesFor (burst + 1.0, rate);
        const auto x = gated (sine (3200.0, 0.1, n), samplesFor (0.3, rate), samplesFor (0.3 + burst, rate), rate);
        const auto r = probeOf (x, rate, s);

        auto over = 0, end = 0;
        for (int i = 0; i < (int) r.hops.size(); ++i)
        {
            const auto& d = r.hops[(size_t) i].decisions[0];
            REQUIRE ((i < hopAt (0.1, rate) || ! d.harshHeld));
            if (d.harshTargetDb > 0.0f)
                ++over;
            else if (over > 0)
            {
                end = i;
                break;
            }
        }
        REQUIRE (end > 0);

        const auto releaseMs = std::clamp (t.harshAutoShare * over * hopMs, (double) t.harshAutoMinMs, (double) t.harshAutoMaxMs);
        const auto ratio = r.hops[(size_t) end].reductions[0].harshDb / r.hops[(size_t) end - 1].reductions[0].harshDb;
        INFO ("burst " << burst << " s: over threshold for " << over * hopMs << " ms, release " << releaseMs << " ms");
        CHECK_THAT (ratio, WithinAbs (std::exp (-hopMs / releaseMs), 1.0e-4));
        if (burst < 0.05)
            CHECK (isExactly (releaseMs, (double) t.harshAutoMinMs));
        else if (burst > 0.4)
            CHECK (isExactly (releaseMs, (double) t.harshAutoMaxMs));
        else
            CHECK ((releaseMs > t.harshAutoMinMs && releaseMs < t.harshAutoMaxMs));
    }
}

TEST_CASE ("Detector: Harsh follows H - body in Voice and H in Allround, from the louder band when Linked", "[detector][harsh]")
{
    // The resonance moves from the left channel to the right at 1 s.
    const auto n = samplesFor (2.0, rate), half = samplesFor (1.0, rate);
    const auto harsh = harmonic ({ .f0 = 180.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -12.0, .peakHz = 3200.0, .peakDb = 24.0,
                                   .peakOctaves = 0.7, .rms = 0.1 }, n, rate);
    const auto plain = vowel (180.0, n, rate, 0.1);
    const auto left = sum (gated (harsh, 0, half, rate), gated (plain, half, n, rate));
    const auto right = sum (gated (plain, 0, half, rate), gated (harsh, half, n, rate));

    for (const auto detection : { Detection::voice, Detection::allround })
    {
        auto s = voiceSettings();
        s.detection = detection;
        s.harshOn = true;
        s.thresholdDb = 24.0f;          // the Ess band stays out of it
        s.thresholdAbsDb = 0.0f;

        // Thresholds 2 dB under the louder band's median, so the targets lie inside Harsh Range
        // and the relative and the absolute level give different ones.
        s.harshThresholdDb = s.harshThresholdAbsDb = 100.0f;
        const auto levels = probeChannels ({ &left, &right }, rate, s);
        std::vector<double> relativeLevels, absoluteLevels;
        for (int i = hopAt (0.1, rate); i < (int) levels.hops.size(); ++i)
        {
            const auto& h = levels.hops[(size_t) i];
            const auto& p = h.paths[h.paths[0].harshDb >= h.paths[1].harshDb ? 0 : 1];
            relativeLevels.push_back (p.harshRelativeDb);
            absoluteLevels.push_back (p.harshDb);
        }
        s.harshThresholdDb = (float) median (relativeLevels) - 2.0f;
        s.harshThresholdAbsDb = (float) median (absoluteLevels) - 2.0f;
        const auto r = probeChannels ({ &left, &right }, rate, s);

        int fromPath[2] {};
        auto inside = 0;
        for (int i = hopAt (0.1, rate); i < (int) r.hops.size(); ++i)
        {
            const auto& h = r.hops[(size_t) i];
            const auto& d = h.decisions[0];
            if (d.harshHeld)
                continue;
            const auto louder = h.paths[0].harshDb >= h.paths[1].harshDb ? 0 : 1;
            const auto& p = h.paths[(size_t) louder];
            const auto level = detection == Detection::voice ? p.harshRelativeDb - s.harshThresholdDb : p.harshDb - s.harshThresholdAbsDb;
            ++fromPath[louder];
            inside += d.harshTargetDb > 0.0f && d.harshTargetDb < s.harshRangeDb ? 1 : 0;
            INFO ((detection == Detection::voice ? "Voice" : "Allround") << ", hop " << i);
            REQUIRE (isExactly (d.harshTargetDb, dsp::reductionDb (level, s.harshRangeDb)));
        }
        CHECK (fromPath[0] > hopAt (0.5, rate));
        CHECK (fromPath[1] > hopAt (0.5, rate));
        CHECK (inside > hopAt (0.5, rate));
    }
}

TEST_CASE ("Detector: look-ahead ramps the reduction in before the ess", "[detector][lookahead]")
{
    const auto n = samplesFor (0.6, rate);
    const auto onset = samplesFor (0.3, rate);
    const auto x = gated (sine (std::sqrt (4500.0 * 12000.0), 0.1, n), onset, n, rate);

    auto s = voiceSettings();
    s.detection = Detection::allround;
    s.thresholdAbsDb = -33.0f;

    for (const auto ms : { 0.0f, 2.0f, 5.0f, 10.0f })
    {
        s.lookaheadMs = ms;
        const auto r = probeOf (x, rate, s);
        const auto hops = (int) std::lround (ms * 0.001 * rate / Detector::hopSize);
        INFO ("look-ahead " << ms << " ms");
        CHECK (r.lookaheadSamples == hops * Detector::hopSize);

        // The first hop with any reduction is the look-ahead before the first with a target.
        auto first = 0, firstTarget = 0;
        while (r.hops[(size_t) first].reductions[0].essDb <= 0.0f)
            ++first;
        while (r.hops[(size_t) firstTarget].decisions[0].essTargetDb <= 0.0f)
            ++firstTarget;
        CHECK (first == firstTarget - hops);

        // Ramped in linearly: each target ahead counts by its distance, 1 for the hop's own down to
        // 1 / (look-ahead + 1) for the newest, so the first reduction is the target / (L + 1).
        for (int i = first; i <= firstTarget; ++i)
        {
            auto expected = 0.0f;
            for (int j = 0; j <= hops; ++j)
                expected = std::max (expected, (1.0f - (float) j / (float) (hops + 1)) * r.hops[(size_t) (i + j)].decisions[0].essTargetDb);
            CHECK_THAT (r.hops[(size_t) i].reductions[0].essDb, WithinAbs (expected, 1.0e-5));
        }
        if (hops > 0)
            CHECK_THAT (r.hops[(size_t) first].reductions[0].essDb,
                        WithinAbs (r.hops[(size_t) firstTarget].decisions[0].essTargetDb / (float) (hops + 1), 1.0e-5));

        // Never more than the largest target within the look-ahead, never less than the hop's own,
        // and rising until the target arrives.
        for (int i = first; i <= firstTarget + hops; ++i)
        {
            auto ahead = 0.0f;
            for (int j = i; j <= i + hops; ++j)
                ahead = std::max (ahead, r.hops[(size_t) j].decisions[0].essTargetDb);
            CHECK (r.hops[(size_t) i].reductions[0].essDb <= ahead + 1.0e-6f);
            CHECK (r.hops[(size_t) i].reductions[0].essDb >= r.hops[(size_t) i].decisions[0].essTargetDb);
            if (i > first && i <= firstTarget)
                CHECK (r.hops[(size_t) i].reductions[0].essDb >= r.hops[(size_t) i - 1].reductions[0].essDb);
        }
    }

    // Live ignores the look-ahead.
    s.latency = LatencyMode::live;
    s.lookaheadMs = 10.0f;
    CHECK (probeOf (x, rate, s).lookaheadSamples == 0);
}

//==============================================================================
TEST_CASE ("Detector: Harsh holds during an ess and on noise in its band", "[detector][harsh]")
{
    // A vowel with a strong resonance at 3.2 kHz: the Harsh band reduces it.
    const auto n = samplesFor (1.4, rate);
    const auto from = samplesFor (0.6, rate), to = samplesFor (0.75, rate);
    const auto harsh = harmonic ({ .f0 = 180.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -12.0,
                                   .peakHz = 3200.0, .peakDb = 30.0, .peakOctaves = 0.7, .rms = 0.1 }, n, rate);
    const auto vowelOnly = sum (gated (harsh, 0, from, rate), gated (harsh, to, n, rate));

    auto s = voiceSettings();
    s.harshOn = true;
    const auto steady = median (series (probeOf (vowelOnly, rate, s), [] (const DetectorFrame& f) { return f.paths[0].harshRelativeDb; },
                                        hopAt (0.3, rate), hopAt (0.55, rate)));
    s.harshThresholdDb = (float) steady - 3.0f;
    s.harshThresholdAbsDb = 0.0f;
    const auto steadyTarget = dsp::reductionDb (3.0f, s.harshRangeDb);

    SECTION ("an ess in place of the vowel: the Harsh target holds")
    {
        s.thresholdDb = -14.0f;
        const auto x = sum (vowelOnly, gated (bandNoise (4500.0, 12000.0, n, rate, 3, 0.03), from, to, rate));
        const auto r = probeOf (x, rate, s);
        REQUIRE (r.events.size() == 1);

        auto essHops = 0;
        for (int i = hopAt (from); i < hopAt (to); ++i)
        {
            const auto& d = r.hops[(size_t) i].decisions[0];
            if (d.essTargetDb <= 0.0f)
                continue;
            ++essHops;
            INFO ("hop " << i);
            CHECK (d.harshHeld);
            CHECK_THAT (d.harshTargetDb, WithinAbs (steadyTarget, 0.5));
        }
        CHECK (essHops > hopAt (0.1, rate));
    }

    SECTION ("noise in the Harsh band (an /ʃ/) holds it without the Ess band, in Voice only")
    {
        s.thresholdDb = 24.0f;      // the Ess band never fires
        s.thresholdAbsDb = 0.0f;
        const auto x = sum (vowelOnly, gated (bandNoise (2500.0, 3700.0, n, rate, 4, 0.05), from, to, rate));

        const auto heldShare = [&] (const dsp::probe::Result& r)
        {
            auto held = 0, count = 0;
            for (int i = hopAt (from + samplesFor (0.01, rate)); i < hopAt (to); ++i, ++count)
            {
                REQUIRE (isExactly (r.hops[(size_t) i].decisions[0].essTargetDb, 0.0));
                held += r.hops[(size_t) i].decisions[0].harshHeld ? 1 : 0;
            }
            return (double) held / count;
        };

        for (const auto cue : { dsp::NoiseCueKind::periodicity, dsp::NoiseCueKind::flatness })
        {
            s.noiseCue = cue;
            s.detection = Detection::voice;
            const auto voice = probeOf (x, rate, s);
            INFO ("cue " << (int) cue << ": held on " << 100.0 * heldShare (voice) << " % of the noise's hops");
            CHECK (heldShare (voice) >= 0.9);

            // The vowel itself is harmonic: no hold before the noise.
            for (int i = hopAt (0.3, rate); i < hopAt (from); ++i)
                CHECK_FALSE (voice.hops[(size_t) i].decisions[0].harshHeld);
        }

        s.detection = Detection::allround;
        CHECK (isExactly (heldShare (probeOf (x, rate, s)), 0.0));
    }
}

//==============================================================================
TEST_CASE ("Detector: stereo modes", "[detector][stereo]")
{
    const auto left = voiceWithEsses (1.5, { { 0.5, 0.65 }, { 1.0, 1.1 } }, 0.1, 0.03);
    const auto right = voiceWithEsses (1.5, { { 0.7, 0.8 } }, 0.08, 0.05, 150.0);

    auto s = voiceSettings();
    s.thresholdDb = -14.0f;
    s.harshOn = true;
    const auto monoLeft = probeOf (left, rate, s), monoRight = probeOf (right, rate, s);

    const auto pathsEqual = [] (const DetectorFrame::Path& a, const DetectorFrame::Path& b)
    {
        return same (a.bandDb, b.bandDb) && same (a.bodyDb, b.bodyDb) && same (a.sibilanceDb, b.sibilanceDb)
            && same (a.harshDb, b.harshDb) && same (a.noise, b.noise);
    };

    SECTION ("Unlinked: two detectors, one per channel")
    {
        s.stereo = StereoMode::unlinked;
        const auto r = probeChannels ({ &left, &right }, rate, s);
        for (size_t i = 0; i < r.hops.size(); ++i)
        {
            const auto& h = r.hops[i];
            REQUIRE (h.numPaths == 2);
            REQUIRE (h.numReductions == 2);
            REQUIRE (pathsEqual (h.paths[0], monoLeft.hops[i].paths[0]));
            REQUIRE (pathsEqual (h.paths[1], monoRight.hops[i].paths[0]));
            REQUIRE (same (h.reductions[0].essDb, monoLeft.hops[i].reductions[0].essDb));
            REQUIRE (same (h.reductions[1].essDb, monoRight.hops[i].reductions[0].essDb));
        }
    }

    SECTION ("Unlinked: an ess on either channel is an event")
    {
        s.stereo = StereoMode::unlinked;
        const auto plain = vowel (180.0, (int) right.size(), rate, 0.1);
        const auto r = probeChannels ({ &plain, &right }, rate, s);
        REQUIRE (r.events.size() == 1);
        CHECK (std::abs (r.events[0].start - samplesFor (0.7, rate)) < samplesFor (0.02, rate));
    }

    SECTION ("Linked: the louder channel's detector drives one reduction")
    {
        s.stereo = StereoMode::linked;
        const auto r = probeChannels ({ &left, &right }, rate, s);
        auto fromLeft = 0, fromRight = 0;
        for (size_t i = 0; i < r.hops.size(); ++i)
        {
            const auto& h = r.hops[i];
            REQUIRE (h.numPaths == 2);
            REQUIRE (h.numReductions == 1);
            REQUIRE (pathsEqual (h.paths[0], monoLeft.hops[i].paths[0]));
            REQUIRE (pathsEqual (h.paths[1], monoRight.hops[i].paths[0]));

            const auto louder = h.paths[0].bandDb >= h.paths[1].bandDb ? 0 : 1;
            (louder == 0 ? fromLeft : fromRight)++;
            REQUIRE (same (h.decisions[0].sibilanceDb, h.paths[(size_t) louder].sibilanceDb));
        }
        CHECK (fromLeft > 0);
        CHECK (fromRight > 0);
    }

    SECTION ("Mid and Side detect on that component")
    {
        Samples inverted (left.size());
        for (size_t i = 0; i < left.size(); ++i)
            inverted[i] = -left[i];

        // (x, x) has x as its mid and nothing as its side; (x, −x) the reverse.
        s.stereo = StereoMode::mid;
        auto r = probeChannels ({ &left, &left }, rate, s);
        CHECK (firstDifference (r, monoLeft) == -1);
        r = probeChannels ({ &left, &inverted }, rate, s);
        CHECK (r.events.empty());
        CHECK (r.hops.back().numPaths == 1);

        s.stereo = StereoMode::side;
        r = probeChannels ({ &left, &inverted }, rate, s);
        CHECK (firstDifference (r, monoLeft) == -1);
        r = probeChannels ({ &left, &left }, rate, s);
        CHECK (r.events.empty());

        // And on a real pair, the mid is what a mono detector reads from (L + R) / 2.
        Samples mid (left.size());
        for (size_t i = 0; i < left.size(); ++i)
            mid[i] = 0.5f * (left[i] + right[i]);
        s.stereo = StereoMode::mid;
        r = probeChannels ({ &left, &right }, rate, s);
        const auto monoMid = probeOf (mid, rate, s);
        CHECK (largestDifference (series (r, sibilance(), hopAt (0.2, rate)), series (monoMid, sibilance(), hopAt (0.2, rate))) < 1.0e-2);
    }

    SECTION ("a mono main bus with a stereo key is linked")
    {
        s.stereo = StereoMode::unlinked;
        s.key = KeySource::external;
        const auto r = probeChannels ({ &left }, rate, s, { 512 }, { &left, &right });
        CHECK (r.hops.back().numPaths == 2);
        CHECK (r.hops.back().numReductions == 1);
    }
}

TEST_CASE ("Detector: the key is the input or the side chain, with its own high-pass", "[detector][key]")
{
    const auto voice = voiceWithEsses (1.5, { { 0.5, 0.65 }, { 1.0, 1.1 } });
    const Samples silence (voice.size(), 0.0f);
    const auto other = bandNoise (4500.0, 12000.0, (int) voice.size(), rate, 99, 0.2);

    auto s = voiceSettings();
    s.thresholdDb = -14.0f;
    const auto internal = probeOf (voice, rate, s);
    REQUIRE (internal.events.size() == 2);

    // Internal ignores whatever is on the side chain.
    CHECK (firstDifference (probeChannels ({ &voice }, rate, s, { 512 }, { &other }), internal) == -1);

    // External: the main signal is ignored, the side chain detected.
    s.key = KeySource::external;
    CHECK (firstDifference (probeChannels ({ &silence }, rate, s, { 512 }, { &voice }), internal) == -1);
    const auto stereoKey = probeChannels ({ &silence, &silence }, rate, s, { 512 }, { &voice, &voice });
    CHECK (stereoKey.events.size() == 2);

    // External without a side chain (the host has the bus off) falls back to the input.
    CHECK (firstDifference (probeChannels ({ &voice }, rate, s), internal) == -1);

    // Key HPF: Off at 0 is no filter at all; at 1 kHz it takes a low voice out of V, not out of B.
    s.key = KeySource::internal;
    s.keyHpfHz = 0.0f;
    CHECK (firstDifference (probeOf (voice, rate, s), internal) == -1);

    const auto low = vowel (100.0, samplesFor (1.0, rate), rate, 0.1);
    const auto lowAndEss = sum (low, bandNoise (4500.0, 12000.0, (int) low.size(), rate, 5, 0.01));
    const auto open = probeOf (lowAndEss, rate, s);
    s.keyHpfHz = 1000.0f;
    const auto filtered = probeOf (lowAndEss, rate, s);
    const auto from = hopAt (0.3, rate);
    const auto bodyDrop = mean (series (open, body(), from)) - mean (series (filtered, body(), from));
    INFO ("V drops by " << bodyDrop << " dB with the key high-passed at 1 kHz");
    CHECK (bodyDrop > 15.0);
    CHECK_THAT (mean (series (open, band(), from)) - mean (series (filtered, band(), from)), WithinAbs (0.0, 0.1));
}

TEST_CASE ("Detector: Detect Low above Detect High swaps them", "[detector][bands]")
{
    const auto x = voiceWithEsses (1.0, { { 0.5, 0.65 } });
    auto s = voiceSettings();
    s.thresholdDb = -14.0f;
    const auto ordered = probeOf (x, rate, s);
    std::swap (s.detectLowHz, s.detectHighHz);
    CHECK (firstDifference (probeOf (x, rate, s), ordered) == -1);
}

TEST_CASE ("Detector: a band narrower than a third of an octave widens around its centre", "[detector][bands]")
{
    // Broadband noise, so B depends on the band's width: a narrow band reads as the third octave
    // around its centre, and no edge reaches past 0.45 fs.
    const auto third = std::exp2 (1.0 / 6.0);
    const auto centre = std::sqrt (5000.0 * 5100.0);
    struct Case
    {
        double rate, low, high, wantLow, wantHigh;
    };
    const Case cases[] { { 48000.0, 5000.0, 5100.0, centre / third, centre * third },
                         { 48000.0, 6000.0, 6000.0, 6000.0 / third, 6000.0 * third },
                         { 44100.0, 5100.0, 5000.0, centre / third, centre * third },
                         { 44100.0, 9000.0, 20000.0, 9000.0, 19845.0 },
                         { 44100.0, 19000.0, 19000.0, 19845.0 / (third * third), 19845.0 } };   // against the top: widens downwards

    for (const auto& c : cases)
    {
        const auto noise = bandNoise (100.0, 0.49 * c.rate, samplesFor (1.0, c.rate), c.rate, 21, 0.1);
        const auto bandOf = [&] (double low, double high)
        {
            auto s = voiceSettings();
            s.detectLowHz = (float) low;
            s.detectHighHz = (float) high;
            return series (probeOf (noise, c.rate, s), band(), hopAt (0.2, c.rate));
        };
        INFO ((c.rate / 1000.0) << " kHz, Detect " << c.low << " … " << c.high << " Hz reads as " << c.wantLow << " … " << c.wantHigh << " Hz");
        CHECK (largestDifference (bandOf (c.low, c.high), bandOf (c.wantLow, c.wantHigh)) < 1.0e-3);
    }
}

//==============================================================================
TEST_CASE ("Detector: any block size gives the same hops", "[detector][blocks]")
{
    const auto r = 44100.0;
    const auto n = samplesFor (2.0, r);
    auto left = vowel (180.0, n, r, 0.1, 1);
    auto right = vowel (150.0, n, r, 0.07, 2);
    left = sum (left, gated (bandNoise (4500.0, 12000.0, n, r, 3, 0.03), samplesFor (0.5, r), samplesFor (0.65, r), r));
    right = sum (right, gated (bandNoise (4500.0, 12000.0, n, r, 4, 0.04), samplesFor (1.2, r), samplesFor (1.3, r), r));

    for (const auto cue : { dsp::NoiseCueKind::periodicity, dsp::NoiseCueKind::flatness, dsp::NoiseCueKind::blend })
        for (const auto stereo : { StereoMode::linked, StereoMode::unlinked })
        {
            auto s = voiceSettings (cue);
            s.thresholdDb = -14.0f;
            s.harshOn = true;
            s.stereo = stereo;
            const auto reference = probeChannels ({ &left, &right }, r, s, { 4096 });
            REQUIRE_FALSE (reference.events.empty());

            for (const auto& blocks : std::vector<std::vector<int>> { { 1 }, { 7, 13, 512, 1, 4096, 100, 31 }, { 32 }, { 33 } })
            {
                const auto other = probeChannels ({ &left, &right }, r, s, blocks);
                INFO ("cue " << (int) cue << ", stereo " << (int) stereo << ", blocks starting " << blocks.front());
                CHECK (firstDifference (other, reference) == -1);
                CHECK (sameEvents (other, reference));
            }
        }
}
