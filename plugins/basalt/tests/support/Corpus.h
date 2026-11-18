#pragma once

// The committed corpus excerpts (tests/corpus: excerpts.tsv and the FLAC files; PLAN.md, Test
// material), read for the corpus gate and the stereo gate's 10 s excerpt.

#include "support/Signals.h"

#include <optional>
#include <string>
#include <vector>

namespace basalt::measure
{

struct Excerpt
{
    std::string name, category;
    double rate = 0.0;
    Channels audio;     // one or two channels, as recorded
};

/** tests/corpus in the source tree, when the build says where it is; empty otherwise. */
std::string defaultCorpusDir();

/** The excerpts listed in `dir`/excerpts.tsv, in its order. */
std::vector<std::string> excerptNames (const std::string& dir);

/** `dir`/`name`.flac with its category from excerpts.tsv; nullopt if it cannot be read. */
std::optional<Excerpt> loadExcerpt (const std::string& dir, const std::string& name);

} // namespace basalt::measure
