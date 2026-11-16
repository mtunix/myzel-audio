#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <utility>
#include <vector>

// The labelled corpora (plugins/halite/tools/corpus/README.md): the committed tier under
// tests/corpus and the cache's subset manifests. Shared by tools/measure and the corpus tests.
namespace halite::corpus
{

/** One labelled phone, in seconds. */
struct Segment
{
    double start = 0.0, end = 0.0;
    std::string phone;      // ARPAbet without stress, or sil / spn
};

enum class PhoneClass { silence, vowel, sibilant, consonant };

PhoneClass classOf (const std::string& phone);
bool isFrontVowel (const std::string& phone);

/** The six sibilants in report order: S Z SH ZH CH JH. -1 for anything else. */
int sibilantIndex (const std::string& phone);
inline constexpr int numSibilants = 6;
const char* sibilantName (int index);

/** A recording, not yet loaded. */
struct Entry
{
    std::string id, corpus, speaker, sex, split, role;
    juce::File audio, labels;
    bool sung = false;      // sung vowels (VocalSet, synthetic): the bright gate, not the speech gates
};

/** A recording in memory: mono float audio at its own rate, and its phones. */
struct Recording
{
    Entry entry;
    double sampleRate = 0.0;
    std::vector<float> audio;
    std::vector<Segment> labels;
    std::vector<std::pair<double, std::string>> parts;     // a stream: where each recording starts (seconds) and its id
};

/**
    Each speaker's recordings back to back, in order, as one recording per speaker (a take with
    pauses between phrases): the detector starts cold once per speaker, not once per sentence.
    Recordings of a speaker must share a rate.
*/
std::vector<Recording> streams (std::vector<Recording>&& recordings);

/** The recording and local time (seconds) of a time in a stream; the recording itself when it is not one. */
std::pair<std::string, double> locate (const Recording&, double seconds);

/** $HALITE_CORPUS_DIR, else ~/.cache/myzel/halite-corpus. */
juce::File cacheDirectory();

/**
    A subset manifest (labels/<subset>/<corpus>.tsv: id, audio, labels, speaker, sex, accent,
    rate, role, split, labeller); paths are relative to `cache`. Empty if the file is missing.
*/
std::vector<Entry> readManifest (const juce::File& tsv, const juce::File& cache, const std::string& corpusName);

/** The committed tier: tests/corpus/excerpts.tsv and its WAV and .lab files; split "committed". */
std::vector<Entry> committedTier (const juce::File& directory);

std::vector<Segment> readLabels (const juce::File&);

/** Reads the audio (WAV or FLAC; the first channel) and the labels. False with a reason on failure. */
bool load (const Entry&, Recording&, juce::String& error);

} // namespace halite::corpus
