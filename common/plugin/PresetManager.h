#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace myzel::plugin
{

/** What presets and A/B need from a plugin's processor. */
class PresetHost
{
public:
    virtual ~PresetHost() = default;

    virtual juce::AudioProcessorValueTreeState& getState() noexcept = 0;
    virtual juce::UndoManager& getUndoManager() noexcept = 0;

    /** The session's UI state (see UiState.h). */
    virtual juce::ValueTree getUiState() = 0;

    /** Called after a preset or A/B slot was applied (e.g. start a fresh engine so the old sound rings out). */
    virtual void presetApplied() {}

    /** The current preset changed (e.g. report it to the host as the current program). */
    virtual void currentPresetChanged (const juce::String& /*name*/, bool /*isUser*/) {}

    /** The mix lock was switched. The UI state holds the setting; this tells the audio side. */
    virtual void mixLockChanged (bool /*isLocked*/) {}
};

/** A factory preset as plain parameter values: parameter ID -> value in the parameter's own units. */
struct FactorySnapshot
{
    juce::String name, category;
    juce::NamedValueSet values;
};

/** Everything a plugin's presets differ in. */
struct PresetFormat
{
    juce::String pluginName;           // user presets live in <app data>/Myzel Audio/<pluginName>/Presets
    juce::Identifier presetType;       // XML root tag of a preset file: never rename, files use it
    int version = 1;                   // stored in saved presets
    juce::StringArray parameterIds;    // what a preset stores (not session settings like Bypass)
    juce::StringArray lockableIds;     // kept as they are while the mix lock is on
    std::vector<FactorySnapshot> factory;
};

/**
    Factory and user presets.

    A preset is a snapshot of the sound parameters (PresetFormat::parameterIds). Factory presets
    are compiled in; user presets are XML files in userApplicationDataDirectory/Myzel Audio/<plugin>/Presets.
    Loading is one undoable transaction, every parameter change is sent to the host as a gesture,
    and the host is told afterwards (PresetHost::presetApplied).

    Mix lock (a per-session setting): loading a preset or switching A/B keeps the lockable
    parameters (the mix) as they are, so browsing sounds on a send never brings the dry back, and
    on an insert never changes the balance. The modified dot ignores them too.

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

    PresetManager (PresetHost& host, PresetFormat format);

    const juce::StringArray& parameterIds() const noexcept { return format.parameterIds; }
    const juce::String& getPluginName() const noexcept { return format.pluginName; }
    juce::File getUserPresetDirectory() const;

    /** Tests: redirect user presets to a scratch folder (an empty File restores the default). */
    static void setUserPresetDirectoryOverride (const juce::File& directory);
    static inline const juce::String fileExtension { ".xml" };

    void scanUserPresets();
    const std::vector<Entry>& getEntries() const noexcept { return entries; }

    /** Current values of the preset parameters. */
    juce::ValueTree capture() const;

    /** Applies a snapshot as one undo transaction and host gestures, then calls presetApplied(). */
    void apply (const juce::ValueTree& snapshot, const juce::String& undoName);

    void load (int index);
    void step (int delta);

    int getCurrentIndex() const;
    juce::String getCurrentName() const;
    juce::String getCurrentCategory() const;
    bool isCurrentUserPreset() const;
    void setCurrent (const juce::String& name, bool isUser);
    bool isModified() const;

    /** False when the plugin has nothing to lock. */
    bool canLockMix() const noexcept { return ! format.lockableIds.isEmpty(); }
    bool isMixLocked() const;
    void setMixLocked (bool shouldLock);

    /** True for the lockable parameters while the mix lock is on: presets and A/B leave them alone. */
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
    juce::ValueTree readPresetFile (const juce::File& file) const;
    juce::ValueTree snapshotFromFactory (const FactorySnapshot& preset) const;

    PresetHost& host;
    const PresetFormat format;
    std::vector<Entry> entries;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace myzel::plugin
