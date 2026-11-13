#include "ui/InkcapEditor.h"

#include "common/plugin/UiState.h"
#include "plugin/PluginProcessor.h"

namespace inkcap
{

namespace
{
    float clampScale (float s)
    {
        return juce::jlimit (ui::metrics::minScale, ui::metrics::maxScale, s);
    }
}

InkcapEditor::Face::Face (ui::InkcapController& c)
    : display (c, dialog), strip (c)
{
    setTitle ("Inkcap");
    addAndMakeVisible (display);
    addAndMakeVisible (strip);
    addChildComponent (dialog);   // over everything while it is open

    // Keyboard order: the display (header, pad, rows), then the keys on the body.
    display.setExplicitFocusOrder (1);
    strip.setExplicitFocusOrder (2);
}

void InkcapEditor::Face::paint (juce::Graphics& g)
{
    const auto& c = ui::Theme::glass();
    const auto a = getLocalBounds().toFloat();

    // The window is the appliance: warm grey to the edges, lit from above (CSS: inset 0 2px 0
    // white .8, inset 0 -3px 0 black .08).
    g.fillAll (c.body);
    g.setColour (c.bodyEdgeLight);
    g.fillRect (a.withHeight (2.0f));
    g.setColour (c.bodyEdgeShade);
    g.fillRect (a.withTop (a.getBottom() - 3.0f));
}

void InkcapEditor::Face::resized()
{
    display.setBounds (ui::layout::display().toNearestInt());
    strip.setBounds (ui::layout::strip().toNearestInt());
    dialog.setBounds (getLocalBounds());
}

//==============================================================================
InkcapEditor::InkcapEditor (InkcapProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      controller (std::make_unique<ui::InkcapController> (p)),
      face (*controller),
      vblank (this, [this] (double t) { animateFrame (t); })
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setTitle ("Inkcap");

    // Read the stored scale first: setting resize limits resizes (and so re-stores the scale).
    const auto storedScale = clampScale ((float) processor.getUiState().getProperty (myzel::plugin::uistate::scale, 1.0f));
    scale = storedScale;

    setResizable (true, false);
    addAndMakeVisible (face);

    tooltips = std::make_unique<juce::TooltipWindow> (&face, 900);
    tooltips->setOpaque (false);

    auto& display = face.display;
    display.getScale = [this] { return scale; };
    display.onScaleChosen = [this] (float s) { setScale (s); };

    controller->getPresets().onChange = [this] { face.display.update(); face.strip.update(); };

    applyScale (storedScale);
    processor.getState().state.addListener (this);
}

InkcapEditor::~InkcapEditor()
{
    processor.getState().state.removeListener (this);
    controller->getPresets().onChange = nullptr;
    controller->setHint ({});
    tooltips.reset();
    setLookAndFeel (nullptr);
}

void InkcapEditor::applyScale (float newScale)
{
    newScale = clampScale (newScale);
    const auto w = (double) ui::metrics::baseWidth, h = (double) ui::metrics::baseHeight;
    setResizeLimits (juce::roundToInt (w * ui::metrics::minScale), juce::roundToInt (h * ui::metrics::minScale),
                     juce::roundToInt (w * ui::metrics::maxScale), juce::roundToInt (h * ui::metrics::maxScale));

    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (w / h);

    scale = newScale;
    setSize (juce::roundToInt (w * scale), juce::roundToInt (h * scale));
    resized();
}

void InkcapEditor::setScale (float newScale)
{
    applyScale (newScale);
}

void InkcapEditor::resized()
{
    if (getWidth() <= 0)
        return;

    scale = clampScale ((float) getWidth() / (float) ui::metrics::baseWidth);
    face.setTransform (juce::AffineTransform::scale (scale));
    face.setBounds (0, 0, ui::metrics::baseWidth, ui::metrics::baseHeight);

    processor.getUiState().setProperty (myzel::plugin::uistate::scale, scale, nullptr);
}

void InkcapEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::Theme::glass().body);
}

void InkcapEditor::animateFrame (double timeSeconds)
{
    const auto dt = lastFrameTime < 0.0 ? (1.0f / 60.0f) : (float) juce::jlimit (0.0, 0.1, timeSeconds - lastFrameTime);
    lastFrameTime = timeSeconds;

    controller->tick (dt);
    face.display.update();
    face.strip.update();
}

bool InkcapEditor::keyPressed (const juce::KeyPress& key)
{
    // Nothing inside has focus yet: the display's shortcuts still work.
    return face.display.keyPressed (key);
}

void InkcapEditor::valueTreeRedirected (juce::ValueTree&)
{
    // The host restored a session while the editor is open: re-read its pages and values.
    face.display.sessionRestored();
    face.display.update();
    face.strip.update();
}

} // namespace inkcap
