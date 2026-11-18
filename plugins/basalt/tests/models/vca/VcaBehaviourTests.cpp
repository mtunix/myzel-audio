// VCA's other rows (models/vca/NOTES.md, Gates; the core's gate table): the stereo link for each
// detector, the detector switch as the chain makes it (warm-up, seed, 30 ms blend in dB), knob
// jumps and Auto release switched without steps, block-size independence, reset and seed, a key
// with NaNs and infinities, and the distortion rows.

#include "VcaTesting.h"

#include "dsp/dynamics/Stereo.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>

using namespace basalt;
using namespace basalt::dynamics;
using namespace basalt::testing;
using namespace basalt::testing::vca;
namespace ref = basalt::reference::vca;
using Catch::Matchers::WithinAbs;
using models::VcaModel;
using myzel::dsp::isExactly;

namespace
{
    /** Two unrelated, bursty noises, 10 s. */
    std::pair<Signal, Signal> burstyStereo (int n)
    {
        auto left = pinkNoise (n, 1u), right = pinkNoise (n, 2u);
        for (int i = 0; i < n; ++i)
        {
            const auto envelope = 0.2 + 0.8 * std::abs (std::sin (2.0 * std::numbers::pi * 0.7 * i / rate));
            left[(size_t) i] *= 0.3 * envelope;
            right[(size_t) i] *= 0.3 * (1.2 - envelope);
        }
        return { left, right };
    }
}

TEST_CASE ("VCA: Link 100 % gives both channels the same reduction to the bit, Link 0 % is Unlinked", "[vca][stereo]")
{
    const auto n = samplesFor (10.0, rate);
    const auto [left, right] = burstyStereo (n);
    const Signal lookahead ((size_t) n, 96.0);

    for (const auto d : { Detector::peak, Detector::rms })
        for (const auto& [attackMs, autoRelease] : { std::pair { 0.1, false }, std::pair { 10.0, true }, std::pair { 30.0, false } })
        {
            auto p = params (-40.0, 0.25, 6.0, 40.0, attackMs, 150.0, autoRelease);
            p.linked = true;
            p.link = 1.0;
            auto m = prepared (d);
            const auto full = runModel (m, p, left, &right, 97, &lookahead);
            INFO (nameOf (d) << ", attack " << attackMs);
            REQUIRE (full[0] == full[1]);
            REQUIRE (*std::max_element (full[0].begin(), full[0].end()) > 6.0);

            p.link = 0.0;
            m.reset();
            const auto none = runModel (m, p, left, &right, 64, &lookahead);

            p.linked = false;
            m.reset();
            const auto unlinked = runModel (m, p, left, &right, 512, &lookahead);
            REQUIRE (none[0] == unlinked[0]);
            REQUIRE (none[1] == unlinked[1]);
            REQUIRE_FALSE (none[0] == none[1]);
        }
}

TEST_CASE ("VCA Peak: the louder channel wins at Link 100 %", "[vca][peak][stereo]")
{
    // Stereo dominant: with the right channel silent, both channels carry the left one's
    // reduction, to the bit the reduction a mono key gives.
    const auto n = samplesFor (3.0, rate);
    const auto [left, unused] = burstyStereo (n);
    const auto quiet = silence (n);

    for (const auto autoRelease : { false, true })
    {
        auto p = params (-40.0, 0.25, 6.0, 40.0, 5.0, 150.0, autoRelease);
        auto mono = prepared (Detector::peak);
        const auto alone = runModel (mono, p, left)[0];

        p.linked = true;
        p.link = 1.0;
        auto stereo = prepared (Detector::peak);
        const auto both = runModel (stereo, p, left, &quiet);
        REQUIRE (both[0] == alone);
        REQUIRE (both[1] == alone);
    }
}

TEST_CASE ("VCA RMS: linked stereo sums power", "[vca][rms][stereo]")
{
    // NOTES.md, 1. Detector, RMS: L_Σ = 10·log10(m_L + m_R). A carrier on one side reads 3.01 dB
    // below its own reading; the same carrier on both sides reads its own.
    constexpr double t = -30.0, level = -10.0;
    const auto n = samplesFor (1.0, rate);
    const auto key = carrier (carrierAmplitude (Detector::rms, level), n);
    const auto quiet = silence (n);

    auto p = params (t, 0.25, 0.0, 40.0, 10.0, 150.0);
    p.linked = true;
    p.link = 1.0;

    const auto msOf = [] (double levelDb) { return 0.5 * std::pow (10.0, levelDb / 10.0); };

    auto m = prepared (Detector::rms);
    const auto oneSided = runModel (m, p, key, &quiet);
    const auto expectedOneSided = ref::staticReductionDb (ref::powerSumLevelDb (msOf (level), 0.0), t, 0.25, 0.0, 40.0);
    REQUIRE_THAT (oneSided[0].back(), WithinAbs (expectedOneSided, 1.0e-6));
    REQUIRE_THAT (oneSided[0].back(), WithinAbs (ref::staticReductionDb (level - ref::carrierRmsOffsetDb, t, 0.25, 0.0, 40.0), 1.0e-6));
    REQUIRE (oneSided[0] == oneSided[1]);

    m.reset();
    const auto centred = runModel (m, p, key, &key);
    REQUIRE_THAT (centred[0].back(), WithinAbs (ref::staticReductionDb (level, t, 0.25, 0.0, 40.0), 1.0e-6));

    // Link 50 %: half the channel's own reduction, half the power sum's.
    p.link = 0.5;
    m.reset();
    const auto half = runModel (m, p, key, &quiet);
    const auto own = ref::staticReductionDb (level, t, 0.25, 0.0, 40.0);
    REQUIRE_THAT (half[0].back(), WithinAbs (0.5 * own + 0.5 * expectedOneSided, 1.0e-6));
    REQUIRE_THAT (half[1].back(), WithinAbs (0.5 * expectedOneSided, 1.0e-6));
}

namespace
{
    /**
        A detector switch as the chain makes it (dynamics/NOTES.md, The chain; DynamicsLane): the
        outgoing model stays heard while the incoming one follows the key from reset for its
        settleMs plus the look-ahead window, is then seeded with the reduction applied and the two
        blend in dB on a 30 ms raised cosine. 64-sample blocks, the switch asked for at `at`.
    */
    struct Switched
    {
        Trace reduction, weight;
        size_t fadeStart = 0, fadeEnd = 0;
    };

    Switched switchDetector (Detector from, Detector to, const ModelParams& p, const Signal& key, int at)
    {
        auto outgoing = prepared (from), incoming = prepared (to);
        const auto warmUp = (int) std::ceil (incoming.settleMs (p) * 0.001 * rate) + 1;
        const auto fadeLength = samplesFor (0.03, rate);

        Switched s;
        s.reduction.assign (key.size(), 0.0);
        s.weight.assign (key.size(), 0.0);

        int warmLeft = -1, fadePosition = -1;
        Trace a (64), b (64);

        for (int start = 0; start < (int) key.size(); start += 64)
        {
            const auto len = std::min (64, (int) key.size() - start);
            KeyBlock k;
            k.numSamples = len;
            k.channels[0] = key.data() + start;
            double* oa[] { a.data(), nullptr };
            double* ob[] { b.data(), nullptr };

            if (start >= at && warmLeft < 0 && fadePosition < 0)
            {
                incoming.reset();
                warmLeft = warmUp;
            }
            else if (warmLeft == 0)
            {
                const double seed[] { outgoing.reduction (0) };
                incoming.seed (seed, 1);
                warmLeft = -2;
                fadePosition = 0;
                s.fadeStart = (size_t) start;
            }

            if (fadePosition >= fadeLength)
            {
                incoming.process (p, k, ob);
                for (int i = 0; i < len; ++i)
                {
                    s.reduction[(size_t) (start + i)] = b[(size_t) i];
                    s.weight[(size_t) (start + i)] = 1.0;
                }
                continue;
            }

            outgoing.process (p, k, oa);

            if (fadePosition >= 0)
            {
                incoming.process (p, k, ob);
                for (int i = 0; i < len; ++i)
                {
                    const auto w = fadePosition >= fadeLength ? 1.0 : 0.5 - 0.5 * std::cos (std::numbers::pi * fadePosition / fadeLength);
                    fadePosition = std::min (fadePosition + 1, fadeLength);
                    s.reduction[(size_t) (start + i)] = a[(size_t) i] + w * (b[(size_t) i] - a[(size_t) i]);
                    s.weight[(size_t) (start + i)] = w;
                }
                if (fadePosition >= fadeLength && s.fadeEnd == 0)
                    s.fadeEnd = (size_t) (start + len);
                continue;
            }

            if (warmLeft > 0)
            {
                incoming.follow (p, k);
                warmLeft = std::max (0, warmLeft - len);
            }

            for (int i = 0; i < len; ++i)
                s.reduction[(size_t) (start + i)] = a[(size_t) i];
        }

        return s;
    }
}

TEST_CASE ("VCA: a detector switch starts at the applied reduction and neither swells nor steps", "[vca][switch]")
{
    // NOTES.md, Gates, Clicks: the switch at 6 dB of reduction, both directions, Attack 10 and
    // 300 ms; the reduction stays within 0.5 dB of the blend of the two detectors' own settled
    // reductions during the fade (on a 1 kHz sine, which both read alike), and its steps stay
    // those of the detectors themselves.
    constexpr double t = -30.0;

    for (const auto attackMs : { 10.0, 300.0 })
        for (const auto from : { Detector::peak, Detector::rms })
            for (const auto useSine : { true, false })
            {
                const auto to = from == Detector::peak ? Detector::rms : Detector::peak;
                const auto p = params (t, 0.25, 0.0, 40.0, attackMs, 150.0, true);
                const auto level = ref::levelForReductionDb (6.0, t, 0.25);
                const auto n = samplesFor (16.0, rate);
                const auto key = useSine ? sine (amplitudeFor (level), 1000.0, rate, n) : carrier (amplitudeFor (level), n);
                const auto at = samplesFor (8.0, rate) / 64 * 64;

                const auto s = switchDetector (from, to, p, key, at);

                auto alone = prepared (from), other = prepared (to);
                const auto ra = runModel (alone, p, key)[0], rb = runModel (other, p, key)[0];

                auto worstBlend = 0.0, worstStep = 0.0, ownStep = 0.0;
                for (auto i = s.fadeStart; i < s.fadeEnd; ++i)
                {
                    worstBlend = std::max (worstBlend, std::abs (s.reduction[i] - (ra[i] + s.weight[i] * (rb[i] - ra[i]))));
                    worstStep = std::max (worstStep, std::abs (s.reduction[i] - s.reduction[i - 1]));
                    ownStep = std::max ({ ownStep, std::abs (ra[i] - ra[i - 1]), std::abs (rb[i] - rb[i - 1]) });
                }

                std::printf ("VCA switch %s -> %s at Attack %g ms, %s: seeded at %.3f dB (settled %.3f / %.3f), largest distance from the blend "
                             "%.4f dB, largest step %.5f dB (the detectors' own %.5f)\n",
                             nameOf (from), nameOf (to), attackMs, useSine ? "1 kHz sine" : "carrier", s.reduction[s.fadeStart - 1],
                             ra[s.fadeStart], rb[s.fadeStart], worstBlend, worstStep, ownStep);

                INFO (nameOf (from) << " -> " << nameOf (to) << ", attack " << attackMs << (useSine ? ", sine" : ", carrier"));
                REQUIRE (s.fadeEnd > s.fadeStart);
                REQUIRE (worstStep <= ownStep + 0.01);
                if (useSine)
                    REQUIRE (worstBlend <= 0.5);
            }
}

TEST_CASE ("VCA: a seed starts the ballistics at the applied reduction", "[vca][switch]")
{
    // Settled 33: at Attack 300 ms the incoming detector starts at the applied 10 dB instead of
    // re-attacking from 0. The RMS mean square, run by follow(), is settled already.
    constexpr double t = -30.0;

    for (const auto d : { Detector::peak, Detector::rms })
    {
        const auto p = params (t, 0.0, 0.0, 40.0, 300.0, 1000.0, true);
        const auto level = carrier (carrierAmplitude (d, t + 10.0), samplesFor (5.0, rate));

        auto m = prepared (d);
        KeyBlock key;
        key.numChannels = 1;
        key.numSamples = samplesFor (m.settleMs (p) * 0.001, rate) + 1;
        key.channels[0] = level.data();
        m.follow (p, key);

        const double seed[] { 10.0 };
        m.seed (seed, 1);
        const auto r = runModel (m, p, level)[0];
        INFO (nameOf (d));
        REQUIRE_THAT (*std::min_element (r.begin(), r.end()), WithinAbs (10.0, 1.0e-6));
        REQUIRE_THAT (*std::max_element (r.begin(), r.end()), WithinAbs (10.0, 1.0e-6));
    }

    SECTION ("Peak Auto starts with the slow stage empty")
    {
        // Seeded at 10 dB and released at once, the network has nothing slow to give back: the
        // release is the fast stage's, as after a short burst.
        const auto p = params (t, 0.25, 0.0, 40.0, 1.0, 100.0, true);
        auto m = prepared (Detector::peak);
        const double seed[] { 10.0 };
        m.seed (seed, 1);
        const auto r = runModel (m, p, silence (samplesFor (3.0, rate)))[0];
        const auto release = releaseTime (r, 0, rate, 10.0);
        std::printf ("VCA Peak Auto released from a seed of 10 dB: %.1f ms to 36.8 %%\n", 1000.0 * release);
        REQUIRE (withinTimeGate (release, 0.100, rate));
    }
}

TEST_CASE ("VCA: reset forgets everything", "[vca][switch]")
{
    const auto n = samplesFor (3.0, rate);
    const auto [left, right] = burstyStereo (n);

    for (const auto d : { Detector::peak, Detector::rms })
    {
        auto p = params (-40.0, 0.25, 6.0, 40.0, 10.0, 300.0, true);
        p.linked = true;
        p.link = 0.7;
        auto used = prepared (d);
        (void) runModel (used, p, left, &right);
        used.reset();
        auto fresh = prepared (d);
        REQUIRE (runModel (used, p, left, &right) == runModel (fresh, p, left, &right));
        REQUIRE (isExactly (used.reduction (0), fresh.reduction (0)));
    }
}

TEST_CASE ("VCA: the output does not depend on the block size", "[vca][blocks]")
{
    // Still settings, and settings that change on given samples: the same bits at blocks of 1,
    // 7, 64, 448 and 4096 (the chain decides at chunk starts, so a change falls on one sample).
    const auto n = samplesFor (2.5, rate);
    const auto [left, right] = burstyStereo (n);
    const std::vector<int> changes { samplesFor (0.3, rate), samplesFor (0.7, rate), samplesFor (1.1, rate), samplesFor (1.5, rate), samplesFor (1.9, rate) };

    for (const auto d : { Detector::peak, Detector::rms })
        for (const auto still : { true, false })
        {
            const auto at = [&] (int pos) {
                auto p = params (-40.0, 0.25, 6.0, 40.0, 10.0, 300.0, true);
                p.linked = true;
                p.link = 1.0;
                if (still)
                    return p;
                if (pos >= changes[0])
                    p.curve.thresholdDb = -30.0;
                if (pos >= changes[1])
                    p.attackMs = 0.5;
                if (pos >= changes[2])
                    p.autoRelease = false;
                if (pos >= changes[3])
                    p.link = 0.4;
                if (pos >= changes[4])
                    p.releaseMs = 40.0;
                return p;
            };

            Trace firstLeft, firstRight;
            for (const auto block : { 1, 7, 64, 448, 4096 })
            {
                auto m = prepared (d);
                Trace r2;
                const auto r1 = runScheduled (m, left, block, changes, at, &right, &r2);
                if (firstLeft.empty())
                {
                    firstLeft = r1;
                    firstRight = r2;
                    continue;
                }
                INFO (nameOf (d) << (still ? ", still" : ", changing") << ", block " << block);
                REQUIRE (r1 == firstLeft);
                REQUIRE (r2 == firstRight);
            }
        }
}

TEST_CASE ("VCA: a knob jump neither steps nor bends the reduction", "[vca][clicks]")
{
    // As Clean's: a carrier over the default Threshold, a knob jumping between its ends every
    // 0.31 s while the reduction still releases from the last jump, at Attack 0.02 ms. The curve
    // fade keeps the slope from stepping. What is left are the detectors' own corners where a
    // release meets its target: RMS's rate limiter (10 dB per Release) and Peak Auto's rectifier
    // catching the falling capacitor; they are reported and bounded by those slopes.
    const auto period = samplesFor (0.31, rate);

    struct Jump
    {
        const char* name;
        double keyDb;
        std::function<void (ModelParams&, bool)> set;
    };
    const Jump jumps[] {
        { "Threshold -60 <-> 0", -4.0, [] (ModelParams& p, bool b) { p.curve.thresholdDb = b ? 0.0 : -60.0; } },
        { "Ratio 1 <-> inf", -4.0, [] (ModelParams& p, bool b) { p.curve.inverseRatio = b ? 0.0 : 1.0; } },
        { "Knee 0 <-> 24", -15.0, [] (ModelParams& p, bool b) { p.curve.kneeDb = b ? 24.0 : 0.0; } },
        { "Range 0 <-> 40", -4.0, [] (ModelParams& p, bool b) { p.curve.rangeDb = b ? 40.0 : 0.0; } },
    };

    for (const auto d : { Detector::peak, Detector::rms })
        for (const auto autoRelease : { false, true })
            for (const auto attackMs : { 0.02, 10.0 })
                for (const auto& jump : jumps)
                {
                    if (d == Detector::rms && autoRelease)
                        continue;

                    const auto key = carrier (carrierAmplitude (d, jump.keyDb), samplesFor (2.0, rate));
                    auto m = prepared (d);
                    const auto r = runScheduled (m, key, 64, {}, [&] (int pos) {
                        auto p = params (-18.0, 0.25, 6.0, 40.0, attackMs, 150.0, autoRelease);
                        jump.set (p, (pos / period) % 2 == 1);
                        return p;
                    });
                    // From just before the first jump: the first attack from reset is the detector's own.
                    const auto [step, corner] = roughness (r, (size_t) (period - 64));

                    // The steepest release the detector itself makes: the rate limiter, or the
                    // fast capacitor discharging the whole Range.
                    const auto ownCorner = d == Detector::rms ? 10.0 * 1000.0 / (150.0 * rate)
                                                              : (autoRelease ? 40.0 * 1000.0 / (VcaModel::peakFastMs (attackMs, 150.0) * rate) : 0.0);

                    std::printf ("VCA %s%s knob jump at Attack %g ms, %s: largest step %.4f dB, largest second difference %.2e dB\n", nameOf (d),
                                 autoRelease && d == Detector::peak ? " Auto" : "", attackMs, jump.name, step, corner);
                    INFO (nameOf (d) << ", auto " << autoRelease << ", attack " << attackMs << ", " << jump.name);
                    REQUIRE (step > 1.0e-4);
                    REQUIRE (step < 0.1);
                    REQUIRE (corner < 5.0e-4 + ownCorner);
                }
}

TEST_CASE ("VCA: time knobs and Auto release glide instead of completing a pending move at once", "[vca][clicks]")
{
    // Attack 300 → 0.02 ms mid-attack, Release 5 s → 5 ms mid-release, and Peak's Auto release
    // switched in mid-release at 6 dB, both ways: no step in the reduction.
    const auto loud = carrier (amplitudeFor (-4.0), samplesFor (1.0, rate));
    const auto jumpAt = samplesFor (0.1, rate) / 64 * 64;

    for (const auto d : { Detector::peak, Detector::rms })
    {
        auto key = loud;
        for (auto& v : key)
            v *= carrierAmplitude (d, 0.0);

        auto m = prepared (d);
        const auto r = runScheduled (m, key, 64, {}, [&] (int pos) { return params (-18.0, 0.25, 6.0, 40.0, pos >= jumpAt ? 0.02 : 300.0, 150.0); });
        const auto [step, corner] = roughness (r, (size_t) jumpAt - 64);
        std::printf ("VCA %s Attack 300 -> 0.02 ms mid-attack: reduction %.3f dB at the jump, largest step %.4f dB, second difference %.2e dB\n",
                     nameOf (d), r[(size_t) jumpAt], step, corner);
        INFO (nameOf (d));
        REQUIRE (r[(size_t) jumpAt] < 0.6 * r.back());
        REQUIRE_THAT (r.back(), WithinAbs (ref::staticReductionDb (-4.0, -18.0, 0.25, 6.0, 40.0), 0.01));
        REQUIRE (step < 0.05);
    }

    // Release 5 s → 5 ms in mid-release, after 0.5 s at 12 dB; Peak, Auto off and on.
    for (const auto autoRelease : { false, true })
    {
        auto key = carrier (amplitudeFor (ref::levelForReductionDb (12.0, -30.0, 0.25)), samplesFor (0.5, rate));
        const auto stop = (int) key.size();
        append (key, silence (samplesFor (1.0, rate)));
        const auto moveAt = stop + samplesFor (0.2, rate) / 64 * 64;
        auto m = prepared (Detector::peak);
        const auto r = runScheduled (m, key, 64, {}, [&] (int pos) { return params (-30.0, 0.25, 0.0, 40.0, 1.0, pos >= moveAt ? 5.0 : 5000.0, autoRelease); });
        const auto [step, corner] = roughness (r, (size_t) moveAt - 64);
        std::printf ("VCA Peak%s Release 5 s -> 5 ms mid-release: largest step %.4f dB, second difference %.2e dB\n", autoRelease ? " Auto" : "", step, corner);
        REQUIRE (step < 0.05);
    }

    // Auto release on and off in mid-release (NOTES.md, Gates, Clicks: Auto on/off at 6 dB with Peak).
    for (const auto attackMs : { 0.02, 10.0 })
        for (const auto turnOn : { true, false })
        {
            auto key = carrier (amplitudeFor (ref::levelForReductionDb (6.0, -30.0, 0.25)), samplesFor (2.0, rate));
            const auto stop = (int) key.size();
            append (key, silence (samplesFor (2.0, rate)));
            const auto moveAt = stop + samplesFor (0.05, rate) / 64 * 64;
            auto m = prepared (Detector::peak);
            const auto r = runScheduled (m, key, 64, {}, [&] (int pos) {
                return params (-30.0, 0.25, 0.0, 40.0, attackMs, 300.0, (pos >= moveAt) == turnOn);
            });
            const auto [step, corner] = roughness (r, (size_t) (moveAt - 64), (size_t) (moveAt + samplesFor (0.04, rate)));
            const auto [stillStep, stillCorner] = roughness (r, (size_t) (moveAt - samplesFor (0.04, rate)), (size_t) (moveAt - 64));
            std::printf ("VCA Peak Auto %s in mid-release at Attack %g ms: largest step %.5f dB, second difference %.2e dB "
                         "(before the move %.5f, %.2e)\n", turnOn ? "on" : "off", attackMs, step, corner, stillStep, stillCorner);
            REQUIRE (step < 1.5 * stillStep + 1.0e-4);
            REQUIRE (corner < 5.0e-6);
        }
}

TEST_CASE ("VCA: a key with NaNs and infinities never leaves a NaN, an infinity or a stuck state", "[vca][safety]")
{
    // The chain sanitises the key (Settled 59); the model holds without it: a NaN reads as
    // silence, an infinity as the key's limit, and the reduction recovers to that of the clean key.
    const auto n = samplesFor (8.0, rate);
    auto clean = sine (amplitudeFor (-20.0), 440.0, rate, n);
    auto dirty = clean;
    dirty[(size_t) samplesFor (0.5, rate)] = std::numeric_limits<double>::quiet_NaN();
    dirty[(size_t) samplesFor (0.6, rate)] = std::numeric_limits<double>::infinity();
    dirty[(size_t) samplesFor (0.7, rate)] = -std::numeric_limits<double>::infinity();
    dirty[(size_t) samplesFor (0.8, rate)] = 1.0e300;
    for (int i = 0; i < 64; ++i)
        dirty[(size_t) (samplesFor (0.9, rate) + i)] = std::numeric_limits<double>::quiet_NaN();

    const Signal lookahead ((size_t) n, 48.0);

    for (const auto d : { Detector::peak, Detector::rms })
        for (const auto autoRelease : { false, true })
        {
            auto p = params (-30.0, 0.25, 6.0, 40.0, 1.0, 50.0, autoRelease);
            p.linked = true;
            auto a = prepared (d), b = prepared (d);
            const auto got = runModel (a, p, dirty, &clean, 64, &lookahead);
            const auto expected = runModel (b, p, clean, &clean, 64, &lookahead);

            INFO (nameOf (d) << ", auto " << autoRelease);
            for (const auto& channel : got)
                for (const auto v : channel)
                {
                    REQUIRE (std::isfinite (v));
                    REQUIRE (v >= 0.0);
                    REQUIRE (v <= 40.0);
                }

            // RMS remembers the 10⁶ clamp for about 35 τ (150 dB at 4.343 dB per τ); Peak Auto
            // keeps a little in its slow stage for minutes.
            const auto from = (size_t) samplesFor (6.0, rate);
            auto worst = 0.0;
            for (auto i = from; i < (size_t) n; ++i)
                worst = std::max (worst, std::abs (got[0][i] - expected[0][i]));
            std::printf ("VCA %s%s after NaN and infinite keys: %.2e dB from the clean key's reduction 5 s later\n", nameOf (d),
                         autoRelease && d == Detector::peak ? " Auto" : "", worst);
            REQUIRE (worst < (d == Detector::peak && autoRelease ? 0.5 : 1.0e-3));
        }
}

namespace
{
    struct Distortion
    {
        double thdN = 0.0, thd = 0.0, third = 0.0;   // dBc
    };

    /** A 1 kHz sine at 48 kHz through the gain, 50 whole periods from `from`: THD+N (fundamental fitted), THD (2–10), H3. */
    Distortion distortionOf (const Signal& x, const Trace& r, size_t from)
    {
        constexpr int periods = 50, perPeriod = 48;
        constexpr auto n = periods * perPeriod;
        const auto w = 2.0 * std::numbers::pi / perPeriod;

        std::vector<double> y (n);
        for (int i = 0; i < n; ++i)
            y[(size_t) i] = x[from + (size_t) i] * std::pow (10.0, -r[from + (size_t) i] / 20.0);

        const auto project = [&] (int k) {
            double s = 0, c = 0;
            for (int i = 0; i < n; ++i)
            {
                s += y[(size_t) i] * std::sin (k * w * (from + (size_t) i));
                c += y[(size_t) i] * std::cos (k * w * (from + (size_t) i));
            }
            return std::pair { 2.0 * s / n, 2.0 * c / n };
        };

        const auto [a1, b1] = project (1);
        auto dc = 0.0;
        for (const auto v : y)
            dc += v;
        dc /= n;

        auto residual = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto fit = dc + a1 * std::sin (w * (from + (size_t) i)) + b1 * std::cos (w * (from + (size_t) i));
            residual += (y[(size_t) i] - fit) * (y[(size_t) i] - fit);
        }

        const auto fundamental = a1 * a1 + b1 * b1;
        auto harmonics = 0.0, third = 0.0;
        for (int k = 2; k <= 10; ++k)
        {
            const auto [a, b] = project (k);
            harmonics += a * a + b * b;
            if (k == 3)
                third = a * a + b * b;
        }

        return { 10.0 * std::log10 (residual / n / (0.5 * fundamental)), 10.0 * std::log10 (harmonics / fundamental),
                 10.0 * std::log10 (third / fundamental) };
    }
}

TEST_CASE ("VCA: distortion of a 1 kHz sine at 6 dB of reduction", "[vca][distortion]")
{
    // NOTES.md, Gates, Distortion; Settled 71: Ratio 4, hard knee, 6 dB of static reduction, the
    // fundamental fitted over 50 whole periods after 25 attack constants (and the release's
    // settling). Peak: THD+N ≤ −80 dBc at Attack ≥ 10 ms and Release ≥ 2 × Attack. RMS: the third
    // harmonic within ±1 dB of the single-pole ripple formula at Attack 15.79 and 50 ms, THD+N ≤
    // −80 dBc at Attack ≥ 100 ms, Release at 5.07 × Attack (the detector alone, one time constant).
    constexpr double t = -30.0;
    const auto level = ref::levelForReductionDb (6.0, t, 0.25);

    struct Row
    {
        double attackMs, releaseMs;
        bool autoRelease;
        double m0Thd;
    };
    const Row peakRows[] {
        { 10.0, 20.0, false, -87.3 }, { 10.0, 50.0, false, -100.0 }, { 10.0, 100.0, false, -107.2 }, { 10.0, 150.0, false, -111.1 },
        { 10.0, 500.0, false, std::nan ("") }, { 30.0, 100.0, false, -114.0 }, { 100.0, 200.0, false, std::nan ("") },
        { 100.0, 1000.0, false, std::nan ("") }, { 10.0, 100.0, true, -97.8 }, { 30.0, 300.0, true, std::nan ("") },
        { 1.0, 50.0, false, std::nan ("") }, { 3.0, 100.0, false, std::nan ("") }, { 10.0, 10.0, false, std::nan ("") },
        { 1.0, 100.0, true, std::nan ("") },
    };

    for (const auto& row : peakRows)
    {
        const auto settle = std::max (25.0 * row.attackMs, 5.0 * row.releaseMs) * 0.001 + (row.autoRelease ? 10.0 : 0.0);
        const auto from = (size_t) samplesFor (settle, rate) / 48 * 48 + 48;
        const auto key = sine (amplitudeFor (level), 1000.0, rate, (int) from + 2400);
        auto m = prepared (Detector::peak);
        const auto r = runModel (m, params (t, 0.25, 0.0, 40.0, row.attackMs, row.releaseMs, row.autoRelease), key, nullptr, 512)[0];
        const auto dist = distortionOf (key, r, from);
        const auto gated = row.attackMs >= 10.0 && row.releaseMs >= 2.0 * row.attackMs;

        std::printf ("VCA Peak%s distortion, Attack %g ms, Release %g ms: THD+N %.1f dBc, THD %.1f dBc (M0 THD %.1f)%s\n",
                     row.autoRelease ? " Auto" : "", row.attackMs, row.releaseMs, dist.thdN, dist.thd, row.m0Thd, gated ? "" : " (reported)");
        if (gated)
        {
            INFO ("attack " << row.attackMs << ", release " << row.releaseMs << ", auto " << row.autoRelease);
            REQUIRE (dist.thdN <= -80.0);
        }
    }

    for (const auto ratio : { 4.0, 0.0 })
        for (const auto attackMs : { ref::markAttackMs, 50.0, 100.0, 300.0 })
            for (const auto atFloor : { true, false })
            {
                const auto inverseRatio = ratio > 0.0 ? 1.0 / ratio : 0.0;
                const auto releaseMs = atFloor ? 5.0667 * attackMs : 1000.0;
                const auto rmsLevel = ref::levelForReductionDb (6.0, t, inverseRatio);
                const auto settle = 25.0 * ref::rmsTimeConstantMs (attackMs) * 0.001;
                const auto from = (size_t) samplesFor (settle, rate) / 48 * 48 + 48;
                const auto key = sine (amplitudeFor (rmsLevel), 1000.0, rate, (int) from + 2400);
                auto m = prepared (Detector::rms);
                const auto r = runModel (m, params (t, inverseRatio, 0.0, 40.0, attackMs, releaseMs), key, nullptr, 512)[0];
                const auto dist = distortionOf (key, r, from);
                const auto formula = ref::rmsThirdHarmonicDbc (attackMs, inverseRatio, 1000.0);
                const auto gated = ratio > 0.0 && atFloor;

                std::printf ("VCA RMS distortion, Ratio %g, Attack %g ms, Release %g ms: THD+N %.1f dBc, THD %.1f dBc, H3 %.1f dBc (formula %.1f)%s\n",
                             ratio, attackMs, releaseMs, dist.thdN, dist.thd, dist.third, formula, gated ? "" : " (reported)");
                if (! gated)
                    continue;

                INFO ("attack " << attackMs);
                if (attackMs < 100.0)
                    REQUIRE_THAT (dist.third, WithinAbs (formula, 1.0));
                else
                    REQUIRE (dist.thdN <= -80.0);
            }
}
