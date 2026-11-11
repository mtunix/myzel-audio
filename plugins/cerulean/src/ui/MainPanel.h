#pragma once

#include "ui/AdvancedDrawer.h"
#include "ui/EditorView.h"
#include "ui/Knob.h"
#include "ui/KnobScale.h"
#include "ui/LampButton.h"
#include "ui/LockBadge.h"
#include "ui/PanelArtwork.h"
#include "ui/PanelController.h"
#include "ui/TailDisplay.h"
#include "ui/ValueBubble.h"

#include <array>

namespace cerulean
{
class CeruleanProcessor;
}

namespace cerulean::ui
{

/**
    The panel view: the whole instrument face at base (100 %) size. The editor scales it with a transform, so
    everything is laid out once on the fixed grid in Theme.h.

    Owns the frame clock: one VBlankAttachment drives the lamps, the legend crossfades, the tail
    display and the Advanced drawer, and each of those repaints only its own pixels when
    something visibly changed. It also follows the parameters that change what a control means
    (Algorithm relabels Param A/B; Pre-delay Sync and Echo Sync turn knobs into note values;
    Freeze Mode makes Freeze momentary), by polling the PanelController once per frame.
*/
class MainPanel final : public EditorView,
                        private juce::ValueTree::Listener
{
public:
    explicit MainPanel (PanelController& controller);
    ~MainPanel() override;

    ViewMode getMode() const noexcept override { return ViewMode::panel; }
    void animateFrame (double timeSeconds) override;
    HeaderBar& getHeader() noexcept override { return header; }
    InlineDialog& getDialog() noexcept override { return dialog; }

    void setAdvancedOpen (bool shouldBeOpen);
    int getBaseHeight() const noexcept override;
    bool isAdvancedOpen() const noexcept { return drawer.isOpen(); }

    // Test hooks for the snapshot tool.
    Knob* findKnob (const juce::String& parameterId) const;
    LampButton& getFreezeKey() noexcept { return freezeKey; }
    AdvancedTab& getAdvancedTab() noexcept { return advancedTab; }
    LampButton& getSyncKey (bool echo) noexcept { return echo ? echoSyncKey : preSyncKey; }
    LampButton& getAlgorithmKey (int index) noexcept { return *algorithmKeys[(size_t) index]; }
    TailDisplay& getTail() noexcept { return tail; }
    AdvancedDrawer& getDrawer() noexcept { return drawer; }
    PresetManager& getPresets() noexcept { return controller.getPresets(); }
    AbCompare& getAb() noexcept { return controller.getAb(); }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void valueTreeRedirected (juce::ValueTree&) override;
    void applyUiState();
    void buildControls();
    void updateMeanings (bool animate);

    PanelController& controller;
    CeruleanProcessor& processor;

    InlineDialog dialog;
    HeaderBar header;
    ValueBubble bubble;
    TailDisplay tail;

    std::vector<std::unique_ptr<Knob>> knobs;
    Knob* preDelayKnob = nullptr;
    Knob* paramAKnob = nullptr;
    Knob* paramBKnob = nullptr;
    KnobScale preDelayScale, paramAScale, paramBScale;
    LockBadge mixLockBadge { Theme::palette().legendDim };

    std::array<std::unique_ptr<LampButton>, 10> algorithmKeys;
    std::unique_ptr<juce::ParameterAttachment> algorithmAttachment;
    LampButton freezeKey { "FREEZE", LampButton::Shape::wide };
    std::unique_ptr<juce::ParameterAttachment> freezeAttachment;
    LampButton duckKey { "DUCK", LampButton::Shape::square };
    LampButton wetKey { "WET", LampButton::Shape::square };
    LampButton preSyncKey { "SYNC", LampButton::Shape::compact };
    LampButton echoSyncKey { "SYNC", LampButton::Shape::compact };
    AdvancedTab advancedTab;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    AdvancedDrawer drawer;

    PanelArtworkSpec artwork;
    drawing::CachedLayer panelLayer;
    juce::SharedResourcePointer<Fonts> fonts;

    // What the meaning-changing controls currently show.
    Meanings shown;
    bool meaningsShown = false;

    // Frame clock.
    juce::VBlankAttachment vblank;
    double lastFrameTime = -1.0;
    int framesSinceRefresh = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};

} // namespace cerulean::ui
