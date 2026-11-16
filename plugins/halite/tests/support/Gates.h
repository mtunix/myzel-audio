#pragma once

#include "dsp/detector/DetectorProbe.h"
#include "support/Corpus.h"

#include <array>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

// The detection gates of PLAN.md (Measurement and quality gates) on labelled recordings, through
// the plugin's own Detector (dsp::probe). Shared by tools/measure and the corpus tests; the
// definitions are in NOTES.md, Measured (M1).
namespace halite::gates
{

/** A frame counts as reduced above this (the event rule's start level). */
inline constexpr float reducedDb = 0.5f;

/** The release-tail gate (PLAN.md, Settled 47): a vowel frame carries Ess reduction above this. */
inline constexpr float carriedDb = 1.0f;
inline constexpr double carriedVowelPercent = 2.0;

/** Vowel frames this close to a sibilant label are not counted (aligner boundaries differ by up to 30 ms). */
inline constexpr double vowelGuardSeconds = 0.030;

/**
    Level independence compares the events whose V stays at least levelMarginDb above the absolute
    body floor at the quietest gain, from this long before them (M1's longest Auto release fell
    from 8 to 0.5 dB in about 220 ms; M3's falls in 83 ms, and the window stays, so readings
    compare): the voices of the documented working range (PLAN.md, Settled 34).
*/
inline constexpr double floorLookbackSeconds = 0.3;
inline constexpr float levelMarginDb = 20.0f;

/** The separability gate (PLAN.md, Settled 34): /s/ recall at the threshold searched for, and the vowel decisions allowed there. */
inline constexpr double separabilityRecall = 95.0;
inline constexpr double separabilityVowelPercent = 2.0;

struct Counts
{
    long total = 0, hits = 0;

    void add (bool hit) noexcept { ++total; hits += hit ? 1 : 0; }
    void add (const Counts& o) noexcept { total += o.total; hits += o.hits; }
    double percent() const noexcept { return total > 0 ? 100.0 * (double) hits / (double) total : 0.0; }
};

struct Token
{
    int sibilant = -1;              // corpus::sibilantIndex
    double start = 0.0, end = 0.0;  // seconds
    bool detected = false;          // an event overlaps it
    bool afterVowel = false;        // the label before it is a vowel
    float peakSibilanceDb = -300.0f;    // the largest S of the hops inside it
    float peakReductionDb = 0.0f;
    float harshMeanDb = 0.0f;       // its mean Harsh reduction
    float harshPeakDb = 0.0f;
    float harshAddedDb = 0.0f;      // its mean Harsh reduction over what the hop before it carried

    // Coverage inside the label: an event that overlaps a token can still let most of it through.
    int hops = 0;                   // hops whose centre lies in the label
    int decidedHops = 0;            // ... whose own target is over reducedDb (a decision)
    int reducedHops = 0;            // ... reduced by more than reducedDb
    float reductionSumDb = 0.0f;    // Ess reduction summed over them
    float firstDecisionMs = -1.0f;  // the first decided hop's centre after the label's start; -1: none
    float firstReducedMs = -1.0f;   // the first hop reduced by more than reducedDb, the same way; -1: none
    bool reducedAtStart = false;    // the label's first hop is reduced: the look-ahead had the reduction in place
    bool onsetHeld = false;         // an onset hold (after a rise or a jump) covers a hop of it
};

struct VowelHit
{
    double time = 0.0;              // seconds, the hop's centre
    float reductionDb = 0.0f;
    bool afterEss = false;
};

struct RecordingResult
{
    std::string id, corpus, speaker, sex, split, role;
    bool sung = false;
    double seconds = 0.0;
    std::vector<Token> tokens;
    Counts vowel;                   // vowel frames more than the guard from any sibilant
    Counts vowelTriggered;          // ... whose own target is over reducedDb: the detector's decisions
    Counts vowelOverThreshold;      // ... whose own S is over the threshold (the literal reading; S > T + 0.23 dB is a decision)
    Counts vowelTail;               // ... reduced, but not by their own S: a release tail
    Counts vowelCarried;            // ... carrying more than carriedDb, decisions and tails together (Settled 47)
    Counts vowelAfterEss;           // ... in vowels right after a sibilant
    Counts vowelAfterSilence;       // ... in vowels right after a pause
    Counts afterEssVowels;          // vowels right after a sibilant with any reduced frame (past the guard)
    Counts bright;                  // sung material: every counted vowel frame
    float brightMaxDb = 0.0f;
    double brightMaxTime = 0.0;     // seconds
    double harshSibilantSum = 0.0;  // Harsh reduction (dB) summed over sibilant hops
    double harshSibilantHeldSum = 0.0;  // ... over the hops where Harsh holds the target from before (an ess, noise, warm-up)
    double harshSibilantAddedSum = 0.0; // ... over what the hop before each token carried
    long harshSibilantHops = 0;
    double harshVowelSum = 0.0;
    Counts harshVowel;              // vowel frames with more than reducedDb of Harsh reduction
    std::vector<float> vowelSibilanceDb;    // S of every counted vowel frame
    std::vector<float> harshRelativeDb;     // H − body of every vowel frame (Harsh on)
    std::vector<float> harshDepthDb;        // pumping: p95 − p5 of the Harsh reduction in each long vowel it acts in
    long harshDips = 0;                     // ... dips of more than pumpDipDb and back in them
    double harshActiveSeconds = 0.0;        // ... their length
    std::vector<VowelHit> vowelHits;
    int events = 0, eventsOnSibilants = 0;
    std::map<std::string, int> eventsElsewhere;    // events overlapping no sibilant, by the phone they overlap most
    std::map<std::string, long> triggers;          // vowel frames whose own S is over the threshold, by kind
};

/** Runs the plugin's detector over a recording at an input gain. */
dsp::probe::Result run (const corpus::Recording&, const dsp::DetectorSettings&, const dsp::DetectorTuning& = {}, float gainDb = 0.0f);

/** `thresholdDb`, the Relative threshold of the run, counts vowelOverThreshold; NaN leaves it empty. */
RecordingResult analyse (const corpus::Recording&, const dsp::probe::Result&, float thresholdDb = std::numeric_limits<float>::quiet_NaN());

/** Coverage inside one sibilant's labels, over many tokens. */
struct Coverage
{
    Counts decided, reduced;        // label hops with a decision, reduced over reducedDb
    double reductionSumDb = 0.0;
    Counts withDecision, late, onsetHeld;   // tokens: any decision; of those, the first more than lateMs in; detected tokens with an onset-held hop
    Counts reducedFromStart;                // tokens with a decision whose label's first hop is already reduced (the look-ahead caught the onset)
    std::vector<float> firstDecisionMs;     // of the tokens with a decision
    std::vector<float> firstReducedMs;      // of the tokens with a decision: the first reduced hop
    std::vector<float> tokenReducedShare;   // of the tokens with a decision: the share of the label's hops reduced, %

    double meanReductionDb() const noexcept { return decided.total > 0 ? reductionSumDb / (double) decided.total : 0.0; }
};

/**
    Pumping, the Harsh band on sustained vowels: vowel labels at least this long in which Harsh
    reduces by more than reducedDb; a dip is a fall of more than pumpDipDb and a rise of as much.
*/
inline constexpr double pumpVowelSeconds = 0.15;
inline constexpr float pumpDipDb = 1.0f;

/** A token's first decision this far into its label counts as late (the onset hold is 30 ms). */
inline constexpr float lateMs = 20.0f;

/** Everything of many recordings, added up. */
struct Summary
{
    int recordings = 0;
    double seconds = 0.0;
    std::array<Counts, corpus::numSibilants> detected;
    std::array<Counts, corpus::numSibilants> detectedAfterVowel;
    Counts vowel, vowelTriggered, vowelOverThreshold, vowelTail, vowelCarried, vowelAfterEss, vowelAfterSilence, afterEssVowels;
    std::array<Coverage, corpus::numSibilants> coverage;
    Counts bright;
    int events = 0, eventsOnSibilants = 0;
    std::map<std::string, int> eventsElsewhere;
    std::map<std::string, long> triggers;
    float brightMaxDb = 0.0f;
    int brightRecordings = 0, brightRecordingsReduced = 0;
    float brightLargestSibilanceDb = -300.0f;   // the largest S of a bright frame
    double harshSibilantSum = 0.0, harshSibilantHeldSum = 0.0, harshSibilantAddedSum = 0.0, harshVowelSum = 0.0;
    long harshSibilantHops = 0;
    Counts harshVowel;
    std::vector<float> harshDepthDb;
    long harshDips = 0;
    double harshActiveSeconds = 0.0;
    Counts harshTokens, harshTokensPeak, harshTokensAdded;  // sibilant tokens whose mean, peak or added Harsh reduction is over reducedDb

    void add (const RecordingResult&);
    double harshSibilantMeanDb() const noexcept { return harshSibilantHops > 0 ? harshSibilantSum / (double) harshSibilantHops : 0.0; }
    double harshSibilantHeldMeanDb() const noexcept { return harshSibilantHops > 0 ? harshSibilantHeldSum / (double) harshSibilantHops : 0.0; }
    double harshSibilantAddedMeanDb() const noexcept { return harshSibilantHops > 0 ? harshSibilantAddedSum / (double) harshSibilantHops : 0.0; }
    double harshVowelMeanDb() const noexcept { return harshVowel.total > 0 ? harshVowelSum / (double) harshVowel.total : 0.0; }
    Counts sz() const noexcept;     // /s/ and /z/ together
};

/** Level independence of one recording: renders at each gain against the first. */
struct LevelResult
{
    int events = 0;                 // events in the working range at the first gain (V levelMarginDb over the floor in the quietest render)
    int compared = 0;               // such event pairs compared (every other gain)
    int listChanges = 0;            // such events at one gain with no counterpart at the other
    int excluded = 0;               // events outside the working range, not compared
    float worstDifferenceDb = 0.0f; // the largest change of an event's peak reduction
    std::string worstId;            // where (recording, seconds), and at which gain
    double worstTime = 0.0;
    float worstGainDb = 0.0f;
    std::vector<std::pair<double, float>> changes;     // time and gain of each list change
    int allCompared = 0, allListChanges = 0;            // every event, the floored ones too (reported)
    float allWorstDifferenceDb = 0.0f;

    /** Each quieter gain against the first, in that render's own working range (reported). */
    struct Gain
    {
        float gainDb = 0.0f;
        int events = 0, inRange = 0, compared = 0, listChanges = 0;
        float worstDifferenceDb = 0.0f;
    };
    std::vector<Gain> perGain;

    void add (const LevelResult&);
};

/**
    With `bodyDb` finite, every gain is first offset so that the median V over the recording's
    vowel frames at the first gain sits at bodyDb (dBFS): the sweep then starts from the same
    level for every recording instead of the corpus's own.
*/
LevelResult levelIndependence (const corpus::Recording&, const dsp::DetectorSettings&, const dsp::DetectorTuning&,
                               const std::vector<float>& gainsDb, float bodyDb = std::numeric_limits<float>::quiet_NaN());

/**
    Synthetic bright sung vowels for the bright-vowel gate: harmonics up to 12 kHz, tilts of 0, −3,
    −6 and −12 dB per octave (the tilt is the recording's speaker, so it is reported per tilt),
    steady and with vibrato, each 0.3 s of silence then 1.7 s of vowel faded in and out over 20 ms,
    labelled IY. Sung, corpus "synthetic". Two sets: tune (f0 110 … 880 Hz, ±50 cents at 5.5 Hz;
    seen while the constants were chosen) and holdout (f0 98 … 784 Hz, ±35 cents at 6.2 Hz, other
    phases; for reporting only).
*/
std::vector<corpus::Recording> syntheticBrightVowels (double sampleRate, bool holdout = false);

/** Runs f (i) for every i < count, in any order and on any thread. */
using ForEach = std::function<void (size_t count, const std::function<void (size_t)>& f)>;

/**
    The separability gate's threshold: the highest Relative threshold (within the parameter's
    −36 … +24 dB, to 0.02 dB) at which at least `recallPercent` of the spoken recordings' tokens
    of one sibilant produce an event. Recall falls with the threshold; at −36 dB short of it, −36.
*/
float thresholdForRecall (const std::vector<corpus::Recording>&, const dsp::DetectorSettings&, const dsp::DetectorTuning&,
                          int sibilant, double recallPercent, const ForEach& = {});

/** B on 4.5 … 12 kHz band noise (PLAN.md, Detection 1): its ripple and how fast it rises. */
struct BandLevelCheck
{
    double rippleDb = 0.0;          // standard deviation of B (dB) per hop, after the first half second
    double offsetDb = 0.0;          // mean B minus the noise's true RMS
    double riseTo3DbMs = 0.0;       // a tone at the band's centre switched on: B within 3 dB of its final value
    double riseTo1DbMs = 0.0;
};

BandLevelCheck bandLevel (double sampleRate, const dsp::DetectorTuning&);

/** Percentile p (0 … 100) by linear interpolation; sorts the vector. NaN when empty. */
double percentile (std::vector<float>& values, double p);

} // namespace halite::gates
