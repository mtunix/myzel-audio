#include "SelfTest.h"

#include "common/plugin/UiState.h"
#include "common/testing/UiTestKit.h"
#include "dsp/common/SeedCode.h"
#include "dsp/modules/glitch/GlitchPattern.h"
#include "plugin/PluginProcessor.h"
#include "ui/InkcapEditor.h"
#include "ui/UiState.h"

namespace
{
using namespace myzel::testing;
using namespace inkcap;
using ui::Display;

void frames (InkcapEditor& editor, int n)
{
    static double t = 0.0;
    for (int i = 0; i < n; ++i)
        editor.animateFrame (t += 1.0 / 60.0);
}

bool near (float a, float b, float tolerance = 0.002f) { return std::abs (a - b) <= tolerance; }

juce::String seedOf (InkcapProcessor& p) { return p.getSeeds().code(); }

/** A click: down and up at a point, as the mouse would. */
void click (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods = {})
{
    c.mouseDown (makeEvent (c, at, mods));
    c.mouseUp (makeEvent (c, at, mods));
}

/** A drag from a point by a distance, in steps. */
void dragBy (juce::Component& c, juce::Point<float> from, juce::Point<float> delta, juce::ModifierKeys mods = {})
{
    c.mouseDown (makeEvent (c, from, mods));
    for (int i = 1; i <= 4; ++i)
        c.mouseDrag (makeEvent (c, from + delta * ((float) i / 4.0f), mods, 1, true));
    c.mouseUp (makeEvent (c, from + delta, mods, 1, true));
}

/** Types into the open inline dialog's text field and presses Return. */
bool confirmDialog (myzel::ui::InlineDialog& dialog, const juce::String& text)
{
    if (! dialog.isOpen())
        return false;

    for (auto* child : dialog.getChildren())
        if (auto* field = dynamic_cast<juce::TextEditor*> (child))
            field->setText (text, false);

    dialog.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    return true;
}

struct Gestures
{
    Gestures (InkcapProcessor& p, const char* id) : parameter (p.getState().getParameter (id)) { parameter->addListener (&recorder); }
    ~Gestures() { parameter->removeListener (&recorder); }
    bool one() const { return recorder.begins == 1 && recorder.ends == 1; }

    juce::AudioProcessorParameter* parameter;
    GestureRecorder recorder;
};

int menuItemTicked (const juce::PopupMenu& menu, int id)
{
    for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
        if (it.getItem().itemID == id)
            return it.getItem().isTicked ? 1 : 0;
    return -1;
}

void checkScale (InkcapProcessor& p, InkcapEditor& editor)
{
    std::printf ("Window size\n");
    check (editor.getWidth() == ui::metrics::baseWidth && editor.getHeight() == ui::metrics::baseHeight, "opens at 928 x 568 (100 %)");

    editor.getDisplay().performMenuItem (Display::scaleItem + 3);
    check (editor.getWidth() == 1392 && editor.getHeight() == 852, "the settings menu's 150 % scales the face");
    check (near ((float) p.getUiState().getProperty (myzel::plugin::uistate::scale), 1.5f), "the scale is stored in the session");
    check (menuItemTicked (editor.getDisplay().settingsMenu(), Display::scaleItem + 3) == 1, "the menu ticks the size in use");

    editor.setScale (3.0f);
    check (near (editor.getScale(), 2.0f) && editor.getWidth() == 1856, "at most 200 %");
    editor.setScale (0.1f);
    check (near (editor.getScale(), 0.75f) && editor.getWidth() == 696 && editor.getHeight() == 426, "at least 75 %");

    editor.setScale (1.25f);
    {
        auto second = std::make_unique<InkcapEditor> (p);
        check (second->getWidth() == 1160 && second->getHeight() == 710, "a new editor opens at the session's scale");
    }
    editor.setScale (1.0f);
}

void checkPad (InkcapProcessor& p, InkcapEditor& editor)
{
    std::printf ("XY pad\n");
    auto& pad = editor.getDisplay().getPad();
    auto& c = editor.getController();
    setParam (p, params::damage, 50.0f);
    setParam (p, params::chaos, 25.0f);
    pad.update();
    c.getUndoManager().clearUndoHistory();

    {
        Gestures d (p, params::damage), ch (p, params::chaos);
        dragBy (pad, pad.puckCentre(), { 37.8f, -27.0f });   // a tenth of the travel each way
        check (near (valueOf (p, params::damage), 60.0f, 0.05f) && near (valueOf (p, params::chaos), 35.0f, 0.05f),
               "dragging the puck moves Damage right and Chaos up by the distance");
        check (d.one() && ch.one(), "a drag is one host gesture per parameter");
    }

    check (c.getUndoManager().canUndo(), "the drag is in the undo history");
    c.getUndoManager().undo();
    check (near (valueOf (p, params::damage), 50.0f) && near (valueOf (p, params::chaos), 25.0f), "one undo step takes back both");

    dragBy (pad, pad.puckCentre(), { 37.8f, 0.0f }, juce::ModifierKeys::shiftModifier);
    check (near (valueOf (p, params::damage), 52.0f, 0.05f), "Shift drags at a fifth of the speed");

    pad.mouseDoubleClick (makeEvent (pad, pad.puckCentre(), {}, 2));
    check (near (valueOf (p, params::damage), 0.0f) && near (valueOf (p, params::chaos), 0.0f), "double-click resets to 0 / 0");

    click (pad, pad.pointFor (0.8f, 0.3f));
    check (near (valueOf (p, params::damage), 80.0f, 0.1f) && near (valueOf (p, params::chaos), 30.0f, 0.1f),
           "a click away from the puck moves it there");

    dragBy (pad, pad.pointFor (0.8f, 0.3f), { 1000.0f, -1000.0f });
    check (near (valueOf (p, params::damage), 100.0f) && near (valueOf (p, params::chaos), 100.0f), "dragging past the edge stops at 100 %");

    setParam (p, params::damage, 50.0f);
    setParam (p, params::chaos, 50.0f);
    pad.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    pad.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    pad.keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0));
    check (near (valueOf (p, params::damage), 51.0f) && near (valueOf (p, params::chaos), 51.2f), "arrow keys move the puck (Shift: fine)");

    // Rings: tight at Chaos 0 or Damage 0, wide at both.
    auto ringWidth = [&] (float d, float ch)
    {
        setParam (p, params::damage, d);
        setParam (p, params::chaos, ch);
        pad.update();
        return pad.ringBounds (1).getWidth();
    };
    const auto calm = ringWidth (50.0f, 0.0f), wild = ringWidth (50.0f, 100.0f), clean = ringWidth (0.0f, 100.0f);
    check (wild > calm * 2.0f, "the contour rings widen with Chaos");
    check (near (clean, ringWidth (0.0f, 0.0f), 0.5f), "at Damage 0 the rings stay tight whatever Chaos says");

    // Accessibility: two sliders. Handlers exist only on the desktop, so this editor goes there.
    {
        auto onScreen = std::make_unique<InkcapEditor> (p);
        onScreen->addToDesktop (0);
        onScreen->setVisible (true);
        auto& shown = onScreen->getDisplay().getPad();

        auto* handler = shown.getAccessibilityHandler();
        check (handler != nullptr && handler->getRole() == juce::AccessibilityRole::group, "the pad is a group for screen readers");

        juce::StringArray sliders;
        juce::AccessibilityValueInterface* chaosValue = nullptr;
        for (auto* child : shown.getChildren())
            if (auto* h = child->getAccessibilityHandler(); h != nullptr && h->getRole() == juce::AccessibilityRole::slider)
            {
                sliders.add (child->getTitle());
                if (child->getTitle() == "Chaos")
                    chaosValue = h->getValueInterface();
            }
        check (sliders == juce::StringArray { "Damage", "Chaos" }, "with two sliders inside: Damage and Chaos");

        if (chaosValue != nullptr)
            chaosValue->setValue (0.42);
        check (near (valueOf (p, params::chaos), 42.0f), "a screen reader sets Chaos through its slider");

        onScreen->removeFromDesktop();
    }

    setParam (p, params::damage, 50.0f);
    setParam (p, params::chaos, 25.0f);
}

void checkSoftKeys (InkcapProcessor& p, InkcapEditor& editor)
{
    std::printf ("Soft keys\n");
    auto& d = editor.getDisplay();
    auto& c = editor.getController();

    d.getPresetsKey().onClick();
    frames (editor, 1);
    check (d.getPresetsPage().isVisible() && ! d.getPad().isVisible() && d.getPresetsKey().isLit(), "presets opens the browser over the display");
    d.getPresetsKey().onClick();
    frames (editor, 1);
    check (! d.getPresetsPage().isVisible() && d.getPad().isVisible(), "pressed again, it closes");

    // A/B through the a/b key.
    setParam (p, params::warbleAmount, 80.0f);
    d.getAbKey().onClick();
    check (c.getAb().getActiveSlot() == 1, "a/b switches to b");
    setParam (p, params::warbleAmount, 20.0f);
    d.getAbKey().onClick();
    check (c.getAb().getActiveSlot() == 0 && near (valueOf (p, params::warbleAmount), 80.0f), "and back to a, with a's sound");
    d.getAbKey().onClick();
    check (near (valueOf (p, params::warbleAmount), 20.0f), "b keeps its own sound");
    d.getAbKey().onSecondaryClick();
    d.getAbKey().onClick();
    check (near (valueOf (p, params::warbleAmount), 20.0f), "right-click copies the slot in use to the other");
    d.getAbKey().onClick();

    // Undo through the undo key.
    c.getUndoManager().clearUndoHistory();
    frames (editor, 1);
    check (! d.getUndoKey().isEnabled(), "undo is dimmed with nothing to undo");
    c.setPlain (params::crushAmount, 90.0f);
    frames (editor, 1);
    check (d.getUndoKey().isEnabled(), "an edit enables it");
    d.getUndoKey().onClick();
    check (near (valueOf (p, params::crushAmount), 50.0f), "undo takes the edit back");
    d.getUndoKey().onSecondaryClick();
    check (near (valueOf (p, params::crushAmount), 90.0f), "right-click on undo redoes");

    // Bypass: a listening switch, never an undo step.
    c.getUndoManager().clearUndoHistory();
    d.getBypassKey().onClick();
    frames (editor, 1);
    check (valueOf (p, params::bypass) > 0.5f && d.getBypassKey().isLit(), "bypass switches Bypass and lights in caution");
    check (! c.getUndoManager().canUndo(), "bypass is not an undo step");
    d.getBypassKey().onClick();

    // Settings: the mix lock.
    std::printf ("Settings menu: the mix lock\n");
    auto& pm = c.getPresets();
    d.performMenuItem (Display::mixLockItem);
    frames (editor, 1);
    check (pm.isMixLocked() && editor.getStrip().getMix().isLocked(), "Lock Mix locks it, and the padlock shows at MIX");
    check (menuItemTicked (d.settingsMenu(), Display::mixLockItem) == 1, "the menu ticks it");
    setParam (p, params::mix, 40.0f);
    pm.load (0);
    check (near (valueOf (p, params::mix), 40.0f), "a preset load keeps Mix while locked");
    d.performMenuItem (Display::mixLockItem);
    pm.load (0);
    check (! pm.isMixLocked() && near (valueOf (p, params::mix), 100.0f), "unlocked, the preset sets Mix again");

    const auto matched = valueOf (p, params::match) > 0.5f;
    d.performMenuItem (Display::matchItem);
    check ((valueOf (p, params::match) > 0.5f) != matched, "Match loudness switches level matching");
    d.performMenuItem (Display::matchItem);

    d.performMenuItem (Display::bpmItem);
    check (confirmDialog (editor.getDialog(), "96") && near (valueOf (p, params::bpm), 96.0f), "the tempo without a host is typed in the dialog");
    d.performMenuItem (Display::bpmItem);
    confirmDialog (editor.getDialog(), "500");
    check (editor.getDialog().isOpen() && near (valueOf (p, params::bpm), 96.0f), "a tempo out of range keeps the dialog open");
    editor.getDialog().close();
}

void checkSeeds (InkcapProcessor& p, InkcapEditor& editor)
{
    std::printf ("Seeds\n");
    auto& d = editor.getDisplay();
    auto& c = editor.getController();
    auto& um = c.getUndoManager();

    d.performMenuItem (Display::seedCopyItem);
    check (juce::SystemClipboard::getTextFromClipboard() == seedOf (p), "Copy puts the code on the clipboard");

    juce::SystemClipboard::copyTextToClipboard ("3f2a91c7");
    d.performMenuItem (Display::seedPasteItem);
    check (seedOf (p) == "3F2A-91C7" && p.getSeeds().label().isEmpty(), "Paste takes a code as that seed");

    c.pasteSeed ("thrift");
    frames (editor, 1);
    check (seedOf (p) == "8719-8E02" && p.getSeeds().label() == "thrift", "a word hashes to its seed and becomes the label");
    check (d.getStatusLine().getSeedText().contains ("\"thrift\" 8719-8E02"), "the seed line shows the word and the code");

    c.pasteSeed ("   ");
    check (seedOf (p) == "8719-8E02" && c.getFlash().contains ("nothing"), "empty text changes nothing, and says so");

    um.undo();
    check (seedOf (p) == "3F2A-91C7", "undo takes a seed change back");
    um.redo();
    check (seedOf (p) == "8719-8E02", "and redo brings it again");

    juce::SystemClipboard::copyTextToClipboard ("0x00000001");
    d.keyPressed (juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0));
    check (seedOf (p) == "0000-0001", "Ctrl/Cmd+V on the display pastes");
    d.keyPressed (juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0));
    check (juce::SystemClipboard::getTextFromClipboard() == "0000-0001", "Ctrl/Cmd+C copies");

    d.performMenuItem (Display::seedWordItem);
    check (confirmDialog (editor.getDialog(), "mushroom")
               && seedOf (p) == juce::String (dsp::seedcode::format (dsp::seedcode::wordHash ("mushroom"))),
           "Type a word... seeds from the word");

    const auto before = seedOf (p);
    editor.getStrip().getReroll().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    check (seedOf (p) != before, "the Reroll pill draws a new seed");

    const auto history = p.getSeeds().history();
    check (history.size() >= 2, "the history keeps the seeds rerolls replaced");
    if (history.size() >= 2)
    {
        d.performMenuItem (Display::historyItem + 1);
        check (p.getSeeds().current() == history[1].seed, "a pick from the history brings that seed back");
    }

    // Lock: presets keep the seed.
    editor.getStrip().getLock().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    frames (editor, 1);
    const auto locked = seedOf (p);
    check (p.isSeedLocked() && editor.getStrip().getLock().isLit(), "the Lock pill locks the seed and lights its LED");
    c.getPresets().load (0);
    check (seedOf (p) == locked, "a preset load keeps a locked seed");
    d.performMenuItem (Display::seedLockItem);
    c.getPresets().load (0);
    check (! p.isSeedLocked() && seedOf (p) == "4393-0A3D", "unlocked, the preset brings its own");
    check (menuItemTicked (d.seedMenu(), Display::seedLockItem) == 0, "the seed menu shows the lock off");
}

void checkModules (InkcapProcessor& p, InkcapEditor& editor)
{
    std::printf ("Module rows, pages and keys\n");
    auto& d = editor.getDisplay();
    auto& c = editor.getController();
    resetParams (p);
    frames (editor, 1);

    auto& row = d.getModuleList().getRow (Module::crush);
    click (row, row.dotArea().getCentre());
    check (valueOf (p, params::crushOn) < 0.5f, "a click on a row's dot switches the module");
    click (row, row.dotArea().getCentre());

    {
        Gestures g (p, params::crushAmount);
        dragBy (row, row.barArea().getCentre(), { row.barArea().getWidth() * 0.2f, 0.0f });
        check (near (valueOf (p, params::crushAmount), 70.0f, 0.2f) && g.one(), "dragging a row sets its Amount, one gesture");
    }
    row.openAmountEntry();
    row.getEntry().commitForTest ("33");
    check (near (valueOf (p, params::crushAmount), 33.0f), "the Amount takes a typed value");
    check (d.getModuleList().isVisible(), "neither opens the page");

    click (row, row.barArea().getCentre().withX (60.0f));
    frames (editor, 1);
    auto& page = d.getModulePage();
    check (page.isVisible() && page.getModule() == Module::crush && ! d.getModuleList().isVisible(), "a click on the row opens its page");
    check ((int) p.getUiState().getProperty (ui::uistate::modulePage) == (int) Module::crush, "the page is stored in the session");

    page.getTab ((int) CrushType::valve)->onClick();
    check ((int) valueOf (p, params::crushType) == (int) CrushType::valve, "a type tab sets the type");
    frames (editor, 1);
    check (page.getTab ((int) CrushType::valve)->isLit() && ! page.getTab (0)->isLit(), "the tab in use is lit");

    setParam (p, params::damage, 100.0f);
    frames (editor, 1);
    check (page.getCaption() == "damage 100 brings it to 33 %", "the caption says what Damage makes of the module");
    setParam (p, params::damage, 0.0f);
    frames (editor, 1);
    check (page.getCaption().startsWith ("damage 0"), "at Damage 0 it says the module does nothing yet");
    setParam (p, params::damage, 50.0f);

    auto* tone = page.findRow (params::crushTone);
    check (tone != nullptr && tone->getKind() == ui::ValueRow::Kind::bipolar, "Tone is a bipolar row");
    if (tone != nullptr)
    {
        tone->openTextEntry();
        tone->getEntry().commitForTest ("dark 40");
        check (near (valueOf (p, params::crushTone), -40.0f) && tone->getValueText() == "dark 40", "typed \"dark 40\" reads back as dark 40");
        tone->openTextEntry();
        tone->getEntry().commitForTest ("loud");
        check (near (valueOf (p, params::crushTone), -40.0f), "text that does not parse changes nothing");
        tone->mouseDown (makeEvent (*tone, tone->barArea().getCentre(), juce::ModifierKeys::altModifier));
        tone->mouseUp (makeEvent (*tone, tone->barArea().getCentre(), juce::ModifierKeys::altModifier));
        check (near (valueOf (p, params::crushTone), 0.0f), "Alt-click returns a row to its default");
    }

    page.getOnKey().onClick();
    check (valueOf (p, params::crushOn) < 0.5f, "the page's on key switches the module");
    page.getOnKey().onClick();

    page.getBackKey().onClick();
    frames (editor, 1);
    check (! page.isVisible() && d.getModuleList().isVisible(), "back returns to the module list");

    // Glitch's page: rows, the rate's steps, and the events beside the pad.
    d.openModulePage ((int) Module::glitch);
    frames (editor, 1);
    check (! d.getWear().isVisible() && page.getEvents().isVisible(), "Glitch's page shows its events where the wear matrix was");

    if (auto* rate = page.findRow (params::glitchRate))
    {
        rate->openTextEntry();
        rate->getEntry().commitForTest ("1/32");
        check ((int) valueOf (p, params::glitchRate) == 10, "Rate takes a note value");
        Gestures g (p, params::glitchRate);
        dragBy (*rate, rate->barArea().getCentre(), { -rate->barArea().getWidth() / 11.0f * 3.0f, 0.0f });
        check ((int) valueOf (p, params::glitchRate) == 7 && g.one(), "dragging Rate moves in whole steps");
    }

    if (auto* length = page.findRow (params::glitchLength))
    {
        Gestures g (p, params::glitchLength);
        dragBy (*length, length->barArea().getCentre(), { length->barArea().getWidth() * 0.25f, 0.0f });
        check (near (valueOf (p, params::glitchLength), 75.0f, 0.5f) && g.one(), "dragging a row moves its value by the distance, one gesture");
        length->keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
        check (near (valueOf (p, params::glitchLength), 74.0f, 0.5f), "arrow keys step a row");
    }

    // The events are glitch::eventAt's for the window shown.
    setParam (p, params::glitchOn, 1.0f);
    setParam (p, params::glitchAmount, 100.0f);
    setParam (p, params::damage, 100.0f);
    setParam (p, params::chaos, 0.0f);
    frames (editor, 1);
    auto& events = page.getEvents();
    const auto s = p.readSettings();
    const auto pattern = dsp::glitch::patternFor (s.glitch, s.damage, s.chaos);
    int expected = 0;
    for (auto k = pattern.grid.firstSlotFrom (events.getWindowStart()); pattern.grid.slotStart (k) < events.getWindowStart() + events.getWindowBeats(); ++k)
        if (dsp::glitch::eventAt (p.getSeeds().current(), pattern, k))
            ++expected;
    int shownInWindow = 0;
    for (const auto& e : events.getEvents())
        if (e.startBeat >= events.getWindowStart())
            ++shownInWindow;
    check (expected > 0 && shownInWindow == expected, "the event view shows exactly the events eventAt gives for its bars");

    setParam (p, params::damage, 0.0f);
    frames (editor, 1);
    check (events.getEvents().empty(), "at Damage 0 nothing fails");

    d.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    frames (editor, 1);
    check (! page.isVisible(), "Escape closes a page");

    // Warble's Sync is a switch row.
    d.openModulePage ((int) Module::warble);
    if (auto* sync = page.findRow (params::warbleSync))
    {
        click (*sync, sync->getLocalBounds().toFloat().getCentre());
        frames (editor, 1);
        check (valueOf (p, params::warbleSync) > 0.5f, "a click on Sync switches it");
        check (page.findRow (params::warbleRate)->getValueText().contains ("/"), "Rate then reads as a note value");
    }
    d.openModulePage (-1);

    // The strip.
    auto& strip = editor.getStrip();
    c.getUndoManager().clearUndoHistory();
    strip.getKey (Module::bits).triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    check (valueOf (p, params::bitsOn) > 0.5f, "a module key switches its module");
    c.getUndoManager().undo();
    check (valueOf (p, params::bitsOn) < 0.5f, "one undo step");

    auto& mix = strip.getMix();
    {
        Gestures g (p, params::mix);
        setParam (p, params::mix, 50.0f);
        dragBy (mix, mix.knobArea().getCentre(), { 0.0f, -40.0f });
        check (near (valueOf (p, params::mix), 70.0f, 0.2f) && g.one(), "dragging MIX up 40 px adds a fifth, one gesture");
    }
    mix.mouseEnter (makeEvent (mix, mix.knobArea().getCentre(), {}));
    check (c.getHint().startsWith ("mix 70"), "MIX lends its read-out to the status line");
    mix.mouseExit (makeEvent (mix, mix.knobArea().getCentre(), {}));
    mix.mouseDoubleClick (makeEvent (mix, mix.knobArea().getCentre(), {}, 2));
    check (near (valueOf (p, params::mix), 100.0f), "double-click returns MIX to its default");

    auto& out = strip.getOutput();
    out.openTextEntry();
    out.getEntry().commitForTest ("-6");
    check (near (valueOf (p, params::output), -6.0f), "OUT takes a typed value in dB");

    // The wear matrix lights with Damage.
    auto& wear = d.getWear();
    for (const auto& [damage, lit] : { std::pair { 0.0f, 0 }, { 50.0f, 48 }, { 100.0f, 96 } })
    {
        setParam (p, params::damage, damage);
        wear.update();
        check (wear.litDots() == lit, (juce::String ("wear at Damage ") + juce::String (damage, 0) + ": " + juce::String (lit) + " dots lit").toRawUTF8());
    }
    resetParams (p);
}

void checkPresets (InkcapProcessor& p, InkcapEditor& editor, const ScratchSession& scratch)
{
    std::printf ("Presets page\n");
    auto& d = editor.getDisplay();
    auto& page = d.getPresetsPage();
    auto& pm = editor.getController().getPresets();
    auto& dialog = editor.getDialog();

    check (pm.getUserPresetDirectory() == scratch.getPresetDirectory(), "user presets live in the scratch folder");
    d.setPresetsOpen (true);
    pm.load (0);
    frames (editor, 1);

    setParam (p, params::roomOn, 1.0f);
    p.getSeeds().paste ("selftest");
    page.getSaveAsKey().onClick();
    check (confirmDialog (dialog, "Selftest A") && ! dialog.isOpen(), "save as asks for a name and saves");
    frames (editor, 1);
    check (scratch.getPresetDirectory().getChildFile ("Selftest A.xml").existsAsFile(), "the file lands in the user folder");
    check (pm.isCurrentUserPreset() && pm.getCurrentName() == "Selftest A", "and becomes the current preset");

    const auto xml = juce::parseXML (scratch.getPresetDirectory().getChildFile ("Selftest A.xml"));
    check (xml != nullptr && xml->getStringAttribute ("seed") == seedOf (p), "the preset keeps the seed");

    page.setCategory ("User");
    check (page.getVisible().size() == 1, "the user category lists it alone");
    page.setCategory ({});
    page.setSearch ("selftest");
    check (page.getVisible().size() == 1, "the search finds it by name");
    page.setSearch ({});

    setParam (p, params::roomAmount, 90.0f);
    frames (editor, 1);
    check (page.getSaveKey().isEnabled(), "save is offered for a changed user preset");
    page.getSaveKey().onClick();
    check (! pm.isModified(), "save writes the changes");

    page.getRenameKey().onClick();
    check (confirmDialog (dialog, "Selftest B") && scratch.getPresetDirectory().getChildFile ("Selftest B.xml").existsAsFile()
               && ! scratch.getPresetDirectory().getChildFile ("Selftest A.xml").existsAsFile(),
           "rename renames the file");

    // A click on a row loads it and keeps the page open (the list paints its rows: find the row's component).
    // The list follows the current preset to the end of the factory set: back to the top for Init's row.
    pm.load ((int) pm.getEntries().size() - 1);
    frames (editor, 1);
    page.getList().scrollToEnsureRowIsOnscreen (0);
    frames (editor, 1);
    auto* content = page.getList().getViewport()->getViewedComponent();
    auto* initRow = content != nullptr ? content->getComponentAt (juce::Point<int> (20, (int) ui::PresetsPage::rowHeight / 2)) : nullptr;
    check (initRow != nullptr && initRow != content, "the list shows its rows");
    if (initRow != nullptr && initRow != content)
    {
        click (*initRow, initRow->getLocalBounds().toFloat().getCentre());
        check (pm.getCurrentIndex() == 0 && page.isVisible(), "a click on a row loads it, the page stays");
    }

    pm.load ((int) pm.getEntries().size() - 1);
    page.getDeleteKey().onClick();
    check (confirmDialog (dialog, {}) && ! scratch.getPresetDirectory().getChildFile ("Selftest B.xml").existsAsFile(), "delete removes the file");

    d.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    frames (editor, 1);
    check (! page.isVisible(), "Escape closes the browser");
}

void checkSession (InkcapProcessor& p, InkcapEditor& editor)
{
    std::printf ("Session\n");
    auto& d = editor.getDisplay();
    d.openModulePage ((int) Module::room);

    juce::MemoryBlock withRoom;
    p.getStateInformation (withRoom);
    d.openModulePage (-1);
    frames (editor, 1);

    p.setStateInformation (withRoom.getData(), (int) withRoom.getSize());
    frames (editor, 1);
    check (d.getModulePage().isVisible() && d.getModulePage().getModule() == Module::room, "a restored session opens its page");

    auto second = std::make_unique<InkcapEditor> (p);
    check (second->getDisplay().getModulePage().isVisible(), "so does a new editor on it");
    second.reset();
    d.openModulePage (-1);
}

} // namespace

int runUiSelfTest (const ScratchSession& scratch)
{
    failures = 0;

    // On the heap: the chain's buffers are too large for the stack.
    auto processor = std::make_unique<InkcapProcessor>();
    auto& p = *processor;
    auto editor = std::make_unique<InkcapEditor> (p);
    editor->setVisible (true);
    frames (*editor, 2);

    checkScale (p, *editor);
    checkPad (p, *editor);
    checkSoftKeys (p, *editor);
    checkSeeds (p, *editor);
    checkModules (p, *editor);
    checkPresets (p, *editor, scratch);
    checkSession (p, *editor);

    editor.reset();
    std::printf ("%s: %d failure%s\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return failures;
}
