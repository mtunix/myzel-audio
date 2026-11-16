#pragma once

#include "common/dsp/Filters.h"
#include "dsp/common/RealFft.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace halite::dsp
{

/**
    Noise cue (a): how periodic the band's envelope is (NOTES.md, "A candidate cue that does not
    need resolved harmonics" and "On onsets, rises and decays").

    The band signal is squared, low-passed at 1.2 kHz (4th order, also the anti-alias filter),
    decimated to about 6 kHz, high-passed at 60 Hz (the detrending: with only the mean removed,
    onsets and decays read periodic) and correlated with itself over a 21.3 ms window at lags of
    1 ms up to the longest lag (12.5 ms: f0 80 Hz … 1 kHz). value() is the largest normalised
    correlation of the window with the window one lag earlier: about 0.3 on noise, above 0.9 on a
    steady or vibrato harmonic sound, between for voiced fricatives.

    The correlations are running sums per lag in double precision. The envelope's level goes with
    the square of the input's power, so after a loud passage the sums would keep its rounding
    residue for good; one lag (or the energy) is summed again in full every few decimated samples,
    round robin, so every sum is exact again within recomputeCycleMs.
*/
class PeriodicityCue
{
public:
    static constexpr double recomputeCycleMs = 50.0;

    /** Allocates. */
    void prepare (double sampleRate, double longestLagMs = 12.5);
    void reset() noexcept;

    void push (double band) noexcept
    {
        const auto envelope = antiAlias[1].processLowpass (antiAlias[0].processLowpass (band * band));
        if (++phase >= decimation)
        {
            phase = 0;
            pushDecimated (detrend.processHighpass (envelope));
        }
    }

    /** The next sample pushed is at this timeline position: it decimates on multiples of its factor there. */
    void anchor (std::int64_t timelinePosition) noexcept
    {
        phase = (int) (((timelinePosition % decimation) + decimation) % decimation);
    }

    /** 0 … 1; 0 while the window holds no energy. */
    double value() const noexcept;

    int decimationFactor() const noexcept { return decimation; }
    int windowLength() const noexcept { return window; }
    int shortestLag() const noexcept { return minLag; }
    int longestLag() const noexcept { return maxLag; }

private:
    void pushDecimated (double e) noexcept;

    /** The newest decimated sample; [-j] is the one j samples earlier, for j < size. */
    const double* newest() const noexcept { return history.data() + size + (int) (count & (std::uint64_t) mask); }

    std::array<myzel::dsp::TptSvf, 2> antiAlias;
    myzel::dsp::TptSvf detrend;
    int decimation = 8, phase = 0;
    int window = 128, minLag = 6, maxLag = 75, recomputeInterval = 4;
    int size = 0, mask = 0;
    std::uint64_t count = 0;
    int untilRecompute = 0, nextRecompute = 0;     // nextRecompute 0: the energy, otherwise a lag
    double energy = 0.0;                            // over the window ending now
    std::vector<double> history;                    // twice the ring, each value written to both halves
    std::vector<double> inverseRoots;               // 1 / √energy of the window ending at each decimated sample
    std::vector<double> sums;
};

/**
    Noise cue (b): spectral flatness of the band over a long frame (NOTES.md, "Spectral flatness").

    2048 points at 44.1/48 kHz, scaled with the rate (4096 at 88.2/96, 8192 at 176.4/192), Hann,
    one frame every quarter frame (10.7 ms at 48 kHz), ending at the newest sample. Each bin of
    the band is divided by the mean power of the bins within ±375 Hz before the flatness is taken,
    so the spectral envelope of the ess (its peak, the band's skirts) does not read as tonality:
    only the fine structure counts. value() is the flatness in dB: about −2.5 dB for noise (e^−γ,
    a single periodogram), far below for resolved harmonics.
*/
class FlatnessCue
{
public:
    static constexpr double envelopeHalfWidthHz = 375.0;

    /** Allocates. */
    void prepare (double sampleRate);
    void reset() noexcept;

    /** The band whose bins are analysed, in Hz. Real-time safe. */
    void setBand (double lowHz, double highHz) noexcept;

    void push (float band) noexcept
    {
        frame[(size_t) writeIndex] = band;
        writeIndex = (writeIndex + 1) & (frameLength - 1);
    }

    /** Analyses the newest frame. Real-time safe. */
    void analyse() noexcept;

    double value() const noexcept { return flatnessDb; }

    int frameSize() const noexcept { return frameLength; }
    int hopSize() const noexcept { return frameLength / 4; }

private:
    double sampleRate = 48000.0;
    int frameLength = 2048, writeIndex = 0;
    int firstBin = 0, lastBin = -1, halfWidth = 16;
    RealFft fft;
    std::vector<float> frame, window, work, binRe, binIm;
    std::vector<double> power;
    double flatnessDb = 0.0;
};

/**
    Noise-likeness 0 … 1 from each cue's raw value (1: noise, 0: harmonic), linear between the two
    ends of DetectorTuning (NOTES.md, Measured (M1)).
*/
struct NoiseMapping
{
    double periodicNoise = 0.55, periodicHarmonic = 0.70;
    double flatNoiseDb = -4.5, flatHarmonicDb = -8.0;

    double fromPeriodicity (double rho) const noexcept
    {
        return std::clamp ((periodicHarmonic - rho) / (periodicHarmonic - periodicNoise), 0.0, 1.0);
    }

    double fromFlatness (double db) const noexcept
    {
        return std::clamp ((db - flatHarmonicDb) / (flatNoiseDb - flatHarmonicDb), 0.0, 1.0);
    }
};

} // namespace halite::dsp
