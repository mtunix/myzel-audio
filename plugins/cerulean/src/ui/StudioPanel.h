#pragma once

#include "common/ui/EditorView.h"
#include "ui/PanelController.h"
#include "common/ui/StudioControls.h"
#include "common/ui/StudioMeters.h"
#include "ui/StudioTail.h"

#include <array>

namespace cerulean
{
class CeruleanProcessor;
}

namespace cerulean::ui
{

/**
    The studio view: Cerulean as workstation software rather than a pedal. Same parameters,
    presets, A/B and undo as the panel, all on one flat screen with nothing hidden:

        ALGORITHM list   | DECAY (headline)  read-outs      FREEZE | OUTPUT: meters,
                         | TAIL graph, with Decay and Pre-delay      |   Mix, Output,
        BEHAVIOUR:       |   handles                                 |   Wet only
          Trails, Freeze | SPACE | CHARACTER | TONE | DUCK cards     |
          mode, Tempo    |  (exact values, typed entry)              |

    Laid out once at the panel's base size (so switching views never resizes the window) and
    scaled by the editor. The editor's frame clock drives the meters, the graph and the meaning
    changes (Param A/B relabel, Pre-delay and Echo sync), polled from the PanelController.
    See src/ui/DESIGN.md.
*/
class StudioPanel final : public EditorView
{
public:
    explicit StudioPanel (PanelController& controller);

    ViewMode getMode() const noexcept override { return ViewMode::studio; }
    int getBaseHeight() const noexcept override { return metrics::baseHeight; }
    void animateFrame (float dt) override;
    std::optional<juce::Point<int>> getMixLockAnchor() const override { return mixLockAnchor; }

    // Test hooks.
    /** The slider that currently edits a parameter (follows rebinding: Pre-delay note, Echo note). */
    StudioSlider* findSlider (const juce::String& parameterId) const;
    SelectorRow& getSelectorRow (int index) noexcept { return *algorithmRows[(size_t) index]; }
    StudioToggle& getFreezeKey() noexcept { return freezeKey; }
    StudioToggle* findToggle (const juce::String& parameterId) noexcept;
    StudioToggle& getFreezeModeKey (FreezeMode mode) noexcept { return mode == FreezeMode::momentary ? momentaryKey : latchKey; }
    StudioTail& getTail() noexcept { return tail; }
    StudioMeters& getMeters() noexcept { return meters; }
    juce::String getCharacterTitle() const;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void buildControls();
    StudioSlider& addSlider (const char* parameterId, const juce::String& label, juce::Rectangle<float> bounds, const juce::String& tip);
    StudioToggle& addToggle (StudioToggle& toggle, const char* parameterId, juce::Rectangle<float> bounds, const juce::String& tip);
    void updateMeanings();
    void updateTempo();
    void renderBackground (juce::Graphics& g, juce::Rectangle<float> area) const;

    PanelController& controller;
    CeruleanProcessor& processor;

    StudioTail tail;
    StudioMeters meters;
    juce::Point<int> mixLockAnchor;

    std::array<std::unique_ptr<SelectorRow>, numAlgorithms> algorithmRows;
    std::unique_ptr<juce::ParameterAttachment> algorithmAttachment;

    std::vector<std::unique_ptr<StudioSlider>> sliders;
    StudioSlider* preDelaySlider = nullptr;
    StudioSlider* paramASlider = nullptr;
    StudioSlider* paramBSlider = nullptr;
    StudioSlider* bpmSlider = nullptr;

    StudioToggle freezeKey { "FREEZE", StudioToggle::Style::block };
    std::unique_ptr<juce::ParameterAttachment> freezeAttachment;
    StudioToggle latchKey { "LATCH", StudioToggle::Style::segmentLeft };
    StudioToggle momentaryKey { "HOLD", StudioToggle::Style::segmentRight };
    std::unique_ptr<juce::ParameterAttachment> freezeModeAttachment;
    StudioToggle trailsKey { "TRAILS", StudioToggle::Style::toggleSwitch };
    StudioToggle layerKey { "LAYER", StudioToggle::Style::toggleSwitch };
    StudioToggle wetKey { "WET ONLY", StudioToggle::Style::block };
    StudioToggle duckKey { "DUCK", StudioToggle::Style::toggleSwitch };
    StudioToggle preSyncKey { "SYNC", StudioToggle::Style::chip };
    StudioToggle echoSyncKey { "SYNC", StudioToggle::Style::chip };
    std::vector<std::pair<juce::String, StudioToggle*>> toggles;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    Meanings shown;
    bool meaningsShown = false;
    bool shownHostTempo = false, shownDuck = false, statusShown = false;
    double shownBpm = -1.0;

    drawing::CachedLayer background;
    juce::SharedResourcePointer<Fonts> fonts;

    int framesSinceTempo = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioPanel)
};

} // namespace cerulean::ui
