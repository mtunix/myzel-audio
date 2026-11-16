#pragma once

#include "dsp/common/EventRing.h"
#include "dsp/detector/DetectorSettings.h"
#include "dsp/detector/Levels.h"
#include "dsp/detector/NoiseCues.h"
#include "dsp/detector/Reduction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace halite::dsp
{

/**
    One hop of the detector. Two clocks: `paths` and `decisions` describe the newest hop of the key
    (detector time, ending at `position`); `reductions` and `eventActive` belong to the hop ending
    at `reductionPosition`, the look-ahead earlier (PLAN.md, Latency). M2 delays the audio by the
    latency and the reductions by latency − reductionDelay to line them up.
*/
struct DetectorFrame
{
    std::int64_t position = 0;
    std::int64_t reductionPosition = 0;
    int numPaths = 1;           // detector paths (1, or 2 for a stereo key in Linked or Unlinked)
    int numReductions = 1;      // 1: one reduction for every channel (or the M/S component), 2: per channel

    struct Path
    {
        float bandDb = silentDb;            // B, power in dB (RMS dBFS)
        float bodyDb = silentDb;            // V, as held
        float relativeDb = 0.0f;            // B − V (while bodyRising, the smaller of B and B with the body's averaging)
        float noise = 1.0f;                 // N, noise-likeness 0 … 1 from the selected cue
        float periodicity = 0.0f;           // raw cue (a); NaN when not computed
        float flatnessDb = 0.0f;            // raw cue (b); NaN when not computed
        float sibilanceDb = silentDb;       // S: Voice, B − V lowered by up to 40 dB × Focus each for a harmonic band and a falling spectrum; Allround, B
        float harshDb = silentDb;           // H (silentDb with Harsh off)
        float harshBodyDb = silentDb;
        float harshRelativeDb = 0.0f;       // H − body
        float harshNoise = 1.0f;            // the selected cue on the Harsh band
        float harshPeriodicity = 0.0f;      // raw cues on the Harsh band; NaN when not computed
        float harshFlatnessDb = 0.0f;
        float presenceDb = 0.0f;            // the band's averaged level over the presence band's: the shape cue (0 when off)
        bool candidate = false;             // B − V over the candidate level: the body holds
        bool bodyHeld = false;              // V may not fall this hop (a candidate, or the body far below V)
        bool bodyRising = false;            // the body rose by more than riseDb over riseMs (an onset)
        bool bodyJumped = false;            // the body's averaged level rose by more than jumpDb over riseMs (an onset)
        bool voiceOnset = false;            // within the onset hold of a rise: S counts the band as harmonic
        bool jumpHeld = false;              // within the onset hold of a jump: S lowered by at least jumpHoldDb × Focus
        bool bodyFloored = false;           // V is under the floor: B − V is measured against the floor
    };

    struct Decision
    {
        float sibilanceDb = silentDb;       // S of the path this reduction follows
        float essTargetDb = 0.0f;
        float harshTargetDb = 0.0f;         // after the hold
        bool harshHeld = false;
    };

    struct Reduction
    {
        float essDb = 0.0f;                 // dB of reduction, ≥ 0
        float harshDb = 0.0f;
        float sibilanceDb = silentDb;       // S, delayed with the reduction
    };

    std::array<Path, 2> paths {};
    std::array<Decision, 2> decisions {};
    std::array<Reduction, 2> reductions {};
    bool eventActive = false;
};

/**
    Where the detectors' band signals go, per key path, for Listen's triggers (what each detector
    hears). A null pointer skips that signal; each one takes the call's numSamples.
*/
struct BandTaps
{
    std::array<float*, 2> ess {}, harsh {};     // the Harsh band reads 0 while Harsh is off
};

/** Receives every hop and every finished event, on the thread that calls process(). */
struct DetectorObserver
{
    virtual ~DetectorObserver() = default;
    virtual void hop (const DetectorFrame&) noexcept {}
    virtual void event (const EssEvent&) noexcept {}
};

/**
    Halite's detectors (PLAN.md, Detection and The Harsh band): Ess and Harsh on the key, in
    32-sample hops at any block size, per key path, linked or not, with the reduction they ask
    for. It reads the key and writes nothing to the audio. The constants and how they were
    chosen: src/dsp/detector/NOTES.md, Implementation (M1) and M1b.
*/
class Detector
{
public:
    static constexpr int hopSize = 32;

    // Constants the corpus did not choose (plan values and M1 decisions, NOTES.md); the ones it
    // did are DetectorTuning. Every level is the same detector: the power averaged over the
    // averaging time, then ballistics with one attack for all.
    static constexpr double attackMs = 0.5;
    static constexpr double bodyAveragingMs = 5.0;
    static constexpr double bodyLowHz = 120.0;
    static constexpr double riseMs = 5.0;                  // the body rises: more than riseDb over riseMs
    static constexpr double harshAveragingMs = 2.0, harshReleaseMs = 40.0;
    static constexpr double harshBodyLowHz = 150.0;
    static constexpr double harshWarmUpMs = 50.0;          // after Harsh is switched on
    static constexpr double eventHangoverMs = 20.0;
    static constexpr double maxLookaheadMs = 10.0;
    static constexpr double minBandOctaves = 1.0 / 3.0;
    static constexpr double keyLimit = 16.0;               // the key is clamped to ±16 (+24 dBFS); NaN and Inf read 0

    /** Allocates. The tuning applies from here on; the plugin runs the defaults. */
    void prepare (double sampleRate, const DetectorTuning& = {});
    void reset() noexcept;

    /**
        After reset(): the next sample is at this position of the host's timeline. Hops then end on
        its multiples of hopSize and the periodicity cue decimates on multiples of its factor, so a
        run started anywhere analyses the same hops as one started earlier (PLAN.md, Reduction,
        Warm start). Positions are counted from it: DetectorFrame and the events use timeline time.
    */
    void anchor (std::int64_t timelinePosition) noexcept;

    const DetectorTuning& getTuning() const noexcept { return tuning; }

    /**
        Runs the detectors over one block. `main` is the input (1 or 2 channels); `key` the side
        chain (0, 1 or 2 channels), read with KeySource::external, else the input is the key.
        Any numSamples. Real-time safe.
    */
    void process (const float* const* main, int numMain, const float* const* key, int numKey,
                  int numSamples, const DetectorSettings&, DetectorObserver* observer, const BandTaps* taps = nullptr) noexcept;

    /** The longest look-ahead at this rate, in hops (maxLookaheadMs, rounded up). */
    static int lookaheadHopsLimit (double rate) noexcept
    {
        return std::max (1, (int) std::ceil (maxLookaheadMs * 0.001 * rate / hopSize));
    }

    /** How far the reductions trail the newest key sample, in samples (a whole number of hops). */
    int reductionDelaySamples() const noexcept { return lookaheadHops * hopSize; }

    bool eventActive() const noexcept { return events.active(); }

    /** Key paths in use since the last process(): 1, or 2 for a stereo key in Linked or Unlinked. */
    int numKeyPaths() const noexcept { return numPaths; }

    struct Bands
    {
        double essLow = 0.0, essHigh = 0.0, harshLow = 0.0, harshHigh = 0.0, keyHpf = -1.0;
        bool operator== (const Bands&) const = default;
    };

    /** The bands the detectors use for these settings: sorted, widened to a third of an octave, under 0.45 fs. */
    static Bands bandsFor (const DetectorSettings&, double sampleRate) noexcept;

    double getSampleRate() const noexcept { return sampleRate; }

private:
    class Path
    {
    public:
        void prepare (double sampleRate, const DetectorTuning&);
        void reset() noexcept;
        void resetHarsh() noexcept;
        void resetCues() noexcept;

        void setBands (double essLow, double essHigh, double harshLow, double harshHigh, double keyHpfHz, double sampleRate,
                       const DetectorTuning&) noexcept;
        void setRiseSpan (int hops) noexcept;
        void anchor (std::int64_t timelinePosition) noexcept;

        void process (const double* key, int n, NoiseCueKind, bool harsh, float* essTap, float* harshTap) noexcept;

        /** Levels and cues at the end of a hop. */
        void read (DetectorFrame::Path&, NoiseCueKind, bool harsh, bool analyseFlatness) noexcept;

        /** For the next hop: whether V may fall. */
        void holdBody (bool candidateHold) noexcept;

        int flatnessHop() const noexcept { return flatness.hopSize(); }

    private:
        double persistent (double rho, float bandDb) noexcept;

        myzel::dsp::TptSvf keyHpf;
        bool keyHpfOn = false;
        BandFilter band, body, harshBand, harshBody;
        PowerEnvelope bandLevel, bodyLevel, harshLevel, harshBodyLevel, presenceLevel;
        PowerEnvelope slowBandLevel, slowHarshLevel;    // with the body's averaging, read while the body rises
        RiseDetector bodyRise, harshBodyRise, bodyJump;
        PeriodicityCue periodicity, harshPeriodicity;
        FlatnessCue flatness, harshFlatness;
        NoiseMapping mapping;
        float riseDb = 2.0f, jumpDb = 0.0f;
        double floorPower = 1.0e-6, gapRatio = 1.0e-3;
        int gapHoldHops = 1, bodyGapHops = 0, harshBodyGapHops = 0;
        bool presenceOn = false;

        // Persistence of a harmonic reading: ρ's largest and B's smallest over the last persistMs.
        SlidingExtreme<true> heldRho;
        SlidingExtreme<false> lowestBand;
        bool persistOn = false;
        float persistRho = 0.8f, persistJumpDb = 6.0f;
    };

    void endHop (const DetectorSettings&, DetectorObserver*) noexcept;

    double sampleRate = 48000.0;
    DetectorTuning tuning;
    std::array<Path, 2> paths;
    std::array<std::array<double, hopSize>, 2> keyBuffer {};
    Bands bands;
    int numPaths = 1, numReductions = 1;
    bool linked = false;
    bool harshWasOn = false;
    int keyRoute = -1;                                      // which signal feeds the paths: the route and the key source
    NoiseCueKind cue = NoiseCueKind::periodicity;

    int hopPhase = 0;
    std::int64_t position = 0;
    int lookaheadHops = 0, maxLookaheadHops = 0;
    int hangoverHops = 1, bodyHoldHops = 1, harshWarmUpHops = 0, flatnessEveryHops = 16;
    int harshWarmUp = 0;
    std::array<int, 2> candidateHops {}, onsetHops {}, jumpHops {};
    int onsetHoldHops = 0;
    std::array<int, 2> essHoldHops {};
    std::array<float, 2> harshHeldTarget {};

    std::array<ReductionShaper, 2> essShapers, harshShapers;
    std::array<HopDelay, 2> sibilanceDelays;
    EventTracker events;
    DetectorFrame frame;
};

} // namespace halite::dsp
