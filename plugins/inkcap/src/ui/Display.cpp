#include "ui/Display.h"

#include "dsp/common/SeedCode.h"
#include "plugin/PluginProcessor.h"

namespace inkcap::ui
{

namespace
{
    const juce::String middleDot = juce::String::fromUTF8 ("\xc2\xb7");

    juce::String shortcut (juce::juce_wchar key, bool shift = false)
    {
        const int mods = juce::ModifierKeys::commandModifier | (shift ? juce::ModifierKeys::shiftModifier : 0);
        return juce::KeyPress ((int) key, juce::ModifierKeys (mods), 0).getTextDescriptionWithIcons();
    }

    juce::PopupMenu::Item item (int id, const juce::String& text, bool enabled = true, bool ticked = false, const juce::String& keys = {})
    {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.isEnabled = enabled;
        i.isTicked = ticked;
        i.shortcutKeyDescription = keys;
        return i;
    }
}

//==============================================================================
/** The seed line: the clock, the seed's word and code, the padlock; or a message for a moment. */
class StatusLine::SeedButton final : public juce::Button
{
public:
    explicit SeedButton (StatusLine& s) : juce::Button ("seed"), line (s)
    {
        setTitle ("Seed");
        setTooltip ("The seed of the chaos: click for copy, paste, a word, reroll, lock and the last seeds");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto& c = Theme::glass();
        auto area = getLocalBounds().toFloat();
        const auto font = line.fonts->mono (metrics::textSize);

        if (line.message.isNotEmpty())
        {
            glassdraw::fittedText (g, line.message, font, area, juce::Justification::centredRight, line.messageIsHint ? c.text : c.amber);
        }
        else
        {
            if (line.locked)
            {
                glassdraw::padlock (g, area.removeFromRight (9.0f).withSizeKeepingCentre (9.0f, 10.0f), highlighted ? c.text : c.textDim);
                area.removeFromRight (5.0f);
            }
            glassdraw::fittedText (g, line.seedText, font, area, juce::Justification::centredRight, highlighted ? c.text : c.textDim);
        }

        if (hasKeyboardFocus (false))
        {
            g.setColour (c.amber);
            g.fillRect (getLocalBounds().toFloat().removeFromBottom (1.0f));
        }
    }

private:
    StatusLine& line;
};

StatusLine::StatusLine (InkcapController& c)
    : controller (c), seedButton (std::make_unique<SeedButton> (*this))
{
    setTitle ("Status");
    seedButton->onClick = [this] { if (onSeedClicked != nullptr) onSeedClicked(); };
    addAndMakeVisible (*seedButton);
    update();
}

juce::Button& StatusLine::getSeedButton() noexcept
{
    return *seedButton;
}

void StatusLine::update()
{
    const auto d = juce::roundToInt (controller.plain ("damage"));
    const auto ch = juce::roundToInt (controller.plain ("chaos"));

    if (d != damage || ch != chaos)
    {
        damage = d, chaos = ch;
        repaint (0, 0, (int) chaosX + 120, getHeight());
    }

    const auto& t = controller.getTelemetry();
    const auto newFollowing = t.followingHost.load (std::memory_order_relaxed);
    const auto newLocked = controller.isSeedLocked();
    const auto label = controller.seedLabel();
    auto newSeed = juce::String (newFollowing ? "sync " : "free ") + middleDot + " ";

    // A seed with a word reads "thrift" 8719-8E02: the word gets what room the code leaves, a
    // long one ends in an ellipsis. Without a word: seed 3F2A-91C7, as the mock.
    const auto font = fonts->mono (metrics::textSize);
    if (label.isNotEmpty())
    {
        const auto room = (float) seedButton->getWidth() - (newLocked ? 14.0f : 0.0f)
                        - glassdraw::textWidth (font, newSeed + "\"\" " + controller.seedCode());
        newSeed += "\"" + glassdraw::fitted (label, font, juce::jmax (24.0f, room)) + "\" ";
    }
    else
    {
        newSeed += "seed ";
    }
    newSeed += controller.seedCode();

    auto newMessage = controller.getHint();
    const auto isHint = newMessage.isNotEmpty();
    if (! isHint)
        newMessage = controller.getFlash();

    if (newSeed != seedText || newMessage != message || newLocked != locked || newFollowing != following || isHint != messageIsHint)
    {
        seedText = newSeed, message = newMessage, locked = newLocked, following = newFollowing, messageIsHint = isHint;
        seedButton->setDescription (seedText + (locked ? ", locked" : ""));
        seedButton->setTooltip (juce::String (following ? "sync: Glitch and Warble follow the host's song position. "
                                                        : "free: no song position from the host, so the clock runs on its own. ")
                                + "Click for the seed menu.");
        seedButton->repaint();
    }
}

juce::Rectangle<float> StatusLine::valueArea (const char* parameterId) const
{
    const auto font = fonts->mono (metrics::textSize);
    const auto isDamage = juce::String (parameterId) == "damage";
    const auto x = (isDamage ? 0.0f : chaosX) + glassdraw::textWidth (font, isDamage ? "damage " : "chaos ");
    return { x, 0.0f, glassdraw::textWidth (font, "100"), (float) getHeight() };
}

void StatusLine::paint (juce::Graphics& g)
{
    const auto& c = Theme::glass();
    const auto font = fonts->mono (metrics::textSize);
    const auto h = (float) getHeight();
    g.setFont (font);

    auto pair = [&] (float x, const char* name, int value, const char* id)
    {
        const juce::String word (name);
        g.setColour (c.text);
        g.drawText (word, juce::Rectangle<float> (x, 0.0f, 80.0f, h), juce::Justification::centredLeft, false);

        if (! (entry.isOpen() && valueArea (id).getX() > x && valueArea (id).getX() < x + 80.0f))
        {
            g.setColour (c.amber);
            g.drawText (juce::String (value), valueArea (id).withWidth (40.0f), juce::Justification::centredLeft, false);
        }
    };

    pair (0.0f, "damage ", damage, "damage");
    pair (chaosX, "chaos ", chaos, "chaos");
}

void StatusLine::resized()
{
    const auto left = chaosX + 66.0f;
    seedButton->setBounds (juce::Rectangle<float> (left, 0.0f, (float) getWidth() - left, (float) getHeight()).toNearestInt());
    seedText.clear();
    update();
}

void StatusLine::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (const auto* id : { "damage", "chaos" })
        if (valueArea (id).expanded (6.0f, 0.0f).contains (e.position))
            openEntry (id);
}

void StatusLine::openEntry (const char* parameterId)
{
    const auto area = valueArea (parameterId).withWidth (48.0f).expanded (5.0f, 2.0f).toNearestInt();
    entry.open (*this, area, juce::String (juce::roundToInt (controller.plain (parameterId))), parameterId,
                [this, parameterId] (const juce::String& t)
                {
                    controller.setFromText (parameterId, t);
                    update();
                    repaint();
                },
                juce::Justification::centredLeft);
    repaint();
}

//==============================================================================
/** The preset's number and name in the header: a click opens the browser. */
class Display::PresetButton final : public juce::Button
{
public:
    explicit PresetButton (Display& d) : juce::Button ("preset"), display (d)
    {
        setTitle ("Preset");
        setTooltip ("The preset: click to browse");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto& c = Theme::glass();
        const auto font = display.fonts->mono (metrics::textSize);
        const auto baseline = 18.0f;
        g.setFont (font);

        auto x = 0.0f;
        if (display.presetNumber.isNotEmpty())
        {
            g.setColour (c.textDim);
            g.drawSingleLineText (display.presetNumber, 0, (int) baseline);
            x = glassdraw::textWidth (font, display.presetNumber) + 14.0f;
        }

        const auto room = (float) getWidth() - x - 14.0f;
        const auto name = glassdraw::fitted (display.presetName, font, room);
        g.setColour (highlighted ? c.amber : c.text);
        g.drawSingleLineText (name, (int) x, (int) baseline);

        if (display.presetModified)
        {
            // Unsaved changes: a small amber dot after the name.
            g.setColour (c.amber);
            g.fillEllipse (x + glassdraw::textWidth (font, name) + 6.0f, baseline - 6.0f, 5.0f, 5.0f);
        }

        if (hasKeyboardFocus (false))
        {
            g.setColour (c.amber);
            g.fillRect (getLocalBounds().toFloat().removeFromBottom (1.0f));
        }
    }

private:
    Display& display;
};

//==============================================================================
Display::Display (InkcapController& c, myzel::ui::InlineDialog& d)
    : controller (c),
      dialog (d),
      presetButton (std::make_unique<PresetButton> (*this)),
      pad (c),
      status (c),
      modules (c),
      wear (c),
      page (c),
      presetsPage (c, d)
{
    // A click on the glass gives it focus, so the seed and undo shortcuts work (PLAN.md, Seeds).
    setTitle ("Display");
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);

    presetsKey.setTooltip ("Presets: browse, save, rename, delete");
    abKey.setTooltip ("Compare two settings: click to switch between a and b. Right-click: copy this setting to the other slot.");
    undoKey.setTooltip ("Undo. Right-click: redo");
    bypassKey.setTooltip ("Bypass: the dry signal, latency-aligned");
    settingsKey.setTooltip ("Settings: level match, tempo, locks, seed, window size");
    bypassKey.setCaution (true);

    presetButton->onClick = [this] { setPresetsOpen (! controller.arePresetsOpen()); };
    presetsKey.onClick = [this] { setPresetsOpen (! controller.arePresetsOpen()); };
    abKey.onClick = [this]
    {
        auto& ab = controller.getAb();
        ab.selectSlot (1 - ab.getActiveSlot());
        controller.flash (ab.getActiveSlot() == 0 ? "comparing: a" : "comparing: b");
        refreshHeader();
    };
    abKey.onSecondaryClick = [this] { performMenuItem (abCopyItem); };
    undoKey.onClick = [this]
    {
        controller.getUndoManager().undo();
        refreshHeader();
    };
    undoKey.onSecondaryClick = [this] { performMenuItem (redoItem); };
    bypassKey.onClick = [this] { controller.toggle (params::bypass); refreshHeader(); };
    settingsKey.onClick = [this] { showMenu (settingsMenu(), settingsKey, 220); };
    status.onSeedClicked = [this] { showMenu (seedMenu(), status.getSeedButton(), 230); };

    modules.onOpen = [this] (Module m) { openModulePage ((int) m); };
    page.onClose = [this] { openModulePage (-1); };
    presetsPage.onClose = [this] { setPresetsOpen (false); };

    addAndMakeVisible (*presetButton);
    for (auto* k : { &presetsKey, &abKey, &undoKey, &bypassKey, &settingsKey })
        addAndMakeVisible (k);

    addAndMakeVisible (pad);
    addAndMakeVisible (status);
    addAndMakeVisible (modules);
    addAndMakeVisible (wear);
    addChildComponent (page);
    addChildComponent (presetsPage);

    refreshHeader();
    showPage();
}

Display::~Display() = default;

juce::Button& Display::getPresetButton() noexcept
{
    return *presetButton;
}

void Display::refreshHeader()
{
    auto& pm = controller.getPresets();
    const auto name = pm.getCurrentName().isNotEmpty() ? pm.getCurrentName() : juce::String ("no preset");
    const auto number = controller.presetNumber();
    const auto modified = pm.isModified();

    if (name != presetName || number != presetNumber || modified != presetModified)
    {
        presetName = name, presetNumber = number, presetModified = modified;
        presetButton->setDescription (name + (modified ? ", modified" : ""));
        presetButton->repaint();
    }

    auto& um = controller.getUndoManager();
    undoKey.setEnabled (um.canUndo());

    const auto slot = controller.getAb().getActiveSlot();
    abKey.setHighlight (slot == 0 ? 0 : 2, 1);
    abKey.setDescription (slot == 0 ? "a is active" : "b is active");

    const auto bypassed = controller.flag (params::bypass);
    bypassKey.setLit (bypassed);
    bypassKey.setToggleState (bypassed, juce::dontSendNotification);

    presetsKey.setLit (controller.arePresetsOpen());
}

void Display::update()
{
    refreshHeader();

    if (controller.getModulePage() != shownPage || controller.arePresetsOpen() != shownPresets)
        showPage();

    if (controller.arePresetsOpen())
    {
        presetsPage.update();
        return;
    }

    pad.update();
    status.update();
    wear.update();

    if (shownPage >= 0)
        page.update();
    else
        modules.update();
}

void Display::sessionRestored()
{
    shownPage = -2;
    showPage();
    presetsPage.refresh();
}

void Display::openModulePage (int moduleIndex)
{
    controller.setModulePage (moduleIndex);
    showPage();

    if (moduleIndex >= 0 && isShowing())
        page.getBackKey().grabKeyboardFocus();
}

void Display::setPresetsOpen (bool open)
{
    controller.setPresetsOpen (open);
    showPage();

    if (! open && isShowing())
        presetsKey.grabKeyboardFocus();
}

void Display::showPage()
{
    shownPage = controller.getModulePage();
    shownPresets = controller.arePresetsOpen();

    const auto mainPage = ! shownPresets;
    pad.setVisible (mainPage);
    status.setVisible (mainPage);
    modules.setVisible (mainPage && shownPage < 0);
    page.setVisible (mainPage && shownPage >= 0);
    wear.setVisible (mainPage && shownPage != (int) Module::glitch);
    presetsPage.setVisible (shownPresets);

    if (shownPage >= 0)
        page.setModule ((Module) shownPage);

    modules.setSelected (shownPage);
    presetsKey.setLit (shownPresets);
    presetsKey.setToggleState (shownPresets, juce::dontSendNotification);
    repaint();
}

void Display::resized()
{
    glassLayer.invalidate();

    const auto header = layout::header();
    auto x = header.getRight();
    for (auto* k : { &settingsKey, &bypassKey, &undoKey, &abKey, &presetsKey })
    {
        const auto w = k->idealWidth();
        x -= w;
        k->setBounds (juce::Rectangle<float> (x, header.getY(), w, header.getHeight()).toNearestInt());
        x -= 16.0f;
    }

    // The preset sits after the name, on the name's baseline.
    const auto nameWidth = glassdraw::textWidth (fonts->sans (metrics::nameSize), "inkcap");
    const auto presetX = header.getX() + nameWidth + 14.0f;
    presetButton->setBounds (juce::Rectangle<float> (presetX, header.getY(), x - 24.0f - presetX, header.getHeight()).toNearestInt());

    pad.setBounds (layout::pad().toNearestInt());
    status.setBounds (layout::statusLine().toNearestInt());

    const auto right = layout::rightColumn();
    modules.setBounds (right.withHeight (layout::rowPitch * numModules).toNearestInt());
    page.setBounds (right.toNearestInt());
    wear.setBounds (layout::wear().translated (right.getX(), right.getY()).toNearestInt());
    presetsPage.setBounds (layout::main().toNearestInt());
}

void Display::paint (juce::Graphics& g)
{
    glassLayer.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> a, float)
    {
        const auto& c = Theme::glass();
        const auto corner = metrics::displayCorner;

        juce::Path glass;
        glass.addRoundedRectangle (a, corner);
        lg.setColour (c.glass);
        lg.fillPath (glass);

        // The glass sits in the body: an inner shadow from above (inset 0 2px 10px), then the
        // faint reflection on its top 120 px.
        lg.saveState();
        lg.reduceClipRegion (glass);
        for (int i = 0; i < 10; ++i)
        {
            const auto t = 1.0f - (float) i / 10.0f;
            lg.setColour (juce::Colours::black.withAlpha (0.6f * 0.16f * t * t));
            lg.drawRoundedRectangle (a.translated (0.0f, 2.0f).reduced ((float) i + 0.5f), juce::jmax (2.0f, corner - (float) i), 1.0f);
        }
        lg.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f), 0.0f, a.getY(),
                                                  juce::Colours::white.withAlpha (0.0f), 0.0f, a.getY() + 120.0f, false));
        lg.fillRect (a.withHeight (120.0f));
        lg.restoreState();

        lg.setColour (c.text);
        lg.setFont (fonts->sans (metrics::nameSize));
        lg.drawSingleLineText ("inkcap", (int) layout::header().getX(), (int) (layout::header().getY() + 18.0f));
    });

    if (keyboardFocused && hasKeyboardFocus (false))
        glassdraw::focusRing (g, getLocalBounds().toFloat().reduced (2.0f), metrics::displayCorner - 2.0f);
}

void Display::focusGained (FocusChangeType cause)
{
    keyboardFocused = cause != focusChangedByMouseClick;
    repaint();
}

void Display::focusLost (FocusChangeType)
{
    keyboardFocused = false;
    repaint();
}

bool Display::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();

    if (mods.isCommandDown() && ! mods.isAltDown())
    {
        if (code == 'C' || code == 'c') { performMenuItem (seedCopyItem); return true; }
        if (code == 'V' || code == 'v') { performMenuItem (seedPasteItem); return true; }
        if (code == 'Z' || code == 'z')
        {
            if (mods.isShiftDown())
                performMenuItem (redoItem);
            else
                controller.getUndoManager().undo();
            refreshHeader();
            return true;
        }
        if (code == 'Y' || code == 'y') { performMenuItem (redoItem); return true; }
    }

    if (key == juce::KeyPress::escapeKey)
    {
        if (controller.arePresetsOpen())
            setPresetsOpen (false);
        else if (controller.getModulePage() >= 0)
            openModulePage (-1);
        else
            return false;
        return true;
    }

    return false;
}

//==============================================================================
juce::PopupMenu Display::seedMenu()
{
    juce::PopupMenu m;
    const auto label = controller.seedLabel();
    m.addSectionHeader ("Seed " + controller.seedCode() + (label.isNotEmpty() ? "  \"" + label + "\"" : juce::String()));
    m.addItem (item (seedCopyItem, "Copy", true, false, shortcut ('c')));
    m.addItem (item (seedPasteItem, "Paste", juce::SystemClipboard::getTextFromClipboard().trim().isNotEmpty(), false, shortcut ('v')));
    m.addItem (item (seedWordItem, "Type a word..."));
    m.addItem (item (seedRerollItem, "Reroll"));
    m.addItem (item (seedLockItem, "Lock: presets keep the seed", true, controller.isSeedLocked()));

    const auto history = controller.getSeeds().history();
    if (! history.empty())
    {
        m.addSectionHeader ("Last seeds");
        const auto current = controller.getSeeds().current();
        for (size_t i = 0; i < history.size(); ++i)
        {
            const auto& e = history[i];
            const auto text = juce::String (dsp::seedcode::format (e.seed)) + (e.label.isNotEmpty() ? "   \"" + e.label + "\"" : juce::String());
            m.addItem (item (historyItem + (int) i, text, true, e.seed == current));
        }
    }
    return m;
}

juce::PopupMenu Display::settingsMenu()
{
    juce::PopupMenu m;
    auto& c = controller;

    m.addSectionHeader ("Sound");
    m.addItem (item (matchItem, "Match loudness", true, c.flag (params::match)));
    m.addItem (item (bpmItem, "Tempo without a host: " + juce::String (juce::roundToInt (c.plain (params::bpm))) + " BPM..."));

    m.addSectionHeader ("Presets and compare");
    m.addItem (item (mixLockItem, "Lock Mix", true, c.getPresets().isMixLocked()));
    m.addItem (item (seedLockItem, "Lock seed", true, c.isSeedLocked()));
    m.addItem (item (abCopyItem, c.getAb().getActiveSlot() == 0 ? "Copy a to b" : "Copy b to a"));
    m.addItem (item (redoItem, "Redo", c.getUndoManager().canRedo(), false, shortcut ('z', true)));
    m.addSubMenu ("Seed", seedMenu());

    m.addSectionHeader ("Window size");
    const auto current = getScale != nullptr ? getScale() : 1.0f;
    for (size_t i = 0; i < scales.size(); ++i)
        m.addItem (item (scaleItem + (int) i, juce::String (juce::roundToInt (scales[i] * 100.0f)) + " %", true,
                         std::abs (scales[i] - current) < 0.01f));

    m.addSeparator();
   #ifdef INKCAP_VERSION_STRING
    m.addItem (item (-1, juce::String ("Inkcap v") + INKCAP_VERSION_STRING, false));
   #endif
    return m;
}

void Display::showMenu (juce::PopupMenu menu, juce::Component& target, int minimumWidth)
{
    auto options = juce::PopupMenu::Options().withTargetComponent (&target).withMinimumWidth (minimumWidth);
    if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
        options = options.withParentComponent (editor);

    juce::Component::SafePointer<Display> safe (this);
    menu.showMenuAsync (options, [safe] (int result)
    {
        if (safe != nullptr && result != 0)
            safe->performMenuItem (result);
    });
}

void Display::performMenuItem (int id)
{
    auto& pm = controller.getPresets();

    switch (id)
    {
        case matchItem:      controller.toggle (params::match); break;
        case bpmItem:        promptBpm(); break;
        case mixLockItem:
            pm.setMixLocked (! pm.isMixLocked());
            controller.flash (pm.isMixLocked() ? "mix locked: presets keep it" : "mix unlocked");
            break;
        case seedLockItem:   controller.setSeedLocked (! controller.isSeedLocked()); break;
        case seedCopyItem:   controller.copySeed(); break;
        case seedPasteItem:  controller.pasteSeedFromClipboard(); break;
        case seedWordItem:   promptWord(); break;
        case seedRerollItem: controller.rerollSeed(); break;
        case abCopyItem:
        {
            auto& ab = controller.getAb();
            ab.copyActiveToOther();
            controller.flash (ab.getActiveSlot() == 0 ? "copied a to b" : "copied b to a");
            break;
        }
        case redoItem:       controller.getUndoManager().redo(); break;
        default:
            if (id >= scaleItem && id < scaleItem + (int) scales.size() && onScaleChosen != nullptr)
                onScaleChosen (scales[(size_t) (id - scaleItem)]);
            else if (id >= historyItem && id < historyItem + SeedState::historySize)
                controller.pickSeed (id - historyItem);
            break;
    }

    refreshHeader();
}

void Display::promptWord()
{
    myzel::ui::InlineDialog::Options o;
    o.title = "Seed from a word";
    o.message = "Any text becomes a seed, the same in every session and on every machine. A code like 3F2A-91C7 is used as it is.";
    o.withTextField = true;
    o.confirmLabel = "Use";

    juce::Component::SafePointer<Display> safe (this);
    o.onConfirm = [safe] (const juce::String& text) -> juce::String
    {
        if (safe == nullptr)
            return {};
        if (safe->controller.pasteSeed (text) == SeedState::Paste::nothing)
            return "Type a word or a seed code.";
        return {};
    };

    dialog.open (std::move (o));
}

void Display::promptBpm()
{
    myzel::ui::InlineDialog::Options o;
    o.title = "Tempo without a host";
    o.message = "Glitch's grid and synced Warble run at this tempo when the host gives none (40 to 240 BPM).";
    o.withTextField = true;
    o.initialText = juce::String (juce::roundToInt (controller.plain (params::bpm)));
    o.confirmLabel = "Set";

    juce::Component::SafePointer<Display> safe (this);
    o.onConfirm = [safe] (const juce::String& text) -> juce::String
    {
        if (safe == nullptr)
            return {};
        const auto bpm = text.retainCharacters ("0123456789.").getFloatValue();
        if (bpm < params::bpmMin || bpm > params::bpmMax)
            return "Choose a tempo from 40 to 240 BPM.";
        safe->controller.setPlain (params::bpm, bpm);
        return {};
    };

    dialog.open (std::move (o));
}

} // namespace inkcap::ui
