#include "SelfTest.h"

#include "ui/Theme.h"

#include "plugin/PluginProcessor.h"
#include "ui/CeruleanEditor.h"
#include "ui/UiState.h"

namespace
{
using namespace cerulean;

int failures = 0;

void check (bool condition, const char* what)
{
    std::printf ("  [%s] %s\n", condition ? " ok " : "FAIL", what);

    if (! condition)
        ++failures;
}

juce::MouseEvent makeEvent (juce::Component& c, juce::Point<float> pos, juce::ModifierKeys mods, int clicks = 1,
                            bool dragged = false)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &c, &c, now, pos, now, clicks, dragged };
}

float valueOf (CeruleanProcessor& p, const char* id)
{
    auto* param = p.getState().getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

void setValue (CeruleanProcessor& p, const char* id, float plain)
{
    auto* param = p.getState().getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

/** Records host gestures, to check that every edit is wrapped in begin/end. */
struct GestureRecorder final : juce::AudioProcessorParameter::Listener
{
    int begins = 0, ends = 0, changes = 0;
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }
};

void frames (CeruleanEditor& editor, int n)
{
    static double t = 0.0;

    for (int i = 0; i < n; ++i)
        editor.animateFrame (t += 1.0 / 60.0);
}

void dragSlider (ui::StudioSlider& s, juce::Point<float> delta, juce::ModifierKeys mods = {})
{
    const auto c = s.barArea().getCentre();
    s.mouseDown (makeEvent (s, c, {}));
    s.mouseDrag (makeEvent (s, c + delta, mods, 1, true));
    s.mouseUp (makeEvent (s, c + delta, {}, 1, true));
}

void drag (ui::Knob& k, float dy, juce::ModifierKeys mods = {})
{
    const auto c = k.getLocalBounds().toFloat().getCentre();
    k.mouseDown (makeEvent (k, c, {}));
    k.mouseDrag (makeEvent (k, c.translated (0.0f, dy), mods, 1, true));
    k.mouseUp (makeEvent (k, c.translated (0.0f, dy), {}, 1, true));
}
}

int runUiSelfTest()
{
    failures = 0;

    const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cerulean-ui-selftest");
    scratch.deleteRecursively();
    ui::PresetManager::setUserPresetDirectoryOverride (scratch);
    scratch.createDirectory();
    ui::viewPreference::setSettingsFileOverride (scratch.getChildFile ("Cerulean.settings"));

    CeruleanProcessor processor;
    auto editor = std::make_unique<CeruleanEditor> (processor);
    editor->setVisible (true);
    auto& panel = editor->getMainPanel();

    auto* decay = panel.findKnob (params::decay);
    auto* tone = panel.findKnob (params::tone);
    check (decay != nullptr && tone != nullptr, "knobs exist");

    GestureRecorder rec;
    processor.getState().getParameter (params::decay)->addListener (&rec);

    std::printf ("Knob drag\n");
    {
        auto* p = processor.getState().getParameter (params::decay);
        const auto before = p->getValue();
        drag (*decay, -64.0f);
        const auto after = p->getValue();
        check (after > before + 0.17f && after < before + 0.23f, "64 px up moves Decay by ~0.2 (320 px = full range)");
        check (rec.begins == 1 && rec.ends == 1, "one host gesture for the drag");

        const auto mid = p->getValue();
        drag (*decay, -64.0f, juce::ModifierKeys::shiftModifier);
        const auto fine = p->getValue() - mid;
        check (fine > 0.01f && fine < 0.04f, "Shift-drag is ~8x finer");

        decay->mouseDoubleClick (makeEvent (*decay, decay->getLocalBounds().toFloat().getCentre(), {}, 2));
        check (std::abs (valueOf (processor, params::decay) - 3.0f) < 0.01f, "double-click resets to default (3 s)");

        const auto beforeWheel = valueOf (processor, params::decay);
        juce::MouseWheelDetails wheel { 0.0f, 0.1f, false, false, false };
        decay->mouseWheelMove (makeEvent (*decay, decay->getLocalBounds().toFloat().getCentre(), {}), wheel);
        check (valueOf (processor, params::decay) > beforeWheel, "wheel up raises the value");
    }

    std::printf ("Tone centre detent\n");
    {
        setValue (processor, params::tone, -1.0f);
        drag (*tone, -15.0f);
        check (std::abs (valueOf (processor, params::tone)) < 0.001f, "dragging through centre sticks at Flat");
        check (processor.getState().getParameter (params::tone)->getCurrentValueAsText() == "Flat", "Tone shows 'Flat' at 12 o'clock");
    }

    std::printf ("Accessibility\n");
    {
        auto owned = decay->createAccessibilityHandler();
        auto* handler = owned.get();
        check (handler != nullptr && handler->getRole() == juce::AccessibilityRole::slider, "knob is an accessible slider");

        if (handler != nullptr && handler->getValueInterface() != nullptr)
        {
            handler->getValueInterface()->setValueAsString ("5");
            check (std::abs (valueOf (processor, params::decay) - 5.0f) < 0.01f, "screen reader can type a value");
            check (handler->getValueInterface()->getCurrentValueAsString() == "5.00 s", "value text uses the parameter's formatting");
        }
    }

    std::printf ("Algorithm keys and Param A/B legends\n");
    {
        panel.getAlgorithmKey (5).onClick();
        frames (*editor, 2);
        check (juce::roundToInt (valueOf (processor, params::algorithm)) == 5, "clicking HALO selects Halo");
        check (panel.getAlgorithmKey (5).getToggleState() && ! panel.getAlgorithmKey (1).getToggleState(), "only the selected key is lit");

        auto* a = panel.findKnob (params::paramA);
        check (a != nullptr && a->getStyle().title == "INTERVAL", "Param A relabels to INTERVAL");
        setValue (processor, params::paramA, 75.0f);
        check (a != nullptr && a->getValueText() == "+12", "Param A reads out the interval");
    }

    std::printf ("Echo sync binds Param A to the echo note\n");
    {
        setValue (processor, params::algorithm, (float) (int) Algorithm::echo);
        setValue (processor, params::echoSync, 1.0f);
        frames (*editor, 2);
        auto* note = panel.findKnob (params::echoNote);
        check (note != nullptr && note->getStyle().steps == numNoteValues, "the Param A knob now controls the echo note");

        const auto before = juce::roundToInt (valueOf (processor, params::echoNote));

        if (note != nullptr)
        {
            note->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
            check (juce::roundToInt (valueOf (processor, params::echoNote)) == before + 1, "arrow key steps one note value");
        }

        setValue (processor, params::echoSync, 0.0f);
        frames (*editor, 2);
        check (panel.findKnob (params::paramA) != nullptr && panel.findKnob (params::paramA)->getStyle().title == "ECHO TIME",
               "Echo Sync off: Param A is Echo time again");
    }

    std::printf ("Pre-delay sync\n");
    {
        setValue (processor, params::preDelaySync, 1.0f);
        frames (*editor, 2);
        auto* k = panel.findKnob (params::preDelayNote);
        check (k != nullptr && k->getStyle().title == "PRE-DELAY", "the Pre-delay knob controls the note value when synced");
        check (k != nullptr && k->getValueText().contains ("ms"), "the synced read-out shows the note and its length");
        setValue (processor, params::preDelaySync, 0.0f);
        frames (*editor, 2);
        check (panel.findKnob (params::preDelay) != nullptr && panel.findKnob (params::preDelay)->getStyle().title == "PRE-DELAY",
               "Sync off: back to milliseconds");
    }

    std::printf ("Freeze\n");
    {
        auto& key = panel.getFreezeKey();
        setValue (processor, params::freezeMode, 0.0f);
        frames (*editor, 1);
        key.onClick();
        check (valueOf (processor, params::freeze) > 0.5f, "latching: a click freezes");
        key.onClick();
        check (valueOf (processor, params::freeze) < 0.5f, "latching: a second click releases");

        setValue (processor, params::freezeMode, 1.0f);
        frames (*editor, 1);
        check (key.isMomentary(), "Freeze Mode Momentary makes the key momentary");
        const auto c = key.getLocalBounds().toFloat().getCentre();
        static_cast<juce::Component&> (key).mouseDown (makeEvent (key, c, juce::ModifierKeys::leftButtonModifier));
        check (valueOf (processor, params::freeze) > 0.5f, "momentary: frozen while held");
        static_cast<juce::Component&> (key).mouseUp (makeEvent (key, c, {}));
        check (valueOf (processor, params::freeze) < 0.5f, "momentary: released on mouse up");
        setValue (processor, params::freezeMode, 0.0f);
        frames (*editor, 1);
    }

    std::printf ("Advanced drawer\n");
    {
        const auto closedHeight = editor->getHeight();
        panel.setAdvancedOpen (true);
        frames (*editor, 2);
        check (panel.getDrawer().isVisible() && panel.isAdvancedOpen(), "ADVANCED opens the deck");
        check (editor->getHeight() == juce::roundToInt ((ui::metrics::baseHeight + ui::layout::deckHeight) * editor->getScale()),
               "the window grows by the deck");
        check (panel.getDrawer().getY() >= ui::metrics::baseHeight, "the deck sits below the panel (nothing is covered)");
        check (panel.getAdvancedTab().getY() > ui::metrics::baseHeight, "the ADVANCED tab moves to the deck's bottom edge");
        check ((bool) processor.getUiState().getProperty (ui::uistate::advancedOpen), "drawer state persisted");
        check (panel.findKnob (params::lowEnd) != nullptr && panel.findKnob (params::duckRelease) != nullptr, "drawer knobs exist");
        check (panel.getDrawer().findKey (params::trails) != nullptr, "Trails key exists");
        setValue (processor, params::width, 150.0f);
        frames (*editor, 1);
        check (panel.getDrawer().getValueTextFor (params::width) == "150 %", "drawer values follow the parameters");

        processor.getMeters().hostHasTempo.store (true);
        processor.getMeters().bpm.store (97.0);
        frames (*editor, 1);
        check (! panel.findKnob (params::bpm)->isEnabled() && panel.getDrawer().getValueTextFor (params::bpm).startsWith ("Host"),
               "BPM is greyed out while the host gives the tempo");
        processor.getMeters().hostHasTempo.store (false);
        frames (*editor, 1);
        check (panel.findKnob (params::bpm)->isEnabled(), "BPM editable again without host tempo");

        panel.setAdvancedOpen (false);
        frames (*editor, 2);
        check (! panel.getDrawer().isVisible() && editor->getHeight() == closedHeight, "closing shrinks the window back");

        panel.getAdvancedTab().onClick();
        frames (*editor, 2);
        check (panel.isAdvancedOpen(), "the ADVANCED tab opens it");
        panel.getAdvancedTab().onClick();
        frames (*editor, 2);
        check (! panel.isAdvancedOpen() && panel.getAdvancedTab().getY() < ui::metrics::baseHeight, "and closes it");
    }

    std::printf ("Sync keys on the panel\n");
    {
        auto& preSync = panel.getSyncKey (false);
        check (preSync.isVisible(), "Pre-delay has its Sync key");
        preSync.setToggleState (true, juce::sendNotificationSync);
        frames (*editor, 1);
        check (valueOf (processor, params::preDelaySync) > 0.5f, "it turns Pre-delay Sync on");
        check (panel.findKnob (params::preDelayNote) != nullptr, "and Pre-delay picks the note");
        preSync.setToggleState (false, juce::sendNotificationSync);
        frames (*editor, 1);

        setValue (processor, params::algorithm, (float) (int) Algorithm::hall);
        frames (*editor, 1);
        check (! panel.getSyncKey (true).isVisible(), "Echo's Sync key is hidden unless Echo is selected");
        setValue (processor, params::algorithm, (float) (int) Algorithm::echo);
        frames (*editor, 1);
        check (panel.getSyncKey (true).isVisible(), "and shown with Echo");
        setValue (processor, params::algorithm, (float) (int) Algorithm::hall);
        frames (*editor, 1);
    }

    std::printf ("Bypass, value read-out, save\n");
    {
        setValue (processor, params::bypass, 1.0f);
        frames (*editor, 1);
        check (panel.getTail().getShape().bypassed && panel.getHeader().getBypassButton().isLit(), "Bypass shows on the tail display");
        setValue (processor, params::bypass, 0.0f);
        frames (*editor, 1);
        check (! panel.getTail().getShape().bypassed, "and goes away");

        auto* toneKnob = panel.findKnob (params::tone);
        check (toneKnob->getValueAnchor().y < toneKnob->getBounds().getCentreY(), "the value sits above the knob (legend stays)");

        auto& presets = panel.getPresets();
        presets.load (0);
        panel.getHeader().refresh();
        check (! panel.getHeader().getSaveButton().isVisible(), "no SAVE without changes");
        setValue (processor, params::size, 77.0f);
        panel.getHeader().refresh();
        check (presets.isModified() && panel.getHeader().getSaveButton().isVisible(), "SAVE appears with unsaved changes");
        presets.load (0);
        panel.getHeader().refresh();
    }

    std::printf ("Undo\n");
    {
        auto& um = processor.getUndoManager();
        um.clearUndoHistory();

        const auto before = valueOf (processor, params::size);
        panel.findKnob (params::size)->setNormalisedValueAsGesture (0.9f);
        check (um.canUndo(), "edit is undoable");
        um.undo();
        check (std::abs (valueOf (processor, params::size) - before) < 0.05f, "undo restores the value");

        um.clearUndoHistory();
        panel.getFreezeKey().onClick();
        panel.getFreezeKey().onClick();
        check (! um.canUndo(), "Freeze is not recorded");
    }

    std::printf ("Presets\n");
    {
        auto& presets = panel.getPresets();
        check (presets.getEntries().size() >= 10, "at least 10 factory presets");

        const auto& entries = presets.getEntries();
        int haloIndex = -1;

        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == "Octave Halo")
                haloIndex = (int) i;

        const auto generation = processor.readSettings().presetGeneration;
        presets.load (haloIndex);
        check (juce::roundToInt (valueOf (processor, params::algorithm)) == (int) Algorithm::halo, "loading a preset sets the parameters");
        check (processor.readSettings().presetGeneration != generation, "loading a preset starts a fresh engine (trails)");
        check (! presets.isModified(), "freshly loaded preset is not modified");
        panel.findKnob (params::decay)->setNormalisedValueAsGesture (0.2f);
        check (presets.isModified(), "editing marks the preset modified");

        const auto saved = presets.saveUserPreset ("Selftest Preset");
        check (saved.wasOk() && presets.isCurrentUserPreset(), "save user preset");
        check (scratch.getChildFile ("Selftest Preset.xml").existsAsFile(), "user preset written as XML");
        const auto idx = presets.getCurrentIndex();
        check (presets.renameUserPreset (idx, "Selftest Renamed").wasOk() && presets.getCurrentName() == "Selftest Renamed",
               "rename user preset");
        check (presets.deleteUserPreset (presets.getCurrentIndex()).wasOk(), "delete user preset");
        check (! presets.userPresetExists ("Selftest Renamed"), "file is gone");

        presets.load (0);
        presets.step (1);
        check (presets.getCurrentIndex() == 1, "next preset");
        presets.step (-2);
        check (presets.getCurrentIndex() == (int) presets.getEntries().size() - 1, "previous preset wraps around");
    }

    std::printf ("A/B compare\n");
    {
        auto& ab = panel.getAb();
        setValue (processor, params::decay, 2.0f);
        ab.selectSlot (1);    // B starts as a copy
        setValue (processor, params::decay, 9.0f);
        const auto generation = processor.readSettings().presetGeneration;
        ab.selectSlot (0);
        check (std::abs (valueOf (processor, params::decay) - 2.0f) < 0.01f, "A recalls its own setting");
        check (processor.readSettings().presetGeneration != generation, "switching A/B starts a fresh engine (trails)");
        ab.selectSlot (1);
        check (std::abs (valueOf (processor, params::decay) - 9.0f) < 0.01f, "B recalls its own setting");
        ab.copyActiveToOther();
        ab.selectSlot (0);
        check (std::abs (valueOf (processor, params::decay) - 9.0f) < 0.01f, "copy B to A");
    }

    std::printf ("Scale\n");
    {
        editor->setScale (1.5f);
        check (editor->getWidth() == juce::roundToInt (ui::metrics::baseWidth * 1.5f)
               && editor->getHeight() == juce::roundToInt (ui::metrics::baseHeight * 1.5f), "150 % size");
        check (std::abs ((float) processor.getUiState().getProperty (ui::uistate::scale) - 1.5f) < 0.001f, "scale persisted");
        editor->setScale (5.0f);
        check (editor->getWidth() == 2 * ui::metrics::baseWidth, "scale is clamped to 200 %");
    }

    std::printf ("Character text parsing\n");
    {
        auto textOf = [] (Algorithm a, bool b, const char* typed)
        {
            const auto pct = params::characterPercentFromText (a, b, typed);
            return pct < 0.0f ? juce::String ("?") : params::characterText (a, b, pct);
        };

        check (textOf (Algorithm::halo, false, "+7") == "+7", "Halo interval by name");
        check (textOf (Algorithm::halo, false, "12") == "+12", "Halo interval by number");
        check (textOf (Algorithm::spring, false, "2") == "2 springs", "Spring count by number");
        check (textOf (Algorithm::choir, false, "o") == "O", "Choir vowel by letter");
        check (textOf (Algorithm::choir, true, "3 voices") == "3.0 voices", "Choir voices");
        check (textOf (Algorithm::room, false, "Early 40") == "Early 40", "Room balance by its read-out");
        check (textOf (Algorithm::rise, false, "350 ms") == "350 ms", "Rise time in ms");
        check (textOf (Algorithm::echo, false, "1.2 s") == "1.20 s", "Echo time in s");
        check (textOf (Algorithm::hall, true, "40") == "40 %", "plain number is a percentage");
        check (textOf (Algorithm::hall, true, "loud") == "?", "nonsense is rejected");
    }

    std::printf ("Studio view\n");
    {
        editor->getController().getPresets().load (2);
        const auto presetName = editor->getController().getPresets().getCurrentName();
        const auto abSlot = editor->getController().getAb().getActiveSlot();
        const auto sizeBefore = editor->getWidth();
        const auto bypassBounds = editor->getMainPanel().getHeader().getBypassButton().getBounds();
        const auto abBounds = editor->getMainPanel().getHeader().getSettingsButton().getBounds();

        editor->setView (ui::ViewMode::studio);
        check (editor->getViewMode() == ui::ViewMode::studio, "switches to the studio view");
        check (processor.getUiState().getProperty (ui::uistate::view).toString() == "studio", "view stored in the session");
        check (ui::viewPreference::getDefault() == ui::ViewMode::studio, "the chosen view becomes the default for new instances");
        check (editor->getWidth() == sizeBefore, "switching does not resize the window");
        check (editor->getController().getPresets().getCurrentName() == presetName
               && editor->getController().getAb().getActiveSlot() == abSlot, "preset and A/B state survive the switch");

        auto& studio = editor->getStudioPanel();
        check (studio.getHeader().getBypassButton().getBounds() == bypassBounds
               && studio.getHeader().getSettingsButton().getBounds() == abBounds, "the header keys keep their places in both views");
        check (studio.getHeader().getViewButton (ui::ViewMode::studio).isLit(), "STUDIO is lit");
        frames (*editor, 2);

        // Every sound parameter (plus the performance ones) has a control.
        for (const auto* id : { params::decay, params::preDelay, params::size, params::diffusion, params::paramA, params::paramB,
                                params::mod, params::modRate, params::tone, params::lowEnd, params::inLowCut, params::inHighCut,
                                params::duckAmount, params::duckThreshold, params::duckRelease, params::mix, params::width,
                                params::output, params::bpm })
            if (studio.findSlider (id) == nullptr)
                check (false, (juce::String ("slider for ") + id).toRawUTF8());

        for (const auto* id : { params::duck, params::wetOnly, params::trails, params::preDelaySync, params::echoSync, params::freeze })
            if (studio.findToggle (id) == nullptr)
                check (false, (juce::String ("toggle for ") + id).toRawUTF8());

        check (true, "every parameter has a control (no hidden drawer)");

        auto& decaySlider = *studio.findSlider (params::decay);
        decaySlider.setNormalisedValueAsGesture (0.5f);
        {
            GestureRecorder r;
            processor.getState().getParameter (params::decay)->addListener (&r);
            dragSlider (decaySlider, { 60.0f, 0.0f });
            processor.getState().getParameter (params::decay)->removeListener (&r);
            check (std::abs (decaySlider.getNormalisedValue() - 0.7f) < 0.01f, "60 px right moves a slider by 0.2 (300 px = full range)");
            check (r.begins == 1 && r.ends == 1, "one host gesture for the drag");
        }
        dragSlider (decaySlider, { 0.0f, 30.0f });
        check (std::abs (decaySlider.getNormalisedValue() - 0.6f) < 0.01f, "dragging down lowers it");
        dragSlider (decaySlider, { 100.0f, 0.0f }, juce::ModifierKeys::shiftModifier);
        check (std::abs (decaySlider.getNormalisedValue() - 0.6333f) < 0.01f, "Shift-drag is 10x finer");

        decaySlider.mouseDoubleClick (makeEvent (decaySlider, { 4.0f, 4.0f }, {}, 2));
        check (std::abs (valueOf (processor, params::decay) - 3.0f) < 0.01f, "double-click resets to default");

        check (decaySlider.commitText ("4.2 s") && std::abs (valueOf (processor, params::decay) - 4.2f) < 0.01f, "type 4.2 s");
        check (decaySlider.commitText ("800 ms") && std::abs (valueOf (processor, params::decay) - 0.8f) < 0.01f, "type 800 ms");
        check (! decaySlider.commitText ("long"), "nonsense is rejected");
        check (std::abs (valueOf (processor, params::decay) - 0.8f) < 0.01f, "and leaves the value alone");
        check (decaySlider.getValueText() == "800 ms", "value text uses the parameter's formatting");

        auto& preDelay = *studio.findSlider (params::preDelay);
        check (preDelay.commitText ("0.2 s") && std::abs (valueOf (processor, params::preDelay) - 200.0f) < 0.5f, "pre-delay accepts seconds");
        check (studio.findSlider (params::duckAmount)->commitText ("-18") && std::abs (valueOf (processor, params::duckAmount) - 18.0f) < 0.01f,
               "duck amount reads -18 dB as 18 dB of ducking");
        check (studio.findSlider (params::output)->commitText ("+3 dB") && std::abs (valueOf (processor, params::output) - 3.0f) < 0.01f,
               "output reads +3 dB");

        {
            auto sliderHandler = decaySlider.createAccessibilityHandler();
            check (sliderHandler != nullptr && sliderHandler->getRole() == juce::AccessibilityRole::slider, "slider is an accessible slider");

            if (sliderHandler != nullptr && sliderHandler->getValueInterface() != nullptr)
            {
                sliderHandler->getValueInterface()->setValueAsString ("5 s");
                check (std::abs (valueOf (processor, params::decay) - 5.0f) < 0.01f, "screen reader can type a value");
            }
        }

        auto& um = processor.getUndoManager();
        um.clearUndoHistory();
        const auto sizeValue = valueOf (processor, params::size);
        studio.findSlider (params::size)->setNormalisedValueAsGesture (0.9f);
        check (um.canUndo(), "studio edits are undoable");
        um.undo();
        check (std::abs (valueOf (processor, params::size) - sizeValue) < 0.05f, "undo restores the value");

        // Algorithm list and the character controls.
        studio.getAlgorithmRow (5).onClick();
        frames (*editor, 1);
        check (juce::roundToInt (valueOf (processor, params::algorithm)) == 5, "clicking HALO selects Halo");
        check (studio.getAlgorithmRow (5).getToggleState() && ! studio.getAlgorithmRow (1).getToggleState(), "only the selected row is lit");
        studio.getAlgorithmRow (5).keyPressed (juce::KeyPress (juce::KeyPress::downKey));
        check (juce::roundToInt (valueOf (processor, params::algorithm)) == 6, "Down arrow selects the next algorithm");
        studio.getAlgorithmRow (6).keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        frames (*editor, 1);
        auto* a = studio.findSlider (params::paramA);
        check (a != nullptr && a->getLabel().contains ("INTERVAL"), "Param A relabels to INTERVAL");
        check (studio.getCharacterTitle().contains ("Halo"), "the Character card names the algorithm");
        check (a != nullptr && a->commitText ("+7") && a->getValueText() == "+7", "type an interval");

        setValue (processor, params::algorithm, (float) (int) Algorithm::echo);
        setValue (processor, params::echoSync, 1.0f);
        frames (*editor, 1);
        auto* note = studio.findSlider (params::echoNote);
        check (note != nullptr && note->getLabel().contains ("ECHO TIME"), "with Echo Sync, Param A edits the echo note");
        check (studio.findToggle (params::echoSync)->isVisible(), "Echo's SYNC key is shown with Echo");
        check (note != nullptr && note->commitText ("1/4") && juce::roundToInt (valueOf (processor, params::echoNote)) == (int) NoteValue::n1_4,
               "type a note value");
        setValue (processor, params::echoSync, 0.0f);
        setValue (processor, params::algorithm, (float) (int) Algorithm::hall);
        frames (*editor, 1);
        check (studio.findSlider (params::paramA) != nullptr && ! studio.findToggle (params::echoSync)->isVisible(),
               "back to Param A, SYNC hidden");

        setValue (processor, params::preDelaySync, 1.0f);
        frames (*editor, 1);
        auto* preNote = studio.findSlider (params::preDelayNote);
        check (preNote != nullptr && ! preNote->getValueText().contains ("ms") && preNote->getCaption().contains ("ms"),
               "synced pre-delay shows the note, and its length under the bar");
        check (preNote != nullptr && preNote->commitText ("1/8d  375 ms") && juce::roundToInt (valueOf (processor, params::preDelayNote)) == (int) NoteValue::n1_8D,
               "the read-out can be typed back");
        setValue (processor, params::preDelaySync, 0.0f);
        frames (*editor, 1);

        // Freeze, latching and momentary.
        auto& key = studio.getFreezeKey();
        key.onClick();
        check (valueOf (processor, params::freeze) > 0.5f, "latching: a click freezes");
        key.onClick();
        check (valueOf (processor, params::freeze) < 0.5f, "latching: a second click releases");
        studio.getFreezeModeKey (FreezeMode::momentary).onClick();
        frames (*editor, 1);
        check (juce::roundToInt (valueOf (processor, params::freezeMode)) == 1 && key.isMomentary(), "MOMENTARY sets Freeze Mode");
        const auto c = key.getLocalBounds().toFloat().getCentre();
        static_cast<juce::Component&> (key).mouseDown (makeEvent (key, c, juce::ModifierKeys::leftButtonModifier));
        check (valueOf (processor, params::freeze) > 0.5f, "momentary: frozen while held");
        static_cast<juce::Component&> (key).mouseUp (makeEvent (key, c, {}));
        check (valueOf (processor, params::freeze) < 0.5f, "momentary: released on mouse up");
        studio.getFreezeModeKey (FreezeMode::latch).onClick();
        frames (*editor, 1);

        // Tempo, duck and the read-outs.
        processor.getMeters().hostHasTempo.store (true);
        processor.getMeters().bpm.store (97.0);
        frames (*editor, 7);
        auto* bpm = studio.findSlider (params::bpm);
        check (! bpm->isEnabled() && bpm->getValueText().startsWith ("Host 97.0"), "Tempo shows the host's tempo, read-only");
        processor.getMeters().hostHasTempo.store (false);
        frames (*editor, 7);
        check (bpm->isEnabled(), "Tempo editable without host tempo");

        setValue (processor, params::duck, 0.0f);
        frames (*editor, 7);
        check (studio.getMeters().getReadoutText().endsWith ("Duck off"), "reduction meter off with Duck off");
        setValue (processor, params::duck, 1.0f);
        frames (*editor, 7);
        check (! studio.getMeters().getReadoutText().endsWith ("Duck off"), "and on with Duck on");
        setValue (processor, params::duck, 0.0f);

        setValue (processor, params::decay, 3.0f);
        setValue (processor, params::lowEnd, 2.0f);
        setValue (processor, params::tone, 0.0f);
        frames (*editor, 1);
        check (studio.getTail().getReadoutText().startsWith ("Low 6.00 s, Mid 3.00 s"), "the graph reads out RT60 per band");
        setValue (processor, params::lowEnd, 1.0f);
        frames (*editor, 1);

        // The graph's Decay handle.
        {
            auto& tailGraph = studio.getTail();
            const auto handle = tailGraph.getDecayHandle();
            check (handle.has_value(), "the graph has a Decay handle");

            if (handle.has_value())
            {
                GestureRecorder r;
                processor.getState().getParameter (params::decay)->addListener (&r);
                tailGraph.mouseDown (makeEvent (tailGraph, *handle, {}));
                tailGraph.mouseDrag (makeEvent (tailGraph, handle->translated (40.0f, 0.0f), {}, 1, true));
                frames (*editor, 1);
                const auto during = tailGraph.getDecayHandle();
                check (during.has_value() && std::abs (during->x - (handle->x + 40.0f)) < 1.5f, "the handle follows the mouse (the axis holds still)");
                tailGraph.mouseUp (makeEvent (tailGraph, handle->translated (40.0f, 0.0f), {}, 1, true));
                processor.getState().getParameter (params::decay)->removeListener (&r);
                check (valueOf (processor, params::decay) > 3.05f, "dragging it right lengthens Decay");
                check (r.begins == 1 && r.ends == 1, "one host gesture for the drag");
                setValue (processor, params::decay, 3.0f);
            }

            setValue (processor, params::bypass, 1.0f);
            frames (*editor, 1);
            check (tailGraph.getShape().bypassed && ! tailGraph.getDecayHandle().has_value(), "bypassed: the graph says so and hides its handles");
            setValue (processor, params::bypass, 0.0f);
            frames (*editor, 1);
        }

        // A new editor follows the session's view, or the default when the session has none.
        {
            CeruleanProcessor fresh;
            auto e2 = std::make_unique<CeruleanEditor> (fresh);
            check (e2->getViewMode() == ui::ViewMode::studio, "a new instance opens in the default view (studio)");
            fresh.getUiState().setProperty (ui::uistate::view, "panel", nullptr);
            e2.reset();
            e2 = std::make_unique<CeruleanEditor> (fresh);
            check (e2->getViewMode() == ui::ViewMode::panel, "a session's stored view wins over the default");
        }

        // The header switch is asynchronous (its button belongs to the view being replaced).
        studio.getHeader().getViewButton (ui::ViewMode::panel).onClick();
        check (editor->getViewMode() == ui::ViewMode::studio, "header switch waits for the click to finish");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (editor->getViewMode() == ui::ViewMode::panel, "then shows the panel");
        check (ui::viewPreference::getDefault() == ui::ViewMode::panel, "and the panel is the default again");
        check (editor->getMainPanel().getAdvancedTab().isVisible(), "ADVANCED is on the panel");
    }

    processor.getState().getParameter (params::decay)->removeListener (&rec);
    editor.reset();
    scratch.deleteRecursively();
    ui::PresetManager::setUserPresetDirectoryOverride ({});
    ui::viewPreference::setSettingsFileOverride ({});

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures;
}
