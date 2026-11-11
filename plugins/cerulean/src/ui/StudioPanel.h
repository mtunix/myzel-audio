#pragma once

#include "ui/EditorView.h"
#include "ui/LockBadge.h"
#include "ui/PanelController.h"
#include "ui/StudioControls.h"
#include "ui/StudioMeters.h"
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
    scaled by the editor. One VBlankAttachment drives the meters, the graph and the meaning
    changes (Param A/B relabel, Pre-delay and Echo sync), polled from the PanelController.
    See src/ui/DESIGN.md.
*/
class StudioPanel final : public EditorView,
                          private juce::ValueTree::Listener
{
public:
    explicit StudioPanel (PanelController& controller);
    ~StudioPanel() override;

    ViewMode getMode() const noexcept override { return ViewMode::studio; }
    void animateFrame (double timeSeconds) override;
    HeaderBar& getHeader() noexcept override { return header; }
    InlineDialog& getDialog() noexcept override { return dialog; }

    // Test hooks.
    /** The slider that currently edits a parameter (follows rebinding: Pre-delay note, Echo note). */
    StudioSlider* findSlider (const juce::String& parameterId) const;
    AlgorithmRow& getAlgorithmRow (int index) noexcept { return *algorithmRows[(size_t) index]; }
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
    void valueTreeRedirected (juce::ValueTree&) override;

    PanelController& controller;
    CeruleanProcessor& processor;

    InlineDialog dialog;
    HeaderBar header;
    StudioTail tail;
    LockBadge mixLockBadge { Theme::studio().textDim };
    StudioMeters meters;

    std::array<std::unique_ptr<AlgorithmRow>, numAlgorithms> algorithmRows;
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

    juce::VBlankAttachment vblank;
    double lastFrameTime = -1.0;
    int framesSinceRefresh = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioPanel)
};

} // namespace cerulean::ui
