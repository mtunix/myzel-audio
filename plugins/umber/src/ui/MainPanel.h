#pragma once

#include "common/ui/EditorController.h"
#include "common/ui/EditorView.h"
#include "common/ui/LockBadge.h"
#include "ui/FactoryPresets.h"
#include "ui/Header.h"
#include "common/ui/InlineDialog.h"
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

    Owns the frame clock: one VBlankAttachment drives the lamps and the meter, and each of
    those repaints only its own pixels when something visibly changed.
*/
class MainPanel final : public EditorView,
                        private juce::ValueTree::Listener
{
public:
    MainPanel (EditorController& controller, UmberProcessor& processor);
    ~MainPanel() override;

    ViewMode getMode() const noexcept override { return ViewMode::panel; }
    int getBaseHeight() const noexcept override { return metrics::baseHeight; }
    void animateFrame (double timeSeconds) override;
    HeaderBar& getHeader() noexcept override { return header; }
    InlineDialog& getDialog() noexcept override { return dialog; }

    // Test hooks for the snapshot tool.
    Knob* findKnob (const juce::String& parameterId) const;
    NeedleMeter& getMeter() noexcept { return meter; }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void valueTreeRedirected (juce::ValueTree&) override;
    void applyUiState();
    void buildControls();
    void updateWeightDimming();

    EditorController& controller;
    UmberProcessor& processor;

    InlineDialog dialog;
    HeaderBar header;
    ValueBubble bubble;
    NeedleMeter meter;
    LockBadge mixLockBadge { Theme::palette().legendDim, "Mix lock is on: presets and A/B keep Mix as it is. Turn it off in the settings menu." };

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

    // Frame clock.
    juce::VBlankAttachment vblank;
    double lastFrameTime = -1.0;
    int framesSinceRefresh = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};

} // namespace umber::ui
