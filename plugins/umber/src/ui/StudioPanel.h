#pragma once

#include "common/ui/EditorController.h"
#include "common/ui/EditorView.h"
#include "common/ui/LockBadge.h"
#include "common/ui/StudioControls.h"
#include "common/ui/StudioMeters.h"
#include "ui/Header.h"
#include "ui/StudioLayout.h"
#include "ui/Theme.h"

#include <array>

namespace umber
{
class UmberProcessor;
}

namespace umber::ui
{

/**
    The studio view: every control on one flat screen, with exact values, typed entry and
    meters. Same parameters, presets and header as the panel.

    Cards: Circuit (the five circuits), Listen (Auto, Delta), Drive (Drive, Slam, Bias, Emphasis
    and a note on the circuit), Input (Stereo, Low Cut with Weight), Tone (High Cut, Tone) and
    Output (meters, Mix, Output). See src/ui/DESIGN.md.
*/
class StudioPanel final : public EditorView,
                          private juce::ValueTree::Listener
{
public:
    StudioPanel (EditorController& controller, UmberProcessor& processor);
    ~StudioPanel() override;

    ViewMode getMode() const noexcept override { return ViewMode::studio; }
    int getBaseHeight() const noexcept override { return metrics::baseHeight; }
    void animateFrame (double timeSeconds) override;
    HeaderBar& getHeader() noexcept override { return header; }
    InlineDialog& getDialog() noexcept override { return dialog; }

    // Test hooks.
    StudioSlider* findSlider (const juce::String& parameterId) const;
    StudioToggle* findToggle (const juce::String& parameterId) noexcept;
    SelectorRow& getCircuitRow (int index) noexcept { return *circuitRows[(size_t) index]; }
    StudioMeters& getMeters() noexcept { return meters; }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void buildControls();
    StudioSlider& addSlider (const char* parameterId, const juce::String& label, juce::Rectangle<float> bounds, const juce::String& tip);
    StudioToggle& addToggle (StudioToggle& toggle, const char* parameterId, juce::Rectangle<float> bounds, const juce::String& tip);
    void renderBackground (juce::Graphics& g, juce::Rectangle<float> area) const;
    void valueTreeRedirected (juce::ValueTree&) override;

    EditorController& controller;
    UmberProcessor& processor;

    InlineDialog dialog;
    HeaderBar header;
    LockBadge mixLockBadge { Theme::studio().textDim, "Mix lock is on: presets and A/B keep Mix as it is. Turn it off in the settings menu." };
    StudioMeters meters;

    std::array<std::unique_ptr<SelectorRow>, 5> circuitRows;
    std::unique_ptr<juce::ParameterAttachment> circuitAttachment;
    int shownCircuit = 0;

    std::vector<std::unique_ptr<StudioSlider>> sliders;
    StudioToggle slamKey { "SLAM", StudioToggle::Style::block };
    StudioToggle weightKey { "WEIGHT", StudioToggle::Style::chip };
    StudioToggle autoKey { "AUTO", StudioToggle::Style::toggleSwitch };
    StudioToggle deltaKey { "DELTA", StudioToggle::Style::toggleSwitch };
    std::vector<std::pair<juce::String, StudioToggle*>> toggles;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    drawing::CachedLayer background;
    juce::SharedResourcePointer<Fonts> fonts;

    juce::VBlankAttachment vblank;
    double lastFrameTime = -1.0;
    int framesSinceRefresh = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioPanel)
};

} // namespace umber::ui
