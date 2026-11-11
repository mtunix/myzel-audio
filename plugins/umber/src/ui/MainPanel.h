#pragma once

#include "ui/AbCompare.h"
#include "ui/HeaderBar.h"
#include "ui/InlineDialog.h"
#include "ui/Knob.h"
#include "ui/LampButton.h"
#include "ui/NeedleMeter.h"
#include "ui/PanelArtwork.h"
#include "ui/PresetManager.h"
#include "ui/ValueBubble.h"

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
class MainPanel final : public juce::Component,
                        private juce::ValueTree::Listener
{
public:
    explicit MainPanel (UmberProcessor& processor);
    ~MainPanel() override;

    /** One animation frame (called on vblank; the snapshot tool calls it directly). */
    void animateFrame (double timeSeconds);

    std::function<void (float)> onScaleChosen;
    std::function<float()> getScale;

    // Test hooks for the snapshot tool.
    Knob* findKnob (const juce::String& parameterId) const;
    NeedleMeter& getMeter() noexcept { return meter; }
    HeaderBar& getHeader() noexcept { return header; }
    InlineDialog& getDialog() noexcept { return dialog; }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void valueTreeRedirected (juce::ValueTree&) override;
    void applyUiState();
    void buildControls();
    void updateWeightDimming();

    UmberProcessor& processor;
    PresetManager presets;
    AbCompare ab;

    InlineDialog dialog;
    HeaderBar header;
    ValueBubble bubble;
    NeedleMeter meter;

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
