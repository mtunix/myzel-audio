#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace cerulean
{
class CeruleanProcessor;
}

namespace cerulean::ui
{

/**
    Factory and user presets.

    A preset is a snapshot of the sound parameters (params::soundParameterIds(): not Bypass,
    Freeze, BPM or Freeze Mode, which are performance/session settings). Factory presets are
    compiled in; user presets are XML files in userApplicationDataDirectory/Myzel Audio/Cerulean/Presets.
    Loading is one undoable transaction, every parameter change is sent to the host as a gesture,
    and the processor is told afterwards (notifyPresetLoaded) so the old sound rings out (Trails).

    Mix lock (a per-session setting): loading a preset or switching A/B keeps Mix and Wet as they
    are, so browsing sounds on a send never brings the dry back, and on an insert never changes
    the balance. The modified dot ignores them too.

    Owned by the processor (host program changes work without an editor).
    Message thread only.
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

    explicit PresetManager (CeruleanProcessor& processor);

    static const juce::StringArray& parameterIds();
    static juce::File getUserPresetDirectory();

    /** Tests: redirect user presets to a scratch folder (an empty File restores the default). */
    static void setUserPresetDirectoryOverride (const juce::File& directory);
    static inline const juce::String fileExtension { ".xml" };

    void scanUserPresets();
    const std::vector<Entry>& getEntries() const noexcept { return entries; }

    /** Current values of the preset parameters. */
    juce::ValueTree capture() const;

    /** Applies a snapshot as one undo transaction and host gestures, then starts a fresh engine
        instance (processor.notifyPresetLoaded()), so the previous sound rings out with Trails. */
    void apply (const juce::ValueTree& snapshot, const juce::String& undoName);

    void load (int index);
    void step (int delta);

    int getCurrentIndex() const;
    juce::String getCurrentName() const;
    juce::String getCurrentCategory() const;
    bool isCurrentUserPreset() const;
    void setCurrent (const juce::String& name, bool isUser);
    bool isModified() const;

    bool isMixLocked() const;
    void setMixLocked (bool shouldLock);

    /** True for Mix and Wet while the mix lock is on: presets and A/B leave them alone. */
    bool isLockedParameter (const juce::String& parameterId) const;

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

    CeruleanProcessor& processor;
    std::vector<Entry> entries;
    int numFactory = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace cerulean::ui
