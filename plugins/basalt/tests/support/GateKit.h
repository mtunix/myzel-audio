#pragma once

// What the gate files share: rows, the job runner, small statistics. Internal to support/.

#include "support/Gates.h"

#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace basalt::measure::kit
{

using Job = std::function<std::vector<Row>()>;

/** Runs the jobs on `threads` threads and returns their rows in job order; a job that throws gives a failing row. */
std::vector<Row> runJobs (const std::vector<Job>&, int threads);

std::string format (const char* text, ...)
#if defined(__GNUC__)
    __attribute__ ((format (printf, 1, 2)))
#endif
    ;

Row gated (const std::string& gate, const std::string& group, const std::string& item, double measured, double target,
           double low, double high, const std::string& unit, const std::string& note = {});
Row reported (const std::string& gate, const std::string& group, const std::string& item, double measured, double target,
              const std::string& unit, const std::string& note = {});
Row unspecified (const std::string& gate, const std::string& group, const std::string& item, const std::string& note);

/** The models v1 builds, in list order. */
std::vector<Model> offeredModels();

/** Character arrives in M3; until then the chain runs every Character setting as Off, and its rows are not exercised. */
inline constexpr bool characterBuilt = false;

/** Whether two models run on different engines in this build (dsp/chain/Engines.h): otherwise a switch between them is none. */
bool distinctEngines (Model a, Model b);

/** Whether VCA's Peak and RMS detectors run on different engines in this build. */
bool distinctDetectors();

/** The mean of x[from, to); NaN if the range is empty or holds a NaN. */
double mean (const std::vector<double>& x, int from, int to);

/** The p-th percentile (0 … 100) of a copy of v; NaN if empty. */
double percentile (std::vector<double> v, double p);

/** The Settings every curve and step starts from: Auto release off, Release 1 s, Knee 0, Range 40, Look-ahead 0. */
Settings measurementBase (Model);

/** One static curve (Curves.cpp): the error furthest from the specification over the model's levels. */
Row staticCurveRow (const Options&, Model, double thresholdDb, double ratio, double kneeDb, double rangeDb, double attackMs, Signal, double hz,
                    bool gate);

/** The VCA detectors a gate runs for a model: VCA's from the options, Peak alone for every other model. */
std::vector<VcaDetector> detectorsFor (const Options&, Model);

/** A label for a model and detector: "vca rms", or the model's key. */
std::string modelLabel (Model, VcaDetector);

/** The level (dBFS of amplitude) whose specified steady reduction is `depthDb` (negative: a lift). */
double levelFor (const ModelSpec&, const Settings&, Signal, double depthDb);

/** ±10 % of a time, or ±0.25 sample where that is wider (dynamics/NOTES.md, Ballistics), in s. */
double timeTolerance (double targetSeconds, double rate);

/** The reduction trace (dB, channel 0) of a key on the side chain, read through the probe. */
std::vector<double> reductionTrace (const Options&, const Settings&, const Samples& key);

/** A step response (Curves.cpp): silence, `burst` or a carrier at `levelDb` for `holdSeconds`, then silence. */
struct StepTimes
{
    double attack = std::numeric_limits<double>::quiet_NaN();     // s, to 63.2 % of the settled reduction
    double release = std::numeric_limits<double>::quiet_NaN();    // s, to `releaseFraction` of the reduction at the step
    double settledDb = std::numeric_limits<double>::quiet_NaN();
    double peakDb = std::numeric_limits<double>::quiet_NaN();      // the largest reduction during the burst
    Settings applied;
    std::vector<double> trace;
    std::vector<float> gain;
    int n0 = 0, n1 = 0;
};

StepTimes stepResponse (const Options&, const Settings&, double levelDb, double holdSeconds, double tailSeconds, const Samples* burst = nullptr,
                        const std::string& dump = {}, double releaseFraction = 0.36787944117144233);

/**
    The static gate's staircase (Curves.cpp): the model's levels, each held until settled and read
    as the mean reduction over its last window (a whole number of periods), with the specified one.
*/
struct Staircase
{
    std::vector<double> levels, measured, specified;    // dB on the detector's reading; reductions in dB
    std::vector<int> ends;                              // each level's last input sample + 1
    int window = 0;
    Settings applied;
    std::vector<double> trace;
    std::vector<float> gain;
};

Staircase staircase (const Options&, const Settings&, Signal, double hz);

/** The rows a model's NOTES add to the static gate and to the ballistics gate (Models.cpp), as jobs. */
std::vector<Job> modelStaticJobs (const Options&, Model);
std::vector<Job> modelBallisticsJobs (const Options&, Model);

/** VCA RMS's third harmonic from its one-pole ripple (vca/NOTES.md, Gates, Distortion), dBc. */
double vcaRmsThirdHarmonicDbc (double attackMs, double inverseRatio, double hz);

/** Upward's shape rows on a carrier staircase (Models.cpp): output monotonic in input, the lift exactly 0 at and under Floor. */
std::vector<Row> liftShapeRows (const Staircase&, const std::string& group, const std::string& item);

/** Upward's Floor sweep (Models.cpp): a level moving 1 dB/s through Floor … Floor + 12 dB, the click measure. */
std::vector<Row> floorSweepRows (const Options&, LatencyMode);

} // namespace basalt::measure::kit
