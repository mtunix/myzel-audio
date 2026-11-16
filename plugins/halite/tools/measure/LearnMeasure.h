#pragma once

#include "support/Corpus.h"

#include "dsp/detector/DetectorSettings.h"
#include "dsp/detector/Learn.h"

#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// Learn on labelled material (PLAN.md, Learn; NOTES.md, M3: Learn): the plugin's own Learn over the
// plugin's own detector, as 8 s captures at every step of each recording (or speaker's stream) and
// over the whole of it, against the plan's rule read from the labels (the median /s/ /z/ token
// reduced by 4 dB, the quietest quarter by none) and, with Harsh on, the same share of the labelled
// vowel frames as Learn reads of its voiced frames.
namespace halite::learning
{

struct CorpusData
{
    std::string name;
    const std::vector<corpus::Recording>* recordings = nullptr;
};

struct Options
{
    dsp::DetectorSettings settings;
    dsp::DetectorTuning tuning;
    float gainDb = 0.0f;
    dsp::LearnTuning learn;
    std::vector<std::pair<std::string, std::vector<std::string>>> vary;    // LearnTuning keys only
    double stepSeconds = 2.0;           // between capture starts
    int jobs = 2;
    bool speakers = false;              // a table per speaker
    std::string thresholdsFile;         // per speaker: Threshold and Harsh Threshold from the whole recording (the first configuration)
    float normaliseDb = std::numeric_limits<float>::quiet_NaN();   // each recording first scaled to this RMS (dBFS) over its labelled speech, or all of it
};

/**
    Synthetic cymbals for Allround, 32 s each at `rate`, −18 dBFS RMS, unlabelled: "hats" (closed
    eighths at 120 bpm with an open one a bar, high-passed noise) and "ride" (eighths of band noise
    with three decaying bell partials). Corpus "cymbals".
*/
std::vector<corpus::Recording> syntheticCymbals (double rate);

/** The LearnTuning keys: captureSeconds, floorDb, floorAbsDb, splitAbsDb, hangoverMs, minLengthMs, medianReductionDb, quietShare, minEsses, harshShare, minVoicedSeconds, voicedNoise. */
bool apply (dsp::LearnTuning&, const std::string& key, const std::string& value);

/** Writes the Markdown report. False when nothing was measured. */
bool measure (const std::vector<CorpusData>&, const Options&, std::FILE* out);

} // namespace halite::learning
