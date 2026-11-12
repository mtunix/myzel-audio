#pragma once

#include "common/ui/EditorView.h"
#include "common/ui/StudioControls.h"
#include "common/ui/StudioMeters.h"
#include "ui/PanelController.h"
#include "ui/StudioTheme.h"

#include <array>

namespace mulberry
{
class MulberryProcessor;
}

namespace mulberry::ui
{

/**
    The studio view's echo graph: the same picture as the panel's echo map (EchoModel), flat, in
    two lanes with a time axis; in Pattern mode its steps are dragged as on the panel.
*/
class StudioEcho final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    StudioEcho();

    void setShape (const EchoShape& shape);
    const EchoShape& getShape() const noexcept { return shape; }
    std::function<void (int step, float level)> onStepLevel;
    std::function<void (int step)> onStepGestureStart, onStepGestureEnd;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> lane (int index) const;
    float xFor (float ms) const;
    juce::Rectangle<float> stepArea (int k) const;

    juce::SharedResourcePointer<Fonts> fonts;
    EchoShape shape;
    std::vector<EchoEvent> events;
    float span = 1000.0f;
    int dragging = -1;
    bool hasShape = false;
};

/**
    The studio view: every control visible with its exact value. Machine and mode lists on the
    left; Time, Sync, Tap and Hold over the echo graph; cards for Loop, Machine, Stereo, Time and
    Duck; meters, Mix, Output and Wet on the right. Same parameters as the panel.
*/
class StudioPanel final : public EditorView
{
public:
    explicit StudioPanel (PanelController& controller);
    ~StudioPanel() override;

    ViewMode getMode() const noexcept override { return ViewMode::studio; }
    int getBaseHeight() const noexcept override { return metrics::baseHeight; }
    void animateFrame (float dt) override;
    std::optional<juce::Point<int>> getMixLockAnchor() const override { return mixLockAnchor; }

    // Test hooks.
    StudioSlider* findSlider (const juce::String& parameterId) const;
    StudioMeters& getMeters() noexcept { return meters; }
    StudioToggle& getHoldBlock() noexcept { return holdKey; }
    StudioToggle& getSyncKey() noexcept { return syncKey; }
    SelectorRow& getMachineRow (int index) noexcept { return *machineRows[(size_t) index]; }
    SelectorRow& getModeRow (int index) noexcept { return *modeRows[(size_t) index]; }
    StudioEcho& getGraph() noexcept { return graph; }

    void paint (juce::Graphics& g) override;

private:
    StudioSlider& addSlider (const char* id, const juce::String& label, juce::Rectangle<float> bounds, const juce::String& tip);
    void addToggle (StudioToggle& toggle, const char* id, juce::Rectangle<float> bounds, const juce::String& tip);
    void buildControls();
    void updateMeanings();

    PanelController& controller;
    MulberryProcessor& processor;

    std::vector<std::unique_ptr<StudioSlider>> sliders;
    StudioSlider* timeSlider = nullptr;
    StudioSlider* rightSlider = nullptr;
    StudioSlider* paramASlider = nullptr;
    StudioSlider* paramBSlider = nullptr;
    std::array<std::unique_ptr<SelectorRow>, numMachines> machineRows;
    std::array<std::unique_ptr<SelectorRow>, numModes> modeRows;
    std::unique_ptr<juce::ParameterAttachment> machineAttachment, modeAttachment, holdAttachment, holdModeAttachment;
    std::array<std::unique_ptr<juce::ParameterAttachment>, params::maxSteps> stepAttachments;

    StudioToggle syncKey { "SYNC", StudioToggle::Style::chip };
    StudioToggle tapKey { "TAP", StudioToggle::Style::chip };
    StudioToggle holdKey { "HOLD", StudioToggle::Style::block };
    StudioToggle duckKey { "", StudioToggle::Style::toggleSwitch };
    StudioToggle trailsKey { "", StudioToggle::Style::toggleSwitch };
    StudioToggle latchKey { "LATCH", StudioToggle::Style::segmentLeft };
    StudioToggle momentaryKey { "PRESS", StudioToggle::Style::segmentRight };
    StudioToggle wetKey { "WET ONLY", StudioToggle::Style::block };
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    StudioEcho graph;
    StudioMeters meters;
    std::optional<juce::Point<int>> mixLockAnchor;
    juce::SharedResourcePointer<Fonts> fonts;

    Meanings shown;
    bool meaningsShown = false;
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioPanel)
};

} // namespace mulberry::ui
