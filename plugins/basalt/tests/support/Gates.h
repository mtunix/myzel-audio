#pragma once

// The gates of PLAN.md (Measurement and quality gates) as measurements, with the core's and each
// model's rows (src/dsp/dynamics/NOTES.md, src/dsp/models/<m>/NOTES.md, Gates). Each gate returns
// rows with a verdict; tools/measure prints them, tests/GateTests.cpp checks them.

#include "support/Rig.h"

#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace basalt::measure
{

enum class Verdict
{
    pass,
    fail,
    reported,       // measured and shown, not gated
    notSpecified    // BasaltMeasure has no specification for this case yet
};

const char* verdictName (Verdict);

struct Row
{
    std::string gate;   // identity, static, ballistics, clicks, stereo, distortion, perf, corpus, selftest
    std::string group;  // rows of a group collapse into one line of the readable table while they all pass
    std::string item;
    double measured = std::numeric_limits<double>::quiet_NaN();
    double target = std::numeric_limits<double>::quiet_NaN();
    double low = -std::numeric_limits<double>::infinity();     // a gated row passes if low <= measured <= high
    double high = std::numeric_limits<double>::infinity();
    std::string unit;
    Verdict verdict = Verdict::reported;
    std::string note;
};

/** True when no gated row failed. */
bool passed (const std::vector<Row>&);

/** The bounds as text: "[-0.25, 0.25]", "<= -60", ">= 0.75", "= 0". */
std::string boundsText (const Row&);

/** One line: gate, group, item, measured against target and bounds, verdict, note. */
std::string describe (const Row&);

//==============================================================================
enum class Signal
{
    sine,
    carrier
};

/** The static gate's grid (PLAN.md, Static curve; each model's NOTES, Gate table). */
struct StaticGrid
{
    std::vector<double> thresholds, ratios, knees, ranges;
    std::vector<double> floors { -70.0 };   // Upward
};

/** One step response of the Ballistics gate. */
struct StepCase
{
    double attackMs, releaseMs, ratio, kneeDb = 0.0;
    bool attackRow = true, releaseRow = true;
    double depthDb = 10.0;      // a deeper step gates only its overshoot (Vari-mu)
};

/**
    What BasaltMeasure has transcribed of a model's NOTES (src/dsp/models/<m>/NOTES.md, Gate
    table), from each model's tests/models/<m>/Reference.h: the specified static curve, what the
    time knobs mean, the grids each gate runs and where a gate applies. The rows a model's NOTES
    add of its own (program dependence, coupling, the rising ratio, Floor) live in Models.cpp.
*/
struct ModelSpec
{
    Model model = Model::clean;
    bool feedback = false;
    bool lowFrequencyGated = false;     // feedback models also gate 100 Hz (PLAN.md, Static curve)
    bool lifts = false;                 // Upward: the reduction is a lift, negative
    bool detectorSwitch = false;        // VCA: the gates run each detector
    bool linksLevels = false;           // Opto: Link blends the levels in dB before the cell, not the reductions
    double budgetPercent = 0.3;         // PLAN.md, Performance budget: 48 kHz stereo, one core, Character off

    // Identity: the noise that sits under the model's onset with Threshold 0, Knee 24, ∞:1 (Opto's is T − 6w).
    bool identityUnderOnset = true;     // Upward lifts there instead
    double identityQuiet = 0.15;

    // Static curve.
    std::vector<double> staticAttacksMs { 10.0 };
    StaticGrid staticFull, staticQuick;

    /** The specified steady reduction (dB; negative for a lift) for a signal whose amplitude is `amplitudeDb` dBFS. */
    std::function<double (const Settings&, Signal, double amplitudeDb)> steadyReduction;

    /** The staircase's levels (dB on the detector's reading); unset: Threshold − 20 … + 30 dB. */
    std::function<std::vector<double> (const Settings&)> staticLevels;

    /** How long the staircase holds a level after a step (s); unset: max(0.1 s, 12 × Attack). */
    std::function<double (const Settings&, Signal, double fromDb, double toDb)> stepSettleSeconds;

    /** The staircase runs downwards: the model settles faster that way (Opto at low ratios). */
    std::function<bool (const Settings&)> staticDescending;

    // Ballistics, on the core's measurement definition (dynamics/NOTES.md, Ballistics).
    std::function<std::vector<StepCase> (bool quick)> steps;   // unset: the model has no rows on the core's definition

    /** What the measured attack and release read for a setting (ms); unset: the knob. */
    std::function<double (const Settings&)> expectedAttackMs, expectedReleaseMs;
    std::function<double (const Settings&)> attackTolerance;   // a fraction; unset 0.10
    double releaseFraction = 0.36787944117144233;               // 1/e; Opto's Release is the time to 50 %

    /** How long a step holds its level (s); unset: max(5 × (Attack + Release), 10 × Attack, 0.25 s). */
    std::function<double (const Settings&)> stepHoldSeconds;

    /** A key lag the step times are measured from the end of (FET's All), ms. */
    std::function<double (const Settings&)> lagMs;

    bool gateOvershoot = false;         // Vari-mu: the attack overshoots by at most 0.05 dB
    bool lookaheadRows = true;
    bool rangeAttackGated = true;       // the attack with Range engaged as without (dynamics/NOTES.md, Range)

    // Distortion (PLAN.md): THD+N of a 1 kHz sine at this static reduction, −80 dBc where the NOTES gate it.
    double distortionDepthDb = 6.0;
    std::function<bool (const Settings&)> distortionGated;
};

/** The specification of what runs for a model (a 1.1 model runs as Clean in v1). */
const ModelSpec& specOf (Model);

//==============================================================================
struct Options
{
    DeviceKind device = DeviceKind::processor;
    std::optional<Model> model;     // identity and perf: every model when unset; every other gate: Clean when unset
    double rate = 48000.0;
    std::vector<LatencyMode> latencies { LatencyMode::normal, LatencyMode::live };
    Probe probe = Probe::external;
    bool quick = false;             // the reduced grids CI runs
    int jobs = 2;
    Settings base;                  // corpus and perf: the settings they run (model from `model`)
    std::vector<VcaDetector> detectors { VcaDetector::peak, VcaDetector::rms };  // VCA's, for the gates whose rows depend on the detector
    std::string corpusDir;
    std::string dumpDir;            // ballistics steps and clicks write their raw renders here (RenderOptions::dump)

    Model modelOrClean() const { return model.value_or (Model::clean); }
};

std::vector<Row> identity (const Options&);
std::vector<Row> staticCurve (const Options&);
std::vector<Row> ballistics (const Options&);
std::vector<Row> clicks (const Options&);
std::vector<Row> stereo (const Options&);
std::vector<Row> distortion (const Options&);
std::vector<Row> perf (const Options&);
std::vector<Row> corpus (const Options&);

/** Every measurement on references with a known answer (the one-pole, the transcription of the NOTES, synthetic signals). */
std::vector<Row> selfTest (const Options&);

//==============================================================================
// The measurement primitives.

/**
    The time (s) at which a reduction trace first reaches `target` after the step at input sample
    n0 (dynamics/NOTES.md, Ballistics): the output from input n0 + j sits at (j + 1)/fs, the state
    before the step (r[n0 − 1]) at 0, crossings interpolated linearly. NaN if never reached.
*/
double crossingSeconds (const std::vector<double>& reductionDb, int n0, double target, bool rising, double rate);

/** Halite's click measure: the largest 3rd difference of x over [from, to) against its RMS, in dB. */
double clickCrestDb (const Samples& x, int from, int to);

/** Samples where out[n] is not in[n − delay] (silence before) bit for bit; the first such index in `first`. */
long delayedMismatches (const Samples& out, const Samples& in, int delay, long* first = nullptr);

/** A file name under o.dumpDir for a row, or empty when nothing is dumped. */
std::string dumpPath (const Options&, const std::string& gate, const std::string& group, const std::string& item);

/** Two float gains' difference in dB, 0 when they are at most one float step apart. */
double gainDifferenceDb (float a, float b);

struct Distortion
{
    double thdN = std::numeric_limits<double>::quiet_NaN();         // dBc: everything but the fundamental
    double thd = std::numeric_limits<double>::quiet_NaN();          // dBc: harmonics 2 … 10
    double third = std::numeric_limits<double>::quiet_NaN();        // dBc: the third harmonic alone
    double reductionDb = std::numeric_limits<double>::quiet_NaN();  // the fundamental against inputAmplitude
};

/**
    THD+N and THD of a sine at `hz` over `length` samples from `from`, a whole number of its
    periods: the fundamental fitted by its sine and cosine projections, THD+N everything else
    against it, THD the harmonics 2 … 10 projected the same way.
*/
Distortion distortionOf (const Samples& x, int from, int length, double rate, double hz, double inputAmplitude);

} // namespace basalt::measure
