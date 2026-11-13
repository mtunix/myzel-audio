#pragma once

#include "common/ui/InlineDialog.h"
#include "common/ui/KitLookAndFeel.h"
#include "ui/Display.h"
#include "ui/InkcapController.h"
#include "ui/Strip.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace inkcap
{

class InkcapProcessor;

/**
    Inkcap's editor: the glass face (src/ui/DESIGN.md). Built on the shared EditorController
    (presets, A/B, undo) but not on the shared PluginEditor, whose header rail and view switch
    the face has no place for (PLAN.md, Settled 9). It brings what PluginEditor would: the
    scale (75-200 % in the shared steps, fixed aspect, stored in the session), the frame clock,
    the session listener, the in-editor dialog and the kit's look-and-feel for menus, tooltips
    and text fields.
*/
class InkcapEditor final : public juce::AudioProcessorEditor,
                           private juce::ValueTree::Listener
{
public:
    explicit InkcapEditor (InkcapProcessor&);
    ~InkcapEditor() override;

    void setScale (float newScale);
    float getScale() const noexcept { return scale; }

    /** One frame of the frame clock (called on vblank; the snapshot tool drives it directly). */
    void animateFrame (double timeSeconds);

    ui::InkcapController& getController() noexcept { return *controller; }
    ui::Display& getDisplay() noexcept { return face.display; }
    ui::Strip& getStrip() noexcept { return face.strip; }
    myzel::ui::InlineDialog& getDialog() noexcept { return face.dialog; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    /** The appliance at base size: the body, the display, the strip, the dialog over them. */
    struct Face final : public juce::Component
    {
        explicit Face (ui::InkcapController&);
        void paint (juce::Graphics&) override;
        void resized() override;

        myzel::ui::InlineDialog dialog;
        ui::Display display;
        ui::Strip strip;
    };

    void applyScale (float newScale);
    void valueTreeRedirected (juce::ValueTree&) override;

    InkcapProcessor& processor;
    myzel::ui::KitLookAndFeel lookAndFeel;              // declared first: outlives every child
    std::unique_ptr<ui::InkcapController> controller;  // outlives the face
    Face face;
    std::unique_ptr<juce::TooltipWindow> tooltips;

    float scale = 1.0f;
    juce::VBlankAttachment vblank;
    double lastFrameTime = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InkcapEditor)
};

} // namespace inkcap
