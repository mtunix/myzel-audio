// The Spectral shape (PLAN.md, Reduction, Spectral; src/dsp/shapes/spectral/NOTES.md): the Stft
// and its exact zero path, the frames' gains, the learned target and its warm start, the Harsh
// line, the timeline grids (Warm-up gate) and a host transport that starts or jumps.

#include "ChainRun.h"
#include "SplitMeasure.h"

#include "dsp/common/Stft.h"
#include "dsp/shapes/spectral/SpectralShape.h"
#include "support/Corpus.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <map>

using namespace halite;
using namespace halite::testing;

namespace
{
    /** Depths that never change, for the engine on its own. */
    struct ConstantDepths final : dsp::ReductionReader
    {
        float ess = 0.0f, harsh = 0.0f;
        float depthAt (std::int64_t, int track) const noexcept override { return track < 2 ? ess : harsh; }
        float largestTarget (std::int64_t, std::int64_t, int) const noexcept override { return ess > 0.0f ? 1.0f : 0.0f; }
    };

    float depthOfDb (double db) { return (float) (1.0 - std::pow (10.0, -db / 20.0)); }

    /** The engine over a mono buffer from timeline position `position`: what the Ess band removes. */
    Samples runEngine (dsp::SpectralShape& shape, const Samples& x, std::int64_t position, const dsp::ReductionReader& depths,
                       const dsp::SpectralSettings& settings = {}, const dsp::ShapeBands& bands = {})
    {
        Samples ess (x.size(), 0.0f), harsh (x.size(), 0.0f);
        for (size_t at = 0; at < x.size(); at += 256)
        {
            const auto n = (int) std::min<size_t> (256, x.size() - at);
            const float* in[] { x.data() + at };
            float* e[] { ess.data() + at };
            float* h[] { harsh.data() + at };
            shape.process (in, e, h, n, position + (std::int64_t) at, bands, settings, depths, { 0, 0 });
        }
        return ess;
    }

    /** Least-squares slope of 10 log10 (v[k]) against log2 (k), dB per octave, over bins [first, last]. */
    double slopeDbPerOctave (const std::vector<double>& v, int first, int last)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        auto count = 0;
        for (int k = first; k <= last; ++k)
        {
            const auto x = std::log2 ((double) k), y = 10.0 * std::log10 (std::max (v[(size_t) k], 1.0e-30));
            sx += x;
            sy += y;
            sxx += x * x;
            sxy += x * y;
            ++count;
        }
        return (count * sxy - sx * sy) / (count * sxx - sx * sx);
    }

    /** Ratio of the RMS of (a − b) to the RMS of b over [from, to), in dB. */
    double differenceDb (const Samples& a, const Samples& b, int from, int to)
    {
        double e = 0, r = 0;
        for (int i = from; i < to; ++i)
        {
            const auto d = (double) a[(size_t) i] - b[(size_t) i];
            e += d * d;
            r += (double) b[(size_t) i] * b[(size_t) i];
        }
        return 10.0 * std::log10 (std::max (e, 1.0e-300) / std::max (r, 1.0e-300));
    }

    /** A sung voice with vibrato and a resonance, and an ess every 0.55 s. */
    Channels singerWithEsses (double rate, double seconds)
    {
        const auto n = samplesFor (seconds, rate);
        auto voice = harmonic ({ .f0 = 196.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -8.0, .vibratoCents = 30.0, .peakHz = 3000.0,
                                 .peakDb = 10.0, .rms = 0.1, .seed = 11 }, n, rate);
        for (auto t = 0.3; t + 0.12 < seconds; t += 0.55)
            voice = sum (voice, gated (bandNoise (5000.0, 11000.0, n, rate, 40u + (unsigned) (t * 10.0), 0.03), samplesFor (t, rate),
                                       samplesFor (t + 0.12, rate), rate));
        return { voice, scaled (voice, 0.8) };
    }

    /** The chain over `in` from timeline position `position` on, with the host's play head. */
    Channels runFrom (const Channels& in, int from, const ChainRun& run, std::int64_t position)
    {
        const juce::ScopedNoDenormals noDenormals;
        dsp::HaliteChain chain;
        chain.prepare (run.rate, 4096, (int) in.size(), run.settings.detector.latency);
        Channels out;
        for (const auto& c : in)
            out.emplace_back (c.begin() + from, c.end());
        const auto n = (int) out[0].size();
        for (int pos = 0; pos < n; pos += 512)
        {
            const auto len = std::min (512, n - pos);
            std::array<float*, 2> channels { out[0].data() + pos, out[out.size() - 1].data() + pos };
            chain.process (channels.data(), (int) out.size(), len, run.settings, nullptr, 0, { position + pos, true, true });
        }
        return out;
    }
}

TEST_CASE ("Stft: the inverse FFT undoes the forward one", "[spectral][stft]")
{
    for (const auto order : { 4, 9, 11 })
    {
        dsp::RealFft fft;
        fft.prepare (order);
        const auto n = 1 << order;
        std::mt19937 rng ((unsigned) order);
        std::normal_distribution<float> gauss;
        Samples x ((size_t) n), back ((size_t) n), re ((size_t) n / 2 + 1), im ((size_t) n / 2 + 1);
        for (auto& v : x)
            v = gauss (rng);
        fft.forward (x.data(), re.data(), im.data());
        fft.inverse (re.data(), im.data(), back.data());

        auto worst = 0.0;
        for (int i = 0; i < n; ++i)
            worst = std::max (worst, (double) std::abs (back[(size_t) i] - x[(size_t) i]));
        INFO (n << " points");
        CHECK (worst <= 1.0e-5);
    }
}

TEST_CASE ("Stft: the latency is one frame, a gain of 1 removes nothing, a constant gain its share", "[spectral][stft]")
{
    // Periodic Hann analysis and synthesis at 75 % overlap sum to 1.5: removing (1 − c) X in every
    // frame removes (1 − c) x, a frame later, wherever the grid lies.
    const auto n = 512;
    std::mt19937 rng (3u);
    std::normal_distribution<float> gauss (0.0f, 0.3f);
    Samples x (8192);
    for (auto& v : x)
        v = gauss (rng);

    for (const auto offset : { 0, 37, 128 })
        for (const auto keep : { 1.0f, 0.25f, 0.0f })
        {
            dsp::Stft stft;
            stft.prepare (n, 2);
            Samples removed (x.size(), 1.0f), other (x.size(), 1.0f), re ((size_t) stft.numBins()), im ((size_t) stft.numBins());
            for (int t = 0; t < (int) x.size(); ++t)
            {
                if ((t + offset) % stft.hopSize() == 0 && keep < 1.0f)
                {
                    stft.analyse (re.data(), im.data());
                    for (size_t k = 0; k < re.size(); ++k)
                    {
                        re[k] *= 1.0f - keep;
                        im[k] *= 1.0f - keep;
                    }
                    stft.synthesise (re.data(), im.data(), 0);
                }
                float* out[] { removed.data() + t, other.data() + t };
                stft.step (x.data() + t, out, 1);
            }

            INFO ("grid offset " << offset << ", gain " << keep);
            auto worst = 0.0;
            for (int t = 0; t < (int) x.size(); ++t)
            {
                REQUIRE (sameBits (other[(size_t) t], 0.0f));
                if (keep >= 1.0f)
                    REQUIRE (sameBits (removed[(size_t) t], 0.0f));
                else if (t >= 2 * n)
                    worst = std::max (worst, (double) std::abs (removed[(size_t) t] - (1.0f - keep) * x[(size_t) (t - n)]));
            }
            CHECK (worst <= 2.0e-6);
        }
}

TEST_CASE ("Spectral: the band loses the depth asked for, as the detector weighs it; outside the band nothing moves", "[spectral]")
{
    // Noise well inside the Detect band and two quiet sines outside it. The band's own bins, weighed
    // by the detector's band filter, lose the depth (Settled 44); B, which also hears the noise's
    // faint skirts past the edges, loses as much within 0.1 dB up to 8 dB.
    const auto rate = 48000.0;
    const auto n = samplesFor (2.0, rate);
    const auto noise = bandNoise (5500.0, 10000.0, n, rate, 7u, 0.1);
    const auto x = sum (noise, sum (sine (2000.0, n, rate, 0.005), sine (15000.0, n, rate, 0.005)));
    dsp::SpectralShape shape;
    shape.prepare (rate, 512);

    for (const auto db : { 3.0, 8.0, 20.0 })
    {
        shape.start (1, false);
        ConstantDepths depths;
        depths.ess = depthOfDb (db);
        dsp::SpectralSettings settings;
        settings.essRangeDb = 24.0f;
        const auto removed = runEngine (shape, x, 0, depths, settings);

        Samples out (x.size());
        for (size_t i = 0; i + 512 < x.size(); ++i)
            out[i] = x[i] - removed[i + 512];
        const auto from = samplesFor (0.5, rate), to = samplesFor (1.9, rate);
        dsp::BandFilter band;
        band.setBand (4500.0, 12000.0, rate);
        Samples bandIn (x.size()), bandOut (x.size());
        for (size_t i = 0; i < x.size(); ++i)
            bandIn[i] = (float) band.process ((double) x[i]);
        band.reset();
        for (size_t i = 0; i < x.size(); ++i)
            bandOut[i] = (float) band.process ((double) out[i]);

        const auto lost = 20.0 * std::log10 (rmsOf (bandIn, from, to) / rmsOf (bandOut, from, to));
        const auto own = ownBandLostDb (x, out, from, to, 4500.0, 12000.0, rate);
        const auto low = 20.0 * std::log10 (amplitudeAt (out, 2000.0, rate, from, to) / amplitudeAt (x, 2000.0, rate, from, to));
        const auto high = 20.0 * std::log10 (amplitudeAt (out, 15000.0, rate, from, to) / amplitudeAt (x, 15000.0, rate, from, to));
        INFO (db << " dB asked: the band's own bins lost " << own << " dB, the detector's band " << lost << " dB; 2 kHz " << low << " dB, 15 kHz "
                 << high << " dB");
        CHECK (std::abs (own - db) <= 0.1);
        if (db <= 8.0)
            CHECK (std::abs (lost - db) <= 0.1);
        CHECK (std::abs (low) <= 0.01);
        CHECK (std::abs (high) <= 0.01);
    }
}

TEST_CASE ("Spectral: the target learns the voice's top end, not its esses, and starts from the neutral curve", "[spectral][target]")
{
    const auto rate = 48000.0;
    const auto n = samplesFor (4.0, rate);
    dsp::ShapeBands bands;

    SECTION ("the engine, every frame non-sibilant: the voice's own slope")
    {
        for (const auto tilt : { -6.0, -12.0 })
        {
            const auto voice = harmonic ({ .f0 = 150.0, .topHz = 0.45 * rate, .tiltDbPerOctave = tilt, .rms = 0.1 }, n, rate);
            dsp::SpectralShape shape;
            shape.prepare (rate, 512);
            shape.start (1, false);
            ConstantDepths none;
            runEngine (shape, Samples (voice.begin(), voice.begin() + samplesFor (0.05, rate)), 0, none);
            const auto& early = shape.analysis (0);
            const auto first = (int) std::ceil (4500.0 / early.binHz()), last = (int) std::floor (12000.0 / early.binHz());
            const auto neutral = slopeDbPerOctave (early.learnedTarget(), first, last);

            shape.start (1, false);
            runEngine (shape, voice, 0, none);
            const auto& a = shape.analysis (0);
            const auto learned = slopeDbPerOctave (a.learnedTarget(), first, last);
            INFO ("tilt " << tilt << " dB/oct: learned " << learned << ", at the start " << neutral << ", frames in the window " << a.learnedFrames());
            CHECK (std::abs (neutral - dsp::SpectralAnalysis::neutralSlopeDb) <= 0.3);
            CHECK (std::abs (learned - tilt) <= 0.5);
            CHECK (a.learnedFrames() >= (int) std::lround (2.0 * rate / 128.0));
        }
    }

    SECTION ("the chain: frames with esses are not learned")
    {
        const auto voice = harmonic ({ .f0 = 150.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -12.0, .rms = 0.1 }, n, rate);
        auto sibilant = voice;
        for (auto t = 0.2; t < 3.8; t += 0.3)
            sibilant = sum (sibilant, gated (bandNoise (4500.0, 12000.0, n, rate, (unsigned) (t * 10.0), 0.05), samplesFor (t, rate),
                                             samplesFor (t + 0.12, rate), rate));
        ChainRun run;
        run.rate = rate;
        run.settings.shape = Shape::spectral;
        dsp::HaliteChain chain;
        chain.prepare (rate, 512, 1, LatencyMode::normal);
        runChain ({ sibilant }, run, &chain);
        REQUIRE (chain.events().count() > 0);

        const auto& a = chain.runningLane().spectralShape().analysis (0);
        const auto first = (int) std::ceil (4500.0 / a.binHz()), last = (int) std::floor (12000.0 / a.binHz());
        const auto learned = slopeDbPerOctave (a.learnedTarget(), first, last);
        INFO ("learned " << learned << " dB/oct from " << a.framesLearned() << " frames");
        CHECK (std::abs (learned + 12.0) <= 1.0);
        // The esses hold 40 % of the time; of the rest, a frame and the 8 ms guard before each
        // ess and a frame after its decision ends are not learned either.
        CHECK (a.framesLearned() < (int) (0.6 * 4.0 * rate / 128.0));
        CHECK (a.framesLearned() > (int) (0.15 * 4.0 * rate / 128.0));
    }
}

TEST_CASE ("Spectral: Linked gives both channels one set of gains from both; Unlinked one each", "[spectral][stereo]")
{
    // An ess on the right only, over a vowel on both; the target has learned the vowel. Linked
    // judges the bins on both channels' powers summed and cuts both alike: the left's vowel
    // loses at the ess's bins what the right's ess does. Unlinked (Allround, no floor) leaves the
    // channel without an ess untouched.
    const auto rate = 48000.0;
    const auto n = samplesFor (4.0, rate);
    const auto voice = harmonic ({ .f0 = 150.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -6.0, .rms = 0.1 }, n, rate);
    const auto ess = gated (bandNoise (9000.0, 11000.0, n, rate, 4u, 0.03), samplesFor (3.0, rate), samplesFor (3.8, rate), rate);
    const Channels in { voice, sum (voice, ess) };
    const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
    const auto from = samplesFor (3.2, rate), to = samplesFor (3.7, rate);

    ChainRun run;
    run.rate = rate;
    run.settings.shape = Shape::spectral;
    run.settings.detector.rangeDb = 24.0f;
    const auto out = runChain (in, run);
    const auto change = [&] (const Channels& o, size_t c, double hz)
    {
        return 20.0 * std::log10 (amplitudeAt (o[c], hz, rate, from + delay, to + delay) / amplitudeAt (in[c], hz, rate, from, to));
    };
    const auto leftAtEss = change (out, 0, 9900.0), leftBeside = change (out, 0, 5550.0);
    dsp::BandFilter band;
    band.setBand (9300.0, 10700.0, rate);
    Samples a ((size_t) n), b ((size_t) n);
    for (int i = 0; i < n; ++i)
        a[(size_t) i] = (float) band.process ((double) in[1][(size_t) i]);
    band.reset();
    for (int i = 0; i < n; ++i)
        b[(size_t) i] = (float) band.process ((double) out[1][(size_t) i]);
    const auto rightEss = 20.0 * std::log10 (rmsOf (a, from, to) / rmsOf (b, from + delay, to + delay));
    INFO ("Linked: the right's ess lost " << rightEss << " dB; the left's vowel " << -leftAtEss << " dB at 9.9 kHz, " << -leftBeside << " dB at 5.55 kHz");
    CHECK (rightEss > 6.0);
    CHECK (leftAtEss < -6.0);
    CHECK (std::abs (leftBeside) < 1.5);

    // The vowel 20 dB down, under the absolute threshold on its own.
    run.settings.detector.stereo = StereoMode::unlinked;
    run.settings.detector.detection = Detection::allround;
    run.settings.detector.thresholdAbsDb = -45.0f;
    const Channels quiet { scaled (voice, 0.1), sum (scaled (voice, 0.1), ess) };
    const auto unlinked = runChain (quiet, run);
    CHECK (firstDifference (unlinked[0], quiet[0], delay) == -1);
    CHECK (firstDifference (unlinked[1], quiet[1], delay) != -1);
}

TEST_CASE ("Spectral: a new Stereo mode keeps what the target has learned", "[spectral][target]")
{
    const auto rate = 48000.0;
    const auto in = singerWithEsses (rate, 4.0);
    const auto at = samplesFor (3.0, rate) / 512 * 512;
    const auto wanted = (int) std::lround (dsp::SpectralAnalysis::learnSeconds * rate / 128.0);

    for (const auto to : { StereoMode::unlinked, StereoMode::mid })
    {
        ChainRun run;
        run.rate = rate;
        run.settings.shape = Shape::spectral;
        dsp::HaliteChain chain;
        chain.prepare (rate, 512, 2, LatencyMode::normal);
        std::vector<double> before;
        auto after = -1;
        run.automate = [&] (int pos, dsp::ChainSettings& s)
        {
            s.detector.stereo = pos >= at ? to : StereoMode::linked;
            const auto& a = chain.runningLane().spectralShape().analysis (0);
            if (pos == at)
                before = a.learnedTarget();
            if (pos == at + 512)
            {
                after = a.learnedFrames();
                auto worst = 0.0;
                for (size_t k = 0; k < before.size(); ++k)
                    worst = std::max (worst, std::abs (10.0 * std::log10 (a.learnedTarget()[k] / before[k])));
                INFO ("the target moved by at most " << worst << " dB across the change");
                CHECK (worst <= 0.5);
            }
        };
        runChain (in, run, &chain);
        INFO ("stereo " << (int) to << ": " << after << " frames in the new lane's window");
        CHECK (after >= wanted);
    }
}

TEST_CASE ("Spectral: a frame is learned only if the detector calls it non-sibilant, and for 8 ms after it", "[spectral][target]")
{
    // A detector that turns sibilant at `onset`: every voiced frame ending at least the guard
    // before it is learned, none after.
    struct Onset final : dsp::ReductionReader
    {
        std::int64_t onset = 0;
        float depthAt (std::int64_t, int) const noexcept override { return 0.0f; }
        float largestTarget (std::int64_t, std::int64_t to, int) const noexcept override { return to > onset ? 6.0f : 0.0f; }
    };
    const auto rate = 48000.0;
    const auto voice = harmonic ({ .f0 = 150.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -9.0, .rms = 0.1 }, samplesFor (2.0, rate), rate);
    Onset reader;
    reader.onset = 128 * 300;
    dsp::SpectralShape shape;
    shape.prepare (rate, 512);
    shape.start (1, false);
    runEngine (shape, voice, 0, reader);
    const auto guard = (int) std::lround (dsp::SpectralTuning{}.onsetGuardMs * 0.001 * rate);
    // Frames end every 128 samples; the one ending at 0 holds only silence.
    const auto expected = (int) ((reader.onset - guard) / 128);
    INFO ("learned " << shape.analysis (0).framesLearned() << ", expected " << expected);
    CHECK (shape.analysis (0).framesLearned() == expected);
}

TEST_CASE ("Spectral: pauses are not learned, nor a vowel's harmonic comb", "[spectral][target]")
{
    // The voice with a pause every other half second, a hiss 60 dB under it there: the target
    // keeps the voice's slope. And on a steady vowel it is smooth: each frame is ERB-smoothed
    // before it is averaged in dB, so the gaps between harmonics do not dig into it.
    const auto rate = 48000.0;
    const auto n = samplesFor (5.0, rate);
    auto voice = harmonic ({ .f0 = 150.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -12.0, .rms = 0.1 }, n, rate);
    for (int i = 0; i < n; ++i)
        if ((i / samplesFor (0.5, rate)) % 2 == 1)
            voice[(size_t) i] = 0.0f;
    const auto x = sum (voice, bandNoise (20.0, 20000.0, n, rate, 77u, 1.0e-4));

    dsp::SpectralShape shape;
    shape.prepare (rate, 512);
    shape.start (1, false);
    ConstantDepths none;
    runEngine (shape, x, 0, none);
    const auto& a = shape.analysis (0);
    const auto first = (int) std::ceil (4500.0 / a.binHz()), last = (int) std::floor (12000.0 / a.binHz());
    const auto slope = slopeDbPerOctave (a.learnedTarget(), first, last);

    // Ripple: the target against its straight line in dB over log f.
    auto ripple = 0.0;
    {
        double sx = 0, sy = 0;
        for (int k = first; k <= last; ++k)
        {
            sx += std::log2 ((double) k);
            sy += 10.0 * std::log10 (a.learnedTarget()[(size_t) k]);
        }
        const auto count = last - first + 1;
        for (int k = first; k <= last; ++k)
        {
            const auto fit = sy / count + slope * (std::log2 ((double) k) - sx / count);
            ripple = std::max (ripple, std::abs (10.0 * std::log10 (a.learnedTarget()[(size_t) k]) - fit));
        }
    }
    INFO ("learned " << slope << " dB/oct from " << a.framesLearned() << " frames, ripple " << ripple << " dB");
    CHECK (std::abs (slope + 12.0) <= 1.0);
    CHECK (a.framesLearned() < (int) (0.55 * 5.0 * rate / 128.0));
    CHECK (ripple <= 2.0);
}

TEST_CASE ("Spectral: in Voice no bin is cut below the voice's own top end", "[spectral][target]")
{
    // The target, learned on the vowel, at the current body level: an ess at 9 … 11 kHz asked to
    // lose 24 dB is cut, the vowel's own harmonics in the band beside it are not. Allround has no
    // voice to keep: there the band's every bin goes as far as it takes.
    const auto rate = 48000.0;
    const auto learn = samplesFor (3.0, rate), n = samplesFor (4.0, rate);
    const auto voice = harmonic ({ .f0 = 150.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -9.0, .rms = 0.1 }, n, rate);
    auto ess = bandNoise (9000.0, 11000.0, n, rate, 21u, 0.03);
    std::fill (ess.begin(), ess.begin() + learn, 0.0f);
    const auto x = sum (voice, ess);

    for (const auto detection : { Detection::voice, Detection::allround })
    {
        dsp::SpectralShape shape;
        shape.prepare (rate, 512);
        shape.start (1, false);
        dsp::SpectralSettings settings;
        settings.detection = detection;
        settings.essRangeDb = 24.0f;
        ConstantDepths none, deep;
        deep.ess = depthOfDb (24.0);
        auto removed = runEngine (shape, Samples (x.begin(), x.begin() + learn), 0, none, settings);
        const auto rest = runEngine (shape, Samples (x.begin() + learn, x.end()), learn, deep, settings);
        removed.insert (removed.end(), rest.begin(), rest.end());

        Samples out (x.size(), 0.0f);
        for (size_t i = 0; i + 512 < x.size(); ++i)
            out[i] = x[i] - removed[i + 512];
        const auto from = learn + samplesFor (0.2, rate), to = n - 1024;
        auto harmonics = 0.0;
        // At 7 kHz and up the ess's own skirt is louder than the vowel's harmonics: not the voice's.
        for (const auto hz : { 5100.0, 5550.0, 6000.0 })
            harmonics = std::max (harmonics, -20.0 * std::log10 (amplitudeAt (out, hz, rate, from, to) / amplitudeAt (x, hz, rate, from, to)));
        dsp::BandFilter band;
        band.setBand (9300.0, 10700.0, rate);
        Samples essIn (x.size()), essOut (x.size());
        for (size_t i = 0; i < x.size(); ++i)
            essIn[i] = (float) band.process ((double) x[i]);
        band.reset();
        for (size_t i = 0; i < x.size(); ++i)
            essOut[i] = (float) band.process ((double) out[i]);
        const auto essLost = 20.0 * std::log10 (rmsOf (essIn, from, to) / rmsOf (essOut, from, to));

        INFO ((detection == Detection::voice ? "Voice" : "Allround") << ": the ess lost " << essLost << " dB, the vowel's harmonics at most " << harmonics << " dB");
        // The floor is the target's mean in dB, which lies about 1 dB under the level's mean in power.
        CHECK (essLost > 15.0);
        if (detection == Detection::voice)
            CHECK (harmonics < 1.5);
        else
            CHECK (harmonics > 10.0);
    }
}

TEST_CASE ("Spectral: the Harsh band cuts against the line through its edges, with no memory", "[spectral][harsh]")
{
    const auto rate = 48000.0;
    const auto n = samplesFor (1.5, rate);
    dsp::SpectralSettings settings;
    settings.harsh = true;
    settings.harshRangeDb = 12.0f;
    const dsp::ShapeBands bands;      // Harsh 2263 … 4525 Hz

    const auto harshGainDb = [&] (dsp::SpectralShape& shape, double hz)
    {
        const auto& a = shape.analysis (0);
        return 20.0 * std::log10 (a.harshGains()[(size_t) std::lround (hz / a.binHz())]);
    };

    SECTION ("a resonance is cut, the edges are not; a straight tilt is cut alike everywhere")
    {
        ConstantDepths depths;
        depths.harsh = depthOfDb (6.0);
        dsp::SpectralShape shape;
        shape.prepare (rate, 512);

        shape.start (1, false);
        runEngine (shape, harmonic ({ .f0 = 150.0, .topHz = 8000.0, .tiltDbPerOctave = -6.0, .peakHz = 3200.0, .peakDb = 18.0, .peakOctaves = 0.4,
                                      .rms = 0.1 }, n, rate),
                   0, depths, settings, bands);
        const auto peak = harshGainDb (shape, 3200.0), edge = harshGainDb (shape, 2300.0), top = harshGainDb (shape, 4450.0);
        INFO ("resonance: " << peak << " dB at 3.2 kHz, " << edge << " at 2.3 kHz, " << top << " at 4.45 kHz");
        CHECK (peak < -6.0);
        CHECK (edge > -0.5);
        CHECK (top > -0.5);

        // White noise: the line is flat, so the band is cut alike, by the 6 dB asked. One frame's
        // gains scatter with the level estimate's noise (NOTES, Musical noise); read their mean in dB
        // over the last 0.5 s, a frame per hop.
        shape.start (1, false);
        const auto noise = bandNoise (20.0, 20000.0, n, rate, 5u, 0.1);
        std::vector<double> meanDb (257, 0.0);
        auto frames = 0;
        Samples ess (128), harsh (128);
        for (int at = 0; at + 128 <= n; at += 128)
        {
            const float* in[] { noise.data() + at };
            float* e[] { ess.data() };
            float* h[] { harsh.data() };
            shape.process (in, e, h, 128, at, bands, settings, depths, { 0, 0 });
            if (at < n - samplesFor (0.5, rate))
                continue;
            for (size_t k = 0; k < meanDb.size(); ++k)
                meanDb[k] += 20.0 * std::log10 (shape.analysis (0).harshGains()[k]);
            ++frames;
        }
        const auto meanAt = [&] (double hz) { return meanDb[(size_t) std::lround (hz / shape.analysis (0).binHz())] / frames; };
        const auto a = meanAt (2500.0), b = meanAt (3200.0), c = meanAt (4200.0);
        INFO ("white noise: " << a << ", " << b << ", " << c << " dB");
        CHECK (std::max ({ a, b, c }) - std::min ({ a, b, c }) <= 1.0);
        CHECK (std::max ({ a, b, c }) < -5.0);

        // Harmonics inside the band only, falling 12 dB an octave, 3 dB asked: the line falls with
        // them, so the band is cut alike (a flat line would take it all from the low end).
        Samples comb ((size_t) n, 0.0f);
        for (auto hz = 2400.0; hz <= 4400.0; hz += 150.0)
            comb = sum (comb, sine (hz, n, rate, 0.05 * std::pow (hz / 2400.0, -2.0), hz / 700.0));
        ConstantDepths light;
        light.harsh = depthOfDb (3.0);
        shape.start (1, false);
        runEngine (shape, comb, 0, light, settings, bands);
        const auto d = harshGainDb (shape, 2500.0), e = harshGainDb (shape, 3200.0), f = harshGainDb (shape, 4200.0);
        INFO ("harmonics at -12 dB/oct: " << d << ", " << e << ", " << f << " dB");
        CHECK (std::max ({ d, e, f }) - std::min ({ d, e, f }) <= 1.5);
        CHECK (std::max ({ d, e, f }) < -1.5);
    }

    SECTION ("the line forgets: after different histories the same sound is cut the same; the learned Ess target does not")
    {
        ConstantDepths depths;
        depths.harsh = depthOfDb (6.0);
        const auto now = harmonic ({ .f0 = 150.0, .topHz = 8000.0, .tiltDbPerOctave = -6.0, .peakHz = 3200.0, .peakDb = 18.0, .rms = 0.1 }, n, rate);
        const auto bright = harmonic ({ .f0 = 220.0, .topHz = 20000.0, .tiltDbPerOctave = 0.0, .rms = 0.1, .seed = 3 }, samplesFor (3.0, rate), rate);
        const auto dull = harmonic ({ .f0 = 110.0, .topHz = 20000.0, .tiltDbPerOctave = -15.0, .rms = 0.1, .seed = 4 }, samplesFor (3.0, rate), rate);

        std::array<std::vector<float>, 2> harshGains;
        std::array<std::vector<double>, 2> targets;
        for (size_t h = 0; h < 2; ++h)
        {
            dsp::SpectralShape shape;
            shape.prepare (rate, 512);
            shape.start (1, false);
            ConstantDepths none;
            runEngine (shape, h == 0 ? bright : dull, 0, none, settings, bands);
            runEngine (shape, now, samplesFor (3.0, rate), depths, settings, bands);
            const auto& a = shape.analysis (0);
            harshGains[h].assign (a.harshGains(), a.harshGains() + 257);
            targets[h] = a.learnedTarget();
        }
        auto worst = 0.0;
        for (size_t k = 0; k < 257; ++k)
            worst = std::max (worst, (double) std::abs (harshGains[0][k] - harshGains[1][k]));
        CHECK (worst <= 1.0e-6);
        CHECK (std::abs (slopeDbPerOctave (targets[0], 48, 128) - slopeDbPerOctave (targets[1], 48, 128)) > 3.0);
    }
}

TEST_CASE ("Spectral: where the bands overlap a bin takes the smaller gain, and its loss goes to that band", "[spectral][harsh]")
{
    // Ess 3 … 8 kHz asked for 2 dB, Harsh 2.3 … 4.5 kHz for 10 dB, white noise: in the overlap
    // the Harsh gain is the smaller, so the Harsh band removes there and the Ess band does not.
    const auto rate = 48000.0;
    const auto x = bandNoise (20.0, 20000.0, samplesFor (1.5, rate), rate, 31u, 0.1);
    dsp::SpectralShape shape;
    shape.prepare (rate, 512);
    shape.start (1, false);
    ConstantDepths depths;
    depths.ess = depthOfDb (2.0);
    depths.harsh = depthOfDb (10.0);
    dsp::SpectralSettings settings;
    settings.harsh = true;
    settings.harshRangeDb = 12.0f;
    dsp::ShapeBands bands;
    bands.essLow = 3000.0;
    bands.essHigh = 8000.0;

    Samples ess (x.size(), 0.0f), harsh (x.size(), 0.0f);
    for (size_t at = 0; at < x.size(); at += 256)
    {
        const auto len = (int) std::min<size_t> (256, x.size() - at);
        const float* in[] { x.data() + at };
        float* e[] { ess.data() + at };
        float* h[] { harsh.data() + at };
        shape.process (in, e, h, len, (std::int64_t) at, bands, settings, depths, { 0, 0 });
    }
    const auto from = samplesFor (0.5, rate), to = samplesFor (1.4, rate);
    const auto at = [&] (const Samples& y) { return amplitudeAt (y, 4000.0, rate, from, to) / amplitudeAt (x, 4000.0, rate, from - 512, to - 512); };
    INFO ("at 4 kHz: the Ess band removes " << at (ess) << " of the input, the Harsh band " << at (harsh));
    CHECK (at (ess) < 0.01);
    CHECK (at (harsh) > 0.5);
}

TEST_CASE ("Spectral: with a side chain the key decides when and how much, the main signal which bins", "[spectral][key]")
{
    // The key's esses set the reduction; the main signal has no ess, but a burst of noise around
    // 9 kHz arrives with each of them, over a vowel whose top end the target has learned. The
    // band loses the key's reduction, taken from the burst; the vowel's top end beside it keeps
    // its level. Without the key's esses nothing changes.
    const auto rate = 48000.0;
    const auto n = samplesFor (4.0, rate);
    const auto voice = vowel (150.0, n, rate, 0.1, 1);
    Samples bursts ((size_t) n, 0.0f), esses ((size_t) n, 0.0f);
    for (auto t = 2.6; t + 0.2 < 4.0; t += 0.5)
    {
        bursts = sum (bursts, gated (bandNoise (8500.0, 9500.0, n, rate, (unsigned) (t * 10.0), 0.004), samplesFor (t, rate), samplesFor (t + 0.2, rate), rate));
        esses = sum (esses, gated (bandNoise (4500.0, 12000.0, n, rate, 90u + (unsigned) (t * 10.0), 0.04), samplesFor (t, rate), samplesFor (t + 0.2, rate), rate));
    }
    const Channels main { sum (voice, bursts) };
    const Channels key { sum (voice, esses) }, quiet { voice };

    ChainRun run;
    run.rate = rate;
    run.settings.shape = Shape::spectral;
    run.settings.detector.key = KeySource::external;
    run.settings.detector.rangeDb = 24.0f;
    run.key = &key;
    dsp::HaliteChain chain;
    chain.prepare (rate, 512, 1, LatencyMode::normal);
    const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
    const auto out = runChain (main, run, &chain);
    auto keySettings = run.settings.detector;
    keySettings.key = KeySource::internal;
    const auto probe = probeOf (key[0], rate, keySettings);
    auto reduction = 0.0;
    auto hops = 0;
    for (auto t = 2.6; t + 0.2 < 4.0; t += 0.5)
        for (auto h = hopAt (t + 0.05, rate); h < hopAt (t + 0.15, rate); ++h, ++hops)
            reduction += probe.hops[(size_t) h].reductions[0].essDb;
    reduction /= std::max (1, hops);

    const auto bandLoss = [&] (double low, double high)
    {
        dsp::BandFilter f;
        f.setBand (low, high, rate);
        Samples a ((size_t) n), b ((size_t) n);
        for (int i = 0; i < n; ++i)
            a[(size_t) i] = (float) f.process ((double) main[0][(size_t) i]);
        f.reset();
        for (int i = 0; i < n; ++i)
            b[(size_t) i] = (float) f.process ((double) out[0][(size_t) i]);
        auto in = 0.0, after = 0.0;
        for (auto t = 2.6; t + 0.2 < 4.0; t += 0.5)
        {
            const auto from = samplesFor (t + 0.05, rate), to = samplesFor (t + 0.15, rate);
            in += rmsOf (a, from, to);
            after += rmsOf (b, from + delay, to + delay);
        }
        return 20.0 * std::log10 (in / after);
    };
    // The vowel's own harmonics beside the burst, read at their frequencies.
    auto beside = 0.0;
    for (const auto hz : { 5100.0, 5550.0, 6000.0 })
    {
        auto in = 0.0, after = 0.0;
        for (auto t = 2.6; t + 0.2 < 4.0; t += 0.5)
        {
            const auto from = samplesFor (t + 0.05, rate), to = samplesFor (t + 0.15, rate);
            in += amplitudeAt (main[0], hz, rate, from, to);
            after += amplitudeAt (out[0], hz, rate, from + delay, to + delay);
        }
        beside = std::max (beside, std::abs (20.0 * std::log10 (in / after)));
    }
    const auto burst = bandLoss (8600.0, 9400.0), band = bandLoss (4500.0, 12000.0);
    INFO ("the key's reduction " << reduction << " dB; the band lost " << band << " dB, the burst " << burst
                                 << " dB, the vowel's harmonics at 5.1 ... 6 kHz at most " << beside << " dB");
    CHECK (reduction > 6.0);
    CHECK (band > 3.0);
    CHECK (burst > band - 0.5);
    CHECK (beside < 0.5);

    run.key = &quiet;
    const auto untouched = runChain (main, run);
    CHECK (firstDifference (untouched[0], main[0], delay) == -1);
}

TEST_CASE ("Spectral: outside events the output is the aligned input bit for bit", "[spectral][identity]")
{
    const auto rate = 48000.0;
    const auto n = samplesFor (2.4, rate);
    const auto voice = vowel (150.0, n, rate, 0.1, 1);
    const auto essFrom = samplesFor (1.0, rate), essTo = samplesFor (1.15, rate);
    const Channels in { sum (voice, gated (bandNoise (4500.0, 12000.0, n, rate, 3, 0.05), essFrom, essTo, rate)) };

    for (const auto harsh : { false, true })
    {
        ChainRun run;
        run.rate = rate;
        run.settings.shape = Shape::spectral;
        run.settings.detector.speedAuto = false;
        run.settings.detector.releaseMs = 40.0f;
        run.settings.detector.harshOn = harsh;
        run.settings.listen = Listen::removed;
        const auto removed = runChain (in, run)[0];
        run.settings.listen = Listen::output;
        const auto out = runChain (in, run)[0];
        const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);

        auto first = -1, last = -1;
        for (int i = 0; i < n; ++i)
            if (! sameBits (removed[(size_t) i], 0.0f))
            {
                first = first < 0 ? i : first;
                last = i;
            }
        INFO ("Harsh " << harsh << ": removed from " << (first - delay) / rate << " s to " << (last - delay) / rate << " s of input");
        REQUIRE (first >= 0);
        // A frame reaches half a frame either side of the look-ahead's ramp and the release's tail.
        CHECK (first - delay >= essFrom - samplesFor (0.0055, rate) - 512);
        CHECK (last - delay <= essTo + samplesFor (0.6, rate) + 512);
        for (int i = 0; i < n; ++i)
            if (i < first || i > last)
                REQUIRE (sameBits (out[(size_t) i], i >= delay ? in[0][(size_t) (i - delay)] : 0.0f));
    }
}

TEST_CASE ("Timeline: an anchored detector started anywhere decides as one that started earlier", "[spectral][timeline]")
{
    const auto rate = 44100.0;     // the periodicity cue decimates by 7 here, so its phase and the hops' differ
    const auto in = singerWithEsses (rate, 4.0)[0];
    dsp::DetectorSettings s;

    struct Hops final : dsp::DetectorObserver
    {
        std::vector<dsp::DetectorFrame> frames;
        void hop (const dsp::DetectorFrame& f) noexcept override { frames.push_back (f); }
    };
    const auto run = [&] (int from, bool anchored)
    {
        dsp::Detector d;
        d.prepare (rate);
        if (anchored)
            d.anchor (from);
        Hops hops;
        for (int pos = from; pos < (int) in.size(); pos += 300)
        {
            const float* x[] { in.data() + pos };
            d.process (x, 1, nullptr, 0, std::min (300, (int) in.size() - pos), s, &hops);
        }
        return hops.frames;
    };

    const auto full = run (0, true);
    for (const auto from : { samplesFor (1.0, rate) + 13, samplesFor (1.3, rate) + 1 })
    {
        const auto late = run (from, true);
        const auto unanchored = run (from, false);
        REQUIRE (late.front().position % 32 == 0);
        auto worstS = 0.0, worstReduction = 0.0, worstUnanchored = 0.0;
        for (const auto& f : late)
        {
            if (f.position < from + samplesFor (1.5, rate))
                continue;
            const auto& g = full[(size_t) (f.position / 32 - 1)];
            REQUIRE (g.position == f.position);
            worstS = std::max (worstS, (double) std::abs (f.paths[0].sibilanceDb - g.paths[0].sibilanceDb));
            worstReduction = std::max (worstReduction, (double) std::abs (f.reductions[0].essDb - g.reductions[0].essDb));
        }
        // Unanchored, its hops end 32 samples after the start and every 32 on: against the
        // timeline's hop that holds the same end.
        for (size_t i = 0; i < unanchored.size(); ++i)
        {
            const auto end = from + 32 * ((std::int64_t) i + 1);
            if (end < from + samplesFor (1.5, rate) || end / 32 >= (std::int64_t) full.size())
                continue;
            worstUnanchored = std::max (worstUnanchored, (double) std::abs (unanchored[i].reductions[0].essDb - full[(size_t) (end / 32)].reductions[0].essDb));
        }
        INFO ("from " << from << ": S within " << worstS << " dB, reductions within " << worstReduction
                      << " dB (on the hop grid of the start, not the timeline's: " << worstUnanchored << " dB)");
        CHECK (worstS <= 1.0e-3);
        CHECK (worstReduction <= 1.0e-3);
    }
}

TEST_CASE ("Warm-up: a render started mid-clip matches the full render once 2 s of non-sibilant frames have passed", "[spectral][warmup]")
{
    // PLAN.md, gate Warm-up: Spectral, Voice, at any sample offset; −60 dB.
    const auto rate = 48000.0;
    const auto in = singerWithEsses (rate, 9.0);
    ChainRun run;
    run.rate = rate;
    run.settings.shape = Shape::spectral;
    run.settings.detector.harshOn = true;
    const auto full = runFrom (in, 0, run, 0);
    const auto wanted = (int) std::lround (dsp::SpectralAnalysis::learnSeconds * rate / 128.0);

    for (const auto from : { samplesFor (2.0, rate) + 7, samplesFor (2.6, rate) + 129, samplesFor (3.1, rate) + 300 })
    {
        const auto late = runFrom (in, from, run, from);

        // When the late render has learned 2 s of frames, and a block more (its first block is partial).
        dsp::HaliteChain chain;
        chain.prepare (rate, 512, 2, LatencyMode::normal);
        Channels part;
        for (const auto& c : in)
            part.emplace_back (c.begin() + from, c.end());
        auto warm = -1;
        for (int pos = 0; pos < (int) part[0].size() && warm < 0; pos += 512)
        {
            std::array<float*, 2> channels { part[0].data() + pos, part[1].data() + pos };
            chain.process (channels.data(), 2, std::min (512, (int) part[0].size() - pos), run.settings, nullptr, 0, { from + pos, true, true });
            if (chain.runningLane().spectralShape().analysis (0).framesLearned() >= wanted + dsp::SpectralAnalysis::blockFrames)
                warm = pos + 512;
        }
        REQUIRE (warm > 0);

        // The full render's samples [from, end) against the late one's [0, end − from).
        Samples a, b;
        for (int c = 0; c < 2; ++c)
        {
            a.insert (a.end(), full[(size_t) c].begin() + from + warm, full[(size_t) c].end());
            b.insert (b.end(), late[(size_t) c].begin() + warm, late[(size_t) c].end());
        }
        const auto after = differenceDb (b, a, 0, (int) a.size());

        Samples a0 (full[0].begin() + from, full[0].begin() + from + warm), b0 (late[0].begin(), late[0].begin() + warm);
        const auto before = differenceDb (b0, a0, 512, warm);
        INFO ("from " << from / rate << " s: warm after " << warm / rate << " s; before " << before << " dB, after " << after << " dB");
        CHECK (after <= -60.0);
    }
}

TEST_CASE ("Warm-up: Split and Wide started mid-clip match the full render once the detector has rebuilt its state", "[warmup]")
{
    // PLAN.md, gate Warm-up: Split, Wide and the Harsh band learn nothing, but the detector they
    // follow does: the body level, its holds and the cues start from silence and take a few
    // hundred ms to read as in the full render (spectral/NOTES.md, Warm-up). Gated at −60 dB from
    // 1 s on; reported: from when the late render matches bit for bit, and after which no 50 ms
    // window differs by more than −60 dB of what the full render removes (its RMS over the span).
    const auto rate = 48000.0;
    const auto entries = corpus::committedTier (juce::File (HALITE_TEST_DATA_DIR).getChildFile ("corpus"));
    Samples speech;
    for (const auto* id : { "vctk-p251-251", "vctk-p225-033" })
    {
        const auto entry = std::find_if (entries.begin(), entries.end(), [id] (const corpus::Entry& e) { return e.id == id; });
        REQUIRE (entry != entries.end());
        corpus::Recording r;
        juce::String error;
        REQUIRE (corpus::load (*entry, r, error));
        speech.insert (speech.end(), r.audio.begin(), r.audio.end());
    }

    struct Material
    {
        const char* name;
        Channels in;
        std::vector<int> starts;
    };
    const std::vector<Material> materials {
        { "the singer", singerWithEsses (rate, 6.0), { samplesFor (2.0, rate) + 7, samplesFor (2.6, rate) + 129, samplesFor (3.1, rate) + 300 } },
        { "two VCTK excerpts", { speech },
          { samplesFor (0.5, rate) + 3, samplesFor (1.1, rate) + 77, samplesFor (1.7, rate) + 200, samplesFor (2.3, rate) + 11,
            samplesFor (2.9, rate) + 150, samplesFor (3.5, rate) + 31, samplesFor (4.1, rate) + 99, samplesFor (4.7, rate) + 5 } },
    };
    const auto window = samplesFor (0.05, rate);

    for (const auto shape : { Shape::split, Shape::wide })
        for (const auto& m : materials)
        {
            ChainRun run;
            run.rate = rate;
            run.settings.shape = shape;
            run.settings.detector.harshOn = true;
            const auto full = runFrom (m.in, 0, run, 0);
            const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);
            auto settledBits = 0.0, settledRemoved = 0.0;
            for (const auto from : m.starts)
            {
                const auto late = runFrom (m.in, from, run, from);
                const auto length = (int) late[0].size();
                auto lastDifferent = -1, lastOver = -1;
                for (size_t c = 0; c < m.in.size(); ++c)
                {
                    auto removed = 0.0;
                    for (int i = 0; i < length; ++i)
                    {
                        if (! sameBits (late[c][(size_t) i], full[c][(size_t) (from + i)]))
                            lastDifferent = std::max (lastDifferent, i);
                        const auto aligned = from + i >= delay ? m.in[c][(size_t) (from + i - delay)] : 0.0f;
                        removed += std::pow ((double) aligned - full[c][(size_t) (from + i)], 2.0) / length;
                    }
                    for (int w = 0; w + window <= length; w += window)
                    {
                        auto error = 0.0;
                        for (int i = w; i < w + window; ++i)
                            error += std::pow ((double) late[c][(size_t) i] - full[c][(size_t) (from + i)], 2.0) / window;
                        if (error > 1.0e-6 * removed)
                            lastOver = std::max (lastOver, w + window);
                    }
                }
                settledBits = std::max (settledBits, (lastDifferent + 1) / rate);
                settledRemoved = std::max (settledRemoved, lastOver / rate);

                Samples a, b;
                const auto after = samplesFor (1.0, rate);
                for (size_t c = 0; c < m.in.size(); ++c)
                {
                    a.insert (a.end(), full[c].begin() + from + after, full[c].end());
                    b.insert (b.end(), late[c].begin() + after, late[c].end());
                }
                const auto apart = differenceDb (b, a, 0, (int) a.size());
                INFO ((shape == Shape::wide ? "Wide, " : "Split, ") << m.name << ", from " << from / rate << " s: after 1 s " << apart << " dB");
                CHECK (apart <= -60.0);
            }
            std::printf ("Warm-up, %s, %s (Harsh on): bit for bit after %.3f s; within -60 dB of what it removes after %.3f s (the latest of %zu starts)\n",
                         shape == Shape::wide ? "Wide" : "Split", m.name, settledBits, settledRemoved, m.starts.size());
        }
}

TEST_CASE ("Timeline: a transport that starts or jumps restarts the analysis there", "[spectral][timeline]")
{
    const auto rate = 48000.0;
    const auto in = singerWithEsses (rate, 8.0);
    const auto delay = dsp::HaliteChain::latencySamples (LatencyMode::normal, rate);

    /** The chain from a stop (the position moving on while stopped, as some hosts do), then playing from `at`. */
    const auto playAfterStop = [&] (const ChainRun& run, std::int64_t at, const Channels& heard)
    {
        const juce::ScopedNoDenormals noDenormals;
        dsp::HaliteChain chain;
        chain.prepare (rate, 512, 2, LatencyMode::normal);
        auto stopped = heard;
        const auto before = (int) stopped[0].size();
        for (int pos = 0; pos < before; pos += 512)
        {
            std::array<float*, 2> channels { stopped[0].data() + pos, stopped[1].data() + pos };
            chain.process (channels.data(), 2, std::min (512, before - pos), run.settings, nullptr, 0, { at - before + pos, true, false });
        }
        Channels played { Samples (in[0].begin() + at, in[0].end()), Samples (in[1].begin() + at, in[1].end()) };
        for (int pos = 0; pos < (int) played[0].size(); pos += 512)
        {
            std::array<float*, 2> channels { played[0].data() + pos, played[1].data() + pos };
            chain.process (channels.data(), 2, std::min (512, (int) played[0].size() - pos), run.settings, nullptr, 0, { at + pos, true, true });
        }
        return played;
    };

    for (const auto shape : { Shape::spectral, Shape::split })
    {
        const auto spectral = shape == Shape::spectral;
        ChainRun run;
        run.rate = rate;
        run.settings.shape = shape;
        INFO ((spectral ? "Spectral" : "Split"));

        // Playing from a silent stop is a render started there.
        {
            const auto at = samplesFor (2.0, rate) + 77;
            const auto fresh = runFrom (in, (int) at, run, at);
            const auto played = playAfterStop (run, at, { Samples (4096, 0.0f), Samples (4096, 0.0f) });
            for (size_t c = 0; c < 2; ++c)
                CHECK (firstDifference (played[c], fresh[c], 0) == -1);
        }

        // Playing after a stop that heard input (live monitoring): the grids already sit right, the
        // position having run on into the start; only the start's restart keeps the target and the
        // detector to what played. After the frame in flight, before the target has refilled:
        {
            const auto at = samplesFor (3.0, rate) + 77, before = samplesFor (2.0, rate);
            const auto fresh = runFrom (in, (int) at, run, at);
            const auto played = playAfterStop (run, at, { Samples (in[0].begin() + at - before, in[0].begin() + at),
                                                          Samples (in[1].begin() + at - before, in[1].begin() + at) });
            Samples a (fresh[0].begin() + 2 * delay, fresh[0].begin() + samplesFor (2.0, rate));
            Samples b (played[0].begin() + 2 * delay, played[0].begin() + samplesFor (2.0, rate));
            const auto apart = differenceDb (b, a, 0, (int) a.size());
            INFO ("the first 2 s after a start that followed live input, against a render started there: " << apart << " dB");
            CHECK (apart <= -60.0);
        }

        // A jump in the middle of an ess (1.4 … 1.52 s), whose reduction moves from hop to hop
        // (Range 24 does not hold it): the audio in flight keeps its reductions, then the render
        // from the new position follows.
        {
            auto jumpRun = run;
            jumpRun.settings.detector.rangeDb = 24.0f;
            const auto jumpAt = samplesFor (1.46, rate) / 512 * 512, to = samplesFor (3.0, rate) + 5;
            const auto steady = runFrom (in, 0, jumpRun, 0);
            const auto fresh = runFrom (in, to, jumpRun, to);

            const juce::ScopedNoDenormals noDenormals;
            dsp::HaliteChain chain;
            chain.prepare (rate, 512, 2, LatencyMode::normal);
            Channels out { Samples ((size_t) jumpAt), Samples ((size_t) jumpAt) };
            for (size_t c = 0; c < 2; ++c)
            {
                std::copy (in[c].begin(), in[c].begin() + jumpAt, out[c].begin());
                out[c].insert (out[c].end(), in[c].begin() + to, in[c].end());
            }
            for (int pos = 0; pos < (int) out[0].size(); pos += 512)
            {
                const auto position = pos < jumpAt ? pos : to + (pos - jumpAt);
                std::array<float*, 2> channels { out[0].data() + pos, out[1].data() + pos };
                chain.process (channels.data(), 2, std::min (512, (int) out[0].size() - pos), jumpRun.settings, nullptr, 0, { position, true, true });
            }

            Samples a (steady[0].begin() + jumpAt, steady[0].begin() + jumpAt + delay), b (out[0].begin() + jumpAt, out[0].begin() + jumpAt + delay);
            const auto inFlight = differenceDb (b, a, 0, delay);
            // Split once its filters have forgotten the audio before the jump, Spectral once its
            // target has (2 s of non-sibilant frames and the first block, which holds frames across
            // the jump: about 3.2 s of this singer).
            const auto settle = spectral ? samplesFor (3.5, rate) : samplesFor (0.05, rate);
            Samples c (fresh[0].begin() + settle, fresh[0].end()), d (out[0].begin() + jumpAt + settle, out[0].end());
            const auto after = differenceDb (d, c, 0, (int) c.size());
            INFO ("in flight " << inFlight << " dB, after " << settle / rate << " s " << after << " dB");
            // The last look-ahead of it holds the newest reduction: the key that would have shaped
            // those never arrived. Before that, Split is the uninterrupted render bit for bit (the
            // spline reads two hops back). Spectral starts on its new grid and warm-start target.
            const auto exact = delay - (int) std::lround (5.0e-3 * rate / 32.0) * 32 - 2 * 32;
            for (size_t ch = 0; ch < 2; ++ch)
            {
                const auto first = firstDifference (Samples (out[ch].begin() + jumpAt, out[ch].end()), Samples (steady[ch].begin() + jumpAt, steady[ch].end()), 0);
                INFO ("channel " << ch << ": the first sample in flight that differs " << first << ", exact up to " << exact);
                if (! spectral)
                    CHECK (first >= exact);
            }
            CHECK (inFlight <= (spectral ? -30.0 : -50.0));
            CHECK (after <= (spectral ? -60.0 : -100.0));
        }
    }
}

TEST_CASE ("Spectral: no block runs more frames than its own hops, whatever starts a Spectral engine", "[spectral][realtime]")
{
    // A Spectral lane that starts (a Shape or Stereo change, a change of latency) and a transport
    // that starts or jumps each begin an engine. Each must run in real time, a frame per hop of the
    // input: one call never catches up on frames before it, which would cost a block many frames'
    // time at once (the M2 review measured 12 frames, 1.6 times a 64-sample block at 192 kHz). With
    // blocks shorter than the hop each engine runs at most one frame per signal; at most four
    // engines run at once (two lanes, each finishing the frame in flight after a jump), two signals each.
    for (const auto rate : { 48000.0, 192000.0 })
    {
        const auto n = samplesFor (3.0, rate);
        const auto in = singerWithEsses (rate, 3.0);
        dsp::HaliteChain chain;
        chain.prepare (rate, 64, 2, LatencyMode::normal);
        auto out = in;
        dsp::ChainSettings s;
        s.detector.thresholdDb = -30.0f;
        const auto at = [rate] (double seconds) { return samplesFor (seconds, rate); };

        std::int64_t position = 0, worst = 0;
        auto playing = true;
        for (int pos = 0; pos + 64 <= n; pos += 64)
        {
            s.shape = pos >= at (0.3) && pos < at (2.6) ? Shape::spectral : Shape::split;
            s.detector.stereo = pos >= at (0.6) && pos < at (2.2) ? StereoMode::unlinked : StereoMode::linked;
            s.detector.latency = pos >= at (1.5) && pos < at (1.7) ? LatencyMode::live : LatencyMode::normal;
            if (pos == at (0.9) / 64 * 64)
                position += at (5.0);                          // a jump while playing
            playing = ! (pos >= at (1.1) && pos < at (1.2));   // a stop, then playing again from there
            if (pos >= at (2.0) && pos < at (2.02))
                position = 7;                                   // jumps every block for 20 ms

            const auto before = chain.lane (0).spectralShape().framesAnalysed() + chain.lane (1).spectralShape().framesAnalysed();
            std::array<float*, 2> channels { out[0].data() + pos, out[1].data() + pos };
            chain.process (channels.data(), 2, 64, s, nullptr, 0, { position, true, playing });
            const auto after = chain.lane (0).spectralShape().framesAnalysed() + chain.lane (1).spectralShape().framesAnalysed();
            worst = std::max (worst, after - before);
            position += 64;
        }
        auto finite = true;
        for (const auto& c : out)
            for (const auto x : c)
                finite = finite && std::isfinite (x);
        INFO (rate << " Hz: at most " << worst << " frames in one 64-sample block");
        CHECK (finite);
        CHECK (worst <= 8);
    }
}

TEST_CASE ("Spectral: musical noise, report", "[.report]")
{
    // A steady noise ess over a vowel, a constant 6 dB asked of the band: how much each in-band
    // bin's gain moves from frame to frame (the std of its dB over 2 s, the mean over the band's
    // bins), and how unevenly the frame's cut lies across them, for each smoothing step.
    const auto rate = 48000.0;
    const auto n = samplesFor (3.0, rate);
    const auto x = sum (vowel (150.0, n, rate, 0.1, 1), bandNoise (4500.0, 12000.0, n, rate, 3, 0.05));
    ConstantDepths depths;
    depths.ess = depthOfDb (6.0);

    struct Variant
    {
        const char* name;
        dsp::SpectralTuning tuning;
    };
    const std::vector<Variant> variants {
        { "raw bins", { 0.0, 0.0, 0.0 } },
        { "1/6 octave", { 0.0, 1.0 / 6.0, 0.0 } },
        { "1/6 octave + 10 ms", { 10.0, 1.0 / 6.0, 0.0 } },
        { "1/6 octave + 10 ms, gains 2.5 ms (built)", { 10.0, 1.0 / 6.0, 2.5 } },
        { "1/6 octave + 10 ms, gains 5 ms", { 10.0, 1.0 / 6.0, 5.0 } },
        { "1/3 octave + 10 ms, gains 2.5 ms", { 10.0, 1.0 / 3.0, 2.5 } },
        { "1/6 octave + 20 ms, gains 2.5 ms", { 20.0, 1.0 / 6.0, 2.5 } },
    };

    std::printf ("\nSpectral, musical noise at 48 kHz (noise ess 4.5 ... 12 kHz over a vowel, 6 dB asked):\n");
    std::printf ("  %-44s %14s %14s %12s\n", "level estimate, gains", "gain std (dB)", "spread (dB)", "lost (dB)");
    for (const auto& v : variants)
    {
        dsp::SpectralShape shape;
        shape.prepare (rate, 512, v.tuning);
        shape.start (1, false);
        const auto& a = shape.analysis (0);
        const auto first = (int) std::ceil (4500.0 / a.binHz()), last = (int) std::floor (12000.0 / a.binHz());
        std::vector<std::vector<double>> history ((size_t) (last - first + 1));
        auto spread = 0.0;
        auto frames = 0;
        Samples removed (x.size(), 0.0f), unused (x.size(), 0.0f);
        for (int at = 0; at + 128 <= n; at += 128)
        {
            const float* in[] { x.data() + at };
            float* e[] { removed.data() + at };
            float* h[] { unused.data() + at };
            shape.process (in, e, h, 128, at, {}, {}, depths, { 0, 0 });
            if (at < samplesFor (0.8, rate))
                continue;
            auto sum = 0.0, sumSquares = 0.0;
            for (int k = first; k <= last; ++k)
            {
                const auto db = 20.0 * std::log10 (std::max (a.essGains()[k], 1.0e-6f));
                history[(size_t) (k - first)].push_back (db);
                sum += db;
                sumSquares += db * db;
            }
            const auto count = last - first + 1;
            spread += std::sqrt (std::max (0.0, sumSquares / count - (sum / count) * (sum / count)));
            ++frames;
        }
        auto deviation = 0.0;
        for (const auto& h : history)
        {
            auto m = 0.0, q = 0.0;
            for (const auto d : h)
                m += d;
            m /= (double) h.size();
            for (const auto d : h)
                q += (d - m) * (d - m);
            deviation += std::sqrt (q / (double) h.size());
        }

        Samples out (x.size());
        for (size_t i = 0; i + 512 < x.size(); ++i)
            out[i] = x[i] - removed[i + 512];
        dsp::BandFilter band;
        band.setBand (4500.0, 12000.0, rate);
        Samples bandIn (x.size()), bandOut (x.size());
        for (size_t i = 0; i < x.size(); ++i)
            bandIn[i] = (float) band.process ((double) x[i]);
        band.reset();
        for (size_t i = 0; i < x.size(); ++i)
            bandOut[i] = (float) band.process ((double) out[i]);
        const auto lost = 20.0 * std::log10 (rmsOf (bandIn, samplesFor (0.8, rate), samplesFor (2.9, rate)) / rmsOf (bandOut, samplesFor (0.8, rate), samplesFor (2.9, rate)));
        std::printf ("  %-44s %14.2f %14.2f %12.2f\n", v.name, deviation / (double) history.size(), spread / frames, lost);
    }
}

TEST_CASE ("Spectral: the learned target on the m1 corpus, report", "[.corpus][spectral]")
{
    // The neutral curve's slope and level (src/dsp/shapes/spectral/NOTES.md, Warm start): what the
    // target learns on each speaker of a split (HALITE_SPLIT, tune by default), once its window is
    // full, every 0.5 s: the slope over 4.5 ... 12 kHz and the bin at 4.5 kHz under the body.
    const auto cache = corpus::cacheDirectory();
    const auto split = juce::SystemStats::getEnvironmentVariable ("HALITE_SPLIT", "tune").toStdString();
    for (const auto* name : { "vctk", "vocalset" })
    {
        const auto entries = corpus::readManifest (cache.getChildFile ("labels/m1").getChildFile (std::string (name) + ".tsv"), cache, name);
        if (entries.empty())
        {
            WARN ("no m1 manifest for " << name << " under " << cache.getFullPathName());
            continue;
        }

        std::map<std::string, std::vector<corpus::Entry>> speakers;
        for (const auto& e : entries)
            if (e.split == split)
                speakers[e.speaker].push_back (e);

        std::vector<double> slopes, levels, shares;
        std::printf ("\nThe learned target, %s, %s (%zu speakers): median over the speaker's readings\n", name, split.c_str(), speakers.size());
        std::printf ("  %-8s %10s %10s %10s %8s\n", "speaker", "seconds", "dB/oct", "4.5 kHz", "learned");
        for (const auto& [speaker, list] : speakers)
        {
            std::vector<corpus::Recording> recordings;
            for (const auto& e : list)
            {
                corpus::Recording r;
                juce::String error;
                if (corpus::load (e, r, error))
                    recordings.push_back (std::move (r));
            }
            auto streamed = corpus::streams (std::move (recordings));
            if (streamed.empty())
                continue;
            auto& stream = streamed.front();
            const auto rate = stream.sampleRate;

            ChainRun run;
            run.rate = rate;
            run.settings.shape = Shape::spectral;
            dsp::HaliteChain chain;
            chain.prepare (rate, 512, 1, LatencyMode::normal);
            const auto wanted = (int) std::lround (dsp::SpectralAnalysis::learnSeconds * rate / (dsp::HaliteChain::frameSize (rate) / 4));
            std::vector<double> s, l;
            const auto every = samplesFor (0.5, rate);
            auto next = every;
            auto& audio = stream.audio;
            const juce::ScopedNoDenormals noDenormals;
            for (int pos = 0; pos + 512 <= (int) audio.size(); pos += 512)
            {
                float* channels[] { audio.data() + pos };
                chain.process (channels, 1, 512, run.settings);
                if (pos < next)
                    continue;
                next += every;
                const auto& a = chain.runningLane().spectralShape().analysis (0);
                if (a.learnedFrames() < wanted)
                    continue;
                const auto first = (int) std::ceil (4500.0 / a.binHz()), last = (int) std::floor (12000.0 / a.binHz());
                s.push_back (slopeDbPerOctave (a.learnedTarget(), first, last));
                l.push_back (10.0 * std::log10 (a.learnedTarget()[(size_t) first]));
            }
            if (s.empty())
                continue;
            std::sort (s.begin(), s.end());
            std::sort (l.begin(), l.end());
            const auto& a = chain.runningLane().spectralShape().analysis (0);
            const auto share = a.framesLearned() / (audio.size() / (dsp::HaliteChain::frameSize (rate) / 4.0));
            slopes.push_back (s[s.size() / 2]);
            levels.push_back (l[l.size() / 2]);
            shares.push_back (share);
            std::printf ("  %-8s %10.1f %10.2f %10.2f %7.0f %%\n", speaker.c_str(), audio.size() / rate, s[s.size() / 2], l[l.size() / 2], 100.0 * share);
        }
        const auto quartiles = [] (std::vector<double> v)
        {
            std::sort (v.begin(), v.end());
            return std::array { v[v.size() / 4], v[v.size() / 2], v[(3 * v.size()) / 4] };
        };
        if (! slopes.empty())
        {
            const auto qs = quartiles (slopes), ql = quartiles (levels), qf = quartiles (shares);
            std::printf ("  quartiles: slope %.2f / %.2f / %.2f dB/oct, 4.5 kHz %.2f / %.2f / %.2f dB, learned %.0f / %.0f / %.0f %% of frames\n",
                         qs[0], qs[1], qs[2], ql[0], ql[1], ql[2], 100.0 * qf[0], 100.0 * qf[1], 100.0 * qf[2]);
        }
    }
}

TEST_CASE ("Spectral: the gain smoothing and the learning constants, report", "[.report]")
{
    // Spectral's constants against what they trade (NOTES.md, M3). The gains' one-pole: the
    // musical-noise proxy of the report above, and how long the band takes to reach a depth that
    // steps on (frames at 48 kHz are 2.67 ms apart). The learning guard and the voiced rule: what the
    // target learns (slope, against the voice's) and how fast (the share of frames learned, which sets
    // how long the warm start lasts: 2 s of learned frames).
    const auto rate = 48000.0;

    struct Step final : dsp::ReductionReader
    {
        std::int64_t from = 0;
        float depth = 0.0f;
        float depthAt (std::int64_t time, int track) const noexcept override { return track < 2 && time >= from ? depth : 0.0f; }
        float largestTarget (std::int64_t, std::int64_t, int) const noexcept override { return 6.0f; }     // nothing learned: the neutral curve, as above
    };

    std::printf ("\nSpectral, the gains' one-pole at 48 kHz (noise ess 4.5 ... 12 kHz over a vowel, 6 dB asked from 1 s on):\n");
    std::printf ("  %-10s %14s %22s %26s\n", "gains", "gain std (dB)", "to within 0.5 dB (ms)", "short over 10 ms (dB)");
    {
        const auto n = samplesFor (3.0, rate);
        const auto x = sum (vowel (150.0, n, rate, 0.1, 1), bandNoise (4500.0, 12000.0, n, rate, 3, 0.05));
        for (const auto ms : { 0.0, 1.25, 2.5, 5.0, 10.0 })
        {
            dsp::SpectralShape shape;
            shape.prepare (rate, 512, { .gainMs = ms });
            shape.start (1, false);
            Step step;
            step.from = samplesFor (1.0, rate);
            step.depth = depthOfDb (6.0);
            const auto& a = shape.analysis (0);
            const auto first = (int) std::ceil (4500.0 / a.binHz()), last = (int) std::floor (12000.0 / a.binHz());
            std::vector<std::vector<double>> history ((size_t) (last - first + 1));
            std::vector<std::pair<double, double>> mean;     // frame centre (s after the step), mean gain over the band (dB)
            Samples removed (x.size(), 0.0f), unused (x.size(), 0.0f);
            for (int at = 0; at + 128 <= n; at += 128)
            {
                const float* in[] { x.data() + at };
                float* e[] { removed.data() + at };
                float* h[] { unused.data() + at };
                shape.process (in, e, h, 128, at, {}, {}, step, { 0, 0 });
                const auto centre = (double) (at + 128 - 256 - step.from) / rate;
                if (centre < 0.0)
                    continue;
                auto sum = 0.0;
                for (int k = first; k <= last; ++k)
                {
                    const auto db = 20.0 * std::log10 (std::max (a.essGains()[k], 1.0e-6f));
                    sum += db;
                    if (centre > 0.3)
                        history[(size_t) (k - first)].push_back (db);
                }
                mean.emplace_back (centre, sum / (last - first + 1));
            }
            auto steady = 0.0;
            auto count = 0;
            for (const auto& [t, db] : mean)
                if (t > 0.3)
                {
                    steady += db;
                    ++count;
                }
            steady /= std::max (1, count);
            auto within = -1.0, shortBy = 0.0;
            auto early = 0;
            for (const auto& [t, db] : mean)
            {
                if (within < 0.0 && std::abs (db - steady) <= 0.5)
                    within = 1000.0 * t;
                if (t < 0.010)
                {
                    shortBy += db - steady;
                    ++early;
                }
            }
            auto deviation = 0.0;
            for (const auto& h : history)
            {
                auto m = 0.0, q = 0.0;
                for (const auto d : h)
                    m += d;
                m /= (double) h.size();
                for (const auto d : h)
                    q += (d - m) * (d - m);
                deviation += std::sqrt (q / (double) h.size());
            }
            std::printf ("  %-10.2f %14.2f %22.1f %26.2f\n", ms, deviation / (double) history.size(), within, shortBy / std::max (1, early));
        }
    }

    // The detector's own targets for a signal, as the chain hands them to the engine.
    struct Targets final : dsp::ReductionReader
    {
        std::vector<float> perHop;
        float depthAt (std::int64_t, int) const noexcept override { return 0.0f; }
        float largestTarget (std::int64_t from, std::int64_t to, int) const noexcept override
        {
            auto largest = 0.0f;
            for (auto h = std::max<std::int64_t> (0, from / 32); h * 32 < to && h < (std::int64_t) perHop.size(); ++h)
                largest = std::max (largest, perHop[(size_t) h]);
            return largest;
        }
    };
    const auto learn = [rate] (const Samples& x, const dsp::SpectralTuning& tuning)
    {
        Targets targets;
        for (const auto& h : probeOf (x, rate, {}).hops)
            targets.perHop.push_back (h.decisions[0].essTargetDb);
        dsp::SpectralShape shape;
        shape.prepare (rate, 512, tuning);
        shape.start (1, false);
        runEngine (shape, x, 0, targets);
        const auto& a = shape.analysis (0);
        const auto first = (int) std::ceil (4500.0 / a.binHz()), last = (int) std::floor (12000.0 / a.binHz());
        return std::pair { slopeDbPerOctave (a.learnedTarget(), first, last), a.framesLearned() / (x.size() / 128.0) };
    };

    {
        const auto n = samplesFor (6.0, rate);
        const auto voice = harmonic ({ .f0 = 150.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -12.0, .rms = 0.1 }, n, rate);
        auto sibilant = voice;
        for (auto t = 0.2; t < 5.8; t += 0.3)
            sibilant = sum (sibilant, gated (bandNoise (4500.0, 12000.0, n, rate, (unsigned) (t * 10.0), 0.05), samplesFor (t, rate),
                                             samplesFor (t + 0.12, rate), rate));
        std::printf ("\nSpectral, the learning guard (a -12 dB/oct vowel, an ess every 0.3 s; the detector's own targets):\n");
        std::printf ("  %-10s %14s %16s %26s\n", "guard (ms)", "slope (dB/oct)", "frames learned", "2 s learned after (s)");
        for (const auto ms : { 0.0, 2.0, 4.0, 8.0, 16.0, 32.0 })
        {
            const auto [slope, share] = learn (sibilant, { .onsetGuardMs = ms });
            std::printf ("  %-10.0f %14.2f %15.0f %% %26.2f\n", ms, slope, 100.0 * share, 2.0 / std::max (share, 1.0e-6));
        }
    }

    {
        const auto n = samplesFor (8.0, rate);
        std::printf ("\nSpectral, the voiced rule (a -12 dB/oct vowel with a pause every other half second, white noise in the pauses only,\n"
                     "its RMS under the vowel's; the noise's body band holds about 10 dB less than its RMS):\n");
        std::printf ("  %-12s %-10s %-11s %14s %16s\n", "noise (dB)", "gap (dB)", "memory (s)", "slope (dB/oct)", "frames learned");
        for (const auto hissDb : { -40.0, -30.0, -20.0, -10.0 })
        {
            auto voice = harmonic ({ .f0 = 150.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -12.0, .rms = 0.1 }, n, rate);
            auto hiss = bandNoise (20.0, 20000.0, n, rate, 77u, 0.1 * std::pow (10.0, hissDb / 20.0));
            for (int i = 0; i < n; ++i)
                ((i / samplesFor (0.5, rate)) % 2 == 1 ? voice : hiss)[(size_t) i] = 0.0f;
            const auto x = sum (voice, hiss);
            for (const auto gap : { 10.0, 20.0, 30.0 })
                for (const auto memory : { 0.5, 1.0, 2.0 })
                {
                    const auto [slope, share] = learn (x, { .voicedGapDb = gap, .bodyMemorySeconds = memory });
                    std::printf ("  %-12.0f %-10.0f %-11.1f %14.2f %15.0f %%\n", hissDb, gap, memory, slope, 100.0 * share);
                }
        }
    }
}
