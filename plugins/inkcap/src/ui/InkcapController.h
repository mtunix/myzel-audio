#pragma once

#include "common/ui/EditorController.h"
#include "dsp/ModuleInfo.h"
#include "dsp/chain/Telemetry.h"
#include "plugin/SeedState.h"

namespace inkcap
{
class InkcapProcessor;
}

namespace inkcap::ui
{

/**
    What the display's pages share: parameter access in the display's terms (a module's Amount,
    its Damage-scaled intensity, read-outs), edits as host gestures and undo steps, the seed
    actions, the page that is open, and the short messages the status line shows. Owned by the
    editor; message thread only.
*/
class InkcapController final : public myzel::ui::EditorController
{
public:
    explicit InkcapController (InkcapProcessor&);

    InkcapProcessor& getProcessor() noexcept { return processor; }
    SeedState& getSeeds() noexcept;
    const dsp::Telemetry& getTelemetry() const noexcept;

    // Values: plain (the parameter's own units) and normalised.
    float plain (const char* id) const;
    float normalised (const char* id) const;

    /** The parameter's read-out as the display prints it: "49 ms", "dark 40", "1/16". */
    juce::String text (const char* id) const;

    float damage() const { return plain ("damage") / 100.0f; }
    float chaos() const { return plain ("chaos") / 100.0f; }
    bool isOn (Module) const;
    float amount (Module) const;            // 0 .. 1
    int type (Module) const;
    juce::String typeName (Module) const;   // as the display prints it ("12-bit")
    static juce::String typeName (Module, int type);
    static juce::String moduleName (Module);   // lower case, "glitch"

    /** How far the module is from clean now (Amount through the Damage curve; 0 when off). */
    float intensity (Module) const;
    bool anyModuleOn() const;

    /** How far the wear wanders, 0 .. 1: Chaos times the pad's wear (dsp::padWear; the pad's rings). */
    float wander() const;

    /** One edit: a host gesture and one undo step (the processor's ParameterUndoRecorder records it). */
    void setPlain (const char* id, float value);
    void setNormalised (const char* id, float value01);
    void toggle (const char* id);
    void resetToDefault (const char* id);

    /** Typed entry: the parameter's own parser reads the text; text it does not understand changes nothing. */
    void setFromText (const char* id, const juce::String& text);

    /** A drag: one gesture from begin to end, whatever happens in between. */
    void beginEdit (const char* id);
    void endEdit (const char* id);

    // Seeds (every change one undo step, see SeedState)
    juce::String seedCode() const;
    juce::String seedLabel() const;
    void copySeed();
    SeedState::Paste pasteSeed (const juce::String& text);
    void pasteSeedFromClipboard();
    void rerollSeed();
    bool pickSeed (int historyIndex);
    bool isSeedLocked() const;
    void setSeedLocked (bool);

    /** The preset's place in the list as the header prints it ("014"), empty when there is none. */
    juce::String presetNumber() const;

    /** A short message for the status line ("seed copied"), shown for a moment. */
    void flash (const juce::String& message);
    juce::String getFlash() const { return flashText; }

    /** A read-out the strip lends the status line while a key or knob is hovered or dragged. */
    void setHint (const juce::String& hint) { hintText = hint; }
    juce::String getHint() const { return hintText; }

    /** Advances the message clock (the editor's frame clock). */
    void tick (float dt);

    // Pages
    int getModulePage() const;              // -1: the module list
    void setModulePage (int moduleIndex);   // stored in the session
    bool arePresetsOpen() const noexcept { return presetsOpen; }
    void setPresetsOpen (bool open) { presetsOpen = open; }

    static constexpr float flashSeconds = 2.4f;

private:
    InkcapProcessor& processor;
    juce::String flashText, hintText;
    float flashLeft = 0.0f;
    bool presetsOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InkcapController)
};

} // namespace inkcap::ui
