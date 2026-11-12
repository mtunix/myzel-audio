#pragma once

#include "common/ui/EditorView.h"
#include "common/ui/Knob.h"
#include "common/ui/LampButton.h"
#include "common/ui/PanelArtwork.h"
#include "common/ui/ValueBubble.h"
#include "ui/AdvancedDrawer.h"
#include "ui/EchoMap.h"
#include "ui/KnobScale.h"
#include "ui/PanelController.h"
#include "ui/Theme.h"

#include <array>

namespace mulberry
{
class MulberryProcessor;
}

namespace mulberry::ui
{

/**
    The panel view: the whole instrument face at base size, laid out on the grid in Theme.h.

    Each frame follows the parameters that change what a control means (Machine relabels Param
    A/B; Sync turns Time into a note value; Mode turns the mode knob into Width, Right time or
    Steps; Hold Mode makes Hold momentary), and drives the lamps, legend crossfades, echo map and
    Advanced deck. Feedback's scale engraves 100 - 120 % as RUN (hatched), never in the caution
    colour.
*/
class MainPanel final : public EditorView
{
public:
    explicit MainPanel (PanelController& controller);

    ViewMode getMode() const noexcept override { return ViewMode::panel; }
    void animateFrame (float dt) override;
    void sessionRestored() override { applyUiState(); }
    std::optional<juce::Point<int>> getMixLockAnchor() const override { return mixLockAnchor; }

    void setAdvancedOpen (bool shouldBeOpen);
    int getBaseHeight() const noexcept override;
    bool isAdvancedOpen() const noexcept { return drawer.isOpen(); }

    // Test hooks for the snapshot tool.
    Knob* findKnob (const juce::String& parameterId) const;
    Knob& getTimeKnob() noexcept { return *timeKnob; }
    Knob& getModeAmountKnob() noexcept { return *modeAmountKnob; }
    LampButton& getHoldKey() noexcept { return holdKey; }
    LampButton& getTapKey() noexcept { return tapKey; }
    LampButton& getSyncKey() noexcept { return syncKey; }
    LampButton& getMachineKey (int index) noexcept { return *machineKeys[(size_t) index]; }
    AdvancedTab& getAdvancedTab() noexcept { return advancedTab; }
    EchoMap& getEchoMap() noexcept { return echoMap; }
    AdvancedDrawer& getDrawer() noexcept { return drawer; }
    PresetManager& getPresets() noexcept { return controller.getPresets(); }
    AbCompare& getAb() noexcept { return controller.getAb(); }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void applyUiState();
    void buildControls();
    void updateMeanings (bool animate);

    PanelController& controller;
    MulberryProcessor& processor;

    ValueBubble bubble;
    EchoMap echoMap;

    std::vector<std::unique_ptr<Knob>> knobs;
    Knob* timeKnob = nullptr;
    Knob* paramAKnob = nullptr;
    Knob* paramBKnob = nullptr;
    Knob* modeAmountKnob = nullptr;
    KnobScale timeScale, paramAScale, paramBScale, modeAmountScale;
    std::optional<juce::Point<int>> mixLockAnchor;

    std::array<std::unique_ptr<LampButton>, numMachines> machineKeys;
    std::unique_ptr<juce::ParameterAttachment> machineAttachment, holdAttachment;
    std::array<std::unique_ptr<juce::ParameterAttachment>, params::maxSteps> stepAttachments;
    LampButton syncKey { "SYNC", LampButton::Shape::square };
    LampButton tapKey { "TAP", LampButton::Shape::square };
    LampButton holdKey { "HOLD", LampButton::Shape::square };
    LampButton duckKey { "DUCK", LampButton::Shape::square };
    LampButton wetKey { "WET", LampButton::Shape::square };
    AdvancedTab advancedTab;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;
    float tapFlash = 0.0f;

    AdvancedDrawer drawer;

    PanelArtworkSpec artwork;
    drawing::CachedLayer panelLayer;
    juce::SharedResourcePointer<Fonts> fonts;

    Meanings shown;
    bool meaningsShown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};

} // namespace mulberry::ui
