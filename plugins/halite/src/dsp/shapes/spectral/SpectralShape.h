#pragma once

#include "dsp/Modes.h"
#include "dsp/common/Stft.h"
#include "dsp/shapes/Shapes.h"

#include <array>
#include <cstdint>
#include <vector>

namespace halite::dsp
{

/** What Spectral reads besides the audio and the bands, once per block. */
struct SpectralSettings
{
    Detection detection = Detection::voice;     // Voice: the learned target; Allround: the signal's own smoothed spectrum
    float essRangeDb = 8.0f, harshRangeDb = 4.0f;
    bool harsh = false;                         // the Harsh band runs (on, or still letting go)
    int reductionDelay = 0;                     // how far the detector's reductions trail the key, in samples
};

/** Spectral's smoothing against musical noise (spectral/NOTES.md); the plugin runs the defaults, the tests vary them. */
struct SpectralTuning
{
    double levelMs = 10.0;              // the level estimate's one-pole (0: none)
    double levelOctaves = 1.0 / 6.0;    // and its width in frequency (0: one bin)
    double gainMs = 2.5;                // the gains' one-pole (0: none)
    double onsetGuardMs = 8.0;          // a frame the detector calls sibilant within this after its end is not learned
    double voicedGapDb = 20.0;          // nor one whose body is this far under the recent body
    double bodyMemorySeconds = 1.0;     // the recent body falls by 1/e in this
};

/**
    One gain computer of the Spectral shape (spectral/NOTES.md): from one frame's spectrum (one
    signal, or two whose powers add, Linked) it keeps the smoothed level estimate and the targets,
    and gives each bin of each band its gain for this frame.

    - Level: each bin's power, a one-pole over levelMs, then the mean over a sixth of an octave
      (SpectralTuning).
    - Ess target, Voice: the long-term average spectrum of the frames the detector called
      non-sibilant (no Ess target on any hop they span, nor within the guard after), each
      relative to its body level, ERB-smoothed and averaged in dB; the last learnSeconds of them,
      in a ring of blocks anchored to the timeline; where fewer frames than that have been heard,
      the neutral curve fills the rest. Allround: the power over allroundMs, an octave wide.
    - Harsh target: a straight line in dB over log frequency through the level at the band's edges.
    - Gains: in a frame whose depth is above 0, the bins of the band that stick out above the
      target the most are cut towards it until the band's own bins, weighed as the detector weighs
      them, have lost the detector's reduction; no bin by more than Range and, in Voice, none below the
      voice's own top end (the target at the current body level). Then a one-pole over 2.5 ms.
      Bins outside the band, and every bin of a frame at depth 0, have gain exactly 1.
*/
class SpectralAnalysis
{
public:
    static constexpr double allroundMs = 300.0, allroundOctaves = 1.0;
    static constexpr double learnSeconds = 2.0;        // the target's memory, counted in frames that were learned
    static constexpr int blockFrames = 32;             // frames per ring block (85 ms at 48 kHz)
    static constexpr double ringSeconds = 8.0;         // the most the ring holds, counted in blocks
    static constexpr double bodyFloorDb = -60.0;       // nor one under this (dBFS)
    static constexpr double learnTopHz = 22050.0;      // learned bins reach this (Detect High's top is 20 kHz)
    static constexpr double neutralSlopeDb = -8.0;     // the neutral curve: VCTK's median over the m1 tune speakers (NOTES.md)
    static constexpr double neutralDb = -52.5;         // its bin at 4.5 kHz, dB under the body's summed bins

    /** Allocates. */
    void prepare (double sampleRate, int frameSize, const SpectralTuning& = {});

    /** State to zero, the target to the neutral curve. */
    void reset() noexcept;

    /** Takes over what another one has learned (its ring, its target), without allocating. Same prepare(). */
    void takeTargetFrom (const SpectralAnalysis&) noexcept;

    void setBands (const ShapeBands&) noexcept;

    /**
        One frame: `re`, `im` hold numSignals spectra (summed as powers), the frame ends at timeline
        position `frameEnd`. Reads the depths of reduction `reduction` at the frame's centre;
        `learn` lets the frame join the target.
    */
    void frame (const float* const* re, const float* const* im, int numSignals, std::int64_t frameEnd, const SpectralSettings&,
                const ReductionReader&, int reduction, bool learn) noexcept;

    /** Amplitude gains per bin after the last frame; exactly 1 where a band does nothing. */
    const float* essGains() const noexcept { return essGain.data(); }
    const float* harshGains() const noexcept { return harshGain.data(); }
    bool essActive() const noexcept { return essOn; }
    bool harshActive() const noexcept { return harshOn; }

    /** The Ess target now (Voice), relative to the body, per bin up to learnTopHz; for the tests and the display. */
    const std::vector<double>& learnedTarget() const noexcept { return target; }
    int learnedFrames() const noexcept { return windowFrames; }     // in the target's window now
    int framesLearned() const noexcept { return learnedTotal; }     // since the warm start
    double binHz() const noexcept { return binWidth; }

    /** The neutral curve: the target before any frame has been learned, relative to the body per bin. */
    static double neutralAt (double hz) noexcept;

    /** Bins [first, last]. */
    struct Range
    {
        int first = 0, last = -1;
    };

    /** The bins each band acts on; every other bin's gain is exactly 1. */
    const Range& essBins() const noexcept { return ess; }
    const Range& harshBins() const noexcept { return harsh; }
    int numBins() const noexcept { return bins; }

    /**
        The Ess band's reference now, for the display's thin line (src/dsp/feed/NOTES.md): Voice, the
        target at the current body level, the floor no bin is cut below; Allround, the signal's own
        spectrum smoothed over an octave and allroundMs. Per bin, as mean-square shares of the
        frame's signals summed (the bins of a signal's frame sum to its mean square). `scratch`:
        numBins() + 1 values. Not on the audio path: the display feed calls it a few dozen times a
        second.
    */
    void referenceLevels (Detection, double* scratch, float* out) const noexcept;

private:

    void learnFrame (std::int64_t frameEnd, const ReductionReader&, int reduction) noexcept;
    void decide (int slot, const ReductionReader&, int reduction) noexcept;
    void closeBlock() noexcept;
    void updateTarget() noexcept;
    void bandGains (const Range& band, const std::vector<double>& weight, const double* shape, float depth, std::vector<float>& gains) noexcept;

    double rate = 48000.0, binWidth = 93.75;
    int n = 512, bins = 257, hop = 128, learnBins = 236;
    double levelKeep = 0.0, allroundKeep = 0.0, gainStep = 1.0, bodyFall = 1.0, powerScale = 1.0, floorPower = 1.0e-6, gapRatio = 0.01;
    int windowWanted = 750, ringBlocks = 94, decideDelayFrames = 4, guardSamples = 384;

    Range ess, harsh, body;
    ShapeBands bands;
    bool bandsSet = false;
    std::vector<double> essWeight, harshWeight;

    std::vector<Range> sixth, octave, erb;
    std::vector<double> power, level, smoothed, allround, prefix, shapeScratch, lowerScratch, gainScratch;
    std::vector<float> essGain, harshGain;
    bool essOn = false, harshOn = false;

    // Learning: frames wait until the detector has decided every hop they span, then join the
    // block of the timeline they end in; a block closes when a later one starts.
    std::vector<float> pendingRelative;
    std::vector<std::int64_t> pendingEnd;
    std::vector<char> pendingVoiced;
    int pendingFirst = 0, pendingCount = 0, pendingSlots = 6;
    double bodyRecent = 0.0;

    std::vector<float> ringSums;
    std::vector<int> ringCounts;
    int ringNewest = -1, ringUsed = 0;
    std::vector<double> blockSums;
    int blockCount = 0;
    std::int64_t blockIndex = 0;
    bool blockOpen = false;

    std::vector<double> neutral, windowSums, target;     // neutral and the sums in ln, the target in power
    int windowFrames = 0, learnedTotal = 0;
};

/**
    The Spectral shape of one lane (PLAN.md, Reduction, Spectral; spectral/NOTES.md): one or two
    signals through an Stft each, frames on the timeline's grid (multiples of the hop), the gains
    from one SpectralAnalysis per reduction (Linked: one for both channels, their powers summed;
    Unlinked: one each). Returns what each band removes, a frame (the latency) after the input.

    Two engines (Stfts and gain computers): one runs; across a restart of the timeline the other
    finishes the frame in flight on the old grid while the new one starts on the new grid.
*/
class SpectralShape
{
public:
    /** Allocates. */
    void prepare (double sampleRate, int frameSize, const SpectralTuning& = {});

    /** From rest: `numSignals` 1 or 2; `coupled`: two signals share one gain computer (Linked). */
    void start (int numSignals, bool coupled) noexcept;

    /** After start(): every gain computer takes the target another Spectral shape has learned (its first one's). */
    void takeTargetFrom (const SpectralShape&) noexcept;

    /**
        After start(): the n samples of each signal that came before the next process() call go into
        the frame its first analysis reads, as if it had heard them; no frame runs. What it removes is
        complete once it has run a frame further, for the samples from the next call on.
    */
    void fill (const float* const* in, int n) noexcept;

    /**
        The host's timeline jumped (or started playing): positions from the next call on are `shift`
        less than before, the first at `boundary`. For one frame, what it removes comes from the
        engine that ran, on its old grid, reading only reductions before the boundary (the audio
        in flight); meanwhile a new engine takes over the frame of input it had, starts on the new
        grid from the warm start, and is heard from then on.
    */
    void restart (std::int64_t shift, std::int64_t boundary) noexcept;

    /**
        n samples: `in` holds the signals as they arrive, the first at timeline position `position`;
        `essRemoved`, `harshRemoved` get what each band removes from the samples a frame earlier.
        `reductions[s]`: the reduction signal s follows.
    */
    void process (const float* const* in, float* const* essRemoved, float* const* harshRemoved, int n, std::int64_t position,
                  const ShapeBands&, const SpectralSettings&, const ReductionReader&, const std::array<int, 2>& reductions) noexcept;

    int frameSize() const noexcept { return engines[0].stfts[0].frameSize(); }

    /** Gain computers in use (analysis (0 … n − 1)) and how many signals each sums: Linked one for two, else one each. */
    int numAnalyses() const noexcept { return coupled ? 1 : signals; }
    int signalsPerAnalysis() const noexcept { return coupled ? signals : 1; }
    const SpectralAnalysis& analysis (int index) const noexcept { return engines[(size_t) running].analyses[(size_t) index]; }

    /** Frames analysed since prepare(), each signal's counted (tests: what one call costs). */
    std::int64_t framesAnalysed() const noexcept { return frameCount; }

private:
    struct Engine
    {
        std::array<Stft, 2> stfts;
        std::array<SpectralAnalysis, 2> analyses;
        std::int64_t gridOffset = 0;    // frames end where (position − gridOffset) is a multiple of the hop
    };

    /** The reductions the outgoing engine reads: none after the boundary, whose key belongs to the new timeline. */
    struct Before final : ReductionReader
    {
        const ReductionReader* reader = nullptr;
        std::int64_t last = 0;
        float depthAt (std::int64_t time, int track) const noexcept override { return reader->depthAt (std::min (time, last), track); }
        float largestTarget (std::int64_t from, std::int64_t to, int r) const noexcept override { return reader->largestTarget (from, to, r); }
    };

    void run (Engine&, const float* const* in, float* const* essRemoved, float* const* harshRemoved, int offset, int n, std::int64_t position,
              const ShapeBands&, const SpectralSettings&, const ReductionReader&, const std::array<int, 2>& reductions, bool learn) noexcept;
    void runFrame (Engine&, std::int64_t frameEnd, const SpectralSettings&, const ReductionReader&, const std::array<int, 2>& reductions,
                   bool learn) noexcept;

    std::array<Engine, 2> engines;
    int running = 0;                // the engine heard once any splice is over
    int spliceLeft = 0;             // samples the other engine still plays the audio in flight for
    std::int64_t spliceLast = 0;    // the last input time it reads a reduction for
    std::array<std::array<std::vector<float>, 2>, 2> spectrum;    // [signal][re, im]
    std::array<std::vector<float>, 2> removed;                    // re, im of one output
    int signals = 1, hop = 128;
    bool coupled = false;
    std::int64_t frameCount = 0;
};

} // namespace halite::dsp
