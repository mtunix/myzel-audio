#include "support/Corpus.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <map>

namespace halite::corpus
{

namespace
{
    constexpr std::array<const char*, numSibilants> sibilants { "S", "Z", "SH", "ZH", "CH", "JH" };
    constexpr std::array<const char*, 15> vowels { "AA", "AE", "AH", "AO", "AW", "AY", "EH", "ER", "EY",
                                                   "IH", "IY", "OW", "OY", "UH", "UW" };

    /** Rows of a tab-separated file with a header, as column name → value. */
    std::vector<std::map<std::string, std::string>> readTable (const juce::File& file)
    {
        std::vector<std::map<std::string, std::string>> rows;
        juce::StringArray lines;
        file.readLines (lines);
        lines.removeEmptyStrings();
        if (lines.isEmpty())
            return rows;

        const auto header = juce::StringArray::fromTokens (lines[0], "\t", "");
        for (int i = 1; i < lines.size(); ++i)
        {
            const auto cells = juce::StringArray::fromTokens (lines[i], "\t", "");
            std::map<std::string, std::string> row;
            for (int c = 0; c < header.size(); ++c)
                row[header[c].toStdString()] = cells[c].toStdString();
            rows.push_back (std::move (row));
        }
        return rows;
    }
}

PhoneClass classOf (const std::string& phone)
{
    if (phone == "sil" || phone == "spn" || phone.empty())
        return PhoneClass::silence;
    if (sibilantIndex (phone) >= 0)
        return PhoneClass::sibilant;
    for (const auto* v : vowels)
        if (phone == v)
            return PhoneClass::vowel;
    return PhoneClass::consonant;
}

bool isFrontVowel (const std::string& phone)
{
    return phone == "IY" || phone == "IH" || phone == "EY" || phone == "EH" || phone == "AE";
}

int sibilantIndex (const std::string& phone)
{
    for (int i = 0; i < numSibilants; ++i)
        if (phone == sibilants[(size_t) i])
            return i;
    return -1;
}

const char* sibilantName (int index)
{
    return index >= 0 && index < numSibilants ? sibilants[(size_t) index] : "?";
}

juce::File cacheDirectory()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("HALITE_CORPUS_DIR", {});
    if (env.isNotEmpty())
        return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".cache/myzel/halite-corpus");
}

std::vector<Entry> readManifest (const juce::File& tsv, const juce::File& cache, const std::string& corpusName)
{
    std::vector<Entry> entries;
    for (auto& row : readTable (tsv))
    {
        Entry e;
        e.id = row["id"];
        e.corpus = corpusName;
        e.speaker = row["speaker"];
        e.sex = row["sex"];
        e.split = row["split"];
        e.role = row["role"];
        e.sung = corpusName == "vocalset";
        e.audio = cache.getChildFile (row["audio"]);
        e.labels = cache.getChildFile (row["labels"]);
        entries.push_back (std::move (e));
    }
    return entries;
}

std::vector<Entry> committedTier (const juce::File& directory)
{
    std::vector<Entry> entries;
    for (auto& row : readTable (directory.getChildFile ("excerpts.tsv")))
    {
        Entry e;
        e.id = row["file"];
        e.corpus = "committed";
        e.speaker = row["speaker"];
        e.sex = row["sex"];
        e.split = "committed";
        e.role = row["role"];
        e.sung = juce::String (row["corpus"]).startsWith ("VocalSet");
        e.audio = directory.getChildFile (e.id + ".wav");
        e.labels = directory.getChildFile (e.id + ".lab");
        entries.push_back (std::move (e));
    }
    return entries;
}

std::vector<Segment> readLabels (const juce::File& file)
{
    std::vector<Segment> segments;
    juce::StringArray lines;
    file.readLines (lines);
    for (const auto& line : lines)
    {
        const auto cells = juce::StringArray::fromTokens (line, "\t", "");
        if (cells.size() < 3)
            continue;
        segments.push_back ({ cells[0].getDoubleValue(), cells[1].getDoubleValue(), cells[2].trim().toStdString() });
    }
    return segments;
}

bool load (const Entry& entry, Recording& out, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerFormat (new juce::WavAudioFormat(), true);
    formats.registerFormat (new juce::FlacAudioFormat(), false);

    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (entry.audio));
    if (reader == nullptr)
    {
        error = "cannot read " + entry.audio.getFullPathName();
        return false;
    }

    const auto n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buffer (1, n);
    if (! reader->read (&buffer, 0, n, 0, true, false))
    {
        error = "cannot decode " + entry.audio.getFullPathName();
        return false;
    }

    out.entry = entry;
    out.sampleRate = reader->sampleRate;
    out.audio.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + n);
    out.labels = readLabels (entry.labels);
    if (out.labels.empty())
    {
        error = "no labels in " + entry.labels.getFullPathName();
        return false;
    }
    return true;
}

std::vector<Recording> streams (std::vector<Recording>&& recordings)
{
    std::vector<Recording> out;
    std::map<std::string, size_t> bySpeaker;
    for (auto& r : recordings)
    {
        const auto [it, added] = bySpeaker.try_emplace (r.entry.speaker, out.size());
        if (added)
        {
            auto& s = out.emplace_back();
            s.entry = r.entry;
            s.entry.id = r.entry.speaker;
            s.sampleRate = r.sampleRate;
        }

        auto& s = out[it->second];
        jassert (juce::exactlyEqual (s.sampleRate, r.sampleRate));
        const auto offset = (double) s.audio.size() / s.sampleRate;
        s.parts.emplace_back (offset, r.entry.id);
        s.audio.insert (s.audio.end(), r.audio.begin(), r.audio.end());
        for (auto seg : r.labels)
        {
            seg.start += offset;
            seg.end += offset;
            s.labels.push_back (std::move (seg));
        }
        r = {};
    }
    return out;
}

std::pair<std::string, double> locate (const Recording& r, double seconds)
{
    if (r.parts.empty())
        return { r.entry.id, seconds };
    auto it = std::upper_bound (r.parts.begin(), r.parts.end(), seconds, [] (double t, const auto& p) { return t < p.first; });
    if (it != r.parts.begin())
        --it;
    return { it->second, seconds - it->first };
}

} // namespace halite::corpus
