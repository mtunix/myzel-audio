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

PluginEditor::PluginEditor (juce::AudioProcessor& p, std::unique_ptr<EditorController> c, int width,
                            HeaderConfig headerConfig, ViewFactory factory)
    : juce::AudioProcessorEditor (p),
      host (c->getHost()),
      controller (std::move (c)),
      makeView (std::move (factory)),
      header (host, controller->getPresets(), controller->getAb(), dialog, headerConfig),
      baseWidth (width),
      vblank (this, [this] (double t) { animateFrame (t); })
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    // Read the stored scale first: setting resize limits resizes (and so re-stores the scale).
    const auto storedScale = clampScale ((float) host.getUiState().getProperty (plugin::uistate::scale, 1.0f));
    scale = storedScale;

    setResizable (true, false);

    header.getScale = [this] { return scale; };
    header.onScaleChosen = [this] (float s) { setScale (s); };
    header.onViewChosen = [this] (ViewMode m)
    {
        if (m != getViewMode())
            setView (m);
    };

    mixLockBadge.setTooltip (headerConfig.mixLockTooltip);

    // Keyboard order: the header and the dialog before the view's controls.
    header.setExplicitFocusOrder (1);
    dialog.setExplicitFocusOrder (2);

    content.addAndMakeVisible (header);
    content.addChildComponent (mixLockBadge);
    content.addChildComponent (dialog);
    addAndMakeVisible (content);

    tooltips = std::make_unique<juce::TooltipWindow> (&content, 900);
    tooltips->setOpaque (false);

    controller->getPresets().onChange = [this]
    {
        header.refresh();
        updateMixLockBadge();
    };

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
    controller->getPresets().onChange = nullptr;
    tooltips.reset();
    view.reset();
    setLookAndFeel (nullptr);
}

void PluginEditor::showView (ViewMode mode)
{
    if (view != nullptr && view->getMode() == mode)
        return;

    view.reset();
    view = makeView (mode, *controller);

    view->onBaseSizeChanged = [this] { applyBaseSize (scale); };
    view->refreshHeader = [this] { header.refresh(); };

    content.addAndMakeVisible (*view, 0);   // under the header, the badge and the dialog

    header.setView (mode);
    header.refresh();
    updateMixLockBadge();

    // A new view starts its frame clock afresh.
    lastFrameTime = -1.0;
    framesSinceRefresh = 0;

    if (getConstrainer() != nullptr && getWidth() > 0)
        applyBaseSize (scale);   // the panel with its deck open is taller than the studio view

    resized();
    repaint();
}

void PluginEditor::updateMixLockBadge()
{
    if (view == nullptr)
        return;

    const auto anchor = view->getMixLockAnchor();
    mixLockBadge.setInk (view->getMode() == ViewMode::studio ? Theme::studio().textDim : Theme::palette().legendDim);

    if (anchor.has_value())
        mixLockBadge.setBounds (juce::Rectangle<int> (LockBadge::width, LockBadge::height).withCentre (*anchor));

    mixLockBadge.setVisible (anchor.has_value() && controller->getPresets().isMixLocked());
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

void PluginEditor::animateFrame (double timeSeconds)
{
    const auto dt = lastFrameTime < 0.0 ? (1.0f / 60.0f)
                                        : (float) juce::jlimit (0.0, 0.1, timeSeconds - lastFrameTime);
    lastFrameTime = timeSeconds;

    view->animateFrame (dt);

    // Undo history and the preset's modified flag change behind the header's back.
    if (++framesSinceRefresh >= 6)
    {
        framesSinceRefresh = 0;
        header.refresh();
    }
}

void PluginEditor::valueTreeRedirected (juce::ValueTree&)
{
    // The host restored a session while the editor is open: re-read it, then follow its view.
    view->sessionRestored();
    header.refresh();
    updateMixLockBadge();

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

    const auto bounds = juce::Rectangle<int> (baseWidth, view->getBaseHeight());
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (bounds);
    view->setBounds (bounds);
    header.setBounds (juce::Rectangle<float> ((float) baseWidth, metrics::headerHeight).toNearestInt());
    dialog.setBounds (bounds);

    host.getUiState().setProperty (plugin::uistate::scale, scale, nullptr);
}

} // namespace myzel::ui
