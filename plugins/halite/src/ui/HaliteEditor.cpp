#include "ui/HaliteEditor.h"

#include "plugin/PluginProcessor.h"
#include "ui/UserSettings.h"

namespace halite
{

using namespace ui;

namespace
{
    constexpr int defaultWidth = 1200, defaultHeight = 720;

    juce::String latencyMs (double sampleRate)
    {
        const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        return juce::String (1000.0 * dsp::HaliteChain::latencySamples (LatencyMode::normal, rate) / rate, 1) + " ms";
    }

    const juce::String times (juce::CharPointer_UTF8 (" \xc3\x97 "));
}

HaliteEditor::HaliteEditor (HaliteProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      controller (std::make_unique<HaliteController> (p)),
      model (*controller),
      header (*controller),
      toolbar (*controller),
      mainView (*controller, model),
      spectrum (*controller, model),
      meters (model),
      sidebar (*controller, model),
      advanced (*controller),
      controlBar (*controller),
      statusLine (*controller),
      vblank (this, [this] (double t) { animateFrame (t); })
{
    applyTheme();
    setLookAndFeel (&look);
    setOpaque (true);
    setTitle ("Halite");
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    for (auto* c : std::initializer_list<juce::Component*> { &header, &toolbar, &mainView, &spectrum, &meters, &sidebar, &advanced,
                                                             &controlBar, &statusLine })
        addAndMakeVisible (c);
    addChildComponent (dialog);

    // Keyboard order follows the window top to bottom; the Advanced row sits above the bar it extends.
    header.setExplicitFocusOrder (1);
    toolbar.setExplicitFocusOrder (2);
    sidebar.setExplicitFocusOrder (3);
    advanced.setExplicitFocusOrder (4);
    controlBar.setExplicitFocusOrder (5);

    header.onPresetMenu = [this] { showMenu (header.getPresetName(), presetMenu(), [this] (int r) { performPresetItem (r); }); };
    header.onSettingsMenu = [this] { showMenu (header.getSettings(), settingsMenu(), [this] (int r) { performSettingsItem (r); }); };
    header.onLatencyMenu = [this]
    {
        juce::PopupMenu m;
        const auto live = controller->latency() == LatencyMode::live;
        m.addItem (latencyItem, "Normal (" + latencyMs (processor.getSampleRate()) + ")", true, ! live);
        m.addItem (latencyItem + 1, "Live (0 ms, no Spectral, no look-ahead)", true, live);
        showMenu (header.getLatency(), m, [this] (int r) { performSettingsItem (r); });
    };
    header.onAbMenu = [this] { showMenu (header.getAb(), abMenu(), [this] (int r) { performAbItem (r); }); };
    toolbar.onZoomMenu = [this] { showMenu (toolbar.getZoom(), zoomMenu(), [this] (int r) { performZoomItem (r); }); };
    toolbar.onToggleSidebar = [this] { toggleSidebar(); };
    toolbar.onFreeze = [this] { model.setFrozen (! model.isFrozen()); };

    controller->addListener (this);
    controller->getPresets().onChange = [this] { header.update(); };
    processor.getState().state.addListener (this);
    themeEvents->addChangeListener (this);
    juce::Desktop::getInstance().addDarkModeSettingListener (this);

    // Free resizing (PLAN.md, Behaviour): any size from the minimum, no fixed aspect; the last size is the user's.
    setResizable (true, true);
    setResizeLimits (minWidth, minHeight, maxWidth, maxHeight);
    const auto stored = userSettings::windowSize();
    const auto valid = stored.getWidth() >= minWidth && stored.getHeight() >= minHeight;
    setSize (valid ? std::min (stored.getWidth(), maxWidth) : defaultWidth, valid ? std::min (stored.getHeight(), maxHeight) : defaultHeight);

    shownLearn = processor.getLearn().state();
    animateFrame (0.0);
}

HaliteEditor::~HaliteEditor()
{
    cancelPendingUpdate();
    if (isTimerRunning())
        timerCallback();

    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    themeEvents->removeChangeListener (this);
    processor.getState().state.removeListener (this);
    controller->removeListener (this);
    controller->getPresets().onChange = nullptr;
    setLookAndFeel (nullptr);
}

//==============================================================================
void HaliteEditor::computeLayout()
{
    const auto w = getWidth(), h = getHeight();
    Layout l;

    l.compact = h < compactHeight;
    controlBar.setCompact (l.compact);
    advanced.setCompact (l.compact);

    auto area = getLocalBounds();
    l.header = area.removeFromTop (l.compact ? 40 : 44);
    l.status = area.removeFromBottom (l.compact ? 26 : 28);
    l.barMode = controlBar.modeFor (w);
    l.controlBar = area.removeFromBottom (controlBar.preferredHeight (w));

    if (controller->isAdvancedOpen())
        l.advanced = area.removeFromBottom (advanced.preferredHeight (w));

    // The sidebar docks only where the display keeps room beside it; the overlay is a passing look.
    if (w >= dockWidth)
        overlayOpen = false;

    l.sidebarDocked = w >= dockWidth && controller->isEventsShown();
    if (l.sidebarDocked)
        l.sidebar = area.removeFromRight (EventsSidebar::width);

    l.display = area;
    l.toolbar = area.removeFromTop (l.compact ? 30 : 32);
    l.meters = area.removeFromRight (MeterStrip::width);

    // The spectrum takes 45 % down to its minimum; the main view keeps the rest, and never less than 72 px
    // (with the Advanced row open in a minimum window, the spectrum gives way to 72 px as well).
    const auto avail = area.getHeight();
    auto spectrumHeight = juce::jlimit (minSpectrum, 320, juce::roundToInt (avail * 0.45));
    if (avail - spectrumHeight < 72)
        spectrumHeight = std::max (72, avail - 72);
    spectrumHeight = std::min (spectrumHeight, avail);

    l.spectrum = area.removeFromBottom (spectrumHeight);
    l.main = area;

    l.sidebarOverlay = ! l.sidebarDocked && overlayOpen;
    if (l.sidebarOverlay)
        l.sidebar = { l.display.getRight() - EventsSidebar::width, l.toolbar.getBottom(), EventsSidebar::width,
                      l.display.getBottom() - l.toolbar.getBottom() };

    layout = l;
}

void HaliteEditor::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    computeLayout();

    header.setBounds (layout.header);
    toolbar.setBounds (layout.toolbar);
    mainView.setBounds (layout.main);
    spectrum.setBounds (layout.spectrum);
    meters.setBounds (layout.meters);
    controlBar.setBounds (layout.controlBar);
    statusLine.setBounds (layout.status);

    advanced.setVisible (! layout.advanced.isEmpty());
    advanced.setBounds (layout.advanced);

    sidebar.setVisible (isSidebarShowing());
    sidebar.setOverlay (layout.sidebarOverlay);
    sidebar.setBounds (layout.sidebar);
    if (layout.sidebarOverlay)
        sidebar.toFront (false);

    toolbar.setSidebarShowing (isSidebarShowing());
    dialog.setBounds (getLocalBounds());
    dialog.toFront (false);

    // The size is the user's: saved once resizing settles.
    startTimer (600);
    repaint();
}

void HaliteEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::current().background);
}

void HaliteEditor::paintOverChildren (juce::Graphics& g)
{
    if (! layout.sidebarOverlay)
        return;

    // The overlay lies over the display: a soft shade along its left edge.
    const auto x = (float) layout.sidebar.getX();
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, x - 12.0f, 0.0f, juce::Colours::black.withAlpha (0.28f), x, 0.0f, false));
    g.fillRect (juce::Rectangle<float> (x - 12.0f, (float) layout.sidebar.getY(), 12.0f, (float) layout.sidebar.getHeight()));
}

//==============================================================================
void HaliteEditor::animateFrame (double timeSeconds)
{
    const auto dt = lastFrameTime < 0.0 ? (1.0 / 60.0) : juce::jlimit (0.0, 0.1, timeSeconds - lastFrameTime);
    lastFrameTime = timeSeconds;

    controller->tick ((float) dt);
    followLearn();
    model.update (dt);

    header.update();
    toolbar.setFrozen (model.isFrozen());
    toolbar.update();
    mainView.update();
    spectrum.update();
    meters.update (dt);
    if (sidebar.isVisible())
        sidebar.update (dt);
    controlBar.update();
    if (advanced.isVisible())
        advanced.update();
    statusLine.update();
}

void HaliteEditor::followLearn()
{
    auto& learner = processor.getLearn();
    const auto state = learner.state();

    if (state == shownLearn)
        return;

    const auto previous = std::exchange (shownLearn, state);
    if (previous != LearnState::listening || (state != LearnState::learned && state != LearnState::tooFew))
        return;

    const auto& r = learner.result();
    const auto unit = controller->detection() == Detection::voice ? juce::String (" dB") : juce::String (" dBFS");
    auto db = [&] (float v) { return withMinus ((v > 0.0f ? "+" : "") + juce::String (v, 1)) + unit; };

    const auto capture = juce::String (juce::roundToInt (dsp::LearnTuning {}.captureSeconds)) + " s";
    const auto heard = r.esses == 0 ? juce::String ("No esses") : "Only " + juce::String (r.esses) + (r.esses == 1 ? " ess" : " esses");
    auto message = state == LearnState::learned
                       ? "Threshold set to " + db (r.essDb) + " from " + juce::String (r.esses) + " esses"
                       : heard + " in " + capture + ": Threshold stays";

    if (r.harshSet)
        message << "; Harsh threshold " << db (r.harshDb);

    controller->flash ("Learn", message);
}

//==============================================================================
void HaliteEditor::showHelp (const juce::Component* source, const HelpText& help, bool fromFocus)
{
    if (fromFocus)
        controller->setFocusHelp (source, help);
    else
        controller->setHover (source, help);

    statusLine.update();
}

void HaliteEditor::hideHelp (const juce::Component* source, bool fromFocus)
{
    if (fromFocus)
        controller->clearFocusHelp (source);
    else
        controller->clearHover (source);

    statusLine.update();
}

void HaliteEditor::uiChanged()
{
    resized();
}

void HaliteEditor::valueTreeRedirected (juce::ValueTree&)
{
    // Hosts may restore a session off the message thread (JUCE's LV2 wrapper does); the editor follows on it.
    if (juce::MessageManager::existsAndIsCurrentThread())
        followRestoredSession();
    else
        triggerAsyncUpdate();
}

void HaliteEditor::handleAsyncUpdate()
{
    followRestoredSession();
}

void HaliteEditor::followRestoredSession()
{
    // The host restored a session while the editor is open: its UI state (view, band, sidebar, rows) comes with it.
    dialog.close();
    overlayOpen = false;
    resized();
    animateFrame (lastFrameTime < 0.0 ? 0.0 : lastFrameTime);
}

void HaliteEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applyTheme();
}

void HaliteEditor::darkModeSettingChanged()
{
    if (userSettings::theme() == ThemeChoice::system)
        applyTheme();
}

void HaliteEditor::timerCallback()
{
    stopTimer();
    userSettings::setWindowSize ({ getWidth(), getHeight() });
}

void HaliteEditor::applyTheme()
{
    theme::apply (theme::resolve (userSettings::theme()));
    look.refresh();
    repaint();
}

void HaliteEditor::setThemeChoice (ThemeChoice choice)
{
    userSettings::setTheme (choice);
    applyTheme();
    themeEvents->sendSynchronousChangeMessage();
}

void HaliteEditor::setWindowSize (int width, int height)
{
    setSize (juce::jlimit (minWidth, maxWidth, width), juce::jlimit (minHeight, maxHeight, height));
    timerCallback();
}

void HaliteEditor::toggleSidebar()
{
    if (getWidth() >= dockWidth)
    {
        controller->setEventsShown (! controller->isEventsShown());
        return;
    }

    overlayOpen = ! overlayOpen;
    resized();
}

bool HaliteEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto letter = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    auto& undo = controller->getUndoManager();

    if (mods.isCommandDown() && letter == 'Z')
    {
        mods.isShiftDown() ? undo.redo() : undo.undo();
        return true;
    }

    if (mods.isCommandDown() && letter == 'Y')
    {
        undo.redo();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        if (dialog.isOpen())
            dialog.close();
        else if (overlayOpen)
            toggleSidebar();
        else
            return false;
        return true;
    }

    return false;
}

//==============================================================================
void HaliteEditor::showMenu (juce::Component& target, juce::PopupMenu menu, std::function<void (int)> chosen)
{
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                        [safe = juce::Component::SafePointer<HaliteEditor> (this), chosen] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                chosen (result);
                        });
}

juce::PopupMenu HaliteEditor::settingsMenu() const
{
    juce::PopupMenu m;
    const auto live = controller->latency() == LatencyMode::live;

    m.addSectionHeader ("Latency");
    m.addItem (latencyItem, "Normal (" + latencyMs (processor.getSampleRate()) + ")", true, ! live);
    m.addItem (latencyItem + 1, "Live (0 ms, no Spectral, no look-ahead)", true, live);

    m.addSectionHeader ("Theme");
    const auto choice = userSettings::theme();
    m.addItem (themeItem + (int) ThemeChoice::dark, "Dark", true, choice == ThemeChoice::dark);
    m.addItem (themeItem + (int) ThemeChoice::light, "Light", true, choice == ThemeChoice::light);
    m.addItem (themeItem + (int) ThemeChoice::system, "System", true, choice == ThemeChoice::system);

    m.addSectionHeader ("Window size");
    for (int i = 0; i < (int) std::size (windowSizes); ++i)
    {
        const auto& s = windowSizes[i];
        m.addItem (sizeItem + i, juce::String (s.name) + "   " + juce::String (s.width) + times + juce::String (s.height), true,
                   getWidth() == s.width && getHeight() == s.height);
    }

    m.addSeparator();
    m.addItem (helpItem, "Help...");
    return m;
}

void HaliteEditor::performSettingsItem (int id)
{
    if (id >= latencyItem && id < latencyItem + numLatencyModes)
        controller->setLatency ((LatencyMode) (id - latencyItem));
    else if (id >= themeItem && id <= themeItem + (int) ThemeChoice::system)
        setThemeChoice ((ThemeChoice) (id - themeItem));
    else if (id >= sizeItem && id < sizeItem + (int) std::size (windowSizes))
        setWindowSize (windowSizes[id - sizeItem].width, windowSizes[id - sizeItem].height);
    else if (id == helpItem)
        openHelp();
}

juce::PopupMenu HaliteEditor::presetMenu() const
{
    auto& pm = controller->getPresets();
    const auto& entries = pm.getEntries();
    const auto current = pm.getCurrentIndex();

    juce::PopupMenu m;
    juce::String category;

    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        const auto c = e.isFactory ? e.category : juce::String ("User");

        if (c != category)
        {
            m.addSectionHeader (c);
            category = c;
        }

        m.addItem (i + 1, e.name, true, i == current);
    }

    m.addSeparator();
    if (pm.isCurrentUserPreset() && pm.isModified())
        m.addItem (saveItem, "Save");
    m.addItem (saveAsItem, "Save as...");
    m.addItem (renameItem, "Rename...", pm.isCurrentUserPreset() && current >= 0);
    m.addItem (deleteItem, "Delete...", pm.isCurrentUserPreset() && current >= 0);
    return m;
}

void HaliteEditor::performPresetItem (int id)
{
    auto& pm = controller->getPresets();
    const auto current = pm.getCurrentIndex();

    if (id >= 1 && id <= (int) pm.getEntries().size())
    {
        pm.load (id - 1);
        return;
    }

    if (id == saveItem && current >= 0)
    {
        const auto result = pm.saveUserPreset (pm.getCurrentName());
        controller->flash ("Preset", result.wasOk() ? "Saved " + pm.getCurrentName() : result.getErrorMessage());
    }
    else if (id == saveAsItem)
    {
        savePresetAs();
    }
    else if (id == renameItem && current >= 0)
    {
        Dialog::Options o;
        o.title = "Rename preset";
        o.message = "A new name for " + pm.getCurrentName() + ".";
        o.withTextField = true;
        o.initialText = pm.getCurrentName();
        o.confirmLabel = "Rename";
        o.onConfirm = [this, current] (const juce::String& name) -> juce::String
        {
            if (name.isEmpty())
                return "A preset needs a name.";
            const auto r = controller->getPresets().renameUserPreset (current, name);
            return r.wasOk() ? juce::String() : r.getErrorMessage();
        };
        dialog.open (o);
    }
    else if (id == deleteItem && current >= 0)
    {
        Dialog::Options o;
        o.title = "Delete preset";
        o.message = "Delete " + pm.getCurrentName() + "? Its file is removed from your user preset folder.";
        o.confirmLabel = "Delete";
        o.onConfirm = [this, current] (const juce::String&) -> juce::String
        {
            const auto r = controller->getPresets().deleteUserPreset (current);
            return r.wasOk() ? juce::String() : r.getErrorMessage();
        };
        dialog.open (o);
    }
}

void HaliteEditor::savePresetAs()
{
    auto& pm = controller->getPresets();

    Dialog::Options o;
    o.title = "Save preset";
    o.message = "Name your preset. It is stored as a file in your user preset folder; the latency stays with the session.";
    o.withTextField = true;
    o.initialText = pm.isCurrentUserPreset() ? pm.getCurrentName() : juce::String();
    o.confirmLabel = "Save";
    o.onConfirm = [this] (const juce::String& name) -> juce::String
    {
        auto& presets = controller->getPresets();

        if (name.isEmpty())
            return "A preset needs a name.";

        if (presets.userPresetExists (name))
        {
            Dialog::Options replace;
            replace.title = "Replace preset";
            replace.message = name + " exists. Replace it with the current sound?";
            replace.confirmLabel = "Replace";
            replace.onConfirm = [this, name] (const juce::String&) -> juce::String
            {
                const auto r = controller->getPresets().saveUserPreset (name);
                return r.wasOk() ? juce::String() : r.getErrorMessage();
            };
            dialog.open (replace);
            return {};
        }

        const auto r = presets.saveUserPreset (name);
        return r.wasOk() ? juce::String() : r.getErrorMessage();
    };
    dialog.open (o);
}

juce::PopupMenu HaliteEditor::zoomMenu() const
{
    const auto view = controller->getDisplayView();
    const auto current = controller->getZoom (view);
    const auto* zooms = view == DisplayView::cloud ? HaliteController::cloudZooms : HaliteController::timelineZooms;

    juce::PopupMenu m;
    for (int i = 0; i < 5; ++i)
        m.addItem (i + 1, juce::String (juce::roundToInt (zooms[i])) + " s", true, juce::approximatelyEqual (zooms[i], current));
    return m;
}

void HaliteEditor::performZoomItem (int id)
{
    const auto view = controller->getDisplayView();
    const auto* zooms = view == DisplayView::cloud ? HaliteController::cloudZooms : HaliteController::timelineZooms;

    if (id >= 1 && id <= 5)
        controller->setZoom (view, zooms[id - 1]);
}

juce::PopupMenu HaliteEditor::abMenu() const
{
    juce::PopupMenu m;
    m.addItem (abCopyItem, controller->getAb().getActiveSlot() == 0 ? "Copy A to B" : "Copy B to A");
    return m;
}

void HaliteEditor::performAbItem (int id)
{
    if (id != abCopyItem)
        return;

    // The slots are UI state, as the family's A/B keeps them: a copy is no undo step (DESIGN.md, Gestures and undo).
    auto& ab = controller->getAb();
    ab.copyActiveToOther();
    controller->flash ("A/B", ab.getActiveSlot() == 0 ? "Copied A to B" : "Copied B to A");
}

void HaliteEditor::openHelp()
{
    Dialog::Options o;
    o.title = "Using Halite";
    o.message = "Drag a knob or a field up or down; Shift drags finely and the wheel steps. Double-click a knob to reset it, "
                "or a field to type a value; right-click either for both. Tab moves between controls, the arrow keys change "
                "them and Return types a value. In the display, drag the threshold line or the spectrum's handles; click an ess "
                "in the events list to find it, and Learn to set Threshold from the next 8 seconds. The status line explains "
                "whatever is under the pointer or in focus.";
    o.confirmLabel = "Close";
    o.withCancel = false;
    dialog.open (o);
}

} // namespace halite
