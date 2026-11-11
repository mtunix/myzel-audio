#include "ui/CeruleanEditor.h"

#include "plugin/PluginProcessor.h"
#include "ui/UiState.h"

namespace cerulean
{

namespace
{
    float clampScale (float s)
    {
        return juce::jlimit (ui::metrics::minScale, ui::metrics::maxScale, s);
    }
}

CeruleanEditor::CeruleanEditor (CeruleanProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      controller (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    // Read the stored scale first: setting resize limits resizes (and so re-stores the scale).
    const auto storedScale = clampScale ((float) processor.getUiState().getProperty (ui::uistate::scale, 1.0f));
    scale = storedScale;

    setResizable (true, false);

    // The session remembers the view it was edited in (the user's default if it has none yet).
    const auto mode = controller.getView();
    processor.getUiState().setProperty (ui::uistate::view, ui::toString (mode), nullptr);
    showView (mode);
    applyBaseSize (storedScale);

    processor.getState().state.addListener (this);
}

CeruleanEditor::~CeruleanEditor()
{
    processor.getState().state.removeListener (this);
    tooltips.reset();
    view.reset();
    setLookAndFeel (nullptr);
}

void CeruleanEditor::showView (ui::ViewMode mode)
{
    if (view != nullptr && view->getMode() == mode)
        return;

    tooltips.reset();
    view.reset();

    if (mode == ui::ViewMode::studio)
        view = std::make_unique<ui::StudioPanel> (controller);
    else
        view = std::make_unique<ui::MainPanel> (controller);

    view->getScale = [this] { return scale; };
    view->onBaseSizeChanged = [this] { applyBaseSize (scale); };
    view->onScaleChosen = [this] (float s) { setScale (s); };

    // The header button that asks for a switch belongs to the view being replaced, so the switch
    // happens after its click handler has returned.
    view->onViewChosen = [safe = juce::Component::SafePointer<CeruleanEditor> (this)] (ui::ViewMode m)
    {
        juce::MessageManager::callAsync ([safe, m]
        {
            if (safe != nullptr)
                safe->setView (m);
        });
    };

    addAndMakeVisible (*view);

    tooltips = std::make_unique<juce::TooltipWindow> (view.get(), 900);
    tooltips->setOpaque (false);

    if (getConstrainer() != nullptr && getWidth() > 0)
        applyBaseSize (scale);   // the panel with its deck open is taller than the studio view

    resized();
    repaint();
}

void CeruleanEditor::applyBaseSize (float newScale)
{
    // Width sets the scale; the height follows the view (the panel's Advanced deck adds a strip).
    newScale = clampScale (newScale);
    const auto w = (double) ui::metrics::baseWidth, h = (double) view->getBaseHeight();
    setResizeLimits (juce::roundToInt (w * ui::metrics::minScale), juce::roundToInt (h * ui::metrics::minScale),
                     juce::roundToInt (w * ui::metrics::maxScale), juce::roundToInt (h * ui::metrics::maxScale));

    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (w / h);

    scale = newScale;
    setSize (juce::roundToInt (w * scale), juce::roundToInt (h * scale));
    resized();
}

void CeruleanEditor::setView (ui::ViewMode mode)
{
    controller.setView (mode);
    showView (mode);
}

ui::MainPanel& CeruleanEditor::getMainPanel() noexcept
{
    auto* panel = dynamic_cast<ui::MainPanel*> (view.get());
    jassert (panel != nullptr);
    return *panel;
}

ui::StudioPanel& CeruleanEditor::getStudioPanel() noexcept
{
    auto* studio = dynamic_cast<ui::StudioPanel*> (view.get());
    jassert (studio != nullptr);
    return *studio;
}

void CeruleanEditor::valueTreeRedirected (juce::ValueTree&)
{
    // The host restored a session while the editor is open: follow its view.
    const auto mode = controller.getView();

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<CeruleanEditor> (this), mode]
    {
        if (safe != nullptr)
            safe->showView (mode);
    });
}

void CeruleanEditor::setScale (float newScale)
{
    applyBaseSize (newScale);
}

void CeruleanEditor::paint (juce::Graphics& g)
{
    g.fillAll (view != nullptr && view->getMode() == ui::ViewMode::studio ? ui::Theme::studio().background
                                                                           : ui::Theme::palette().panelBottom);
}

void CeruleanEditor::resized()
{
    if (view == nullptr || getWidth() <= 0)
        return;

    scale = clampScale ((float) getWidth() / (float) ui::metrics::baseWidth);
    view->setTransform (juce::AffineTransform::scale (scale));
    view->setBounds (0, 0, ui::metrics::baseWidth, view->getBaseHeight());
    processor.getUiState().setProperty (ui::uistate::scale, scale, nullptr);
}

} // namespace cerulean
