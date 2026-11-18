#include "support/Corpus.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace basalt::measure
{

namespace
{
    juce::StringArray tableLines (const std::string& dir)
    {
        juce::StringArray lines;
        juce::File (dir).getChildFile ("excerpts.tsv").readLines (lines);
        lines.removeEmptyStrings();
        return lines;
    }
}

std::string defaultCorpusDir()
{
#ifdef BASALT_TEST_DATA_DIR
    return juce::File (BASALT_TEST_DATA_DIR).getChildFile ("corpus").getFullPathName().toStdString();
#else
    return {};
#endif
}

std::vector<std::string> excerptNames (const std::string& dir)
{
    std::vector<std::string> names;
    const auto lines = tableLines (dir);
    for (int i = 1; i < lines.size(); ++i)
        names.push_back (juce::StringArray::fromTokens (lines[i], "\t", "")[0].toStdString());
    return names;
}

std::optional<Excerpt> loadExcerpt (const std::string& dir, const std::string& name)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto file = juce::File (dir).getChildFile (juce::String (name) + ".flac");
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples <= 0)
        return std::nullopt;

    const auto channels = (int) std::min<unsigned> (reader->numChannels, 2);
    juce::AudioBuffer<float> buffer (channels, (int) reader->lengthInSamples);
    if (! reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, channels > 1))
        return std::nullopt;

    Excerpt e;
    e.name = name;
    e.rate = reader->sampleRate;
    for (int c = 0; c < channels; ++c)
        e.audio.emplace_back (buffer.getReadPointer (c), buffer.getReadPointer (c) + buffer.getNumSamples());

    const auto lines = tableLines (dir);
    for (int i = 1; i < lines.size(); ++i)
    {
        const auto fields = juce::StringArray::fromTokens (lines[i], "\t", "");
        if (fields.size() > 1 && fields[0].toStdString() == name)
            e.category = fields[1].toStdString();
    }
    return e;
}

} // namespace basalt::measure
