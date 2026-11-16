// The chain around the shapes (PLAN.md, Reduction, Latency, Signal flow): latency reported equals
// latency measured, any block size, stereo modes, the side chain, Listen, the Shape crossfade,
// Bypass and a change of latency.

#include "ChainRun.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>

using namespace halite;
using namespace halite::testing;

namespace
{
    /** A voice with esses every 0.4 s and a resonance in the Harsh band; the right channel quieter. */
    Channels voiceWithEsses (double rate, double seconds, unsigned seed = 1)
    {
        const auto n = samplesFor (seconds, rate);
        auto voice = harmonic ({ .f0 = 160.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -9.0, .peakHz = 3200.0, .peakDb = 18.0, .rms = 0.1, .seed = seed }, n, rate);
        for (auto t = 0.2; t + 0.15 < seconds; t += 0.4)
            voice = sum (voice, gated (bandNoise (4500.0, 12000.0, n, rate, seed + (unsigned) (t * 100.0), 0.04), samplesFor (t, rate),
                                       samplesFor (t + 0.15, rate), rate));
        return { voice, scaled (voice, 0.7) };
    }

    /** The lag of the largest correlation between out and in over [0, maxLag], from `from` on. */
    int measuredLag (const Samples& out, const Samples& in, int maxLag, int from)
    {
        auto best = -1.0e300;
        auto lag = -1;
        const auto to = (int) out.size();
        for (int l = 0; l <= maxLag; ++l)
        {
            auto c = 0.0;
            for (int i = from + l; i < to; ++i)
                c += (double) out[(size_t) i] * in[(size_t) (i - l)];
            if (c > best)
            {
                best = c;
                lag = l;
            }
        }
        return lag;
    }

    double largestDifference (const Samples& a, const Samples& b, int from = 0, int to = -1)
    {
        to = to < 0 ? (int) a.size() : to;
        auto d = 0.0;
        for (int i = from; i < to; ++i)
            d = std::max (d, (double) std::abs (a[(size_t) i] - b[(size_t) i]));
        return d;
    }

    float raisedCosine (int i, int length)
    {
        return i <= 0 ? 0.0f : (i >= length ? 1.0f : (float) (0.5 - 0.5 * std::cos (std::numbers::pi * i / length)));
    }
}

TEST_CASE ("Latency: reported equals measured, every shape and rate, Normal and Live", "[latency]")
{
    // White noise is sibilant from start to end: the reduction acts while the delay is measured.
    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (const auto latency : { LatencyMode::normal, LatencyMode::live })
            for (const auto shape : { Shape::split, Shape::spectral, Shape::wide })
            {
                const auto n = samplesFor (0.15, rate);
                std::mt19937 rng (5u);
                std::normal_distribution<float> gauss (0.0f, 0.1f);
                Samples in ((size_t) n);
                for (auto& v : in)
                    v = gauss (rng);

                ChainRun run;
                run.rate = rate;
                run.settings.shape = shape;
                run.settings.detector.latency = latency;
                const auto out = runChain ({ in }, run)[0];

                const auto reported = dsp::HaliteChain::latencySamples (latency, rate);
                INFO (rate << " Hz, " << (latency == LatencyMode::live ? "Live" : "Normal") << ", shape " << (int) shape);
                CHECK (measuredLag (out, in, 2 * dsp::HaliteChain::frameSize (rate), samplesFor (0.05, rate)) == reported);
            }
}

TEST_CASE ("Latency: the frame holds the longest look-ahead and a hop at every rate", "[latency]")
{
    // Normal reads each sample's hop up to look-ahead + 31 samples after it arrives (NOTES.md, Timing).
    for (const auto rate : { 22050.0, 32000.0, 44100.0, 48000.0, 64000.0, 88200.0, 96000.0, 176400.0, 192000.0, 384000.0 })
    {
        const auto frame = dsp::HaliteChain::frameSize (rate);
        INFO (rate << " Hz: frame " << frame);
        CHECK ((frame & (frame - 1)) == 0);
        CHECK (frame >= dsp::Detector::lookaheadHopsLimit (rate) * dsp::Detector::hopSize + dsp::Detector::hopSize - 1);
        // 10.7 ms at the 48 kHz family, 11.6 at 44.1; 16 ms where the look-ahead forces a doubling.
        CHECK (1000.0 * frame / rate >= 10.6);
        CHECK (1000.0 * frame / rate <= 16.1);
    }
    CHECK (dsp::HaliteChain::frameSize (48000.0) == 512);
    CHECK (dsp::HaliteChain::frameSize (44100.0) == 512);
    CHECK (dsp::HaliteChain::frameSize (96000.0) == 1024);
    CHECK (dsp::HaliteChain::frameSize (192000.0) == 2048);
}

TEST_CASE ("Blocks: any block size gives the same output, bit for bit", "[blocks]")
{
    const auto rate = 44100.0;
    const auto in = voiceWithEsses (rate, 1.3);

    for (const auto shape : { Shape::split, Shape::wide, Shape::spectral })
        for (const auto latency : { LatencyMode::normal, LatencyMode::live })
        {
            ChainRun run;
            run.rate = rate;
            run.settings.shape = shape;
            run.settings.detector.latency = latency;
            run.settings.detector.harshOn = true;
            run.settings.detector.stereo = StereoMode::unlinked;

            // 10 ms of look-ahead: Spectral reads the newest sample the detector has surely reduced.
            for (const auto lookahead : { 5.0f, 10.0f })
            {
                run.settings.detector.lookaheadMs = lookahead;
                run.blocks = { 512 };
                const auto reference = runChain (in, run);
                for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 32 }, { 33 }, { 4096 }, { 17, 999, 1, 4096, 256, 2 } })
                {
                    run.blocks = sizes;
                    const auto out = runChain (in, run);
                    INFO ("shape " << (int) shape << ", Live " << (latency == LatencyMode::live) << ", look-ahead " << lookahead << ", first block "
                                   << sizes.front());
                    for (size_t c = 0; c < 2; ++c)
                        CHECK (firstDifference (out[c], reference[c], 0) == -1);
                }
            }
        }
}

TEST_CASE ("Stereo: Linked, Unlinked, Mid and Side", "[stereo]")
{
    const auto rate = 48000.0;
    const auto n = samplesFor (1.2, rate);
    const auto voice = vowel (150.0, n, rate, 0.1, 1);
    const auto ess = gated (bandNoise (4500.0, 12000.0, n, rate, 3, 0.05), samplesFor (0.5, rate), samplesFor (0.7, rate), rate);
    const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);

    for (const auto shape : { Shape::split, Shape::wide, Shape::spectral })
    {
        INFO ("shape " << (int) shape);
        ChainRun run;
        run.rate = rate;
        run.settings.shape = shape;

        SECTION ("Linked: one reduction for both channels (Spectral: one set of gains)")
        {
            const auto left = sum (voice, ess);
            const Channels in { left, scaled (left, 0.5) };
            const auto out = runChain (in, run);
            REQUIRE (firstDifference (out[0], in[0], delay) != -1);
            // Halving is exact in floating point, so one gain on both gives exactly half.
            CHECK (firstDifference (out[1], scaled (out[0], 0.5), 0) == -1);
        }

        SECTION ("Unlinked: the channel without an ess stays untouched")
        {
            run.settings.detector.stereo = StereoMode::unlinked;
            const Channels in { sum (voice, ess), voice };
            const auto out = runChain (in, run);
            CHECK (firstDifference (out[0], in[0], delay) != -1);
            CHECK (firstDifference (out[1], in[1], delay) == -1);
        }

        SECTION ("Mid and Side reduce only their component")
        {
            // The ess only in the side: Mid hears nothing; Side reduces it and leaves the mid alone.
            const Channels in { sum (voice, ess), sum (voice, scaled (ess, -1.0)) };

            run.settings.detector.stereo = StereoMode::mid;
            auto out = runChain (in, run);
            CHECK (firstDifference (out[0], in[0], delay) == -1);
            CHECK (firstDifference (out[1], in[1], delay) == -1);

            run.settings.detector.stereo = StereoMode::side;
            out = runChain (in, run);
            Samples midIn (in[0].size()), midOut (in[0].size()), sideIn (in[0].size()), sideOut (in[0].size());
            for (size_t i = 0; i < midIn.size(); ++i)
            {
                midIn[i] = 0.5f * (in[0][i] + in[1][i]);
                midOut[i] = 0.5f * (out[0][i] + out[1][i]);
                sideIn[i] = 0.5f * (in[0][i] - in[1][i]);
                sideOut[i] = 0.5f * (out[0][i] - out[1][i]);
            }
            CHECK (largestDifference (midOut, delayed (midIn, delay)) <= 1.0e-7);
            CHECK (largestDifference (sideOut, delayed (sideIn, delay)) > 0.01);
        }
    }
}

TEST_CASE ("Stereo: a change crossfades over 30 ms, each lane cutting by its own mode's reductions", "[stereo][shape]")
{
    // Wide, an external key with a steady ess on the left only, far over the threshold, so Range
    // holds every reduction at 8 dB. Linked cuts both channels by it, Unlinked only the left. The
    // detector's reductions mean something else in each mode; read by the other lane they would cut
    // the right by 8 dB within a hop or two, mid-fade (the M2 review's finding).
    const auto rate = 48000.0;
    const auto n = samplesFor (1.2, rate);
    const auto at = samplesFor (0.6, rate) / 256 * 256;
    const auto fade = samplesFor (dsp::HaliteChain::fadeSeconds, rate);
    const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
    const Channels in { sine (330.0, n, rate, 0.25), sine (330.0, n, rate, 0.2, 1.1) };
    const Channels key { sum (vowel (150.0, n, rate, 0.05, 1), bandNoise (4500.0, 12000.0, n, rate, 3, 0.3)), vowel (150.0, n, rate, 0.002, 2) };
    const auto depth = 1.0f - std::pow (10.0f, -8.0f / 20.0f);

    for (const auto& [from, to] : std::vector<std::pair<StereoMode, StereoMode>> { { StereoMode::unlinked, StereoMode::linked },
                                                                                  { StereoMode::linked, StereoMode::unlinked } })
    {
        ChainRun run;
        run.rate = rate;
        run.settings.shape = Shape::wide;
        run.settings.detector.key = KeySource::external;
        run.settings.detector.detection = Detection::allround;
        run.settings.detector.thresholdAbsDb = -36.0f;
        run.key = &key;
        run.blocks = { 256 };
        run.automate = [&] (int pos, dsp::ChainSettings& s) { s.detector.stereo = pos >= at ? to : from; };
        const auto out = runChain (in, run);

        // The left: cut by Range throughout, the same in both lanes.
        auto worstLeft = 0.0;
        for (int i = samplesFor (0.3, rate); i < n; ++i)
            worstLeft = std::max (worstLeft, std::abs ((double) out[0][(size_t) i] - in[0][(size_t) (i - delay)] * (1.0 - depth)));

        // The right: from its old cut to its new one along the raised cosine, starting once the new
        // mode's reductions reach the output (the look-ahead earlier than the audio).
        const auto rightBefore = from == StereoMode::linked ? depth : 0.0f, rightAfter = to == StereoMode::linked ? depth : 0.0f;
        auto moved = -1;
        for (int i = at; i < n && moved < 0; ++i)
            if (std::abs (out[1][(size_t) i] - in[1][(size_t) (i - delay)] * (1.0f - rightBefore)) > 1.0e-6f)
                moved = i;
        REQUIRE (moved >= 0);
        const auto errorFrom = [&] (int start)
        {
            auto worst = 0.0;
            for (int i = samplesFor (0.3, rate); i < n; ++i)
            {
                const auto w = raisedCosine (i - start, fade);
                const auto cut = rightBefore + w * (rightAfter - rightBefore);
                worst = std::max (worst, std::abs ((double) out[1][(size_t) i] - in[1][(size_t) (i - delay)] * (1.0 - cut)));
            }
            return worst;
        };
        // A raised cosine starts flat: its first sample is found by fitting, a few samples before it shows.
        auto start = moved;
        auto worstRight = errorFrom (moved);
        for (int s = moved - 64; s < moved; ++s)
            if (const auto e = errorFrom (s); e < worstRight)
            {
                worstRight = e;
                start = s;
            }
        INFO ((int) from << " -> " << (int) to << ": the right's fade starts " << start - at << " samples after the change; left within "
                         << worstLeft << ", right within " << worstRight << " of the raised cosine");
        // Not before the new mode's first reductions reach the output, the 5 ms look-ahead earlier than the audio.
        CHECK (start - at >= delay - (int) std::lround (5.0e-3 * rate / 32.0) * 32);
        CHECK (start - at <= delay + 256);
        CHECK (worstLeft <= 1.0e-6);
        CHECK (worstRight <= 1.0e-6);
    }
}

TEST_CASE ("Input: the shapes take NaN and Inf as 0, and cut a hot signal as any other", "[input]")
{
    // An external key holds 8 dB (Allround), so the main signal's level changes nothing but itself.
    const auto rate = 48000.0;
    const auto n = samplesFor (1.0, rate);
    const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
    const Channels key { sum (vowel (150.0, n, rate, 0.05, 1), bandNoise (4500.0, 12000.0, n, rate, 3, 0.3)) };
    const auto cold = sum (sine (7000.0, n, rate, 0.5), bandNoise (4500.0, 12000.0, n, rate, 9, 0.05));

    for (const auto shape : { Shape::split, Shape::wide, Shape::spectral })
    {
        INFO ("shape " << (int) shape);
        ChainRun run;
        run.rate = rate;
        run.settings.shape = shape;
        run.settings.detector.key = KeySource::external;
        run.settings.detector.detection = Detection::allround;
        run.settings.detector.thresholdAbsDb = -36.0f;
        run.key = &key;

        // +26 dBFS peaks: what it removes scales with the signal (the shapes are linear in it).
        const auto hot = scaled (cold, 40.0);
        const auto quiet = runChain ({ cold }, run)[0], loud = runChain ({ hot }, run)[0];
        auto error = 0.0, removed = 0.0;
        for (int i = samplesFor (0.5, rate); i < n; ++i)
        {
            const auto r = 40.0 * ((double) cold[(size_t) (i - delay)] - quiet[(size_t) i]);
            const auto e = ((double) hot[(size_t) (i - delay)] - loud[(size_t) i]) - r;
            error += e * e;
            removed += r * r;
        }
        const auto errorDb = 10.0 * std::log10 (std::max (error, 1.0e-300) / removed);
        INFO ("removed at +26 dBFS against 40 x removed at -6 dBFS: " << errorDb << " dB");
        CHECK (removed > 0.0);
        CHECK (errorDb <= -100.0);

        // NaN and Inf: the dry path passes them on, the shapes and the detector read 0.
        auto broken = cold, zeroed = cold;
        const std::array<int, 3> bad { samplesFor (0.6, rate), samplesFor (0.6, rate) + 1, samplesFor (0.7, rate) };
        broken[(size_t) bad[0]] = std::numeric_limits<float>::quiet_NaN();
        broken[(size_t) bad[1]] = std::numeric_limits<float>::infinity();
        broken[(size_t) bad[2]] = -std::numeric_limits<float>::infinity();
        for (const auto i : bad)
            zeroed[(size_t) i] = 0.0f;
        const auto a = runChain ({ broken }, run)[0], b = runChain ({ zeroed }, run)[0];
        auto others = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto isBad = std::find (bad.begin(), bad.end(), i - delay) != bad.end();
            if (isBad)
                CHECK (! std::isfinite (a[(size_t) i]));
            else
                others += sameBits (a[(size_t) i], b[(size_t) i]) ? 0 : 1;
        }
        CHECK (others == 0);
    }
}

TEST_CASE ("Key: the side chain decides, and a missing bus falls back to the input", "[key]")
{
    const auto rate = 48000.0;
    const auto n = samplesFor (1.2, rate);
    const auto voice = vowel (150.0, n, rate, 0.1, 1);
    const auto ess = gated (bandNoise (4500.0, 12000.0, n, rate, 3, 0.05), samplesFor (0.5, rate), samplesFor (0.7, rate), rate);
    const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
    const Channels plain { voice }, sibilant { sum (voice, ess) };

    ChainRun run;
    run.rate = rate;
    run.settings.shape = Shape::wide;

    // External: the esses on the key reduce the plain voice on the main bus, where they are not.
    run.settings.detector.key = KeySource::external;
    run.key = &sibilant;
    auto out = runChain (plain, run);
    const auto from = samplesFor (0.55, rate) + delay, to = samplesFor (0.65, rate) + delay;
    CHECK (rmsOf (out[0], from, to) < 0.7 * rmsOf (delayed (voice, delay), from, to));

    // Internal: the main bus is the key, and it has no esses.
    run.settings.detector.key = KeySource::internal;
    out = runChain (plain, run);
    CHECK (firstDifference (out[0], voice, delay) == -1);

    // External without a side chain: the main bus is the key, as Internal.
    run.settings.detector.key = KeySource::internal;
    run.key = nullptr;
    const auto internal = runChain (sibilant, run);
    run.settings.detector.key = KeySource::external;
    const auto fallback = runChain (sibilant, run);
    CHECK (firstDifference (internal[0], sibilant[0], delay) != -1);
    CHECK (firstDifference (fallback[0], internal[0], 0) == -1);
}

TEST_CASE ("Listen: Output, Removed, each band's part, and what each detector hears", "[listen]")
{
    const auto rate = 48000.0;
    const auto in = voiceWithEsses (rate, 1.0);
    const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);

    for (const auto shape : { Shape::split, Shape::wide, Shape::spectral })
    {
        ChainRun run;
        run.rate = rate;
        run.settings.shape = shape;
        run.settings.detector.harshOn = true;
        run.settings.detector.harshThresholdDb = -20.0f;

        const auto heard = [&] (Listen l)
        {
            run.settings.listen = l;
            return runChain (in, run);
        };
        const auto output = heard (Listen::output), removed = heard (Listen::removed);
        const auto essRemoved = heard (Listen::essRemoved), harshRemoved = heard (Listen::harshRemoved);
        const auto essTrigger = heard (Listen::essTrigger), harshTrigger = heard (Listen::harshTrigger);

        // The detectors' bands as they hear the key (each channel its own path, Linked), delayed.
        const auto bands = dsp::Detector::bandsFor (run.settings.detector, rate);
        INFO ("shape " << (int) shape);
        for (size_t c = 0; c < 2; ++c)
        {
            Samples sumBack (in[c].size()), parts (in[c].size()), essBand (in[c].size()), harshBand (in[c].size());
            dsp::BandFilter e, h;
            e.setBand (bands.essLow, bands.essHigh, rate);
            h.setBand (bands.harshLow, bands.harshHigh, rate);
            for (size_t i = 0; i < sumBack.size(); ++i)
            {
                sumBack[i] = output[c][i] + removed[c][i];
                parts[i] = essRemoved[c][i] + harshRemoved[c][i];
                essBand[i] = (float) e.process ((double) in[c][i]);
                harshBand[i] = (float) h.process ((double) in[c][i]);
            }

            CHECK (rmsOf (essRemoved[c]) > 1.0e-3);
            CHECK (rmsOf (harshRemoved[c]) > 1.0e-4);
            CHECK (largestDifference (sumBack, delayed (in[c], delay)) <= 1.0e-6);
            CHECK (firstDifference (removed[c], parts, 0) == -1);
            CHECK (firstDifference (essTrigger[c], essBand, delay) == -1);
            CHECK (firstDifference (harshTrigger[c], harshBand, delay) == -1);
        }
    }
}

TEST_CASE ("Shape: a change crossfades over 30 ms with both shapes running", "[shape]")
{
    const auto rate = 48000.0;
    const auto in = voiceWithEsses (rate, 1.4);
    const auto at = samplesFor (0.6, rate) / 512 * 512;    // the first block with the new Shape
    const auto fade = samplesFor (dsp::HaliteChain::fadeSeconds, rate);

    for (const auto& [from, to] : std::vector<std::pair<Shape, Shape>> { { Shape::split, Shape::wide }, { Shape::wide, Shape::split },
                                                                        { Shape::spectral, Shape::wide }, { Shape::spectral, Shape::split } })
    {
        ChainRun run;
        run.rate = rate;
        run.settings.shape = from;
        const auto before = runChain (in, run);
        run.settings.shape = to;
        const auto after = runChain (in, run);

        run.settings.shape = from;
        run.automate = [&] (int pos, dsp::ChainSettings& s) { s.shape = pos >= at ? to : from; };
        const auto switched = runChain (in, run);

        INFO ((int) from << " -> " << (int) to);
        for (size_t c = 0; c < 2; ++c)
        {
            // Before: the old shape exactly. During: both, mixed by a raised cosine. After: the new
            // shape, which started from rest at the switch (Wide has no state; Split's filters
            // settle within the fade).
            CHECK (firstDifference (switched[c], before[c], 0) >= at);
            auto worst = 0.0;
            for (int i = at; i < at + fade; ++i)
            {
                const auto w = raisedCosine (i - at, fade);
                const auto expected = before[c][(size_t) i] + w * (after[c][(size_t) i] - before[c][(size_t) i]);
                worst = std::max (worst, (double) std::abs (switched[c][(size_t) i] - expected));
            }
            if (to == Shape::wide)
                CHECK (worst <= 1.0e-6);
            CHECK (largestDifference (switched[c], after[c], at + fade + 2 * 512) <= (to == Shape::wide ? 1.0e-6 : 1.0e-4));
        }
    }
}

TEST_CASE ("Shape: a change into Spectral fades in a lane that runs as if it had run all along, once its target has learned", "[shape][spectral]")
{
    // The new lane is handed the frame of input before the change and runs unheard until its first
    // frame's overlap-add is complete (the old shape plays on alone, exactly); then it fades in. It
    // learns from its warm start: on the timeline's grid, its target is the one of a Spectral render
    // from the start once 2 s of non-sibilant frames have passed.
    const auto rate = 48000.0;
    const auto in = voiceWithEsses (rate, 8.0);
    const auto at = samplesFor (0.8, rate) / 512 * 512;
    const auto frame = dsp::HaliteChain::frameSize (rate);
    const auto fade = samplesFor (dsp::HaliteChain::fadeSeconds, rate);
    const auto wanted = (int) std::lround (dsp::SpectralAnalysis::learnSeconds * rate / 128.0) + dsp::SpectralAnalysis::blockFrames;

    for (const auto from : { Shape::split, Shape::wide })
    {
        ChainRun run;
        run.rate = rate;
        run.settings.shape = from;
        const auto before = runChain (in, run);
        run.settings.shape = Shape::spectral;
        const auto spectral = runChain (in, run);

        // When the new lane has learned 2 s of frames and a block more (its first block is partial).
        dsp::HaliteChain chain;
        chain.prepare (rate, 4096, 2, LatencyMode::normal);
        auto warm = -1;
        run.automate = [&] (int pos, dsp::ChainSettings& s)
        {
            s.shape = pos >= at ? Shape::spectral : from;
            if (warm < 0 && pos > at && chain.runningLane().spectralShape().analysis (0).framesLearned() >= wanted)
                warm = pos;
        };
        const auto switched = runChain (in, run, &chain);
        REQUIRE (warm > 0);

        INFO ((int) from << " -> Spectral, warm " << (warm - at) / rate << " s after the change");
        for (size_t c = 0; c < 2; ++c)
        {
            const auto first = firstDifference (switched[c], before[c], 0);
            const auto after = at + frame + fade;
            Samples a (spectral[c].begin() + after, spectral[c].begin() + after + samplesFor (0.2, rate));
            Samples b (switched[c].begin() + after, switched[c].begin() + after + samplesFor (0.2, rate));
            const auto early = largestDifference (a, b);
            const auto late = largestDifference (switched[c], spectral[c], warm);
            INFO ("the old shape alone until " << first - at << " samples after the change; right after the fade within " << early
                                               << ", once warm within " << late);
            CHECK (first >= at + frame);
            CHECK (early <= 0.02);
            CHECK (late <= 1.0e-6);
        }
    }
}

TEST_CASE ("Bypass: the latency-aligned input, crossfaded over 20 ms", "[bypass]")
{
    const auto rate = 48000.0;
    const auto in = voiceWithEsses (rate, 1.2);
    const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
    const auto at = samplesFor (0.4, rate) / 512 * 512;
    const auto fade = samplesFor (dsp::HaliteChain::bypassSeconds, rate);

    ChainRun run;
    run.rate = rate;
    run.settings.shape = Shape::wide;
    const auto processed = runChain (in, run);
    run.automate = [&] (int pos, dsp::ChainSettings& s) { s.bypass = pos >= at; };
    const auto bypassed = runChain (in, run);

    for (size_t c = 0; c < 2; ++c)
    {
        const auto dry = delayed (in[c], delay);
        REQUIRE (firstDifference (processed[c], in[c], delay) != -1);
        CHECK (firstDifference (bypassed[c], processed[c], 0) >= at);
        auto worst = 0.0;
        for (int i = at; i < at + fade; ++i)
        {
            const auto w = raisedCosine (i - at, fade);
            worst = std::max (worst, (double) std::abs (bypassed[c][(size_t) i] - (processed[c][(size_t) i] + w * (dry[(size_t) i] - processed[c][(size_t) i]))));
        }
        CHECK (worst <= 1.0e-6);
        for (int i = at + fade; i < (int) dry.size(); ++i)
            REQUIRE (sameBits (bypassed[c][(size_t) i], dry[(size_t) i]));
    }
}

TEST_CASE ("Latency: a change fades out, switches and fades in", "[latency]")
{
    const auto rate = 48000.0;
    const auto n = samplesFor (1.0, rate);
    const Channels in { vowel (150.0, n, rate, 0.1, 1) };
    const auto at = samplesFor (0.4, rate) / 512 * 512;
    const auto fade = samplesFor (dsp::HaliteChain::latencyFadeSeconds, rate);

    const auto frame = dsp::HaliteChain::frameSize (rate);

    for (const auto shape : { Shape::split, Shape::spectral })
        for (const auto& [from, to] : std::vector<std::pair<LatencyMode, LatencyMode>> { { LatencyMode::normal, LatencyMode::live },
                                                                                        { LatencyMode::live, LatencyMode::normal } })
        {
            ChainRun run;
            run.rate = rate;
            run.settings.shape = shape;
            run.settings.detector.latency = from;
            run.automate = [&] (int pos, dsp::ChainSettings& s) { s.detector.latency = pos >= at ? to : from; };
            const auto out = runChain (in, run)[0];

            const auto before = dsp::HaliteChain::latencySamples (from, rate), after = dsp::HaliteChain::latencySamples (to, rate);
            auto lastSilent = at + fade;
            for (int i = at + fade; i < at + 4096; ++i)
                if (juce::exactlyEqual (out[(size_t) i], 0.0f))
                    lastSilent = i;
            INFO ((shape == Shape::spectral ? "Spectral, " : "Split, ") << (from == LatencyMode::live ? "Live" : "Normal") << " -> "
                                                                       << (to == LatencyMode::live ? "Live" : "Normal") << ": silent until "
                                                                       << lastSilent - at << " samples after the change");
            for (int i = 0; i < at; ++i)
                REQUIRE (sameBits (out[(size_t) i], i >= before ? in[0][(size_t) (i - before)] : 0.0f));
            // Out over 10 ms, silent for the rest of that chunk, in over 10 ms, then the new delay.
            // Spectral comes back once its first frame is complete (it cannot run in Live).
            CHECK (juce::exactlyEqual (out[(size_t) (at + fade)], 0.0f));
            if (shape == Shape::spectral && to == LatencyMode::normal)
                CHECK (lastSilent >= at + fade + frame);
            else
                CHECK (lastSilent < at + fade + 256);
            for (int i = lastSilent + fade + 256; i < n; ++i)
                REQUIRE (sameBits (out[(size_t) i], in[0][(size_t) (i - after)]));
        }
}

TEST_CASE ("Latency: a change taken back before the audio has faded out fades back in", "[latency]")
{
    const auto rate = 48000.0;
    const auto n = samplesFor (0.6, rate);
    const Channels in { vowel (150.0, n, rate, 0.1, 1) };
    const auto at = samplesFor (0.3, rate) / 64 * 64;
    const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);

    ChainRun run;
    run.rate = rate;
    run.blocks = { 64 };
    run.automate = [&] (int pos, dsp::ChainSettings& s) { s.detector.latency = pos >= at && pos < at + 128 ? LatencyMode::live : LatencyMode::normal; };
    const auto out = runChain (in, run)[0];
    for (int i = at + samplesFor (0.05, rate); i < n; ++i)
        REQUIRE (sameBits (out[(size_t) i], in[0][(size_t) (i - delay)]));
}

TEST_CASE ("Timing: the gain follows the detector's reductions hop by hop, on the audio's clock", "[latency][timing]")
{
    // The key is a voice with esses (external); the main input is a constant 0.5, so in Wide the
    // depth is exactly 2 × removed. At the last sample of input hop j the quadratic spline is at
    // the mean of hops j − 1 and j (Normal), or of j − 2 and j − 1 (Live, which reads a hop late).
    // The probe's reductions are moved back by the look-ahead to the hop they act on, so a
    // reduction early or late by one hop fails.
    const auto rate = 48000.0;
    const auto n = samplesFor (1.2, rate);
    const auto voice = vowel (150.0, n, rate, 0.1, 1);
    Samples sibilant = voice;
    for (auto t = 0.3; t < 1.0; t += 0.35)
        sibilant = sum (sibilant, gated (bandNoise (4500.0, 12000.0, n, rate, (unsigned) (t * 100.0), 0.03), samplesFor (t, rate),
                                         samplesFor (t + 0.12, rate), rate));
    const Channels key { sibilant };
    const Channels in { Samples ((size_t) n, 0.5f) };

    for (const auto latency : { LatencyMode::normal, LatencyMode::live })
        for (const auto lookaheadMs : { 0.0f, 5.0f, 10.0f })
        {
            if (latency == LatencyMode::live && lookaheadMs > 0.0f)
                continue;

            ChainRun run;
            run.rate = rate;
            run.settings.shape = Shape::wide;
            run.settings.detector.key = KeySource::external;
            run.settings.detector.latency = latency;
            run.settings.detector.lookaheadMs = lookaheadMs;
            run.settings.listen = Listen::removed;
            run.key = &key;
            run.blocks = { 300, 1, 77 };
            const auto removed = runChain (in, run)[0];
            const auto delay = dsp::HaliteChain::latencySamples (latency, rate);

            auto detectorSettings = run.settings.detector;
            const auto probe = probeOf (sibilant, rate, detectorSettings);
            const auto depthOf = [&] (int hop) { return hop < 0 ? 0.0f : dsp::ReductionTrack::depthOf (probe.hops[(size_t) hop].reductions[0].essDb); };
            const auto lag = latency == LatencyMode::live ? 1 : 0;

            auto worst = 0.0, largest = 0.0;
            for (int hop = 2; (hop + 1) * 32 + delay <= n && hop < (int) probe.hops.size(); ++hop)
            {
                const auto end = (hop + 1) * 32 - 1;
                const auto applied = 2.0 * removed[(size_t) (end + delay)];
                const auto expected = 0.5 * (depthOf (hop - 1 - lag) + depthOf (hop - lag));
                worst = std::max (worst, std::abs (applied - expected));
                largest = std::max (largest, applied);
            }
            INFO ((latency == LatencyMode::live ? "Live" : "Normal") << ", look-ahead " << lookaheadMs << " ms: largest depth " << largest
                                                                    << ", worst difference " << worst);
            CHECK (largest > 0.5);
            CHECK (worst <= 1.0e-6);
        }
}

TEST_CASE ("Shape: Spectral runs in Normal; in Live it falls back to Split, and the status says why", "[shape]")
{
    const auto rate = 48000.0;
    const auto in = voiceWithEsses (rate, 0.6);

    for (const auto latency : { LatencyMode::normal, LatencyMode::live })
    {
        ChainRun run;
        run.rate = rate;
        run.settings.detector.latency = latency;
        run.settings.shape = Shape::split;
        const auto split = runChain (in, run);

        run.settings.shape = Shape::spectral;
        dsp::HaliteChain chain;
        chain.prepare (rate, 512, 2, latency);
        const auto spectral = runChain (in, run, &chain);

        const auto live = latency == LatencyMode::live;
        INFO ((live ? "Live" : "Normal"));
        CHECK (chain.status().shape.load() == (int) (live ? Shape::split : Shape::spectral));
        CHECK (chain.status().fallback.load() == (int) (live ? dsp::ShapeFallback::live : dsp::ShapeFallback::none));
        CHECK (chain.status().latencySamples.load() == dsp::HaliteChain::latencySamples (latency, rate));
        for (size_t c = 0; c < 2; ++c)
        {
            if (live)
                CHECK (firstDifference (spectral[c], split[c], 0) == -1);
            else
                CHECK (firstDifference (spectral[c], split[c], 0) != -1);
        }
    }
}
