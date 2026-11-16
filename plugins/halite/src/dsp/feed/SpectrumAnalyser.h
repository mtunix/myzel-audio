#pragma once

#include "dsp/common/RealFft.h"
#include "dsp/feed/SampleFifo.h"

#include <array>
#include <cstdint>
#include <vector>

namespace halite::dsp
{

struct SpectrumSettings
{
    double bandOctaves = 1.0 / 6.0;     // each point's band: as wide as Spectral's level estimate
    double attackMs = 20.0, releaseMs = 300.0;
    float floorDb = -150.0f;
};

/**
    The display's spectrum (PLAN.md, Parts, Spectrum; src/dsp/feed/NOTES.md), on the message
    thread from the feed's sample streams; it never touches Spectral's STFT. Each update() reads the
    newest window of the input and output streams, Hann-windowed, and gives each display point the
    power of the band a sixth of an octave wide around it, in dB of mean square (a full-scale sine
    reads −3.0 dB, as the detector's levels and the Absolute thresholds read), the channels'
    mean, with attack and release in power. Display points are spaced logarithmically from
    lowestHz to highestHz. No allocation after prepare(); not thread-safe (one owner).
*/
class SpectrumAnalyser
{
public:
    static constexpr double lowestHz = 200.0, highestHz = 20000.0;

    using Settings = SpectrumSettings;

    /** Allocates. The window: 4096 points at 44.1 and 48 kHz, doubled per doubling of the rate (85 ms). */
    void prepare (double sampleRate, int numPoints, const Settings& = {});

    /**
        Reads the newest window and moves the spectra on by the time the streams advanced since the
        last call (a stopped audio thread freezes them). False if nothing new arrived or the window
        could not be read intact.
    */
    bool update (const SampleFifo&) noexcept;

    /** Back to the floor, as before the first update (a new view, a resumed display). */
    void clear() noexcept;

    int numPoints() const noexcept { return (int) pointHz.size(); }
    const float* inputDb() const noexcept { return input.data(); }
    const float* outputDb() const noexcept { return output.data(); }

    /** The display points' frequencies, lowestHz … highestHz. */
    const float* frequencies() const noexcept { return pointHz.data(); }

    int fftSize() const noexcept { return fft.size(); }
    double getSampleRate() const noexcept { return rate; }

    /**
        The same band levels for any spectrum given as mean-square shares per bin (bin k centred at
        k · binHz, up to SpectralSnapshot::maxBins): Spectral's target, so its thin line reads on the
        spectrum's scale. Writes numPoints() values in dB.
    */
    void bandLevels (const float* meanSquarePerBin, int bins, double binHz, float* outDb) noexcept;

    /** The log axis: 0 at lowestHz, 1 at highestHz, and back. */
    static double proportionOf (double hz) noexcept;
    static double frequencyAt (double proportion) noexcept;

    static int fftSizeFor (double sampleRate) noexcept;

private:
    void bandPowers (const double* cumulative, int bins, double binHz, double* out) const noexcept;

    double rate = 48000.0;
    Settings settings;
    RealFft fft;
    std::vector<float> window, frame, re, im;
    std::vector<std::vector<float>> streams;
    std::vector<double> power, cumulative, bands, bandScratch;
    std::vector<float> pointHz, input, output;
    std::vector<double> inputPower, outputPower;
    std::uint64_t lastEnd = 0;
    bool started = false;
};

/**
    The In and Out meters (PLAN.md, Parts, Meters) from the same streams: per stream, the sample
    peak since the last update and the RMS over an exponential window, both in dB (RMS as mean
    square, so a full-scale sine reads −3.0 dB, as the detector does). Every sample counts, as long
    as the reader comes back within about a second (the first update reads the last second). Message
    thread; no allocation.
*/
class LevelMeters
{
public:
    static constexpr double rmsMs = 300.0;
    static constexpr float floorDb = -150.0f;

    void prepare (double sampleRate) noexcept;

    /** Reads every sample since the last call. False if none arrived. */
    bool update (const SampleFifo&) noexcept;

    /** SampleFifo::inputStream (c) or outputStream (c). */
    float peakDb (int stream) const noexcept { return peak[(size_t) stream]; }
    float rmsDb (int stream) const noexcept { return rms[(size_t) stream]; }

private:
    double keep = 0.0;
    std::uint64_t cursor = 0;
    bool started = false;
    std::array<double, SampleFifo::numStreams> meanSquare {};
    std::array<float, SampleFifo::numStreams> peak {}, rms {};
    std::array<std::array<float, 1024>, SampleFifo::numStreams> chunk {};
};

} // namespace halite::dsp
