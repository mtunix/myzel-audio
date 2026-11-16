#include "dsp/shapes/spectral/SpectralShape.h"

#include "dsp/detector/Detector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace halite::dsp
{

namespace
{
    constexpr int reductionHop = Detector::hopSize;
    constexpr int essTrack (int reduction) noexcept { return reduction; }
    constexpr int harshTrack (int reduction) noexcept { return 2 + reduction; }

    std::int64_t floorDiv (std::int64_t a, std::int64_t b) noexcept
    {
        const auto q = a / b;
        return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
    }

    /** The power response of the detector's band filter (Levels.h, BandFilter) at hz: bilinear Butterworth edges. */
    double bandResponse (double hz, double low, double high, double rate) noexcept
    {
        const auto warp = [rate] (double f) { return std::tan (std::numbers::pi * std::clamp (f, 1.0, 0.49 * rate) / rate); };
        if (hz <= 0.0)
            return 0.0;
        const auto w = warp (hz), wl = warp (low), wh = warp (high);
        return 1.0 / (1.0 + std::pow (wl / w, 4.0)) / (1.0 + std::pow (w / wh, 4.0));
    }

    /**
        The band's power gains r_k = clamp (λ shape_k / level_k, lower_k, 1), with λ chosen so that
        this frame's band, weighted, keeps `kept` of its own power, as far as the lower bounds allow.
        The bins past the band (the band filter's skirts) are not in the sum: the band itself loses
        the reduction (Settled 39, 44). The smoothed level decides which bins stick out of the shape
        and so are cut first and furthest; the frame's own power decides λ, so the frame loses
        exactly what is asked and the gains are not biased towards the bins that happen to be loud
        in it. Only the shape's form matters, not its scale. `gains` holds each bin's shape / level
        on entry, `lower` each bin's lowest gain.
    */
    void waterFill (const double* power, const double* weight, const double* lower, int first, int last, double kept, double* gains) noexcept
    {
        auto total = 0.0, floorTotal = 0.0, lo = std::numeric_limits<double>::max(), hi = 0.0;
        for (int k = first; k <= last; ++k)
        {
            const auto a = weight[k] * power[k];
            if (! (a > 0.0) || ! (gains[k] > 0.0) || ! (lower[k] < 1.0))
            {
                gains[k] = -1.0;    // holds nothing, or may not be cut: left alone
                total += a > 0.0 ? a : 0.0;
                floorTotal += a > 0.0 ? a : 0.0;
                continue;
            }
            total += a;
            floorTotal += a * lower[k];
            gains[k] = std::min (gains[k], 1.0e30);
            lo = std::min (lo, lower[k] / gains[k]);
            hi = std::max (hi, 1.0 / gains[k]);
        }

        const auto wanted = kept * total;
        if (! (total > 1.0e-30) || wanted >= total || ! (hi > 0.0))
        {
            std::fill (gains + first, gains + last + 1, 1.0);
            return;
        }

        const auto keptAt = [&] (double lambda)
        {
            auto f = 0.0;
            for (int k = first; k <= last; ++k)
            {
                const auto a = weight[k] * power[k];
                if (a > 0.0)
                    f += gains[k] > 0.0 ? a * std::clamp (lambda * gains[k], lower[k], 1.0) : a;
            }
            return f;
        };

        auto lambda = lo;
        if (wanted > floorTotal)
        {
            // The kept power is piecewise linear and rising in λ: halve the bracket in log λ, then
            // interpolate inside it, where no bin changes regime any more.
            for (int i = 0; i < 40; ++i)
            {
                const auto mid = std::sqrt (lo * hi);
                if (keptAt (mid) < wanted)
                    lo = mid;
                else
                    hi = mid;
            }
            const auto fLo = keptAt (lo), fHi = keptAt (hi);
            lambda = fHi > fLo ? lo + (wanted - fLo) * (hi - lo) / (fHi - fLo) : hi;
        }

        for (int k = first; k <= last; ++k)
            gains[k] = gains[k] > 0.0 ? std::clamp (lambda * gains[k], lower[k], 1.0) : 1.0;
    }
}

//==============================================================================
double SpectralAnalysis::neutralAt (double hz) noexcept
{
    return std::pow (10.0, (neutralDb + neutralSlopeDb * std::log2 (std::max (hz, 20.0) / 4500.0)) / 10.0);
}

void SpectralAnalysis::prepare (double sampleRate, int frameSize, const SpectralTuning& tuning)
{
    rate = sampleRate;
    n = frameSize;
    bins = n / 2 + 1;
    hop = n / 4;
    binWidth = rate / n;
    learnBins = std::min (bins, (int) std::ceil (learnTopHz / binWidth) + 1);

    const auto hopSeconds = hop / rate;
    levelKeep = tuning.levelMs > 0.0 ? std::exp (-hopSeconds / (tuning.levelMs * 0.001)) : 0.0;
    allroundKeep = std::exp (-hopSeconds / (allroundMs * 0.001));
    gainStep = tuning.gainMs > 0.0 ? 1.0 - std::exp (-hopSeconds / (tuning.gainMs * 0.001)) : 1.0;
    bodyFall = std::exp (-hopSeconds / tuning.bodyMemorySeconds);
    // A one-sided Hann-windowed power spectrum, summed, over the mean square.
    powerScale = 16.0 / (3.0 * (double) n * (double) n);
    floorPower = std::pow (10.0, bodyFloorDb / 10.0);
    gapRatio = std::pow (10.0, -tuning.voicedGapDb / 10.0);
    windowWanted = std::max (1, (int) std::lround (learnSeconds / hopSeconds));
    ringBlocks = std::max (1, (int) std::ceil (ringSeconds / (hopSeconds * blockFrames)));
    // A frame is decided once the detector has decided every hop it spans and the guard after it.
    guardSamples = (int) std::lround (tuning.onsetGuardMs * 0.001 * rate);
    decideDelayFrames = 1 + (guardSamples + hop - 1) / hop;
    pendingSlots = decideDelayFrames + 2;

    const auto rangeAround = [] (int k, double below, double above, int count)
    {
        return Range { std::clamp ((int) std::lround (below), 0, k), std::clamp ((int) std::lround (above), k, count - 1) };
    };
    sixth.resize ((size_t) bins);
    octave.resize ((size_t) bins);
    for (int k = 0; k < bins; ++k)
    {
        sixth[(size_t) k] = rangeAround (k, k * std::exp2 (-tuning.levelOctaves / 2.0), k * std::exp2 (tuning.levelOctaves / 2.0), bins);
        octave[(size_t) k] = rangeAround (k, k * std::exp2 (-allroundOctaves / 2.0), k * std::exp2 (allroundOctaves / 2.0), bins);
    }
    erb.resize ((size_t) learnBins);
    neutral.resize ((size_t) learnBins);
    for (int k = 0; k < learnBins; ++k)
    {
        // Glasberg and Moore (1990): ERB = 24.7 (4.37 f / 1000 + 1) Hz.
        const auto hz = k * binWidth;
        const auto half = 0.5 * 24.7 * (4.37 * hz / 1000.0 + 1.0) / binWidth;
        erb[(size_t) k] = rangeAround (k, k - half, k + half, learnBins);
        neutral[(size_t) k] = std::log (neutralAt (std::max (hz, binWidth)));
    }

    for (auto* v : { &power, &level, &smoothed, &allround, &shapeScratch, &lowerScratch, &gainScratch, &essWeight, &harshWeight })
        v->assign ((size_t) bins, 0.0);
    prefix.assign ((size_t) bins + 1, 0.0);
    essGain.assign ((size_t) bins, 1.0f);
    harshGain.assign ((size_t) bins, 1.0f);

    pendingRelative.assign ((size_t) (pendingSlots * learnBins), 0.0f);
    pendingEnd.assign ((size_t) pendingSlots, 0);
    pendingVoiced.assign ((size_t) pendingSlots, 0);
    ringSums.assign ((size_t) (ringBlocks * learnBins), 0.0f);
    ringCounts.assign ((size_t) ringBlocks, 0);
    blockSums.assign ((size_t) learnBins, 0.0);
    windowSums.assign ((size_t) learnBins, 0.0);
    target.assign ((size_t) learnBins, 0.0);

    bandsSet = false;
    reset();
}

void SpectralAnalysis::reset() noexcept
{
    std::fill (smoothed.begin(), smoothed.end(), 0.0);
    std::fill (allround.begin(), allround.end(), 0.0);
    std::fill (essGain.begin(), essGain.end(), 1.0f);
    std::fill (harshGain.begin(), harshGain.end(), 1.0f);
    essOn = harshOn = false;

    pendingFirst = pendingCount = 0;
    bodyRecent = 0.0;
    ringNewest = -1;
    ringUsed = 0;
    blockOpen = false;
    blockCount = 0;
    blockIndex = 0;
    learnedTotal = 0;
    updateTarget();
}

void SpectralAnalysis::takeTargetFrom (const SpectralAnalysis& other) noexcept
{
    std::copy (other.ringSums.begin(), other.ringSums.end(), ringSums.begin());
    std::copy (other.ringCounts.begin(), other.ringCounts.end(), ringCounts.begin());
    std::copy (other.blockSums.begin(), other.blockSums.end(), blockSums.begin());
    std::copy (other.target.begin(), other.target.end(), target.begin());
    ringNewest = other.ringNewest;
    ringUsed = other.ringUsed;
    blockCount = other.blockCount;
    blockIndex = other.blockIndex;
    blockOpen = other.blockOpen;
    windowFrames = other.windowFrames;
    learnedTotal = other.learnedTotal;
    bodyRecent = other.bodyRecent;
}

void SpectralAnalysis::setBands (const ShapeBands& b) noexcept
{
    using myzel::dsp::isExactly;
    if (bandsSet && isExactly (b.essLow, bands.essLow) && isExactly (b.essHigh, bands.essHigh) && isExactly (b.harshLow, bands.harshLow)
        && isExactly (b.harshHigh, bands.harshHigh))
        return;
    bands = b;
    bandsSet = true;

    const auto rangeOf = [this] (double low, double high)
    {
        Range r { std::clamp ((int) std::ceil (low / binWidth), 1, bins - 1), std::clamp ((int) std::floor (high / binWidth), 1, bins - 1) };
        r.last = std::max (r.first, r.last);
        return r;
    };
    ess = rangeOf (b.essLow, b.essHigh);
    harsh = rangeOf (b.harshLow, b.harshHigh);

    // The detector's body band (Detector::Path::setBands).
    const DetectorTuning tuning;
    body = rangeOf (Detector::bodyLowHz, std::min (tuning.bodyCapHz, b.essLow * tuning.bodyCapShare));

    for (int k = 0; k < bins; ++k)
    {
        essWeight[(size_t) k] = bandResponse (k * binWidth, b.essLow, b.essHigh, rate);
        harshWeight[(size_t) k] = bandResponse (k * binWidth, b.harshLow, b.harshHigh, rate);
    }
}

void SpectralAnalysis::frame (const float* const* re, const float* const* im, int numSignals, std::int64_t frameEnd,
                              const SpectralSettings& settings, const ReductionReader& reader, int reduction, bool learn) noexcept
{
    for (int k = 0; k < bins; ++k)
    {
        auto p = 0.0;
        for (int s = 0; s < numSignals; ++s)
            p += (double) re[s][k] * re[s][k] + (double) im[s][k] * im[s][k];
        power[(size_t) k] = p;
        smoothed[(size_t) k] += (1.0 - levelKeep) * (p - smoothed[(size_t) k]);
        allround[(size_t) k] += (1.0 - allroundKeep) * (p - allround[(size_t) k]);
    }

    if (learn)
        learnFrame (frameEnd, reader, reduction);

    // The depths at the frame's centre, or at the newest sample the detector has surely reduced
    // by now (it trails the key by its look-ahead), so the block size never changes the result.
    const auto known = floorDiv (frameEnd - settings.reductionDelay, reductionHop) * reductionHop - 1;
    const auto at = std::min (frameEnd - n / 2, known);
    const auto essDepth = reader.depthAt (at, essTrack (reduction));
    const auto harshDepth = settings.harsh ? reader.depthAt (at, harshTrack (reduction)) : 0.0f;

    if (! (essDepth > 0.0f) && ! (harshDepth > 0.0f))
    {
        if (essOn || harshOn)
        {
            std::fill (essGain.begin(), essGain.end(), 1.0f);
            std::fill (harshGain.begin(), harshGain.end(), 1.0f);
        }
        essOn = harshOn = false;
        return;
    }

    // The level estimate: smoothed over levelMs, then over a sixth of an octave.
    prefix[0] = 0.0;
    for (int k = 0; k < bins; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + smoothed[(size_t) k];
    for (int k = 0; k < bins; ++k)
    {
        const auto& r = sixth[(size_t) k];
        level[(size_t) k] = (prefix[(size_t) r.last + 1] - prefix[(size_t) r.first]) / (r.last - r.first + 1);
    }

    const auto deepestFor = [] (float rangeDb) { return std::pow (10.0, -std::max (rangeDb, 0.0f) / 10.0); };

    if (essDepth > 0.0f)
    {
        const auto deepest = deepestFor (settings.essRangeDb);
        if (settings.detection == Detection::voice)
        {
            // In Voice no bin is cut below the voice's own top end: the target at the current body level.
            auto bodyLevel = 0.0;
            for (int k = body.first; k <= body.last; ++k)
                bodyLevel += smoothed[(size_t) k];
            for (int k = ess.first; k <= ess.last; ++k)
            {
                const auto t = target[(size_t) std::min (k, learnBins - 1)];
                shapeScratch[(size_t) k] = t;
                lowerScratch[(size_t) k] = level[(size_t) k] > 0.0 ? std::clamp (t * bodyLevel / level[(size_t) k], deepest, 1.0) : deepest;
            }
        }
        else
        {
            prefix[0] = 0.0;
            for (int k = 0; k < bins; ++k)
                prefix[(size_t) k + 1] = prefix[(size_t) k] + allround[(size_t) k];
            for (int k = ess.first; k <= ess.last; ++k)
            {
                const auto& r = octave[(size_t) k];
                shapeScratch[(size_t) k] = (prefix[(size_t) r.last + 1] - prefix[(size_t) r.first]) / (r.last - r.first + 1);
                lowerScratch[(size_t) k] = deepest;
            }
        }
        bandGains (ess, essWeight, shapeScratch.data(), essDepth, essGain);
        essOn = true;
    }
    else if (essOn)
    {
        std::fill (essGain.begin(), essGain.end(), 1.0f);
        essOn = false;
    }

    if (harshDepth > 0.0f)
    {
        // Its own reference, no memory: the line in dB over log f through the level at the edges.
        const auto lowEdge = std::log (std::max (level[(size_t) harsh.first], 1.0e-30));
        const auto highEdge = std::log (std::max (level[(size_t) harsh.last], 1.0e-30));
        const auto span = std::log ((double) harsh.last / harsh.first);
        const auto deepest = deepestFor (settings.harshRangeDb);
        for (int k = harsh.first; k <= harsh.last; ++k)
        {
            const auto u = span > 0.0 ? std::log ((double) k / harsh.first) / span : 0.0;
            shapeScratch[(size_t) k] = std::exp (lowEdge + u * (highEdge - lowEdge));
            lowerScratch[(size_t) k] = deepest;
        }
        bandGains (harsh, harshWeight, shapeScratch.data(), harshDepth, harshGain);
        harshOn = true;
    }
    else if (harshOn)
    {
        std::fill (harshGain.begin(), harshGain.end(), 1.0f);
        harshOn = false;
    }
}

void SpectralAnalysis::referenceLevels (Detection detection, double* scratch, float* out) const noexcept
{
    if (detection == Detection::voice)
    {
        auto bodyLevel = 0.0;
        for (int k = body.first; k <= body.last; ++k)
            bodyLevel += smoothed[(size_t) k];
        for (int k = 0; k < bins; ++k)
            out[k] = (float) (target[(size_t) std::min (k, learnBins - 1)] * bodyLevel * powerScale);
        return;
    }

    scratch[0] = 0.0;
    for (int k = 0; k < bins; ++k)
        scratch[k + 1] = scratch[k] + allround[(size_t) k];
    for (int k = 0; k < bins; ++k)
    {
        const auto& r = octave[(size_t) k];
        out[k] = (float) ((scratch[r.last + 1] - scratch[r.first]) / (r.last - r.first + 1) * powerScale);
    }
}

void SpectralAnalysis::bandGains (const Range& band, const std::vector<double>& weight, const double* shape, float depth,
                                  std::vector<float>& gains) noexcept
{
    for (int k = band.first; k <= band.last; ++k)
        gainScratch[(size_t) k] = level[(size_t) k] > 0.0 ? shape[k] / level[(size_t) k] : 0.0;

    const auto kept = (1.0 - (double) depth) * (1.0 - (double) depth);
    waterFill (power.data(), weight.data(), lowerScratch.data(), band.first, band.last, kept, gainScratch.data());

    for (int k = 0; k < band.first; ++k)
        gains[(size_t) k] = 1.0f;
    for (int k = band.last + 1; k < bins; ++k)
        gains[(size_t) k] = 1.0f;
    for (int k = band.first; k <= band.last; ++k)
    {
        auto& g = gains[(size_t) k];
        g += (float) gainStep * ((float) std::sqrt (gainScratch[(size_t) k]) - g);
    }
}

void SpectralAnalysis::learnFrame (std::int64_t frameEnd, const ReductionReader& reader, int reduction) noexcept
{
    auto bodyPower = 0.0;
    for (int k = body.first; k <= body.last; ++k)
        bodyPower += power[(size_t) k];
    bodyRecent = std::max (bodyPower, bodyRecent * bodyFall);
    const auto voiced = bodyPower > 0.0 && bodyPower * powerScale >= floorPower && bodyPower >= bodyRecent * gapRatio;

    if (pendingCount == pendingSlots)
    {
        decide (pendingFirst, reader, reduction);
        pendingFirst = (pendingFirst + 1) % pendingSlots;
        --pendingCount;
    }
    const auto slot = (pendingFirst + pendingCount) % pendingSlots;
    auto* relative = pendingRelative.data() + (size_t) slot * (size_t) learnBins;
    for (int k = 0; k < learnBins; ++k)
        relative[k] = voiced ? (float) (power[(size_t) k] / bodyPower) : 0.0f;
    pendingEnd[(size_t) slot] = frameEnd;
    pendingVoiced[(size_t) slot] = voiced ? 1 : 0;
    ++pendingCount;

    while (pendingCount > 0 && pendingEnd[(size_t) pendingFirst] <= frameEnd - (std::int64_t) decideDelayFrames * hop)
    {
        decide (pendingFirst, reader, reduction);
        pendingFirst = (pendingFirst + 1) % pendingSlots;
        --pendingCount;
    }
}

void SpectralAnalysis::decide (int slot, const ReductionReader& reader, int reduction) noexcept
{
    const auto end = pendingEnd[(size_t) slot];
    const auto index = floorDiv (end, (std::int64_t) blockFrames * hop);
    if (! blockOpen || index != blockIndex)
    {
        if (blockOpen)
            closeBlock();
        blockOpen = true;
        blockIndex = index;
        blockCount = 0;
        std::fill (blockSums.begin(), blockSums.end(), 0.0);
    }

    // The detector decides an onset a few ms after it (B's averaging): the guard keeps the frame
    // that ends on an ess's first ms out.
    if (pendingVoiced[(size_t) slot] == 0 || reader.largestTarget (end - n, end + guardSamples, reduction) > 0.0f)
        return;

    // ERB-smoothed, then averaged in dB: a stray frame with an ess in it, 30 dB over the voice's
    // top end, moves the mean of 750 frames by a fraction of a dB, not by orders of magnitude.
    const auto* relative = pendingRelative.data() + (size_t) slot * (size_t) learnBins;
    prefix[0] = 0.0;
    for (int k = 0; k < learnBins; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + relative[k];
    for (int k = 0; k < learnBins; ++k)
    {
        const auto& r = erb[(size_t) k];
        const auto mean = (prefix[(size_t) r.last + 1] - prefix[(size_t) r.first]) / (r.last - r.first + 1);
        blockSums[(size_t) k] += std::log (std::max (mean, 1.0e-30));
    }
    ++blockCount;
    ++learnedTotal;
}

void SpectralAnalysis::closeBlock() noexcept
{
    if (blockCount == 0)
        return;

    ringNewest = (ringNewest + 1) % ringBlocks;
    ringUsed = std::min (ringUsed + 1, ringBlocks);
    auto* sums = ringSums.data() + (size_t) ringNewest * (size_t) learnBins;
    for (int k = 0; k < learnBins; ++k)
        sums[k] = (float) blockSums[(size_t) k];
    ringCounts[(size_t) ringNewest] = blockCount;
    updateTarget();
}

void SpectralAnalysis::updateTarget() noexcept
{
    // The newest blocks until they hold learnSeconds of frames, summed afresh: a finite memory.
    std::fill (windowSums.begin(), windowSums.end(), 0.0);
    auto count = 0;
    for (int i = 0; i < ringUsed && count < windowWanted; ++i)
    {
        const auto slot = (ringNewest - i + ringBlocks) % ringBlocks;
        const auto* sums = ringSums.data() + (size_t) slot * (size_t) learnBins;
        for (int k = 0; k < learnBins; ++k)
            windowSums[(size_t) k] += sums[k];
        count += ringCounts[(size_t) slot];
    }
    windowFrames = count;

    // Until it holds that many, the neutral curve stands in for the frames not heard yet.
    const auto missing = (double) std::max (0, windowWanted - count), total = (double) std::max (windowWanted, count);
    for (int k = 0; k < learnBins; ++k)
        target[(size_t) k] = std::exp ((windowSums[(size_t) k] + missing * neutral[(size_t) k]) / total);
}

//==============================================================================
void SpectralShape::prepare (double sampleRate, int frameSize, const SpectralTuning& tuning)
{
    for (auto& e : engines)
    {
        for (auto& s : e.stfts)
            s.prepare (frameSize, 2);
        for (auto& a : e.analyses)
            a.prepare (sampleRate, frameSize, tuning);
    }
    const auto bins = (size_t) engines[0].stfts[0].numBins();
    for (auto& signal : spectrum)
        for (auto& part : signal)
            part.assign (bins, 0.0f);
    for (auto& part : removed)
        part.assign (2 * bins, 0.0f);
    hop = engines[0].stfts[0].hopSize();
    frameCount = 0;
    start (1, false);
}

void SpectralShape::start (int numSignals, bool shareGains) noexcept
{
    signals = std::clamp (numSignals, 1, 2);
    coupled = shareGains && signals == 2;
    spliceLeft = 0;
    auto& e = engines[(size_t) running];
    for (auto& s : e.stfts)
        s.reset();
    for (auto& a : e.analyses)
        a.reset();
    e.gridOffset = 0;
}

void SpectralShape::takeTargetFrom (const SpectralShape& other) noexcept
{
    for (auto& a : engines[(size_t) running].analyses)
        a.takeTargetFrom (other.analysis (0));
}

void SpectralShape::fill (const float* const* in, int n) noexcept
{
    const std::array<float*, 2> nowhere {};
    auto& e = engines[(size_t) running];
    for (int s = 0; s < signals; ++s)
        e.stfts[(size_t) s].step (in[s], nowhere.data(), n);
}

void SpectralShape::restart (std::int64_t shift, std::int64_t boundary) noexcept
{
    // A second restart within the frame drops what the first one's outgoing engine had left to add.
    auto& outgoing = engines[(size_t) running];
    running = 1 - running;
    auto& incoming = engines[(size_t) running];
    for (size_t s = 0; s < incoming.stfts.size(); ++s)
        incoming.stfts[s].continueFrom (outgoing.stfts[s]);
    for (auto& a : incoming.analyses)
        a.reset();
    incoming.gridOffset = 0;

    outgoing.gridOffset -= shift;
    spliceLast = boundary - 1;
    spliceLeft = frameSize();
}

void SpectralShape::process (const float* const* in, float* const* essRemoved, float* const* harshRemoved, int n, std::int64_t position,
                             const ShapeBands& bands, const SpectralSettings& settings, const ReductionReader& reader,
                             const std::array<int, 2>& reductions) noexcept
{
    auto& now = engines[(size_t) running];
    if (spliceLeft <= 0)
    {
        run (now, in, essRemoved, harshRemoved, 0, n, position, bands, settings, reader, reductions, true);
        return;
    }

    // The audio in flight from the engine that analysed it; the new one runs unheard until it is done.
    const auto m = std::min (n, spliceLeft);
    Before before;
    before.reader = &reader;
    before.last = spliceLast;
    run (engines[(size_t) (1 - running)], in, essRemoved, harshRemoved, 0, m, position, bands, settings, before, reductions, false);
    run (now, in, nullptr, nullptr, 0, m, position, bands, settings, reader, reductions, true);
    spliceLeft -= m;
    if (m < n)
        run (now, in, essRemoved, harshRemoved, m, n - m, position + m, bands, settings, reader, reductions, true);
}

void SpectralShape::run (Engine& e, const float* const* in, float* const* essRemoved, float* const* harshRemoved, int offset, int n,
                         std::int64_t position, const ShapeBands& bands, const SpectralSettings& settings, const ReductionReader& reader,
                         const std::array<int, 2>& reductions, bool learn) noexcept
{
    const auto used = coupled ? 1 : signals;
    for (int a = 0; a < used; ++a)
        e.analyses[(size_t) a].setBands (bands);

    for (int i = 0; i < n;)
    {
        const auto t = position + i;
        const auto phase = (int) ((t - e.gridOffset) - floorDiv (t - e.gridOffset, hop) * hop);
        if (phase == 0)
            runFrame (e, t, settings, reader, reductions, learn);

        const auto m = std::min (n - i, hop - phase);
        for (int s = 0; s < signals; ++s)
        {
            std::array<float*, 2> out { essRemoved != nullptr ? essRemoved[s] + offset + i : nullptr,
                                        harshRemoved != nullptr ? harshRemoved[s] + offset + i : nullptr };
            e.stfts[(size_t) s].step (in[s] + offset + i, out.data(), m);
        }
        i += m;
    }
}

void SpectralShape::runFrame (Engine& engine, std::int64_t frameEnd, const SpectralSettings& settings, const ReductionReader& reader,
                              const std::array<int, 2>& reductions, bool learn) noexcept
{
    auto& stfts = engine.stfts;
    auto& analyses = engine.analyses;
    for (int s = 0; s < signals; ++s)
        stfts[(size_t) s].analyse (spectrum[(size_t) s][0].data(), spectrum[(size_t) s][1].data());
    frameCount += signals;

    if (coupled)
    {
        const std::array<const float*, 2> re { spectrum[0][0].data(), spectrum[1][0].data() };
        const std::array<const float*, 2> im { spectrum[0][1].data(), spectrum[1][1].data() };
        analyses[0].frame (re.data(), im.data(), 2, frameEnd, settings, reader, reductions[0], learn);
    }
    else
    {
        for (int s = 0; s < signals; ++s)
        {
            const float* re = spectrum[(size_t) s][0].data();
            const float* im = spectrum[(size_t) s][1].data();
            analyses[(size_t) s].frame (&re, &im, 1, frameEnd, settings, reader, reductions[(size_t) s], learn);
        }
    }

    const auto bins = stfts[0].numBins();
    for (int s = 0; s < signals; ++s)
    {
        const auto& a = analyses[(size_t) (coupled ? 0 : s)];
        if (! a.essActive() && ! a.harshActive())
            continue;

        // Each bin takes the smaller of the two bands' gains, so overlapping bands never cut a bin
        // twice; what it loses goes to the band whose gain that is.
        const auto* x = spectrum[(size_t) s][0].data();
        const auto* y = spectrum[(size_t) s][1].data();
        const auto* ge = a.essGains();
        const auto* gh = a.harshGains();
        auto* essRe = removed[0].data();
        auto* essIm = essRe + bins;
        auto* harshRe = removed[1].data();
        auto* harshIm = harshRe + bins;
        auto anyEss = false, anyHarsh = false;
        for (int k = 0; k < bins; ++k)
        {
            const auto e = ge[k], h = gh[k];
            essRe[k] = essIm[k] = harshRe[k] = harshIm[k] = 0.0f;
            if (e <= h && e < 1.0f)
            {
                essRe[k] = (1.0f - e) * x[k];
                essIm[k] = (1.0f - e) * y[k];
                anyEss = true;
            }
            else if (h < 1.0f)
            {
                harshRe[k] = (1.0f - h) * x[k];
                harshIm[k] = (1.0f - h) * y[k];
                anyHarsh = true;
            }
        }
        if (anyEss)
            stfts[(size_t) s].synthesise (essRe, essIm, 0);
        if (anyHarsh)
            stfts[(size_t) s].synthesise (harshRe, harshIm, 1);
    }
}

} // namespace halite::dsp
