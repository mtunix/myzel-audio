#pragma once

#include "support/Corpus.h"

#include "dsp/detector/DetectorSettings.h"

#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

// The reduction gates of PLAN.md (Measurement and quality gates: accuracy, collateral, the Harsh
// band, Listen Removed) on labelled material, through the plugin's own chain: every ess the
// detector finds, read the way the detector reads it. Recordings are loaded one unit at a time (a
// recording, or with `stream` one speaker's recordings back to back) and dropped once measured.
namespace halite::reduction
{

struct CorpusEntries
{
    std::string name;
    std::vector<corpus::Entry> entries;
};

struct Options
{
    dsp::DetectorSettings settings;     // the Ess pass runs with Harsh off, the Harsh pass with it on
    float gainDb = 0.0f;                // the input's gain
    std::vector<Shape> shapes { Shape::split, Shape::spectral, Shape::wide };
    bool stream = false;
    int jobs = 2;
    int worst = 0;                      // list this many worst events per shape and reading

    /** Per "corpus/speaker": the threshold and Harsh Threshold to run there (what Learn set), in the detection's unit; NaN keeps the setting. */
    std::map<std::string, std::pair<float, float>> thresholds;
};

/** Measures every shape on every corpus and writes the Markdown report. False when a recording does not load. */
bool measure (const std::vector<CorpusEntries>&, const Options&, std::FILE* out);

} // namespace halite::reduction
