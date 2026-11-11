#include "common/ui/PluginEditor.h"

#include "common/plugin/UiState.h"
#include "common/ui/StudioTheme.h"

namespace myzel::ui
{

namespace
{
    float clampScale (float s)
    {
        return juce::jlimit (metrics::minScale, metrics::maxScale, s);
    }
}

PluginEditor::PluginEditor (juce::AudioProcessor& p, std::unique_ptr<EditorController> c, int width, ViewFactory factory)
    : juce::AudioProcessorEditor (p),
      host (c->getHost()),
      controller (std::move (c)),
      makeView (std::move (factory)),
      baseWidth (width)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    // Read the stored scale first: setting resize limits resizes (and so re-stores the scale).
    const auto storedScale = clampScale ((float) host.getUiState().getProperty (plugin::uistate::scale, 1.0f));
    scale = storedScale;

    setResizable (true, false);

    // The session remembers the view it was edited in (the user's default if it has none yet).
    const auto mode = controller->getView();
    host.getUiState().setProperty (plugin::uistate::view, toString (mode), nullptr);
    showView (mode);
    applyBaseSize (storedScale);

    host.getState().state.addListener (this);
}

PluginEditor::~PluginEditor()
{
    host.getState().state.removeListener (this);
    tooltips.reset();
    view.reset();
    setLookAndFeel (nullptr);
}

void PluginEditor::showView (ViewMode mode)
{
    if (view != nullptr && view->getMode() == mode)
        return;

    tooltips.reset();
    view.reset();

    view = makeView (mode, *controller);

    view->getScale = [this] { return scale; };
    view->onBaseSizeChanged = [this] { applyBaseSize (scale); };
    view->onScaleChosen = [this] (float s) { setScale (s); };

    // The header button that asks for a switch belongs to the view being replaced, so the switch
    // happens after its click handler has returned.
    view->onViewChosen = [safe = juce::Component::SafePointer<PluginEditor> (this)] (ViewMode m)
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

void PluginEditor::applyBaseSize (float newScale)
{
    // Width sets the scale; the height follows the view (the panel's Advanced deck adds a strip).
    newScale = clampScale (newScale);
    const auto w = (double) baseWidth, h = (double) view->getBaseHeight();
    setResizeLimits (juce::roundToInt (w * metrics::minScale), juce::roundToInt (h * metrics::minScale),
                     juce::roundToInt (w * metrics::maxScale), juce::roundToInt (h * metrics::maxScale));

    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (w / h);

    scale = newScale;
    setSize (juce::roundToInt (w * scale), juce::roundToInt (h * scale));
    resized();
}

void PluginEditor::setView (ViewMode mode)
{
    controller->setView (mode);
    showView (mode);
}

void PluginEditor::valueTreeRedirected (juce::ValueTree&)
{
    // The host restored a session while the editor is open: follow its view.
    const auto mode = controller->getView();

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginEditor> (this), mode]
    {
        if (safe != nullptr)
            safe->showView (mode);
    });
}

void PluginEditor::setScale (float newScale)
{
    applyBaseSize (newScale);
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (view != nullptr && view->getMode() == ViewMode::studio ? Theme::studio().background
                                                                       : Theme::palette().panelBottom);
}

void PluginEditor::resized()
{
    if (view == nullptr || getWidth() <= 0)
        return;

    scale = clampScale ((float) getWidth() / (float) baseWidth);
    view->setTransform (juce::AffineTransform::scale (scale));
    view->setBounds (0, 0, baseWidth, view->getBaseHeight());
    host.getUiState().setProperty (plugin::uistate::scale, scale, nullptr);
}

} // namespace myzel::ui
