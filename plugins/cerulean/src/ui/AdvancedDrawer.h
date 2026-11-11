#pragma once

#include "ui/Knob.h"
#include "ui/LampButton.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace cerulean
{
class CeruleanProcessor;
}

namespace cerulean::ui
{

/**
    The Advanced deck: the hidden parameters on a lower section of the instrument, below the
    panel. Opening it grows the editor by layout::deckHeight; nothing on the panel is covered.
    The ADVANCED tab that opens and closes it is the panel's (engraved on the bottom edge).

    One row of small knobs and a key in labelled groups, each with its value printed underneath:

        TANK: Low End, Diffusion, Mod Rate, Width | INPUT: Low Cut, High Cut |
        DUCK: Amount, Threshold, Release | TEMPO: BPM | FREEZE: Mode | TAILS: Trails

    Pre-delay Sync and Echo Sync are on the panel itself (under Pre-delay and Param A), and the
    note values are picked with those knobs, so the deck has no sync row.

    The artwork is cached; values are polled on the panel's frame clock while it is open and
    repainted only when their text changes. BPM is greyed out while the host provides a tempo.
*/
class AdvancedDrawer final : public juce::Component
{
public:
    AdvancedDrawer (CeruleanProcessor& processor, ValueBubble& bubble);
    ~AdvancedDrawer() override;

    void setOpen (bool shouldBeOpen);
    bool isOpen() const noexcept { return targetOpen; }

    /** Frame clock: value read-outs and LEDs (while open). Returns true while an LED fades. */
    bool tick (float dtSeconds);

    /** Test hooks. */
    Knob* findKnob (const juce::String& parameterId) const;
    LampButton* findKey (const juce::String& parameterId) const;
    juce::String getValueTextFor (const juce::String& parameterId) const;

    void resized() override;

private:
    class Tray;
    struct Cell;

    void refreshValues (bool force);

    CeruleanProcessor& processor;
    std::unique_ptr<Tray> tray;
    bool targetOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdvancedDrawer)
};

//==============================================================================
/**
    ADVANCED, engraved on the instrument's bottom edge (the panel's while the deck is closed, the
    deck's while it is open): legend, a small LED and a chevron between engraved rules. Click,
    Space or Return toggles the deck.
*/
class AdvancedTab final : public juce::Button
{
public:
    AdvancedTab();

    void setOpenState (bool isOpen);

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    juce::SharedResourcePointer<Fonts> fonts;
    bool open = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdvancedTab)
};

} // namespace cerulean::ui
