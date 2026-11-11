#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace umber
{
class UmberProcessor;
}

namespace umber::ui
{

/**
    Factory and user presets.

    A preset is a snapshot of the sound parameters (not Quality/Render/Bypass, which are
    session settings). Factory presets are compiled in; user presets are XML files in
    userApplicationDataDirectory/Myzel Audio/Umber/Presets. Loading is one undoable transaction
    and every parameter change is sent to the host as a gesture.
*/
class PresetManager
{
public:
    struct Entry
    {
        juce::String name, category;
        bool isFactory = true;
        juce::File file;
        juce::ValueTree values;
    };

    explicit PresetManager (UmberProcessor& processor);

    static const juce::StringArray& parameterIds();
    static juce::File getUserPresetDirectory();
    static inline const juce::String fileExtension { ".xml" };

    void scanUserPresets();
    const std::vector<Entry>& getEntries() const noexcept { return entries; }

    /** Current values of the preset parameters. */
    juce::ValueTree capture() const;

    /** Applies a snapshot as one undo transaction and host gestures. */
    void apply (const juce::ValueTree& snapshot, const juce::String& undoName);

    void load (int index);
    void step (int delta);

    int getCurrentIndex() const;
    juce::String getCurrentName() const;
    juce::String getCurrentCategory() const;
    bool isCurrentUserPreset() const;
    void setCurrent (const juce::String& name, bool isUser);
    bool isModified() const;

    bool userPresetExists (const juce::String& name) const;
    juce::Result saveUserPreset (const juce::String& name);
    juce::Result renameUserPreset (int index, const juce::String& newName);
    juce::Result deleteUserPreset (int index);

    /** Called after the list or the current preset changed. */
    std::function<void()> onChange;

private:
    void notify();
    juce::File fileFor (const juce::String& name) const;
    static juce::ValueTree readPresetFile (const juce::File& file);

    UmberProcessor& processor;
    std::vector<Entry> entries;
    int numFactory = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace umber::ui
