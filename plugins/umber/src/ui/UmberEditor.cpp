#include "ui/UmberEditor.h"

#include "plugin/PluginProcessor.h"
#include "ui/UiState.h"

namespace umber
{

namespace
{
    float clampScale (float s)
    {
        return juce::jlimit (ui::metrics::minScale, ui::metrics::maxScale, s);
    }
}

UmberEditor::UmberEditor (UmberProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    panel = std::make_unique<ui::MainPanel> (p);
    panel->getScale = [this] { return scale; };
    panel->onScaleChosen = [this] (float s) { setScale (s); };
    addAndMakeVisible (*panel);

    tooltips = std::make_unique<juce::TooltipWindow> (panel.get(), 900);
    tooltips->setOpaque (false);

    // Read the stored scale first: setting resize limits resizes (and so re-stores the scale).
    const auto storedScale = clampScale ((float) processor.getUiState().getProperty (ui::uistate::scale, 1.0f));

    const auto w = (double) ui::metrics::baseWidth, h = (double) ui::metrics::baseHeight;
    setResizable (true, false);
    setResizeLimits (juce::roundToInt (w * ui::metrics::minScale), juce::roundToInt (h * ui::metrics::minScale),
                     juce::roundToInt (w * ui::metrics::maxScale), juce::roundToInt (h * ui::metrics::maxScale));

    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (w / h);

    scale = storedScale;
    setSize (juce::roundToInt (w * scale), juce::roundToInt (h * scale));
}

UmberEditor::~UmberEditor()
{
    tooltips.reset();
    panel.reset();
    setLookAndFeel (nullptr);
}

void UmberEditor::setScale (float newScale)
{
    newScale = clampScale (newScale);
    setSize (juce::roundToInt ((float) ui::metrics::baseWidth * newScale),
             juce::roundToInt ((float) ui::metrics::baseHeight * newScale));
}

void UmberEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::Theme::palette().panelBottom);
}

void UmberEditor::resized()
{
    if (panel == nullptr)
        return;

    scale = clampScale ((float) getWidth() / (float) ui::metrics::baseWidth);
    panel->setTransform (juce::AffineTransform::scale (scale));
    panel->setBounds (0, 0, ui::metrics::baseWidth, ui::metrics::baseHeight);
    processor.getUiState().setProperty (ui::uistate::scale, scale, nullptr);
}

} // namespace umber
