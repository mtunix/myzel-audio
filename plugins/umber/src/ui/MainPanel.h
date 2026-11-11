#pragma once

#include "common/ui/EditorController.h"
#include "common/ui/EditorView.h"
#include "ui/FactoryPresets.h"
#include "common/ui/Knob.h"
#include "common/ui/LampButton.h"
#include "ui/NeedleMeter.h"
#include "common/ui/PanelArtwork.h"
#include "common/ui/ValueBubble.h"
#include "ui/Theme.h"

#include <array>

namespace umber
{
class UmberProcessor;
}

namespace umber::ui
{

/**
    The whole instrument face at base (100 %) size. The editor scales it with a transform, so
    everything is laid out once on the fixed grid in Theme.h.

    The editor's frame clock drives the lamps and the meter, and each of those repaints only
    its own pixels when something visibly changed.
*/
class MainPanel final : public EditorView
{
public:
    MainPanel (EditorController& controller, UmberProcessor& processor);

    ViewMode getMode() const noexcept override { return ViewMode::panel; }
    int getBaseHeight() const noexcept override { return metrics::baseHeight; }
    void animateFrame (float dt) override;
    void sessionRestored() override { applyUiState(); }
    std::optional<juce::Point<int>> getMixLockAnchor() const override { return mixLockAnchor; }

    // Test hooks for the snapshot tool.
    Knob* findKnob (const juce::String& parameterId) const;
    NeedleMeter& getMeter() noexcept { return meter; }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void applyUiState();
    void buildControls();
    void updateWeightDimming();

    EditorController& controller;
    UmberProcessor& processor;

    ValueBubble bubble;
    NeedleMeter meter;
    std::optional<juce::Point<int>> mixLockAnchor;

    std::vector<std::unique_ptr<Knob>> knobs;

    std::array<std::unique_ptr<LampButton>, 5> modelKeys;
    std::unique_ptr<juce::ParameterAttachment> modelAttachment;
    LampButton slamKey { "SLAM", LampButton::Shape::wide };
    LampButton weightKey { "WEIGHT", LampButton::Shape::square };
    LampButton autoKey { "AUTO", LampButton::Shape::square };
    LampButton deltaKey { "DELTA", LampButton::Shape::square };
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    PanelArtworkSpec artwork;
    drawing::CachedLayer panelLayer;
    juce::SharedResourcePointer<Fonts> fonts;

    int framesSinceDimming = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};

} // namespace umber::ui
