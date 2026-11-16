#include "dsp/detector/NoiseCues.h"

#include <cmath>
#include <numbers>

namespace halite::dsp
{

namespace
{
    int powerOfTwoAtLeast (int n)
    {
        int size = 1;
        while (size < n)
            size <<= 1;
        return size;
    }
}

//==============================================================================
void PeriodicityCue::prepare (double sampleRate, double longestLagMs)
{
    // The constants of NOTES.md's measurement: 1024 samples and lags from 1 ms at 48 kHz, envelope
    // low-passed at 1.2 kHz and decimated to about 6 kHz.
    decimation = std::max (1, (int) std::lround (sampleRate / 6000.0));
    const auto decimatedRate = sampleRate / decimation;
    window = (int) std::lround (decimatedRate * 1024.0 / 48000.0);
    minLag = (int) std::ceil (decimatedRate * 0.001);
    maxLag = std::max (minLag, (int) std::lround (decimatedRate * longestLagMs * 0.001));
    recomputeInterval = std::max (1, (int) (recomputeCycleMs * 0.001 * decimatedRate / (maxLag - minLag + 2)));

    // 4th-order Butterworth: two sections at Q 0.541 and 1.307.
    antiAlias[0].setCoefficients (1200.0, 0.5411961, sampleRate);
    antiAlias[1].setCoefficients (1200.0, 1.3065630, sampleRate);
    detrend.setCoefficients (60.0, std::numbers::sqrt2 / 2.0, decimatedRate);

    size = powerOfTwoAtLeast (window + maxLag + 1);
    mask = size - 1;
    history.assign ((size_t) size * 2, 0.0);
    inverseRoots.assign ((size_t) size, 0.0);
    sums.assign ((size_t) maxLag + 1, 0.0);
    reset();
}

void PeriodicityCue::reset() noexcept
{
    for (auto& f : antiAlias)
        f.reset();
    detrend.reset();
    std::fill (history.begin(), history.end(), 0.0);
    std::fill (inverseRoots.begin(), inverseRoots.end(), 0.0);
    std::fill (sums.begin(), sums.end(), 0.0);
    phase = 0;
    count = 0;
    untilRecompute = 0;
    nextRecompute = 0;
    energy = 0.0;
}

void PeriodicityCue::pushDecimated (double e) noexcept
{
    ++count;
    const auto slot = (size_t) (count & (std::uint64_t) mask);
    history[slot] = history[slot + (size_t) size] = e;

    // h[-j] is the envelope j decimated samples ago.
    const auto* h = newest();
    const auto leaving = h[-window];
    energy += e * e - leaving * leaving;

    for (int k = minLag; k <= maxLag; ++k)
        sums[(size_t) k] += e * h[-k] - leaving * h[-window - k];

    if (--untilRecompute <= 0)
    {
        untilRecompute = recomputeInterval;
        const auto k = nextRecompute;
        auto exact = 0.0;
        for (int j = 0; j < window; ++j)
            exact += h[-j] * h[-j - k];

        if (k == 0)
            energy = exact;
        else
            sums[(size_t) k] = exact;
        nextRecompute = k == 0 ? minLag : (k < maxLag ? k + 1 : 0);
    }

    inverseRoots[slot] = energy > 1.0e-280 ? 1.0 / std::sqrt (energy) : 0.0;
}

double PeriodicityCue::value() const noexcept
{
    const auto now = inverseRoots[(size_t) (count & (std::uint64_t) mask)];
    if (now <= 0.0)
        return 0.0;

    auto best = 0.0;
    for (int k = minLag; k <= maxLag; ++k)
        best = std::max (best, sums[(size_t) k] * inverseRoots[(size_t) ((count - (std::uint64_t) k) & (std::uint64_t) mask)]);
    return std::min (best * now, 1.0);
}

//==============================================================================
void FlatnessCue::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    const auto order = 11 + std::max (0, (int) std::lround (std::log2 (sampleRate / 48000.0)));
    frameLength = 1 << order;
    fft.prepare (order);

    frame.assign ((size_t) frameLength, 0.0f);
    work.assign ((size_t) frameLength, 0.0f);
    binRe.assign ((size_t) frameLength / 2 + 1, 0.0f);
    binIm.assign ((size_t) frameLength / 2 + 1, 0.0f);
    power.assign ((size_t) frameLength / 2 + 1, 0.0);
    window.resize ((size_t) frameLength);
    for (int i = 0; i < frameLength; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / frameLength));

    halfWidth = std::max (2, (int) std::lround (envelopeHalfWidthHz * frameLength / sampleRate));
    reset();
}

void FlatnessCue::reset() noexcept
{
    std::fill (frame.begin(), frame.end(), 0.0f);
    writeIndex = 0;
    flatnessDb = 0.0;
}

void FlatnessCue::setBand (double lowHz, double highHz) noexcept
{
    const auto binHz = sampleRate / frameLength;
    firstBin = std::max (1, (int) std::ceil (lowHz / binHz));
    lastBin = std::min (frameLength / 2 - 1, (int) std::floor (highHz / binHz));
}

void FlatnessCue::analyse() noexcept
{
    constexpr int minBins = 4;
    if (lastBin - firstBin + 1 < minBins)
    {
        flatnessDb = 0.0;
        return;
    }

    // writeIndex is the oldest sample.
    for (int i = 0; i < frameLength; ++i)
        work[(size_t) i] = frame[(size_t) ((writeIndex + i) & (frameLength - 1))] * window[(size_t) i];

    fft.forward (work.data(), binRe.data(), binIm.data());

    for (int k = firstBin; k <= lastBin; ++k)
        power[(size_t) k] = (double) binRe[(size_t) k] * binRe[(size_t) k] + (double) binIm[(size_t) k] * binIm[(size_t) k];

    // The local mean is a running sum over the bins within ±halfWidth, clipped at the band edges.
    auto windowSum = 0.0;
    auto from = firstBin, to = firstBin;     // the sum covers [from, to)
    auto logSum = 0.0, peak = 0.0;
    for (int k = firstBin; k <= lastBin; ++k)
        peak = std::max (peak, power[(size_t) k]);

    if (! (peak > 0.0))
    {
        flatnessDb = 0.0;
        return;
    }

    // Relative to the band's peak, so the floor does not depend on the level.
    const auto floor = peak * 1.0e-15;
    for (int k = firstBin; k <= lastBin; ++k)
    {
        for (; to <= std::min (lastBin, k + halfWidth); ++to)
            windowSum += power[(size_t) to];
        for (; from < k - halfWidth; ++from)
            windowSum -= power[(size_t) from];

        const auto localMean = std::max (windowSum / (to - from), floor);
        logSum += std::log (std::max (power[(size_t) k], floor) / localMean);
    }

    flatnessDb = 10.0 / std::numbers::ln10 * logSum / (lastBin - firstBin + 1);
}

} // namespace halite::dsp
