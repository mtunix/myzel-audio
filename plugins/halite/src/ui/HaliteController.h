#pragma once

#include "common/ui/EditorController.h"
#include "dsp/Modes.h"
#include "ui/Help.h"
#include "ui/Theme.h"

namespace halite
{
class HaliteProcessor;
}

namespace halite::ui
{

enum class DisplayView
{
    timeline = 0,
    cloud
};

enum class Band
{
    ess = 0,
    harsh
};

/**
    What the editor's parts share: parameter access in the workbench's terms, every edit as a host
    gesture and an undo step, the session's UI state (src/ui/UiState.h), the status line's help and
    short messages. Owned by the editor; message thread only.
*/
class HaliteController final : public myzel::ui::EditorController
{
public:
    explicit HaliteController (HaliteProcessor&);

    HaliteProcessor& getProcessor() noexcept { return processor; }

    float plain (const char* id) const;
    float normalised (const char* id) const;
    juce::String text (const char* id) const;       // the parameter's read-out, minus sign typeset

    /** One edit: a host gesture and one undo step (Bypass and Listen are not recorded). */
    void setNormalised (const char* id, float value01);
    void setPlain (const char* id, float value);
    void toggle (const char* id);
    void resetToDefault (const char* id);

    /** Typed entry: the parameter's own parser reads the text; text it does not understand changes nothing. */
    void setFromText (const char* id, const juce::String& text);

    /** A drag or a burst of wheel steps: one gesture from begin to end. Two parameters edited as one
        (Speed: Auto and its release) open one undo transaction between them. */
    void beginEdit (const char* id, bool newTransaction = true);
    void endEdit (const char* id);

    // The modes, as the controls read them
    Detection detection() const;
    Shape shape() const;
    bool harshOn() const;
    LatencyMode latency() const;

    /** The threshold the band's Threshold control edits: relative in Voice, absolute in Allround. */
    const char* thresholdId (Band) const;

    /** "10.7 ms" or "Live": the latency the host is told. */
    juce::String latencyText() const;

    /** Detection · Shape · Stereo · look-ahead, for the status line's right side. */
    juce::String modeSummary() const;

    void setLatency (LatencyMode);

    // UI state (stored in the session; never undoable)
    DisplayView getDisplayView() const;
    void setDisplayView (DisplayView);
    Band getBand() const;
    void setBand (Band);
    bool isEventsShown() const;
    void setEventsShown (bool);
    bool isAdvancedOpen() const;
    void setAdvancedOpen (bool);
    float getZoom (DisplayView) const;       // seconds
    void setZoom (DisplayView, float seconds);

    static constexpr float timelineZooms[] { 2.0f, 4.0f, 6.0f, 8.0f, 10.0f };
    static constexpr float cloudZooms[] { 10.0f, 20.0f, 30.0f, 45.0f, 60.0f };

    // The status line
    void setHover (const juce::Component*, const HelpText&);
    void clearHover (const juce::Component*);
    void setFocusHelp (const juce::Component*, const HelpText&);
    void clearFocusHelp (const juce::Component*);
    HelpText currentHelp() const;

    /** A short message ("Threshold set to −14.2 dB from 12 esses"), shown for a moment in the status line. */
    void flash (const juce::String& title, const juce::String& message);
    bool isFlashing() const noexcept { return flashLeft > 0.0f; }

    /** Advances the message clock (the editor's frame clock). */
    void tick (float dt);

    /** Something every part may show changed (UI state, theme, a session restored): re-read and repaint. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void uiChanged() = 0;
    };

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }
    void notifyUiChanged() { listeners.call ([] (Listener& l) { l.uiChanged(); }); }

    static constexpr float flashSeconds = 3.0f;

private:
    juce::ValueTree ui() const;

    HaliteProcessor& processor;
    juce::ListenerList<Listener> listeners;

    const juce::Component* hoverSource = nullptr;
    const juce::Component* focusSource = nullptr;
    HelpText hoverHelp, focusHelp, flashHelp;
    float flashLeft = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HaliteController)
};

} // namespace halite::ui
