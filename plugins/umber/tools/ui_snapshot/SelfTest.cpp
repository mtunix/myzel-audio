#include "SelfTest.h"

#include "ui/Theme.h"

#include "plugin/PluginProcessor.h"
#include "ui/Header.h"
#include "ui/UmberEditor.h"
#include "ui/UiState.h"

namespace
{
using namespace umber;

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

float valueOf (UmberProcessor& p, const char* id)
{
    auto* param = p.getState().getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

/** Records host gestures, to check that every edit is wrapped in begin/end. */
struct GestureRecorder final : juce::AudioProcessorParameter::Listener
{
    int begins = 0, ends = 0, changes = 0;
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }
};

void flushUndo (UmberProcessor& p)
{
    // APVTS writes parameter values into its ValueTree (and so the undo history) on a timer;
    // copyState() forces that flush.
    juce::ignoreUnused (p.getState().copyState());
}

void dragSlider (myzel::ui::StudioSlider& s, juce::Point<float> delta)
{
    const auto c = s.barArea().getCentre();
    s.mouseDown (makeEvent (s, c, {}));
    s.mouseDrag (makeEvent (s, c + delta, {}, 1, true));
    s.mouseUp (makeEvent (s, c + delta, {}, 1, true));
}

void frames (UmberEditor& editor, int count)
{
    for (int i = 0; i < count; ++i)
        editor.animateFrame ((double) i / 60.0);
}
}

int runUiSelfTest()
{
    // Never touch the user's real default-view setting; start in the panel view.
    const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("umber-ui-selftest");
    scratch.deleteRecursively();
    scratch.createDirectory();
    myzel::ui::viewPreference::setSettingsFileOverride (scratch.getChildFile ("Umber.settings"));

    failures = 0;
    UmberProcessor processor;
    auto editor = std::make_unique<UmberEditor> (processor);
    editor->setVisible (true);
    auto& panel = editor->getMainPanel();

    auto* drive = panel.findKnob (params::drive);
    auto* tone = panel.findKnob (params::tone);
    check (drive != nullptr && tone != nullptr, "knobs exist");

    GestureRecorder rec;
    processor.getState().getParameter (params::drive)->addListener (&rec);

    std::printf ("Knob drag\n");
    {
        const auto before = valueOf (processor, params::drive);
        const auto c = drive->getLocalBounds().toFloat().getCentre();
        drive->mouseDown (makeEvent (*drive, c, {}));
        drive->mouseDrag (makeEvent (*drive, c.translated (0.0f, -64.0f), {}, 1, true));
        drive->mouseUp (makeEvent (*drive, c.translated (0.0f, -64.0f), {}, 1, true));
        const auto after = valueOf (processor, params::drive);
        check (after > before + 1.5f && after < before + 2.5f, "64 px up raises Drive by ~2 (320 px = full range)");
        check (rec.begins == 1 && rec.ends == 1, "one host gesture for the drag");

        const auto mid = valueOf (processor, params::drive);
        drive->mouseDown (makeEvent (*drive, c, {}));
        drive->mouseDrag (makeEvent (*drive, c.translated (0.0f, -64.0f), juce::ModifierKeys::shiftModifier, 1, true));
        drive->mouseUp (makeEvent (*drive, c.translated (0.0f, -64.0f), {}, 1, true));
        const auto fine = valueOf (processor, params::drive) - mid;
        check (fine > 0.1f && fine < 0.4f, "Shift-drag is ~8x finer");

        drive->mouseDoubleClick (makeEvent (*drive, c, {}, 2));
        check (std::abs (valueOf (processor, params::drive) - 3.0f) < 0.01f, "double-click resets to default (3.0)");

        const auto beforeWheel = valueOf (processor, params::drive);
        juce::MouseWheelDetails wheel { 0.0f, 0.1f, false, false, false };
        drive->mouseWheelMove (makeEvent (*drive, c, {}), wheel);
        check (valueOf (processor, params::drive) > beforeWheel, "wheel up raises the value");
    }

    std::printf ("Tone centre detent\n");
    {
        const auto c = tone->getLocalBounds().toFloat().getCentre();
        auto* p = processor.getState().getParameter (params::tone);
        p->setValueNotifyingHost (p->convertTo0to1 (-1.0f));
        tone->mouseDown (makeEvent (*tone, c, {}));
        tone->mouseDrag (makeEvent (*tone, c.translated (0.0f, -15.0f), {}, 1, true));
        tone->mouseUp (makeEvent (*tone, c.translated (0.0f, -15.0f), {}, 1, true));
        check (std::abs (valueOf (processor, params::tone)) < 0.001f, "dragging through centre sticks at Flat");
        check (p->getCurrentValueAsText() == "Flat", "Tone shows 'Flat' at 12 o'clock");
    }

    std::printf ("Accessibility\n");
    {
        // (The live handler needs a native window; create one directly for the test.)
        auto owned = drive->createAccessibilityHandler();
        auto* handler = owned.get();
        check (handler != nullptr && handler->getRole() == juce::AccessibilityRole::slider, "knob is an accessible slider");

        if (handler != nullptr && handler->getValueInterface() != nullptr)
        {
            handler->getValueInterface()->setValueAsString ("7.5");
            check (std::abs (valueOf (processor, params::drive) - 7.5f) < 0.01f, "screen reader can type a value");
            check (handler->getValueInterface()->getCurrentValueAsString() == "7.5", "value text uses the parameter's formatting");
        }
    }

    std::printf ("Undo\n");
    {
        auto& um = processor.getUndoManager();
        um.clearUndoHistory();
        flushUndo (processor);
        um.clearUndoHistory();

        const auto before = valueOf (processor, params::drive);
        drive->setNormalisedValueAsGesture (0.9f);
        flushUndo (processor);
        check (um.canUndo(), "edit is undoable");
        um.undo();
        flushUndo (processor);
        check (std::abs (valueOf (processor, params::drive) - before) < 0.02f, "undo restores the value");
    }

    std::printf ("Presets\n");
    {
        ui::PresetManager presets (processor, ui::presetFormat());
        check (presets.getEntries().size() >= 20, "at least 20 factory presets");

        const auto& entries = presets.getEntries();
        int fuzzIndex = -1;

        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == "Scorched Earth")
                fuzzIndex = (int) i;

        presets.load (fuzzIndex);
        check (juce::roundToInt (valueOf (processor, params::model)) == 4 && valueOf (processor, params::slam) > 0.5f,
               "loading a preset sets the parameters");
        check (! presets.isModified(), "freshly loaded preset is not modified");
        drive->setNormalisedValueAsGesture (0.2f);
        check (presets.isModified(), "editing marks the preset modified");

        const auto saved = presets.saveUserPreset ("Selftest Preset");
        check (saved.wasOk() && presets.isCurrentUserPreset(), "save user preset");
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
        ui::PresetManager presets (processor, ui::presetFormat());
        ui::AbCompare ab (processor, presets);
        auto* p = processor.getState().getParameter (params::drive);

        p->setValueNotifyingHost (p->convertTo0to1 (2.0f));
        ab.selectSlot (1);    // B starts as a copy
        p->setValueNotifyingHost (p->convertTo0to1 (9.0f));
        ab.selectSlot (0);
        check (std::abs (valueOf (processor, params::drive) - 2.0f) < 0.01f, "A recalls its own setting");
        ab.selectSlot (1);
        check (std::abs (valueOf (processor, params::drive) - 9.0f) < 0.01f, "B recalls its own setting");
        ab.copyActiveToOther();
        ab.selectSlot (0);
        check (std::abs (valueOf (processor, params::drive) - 9.0f) < 0.01f, "copy B to A");
    }

    std::printf ("Scale\n");
    {
        editor->setScale (1.5f);
        check (editor->getWidth() == juce::roundToInt (umber::ui::metrics::baseWidth * 1.5f)
               && editor->getHeight() == juce::roundToInt (umber::ui::metrics::baseHeight * 1.5f), "150 % size");
        check (std::abs ((float) processor.getUiState().getProperty (ui::uistate::scale) - 1.5f) < 0.001f, "scale persisted");
        editor->setScale (5.0f);
        check (editor->getWidth() == 2 * umber::ui::metrics::baseWidth, "scale is clamped to 200 %");
    }

    std::printf ("Settings menu\n");
    {
        juce::PopupMenu menu;
        ui::headerConfig (processor).addSettings (menu);

        bool allHaveIds = true;
        juce::PopupMenu::Item* highest = nullptr;

        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            auto& item = it.getItem();

            if (item.action != nullptr)
            {
                allHaveIds = allHaveIds && item.itemID != 0;

                if (highest == nullptr && item.itemID >= myzel::ui::HeaderConfig::firstPluginItemId
                                       && item.itemID < myzel::ui::HeaderConfig::firstPluginItemId + 100)
                    highest = &item;   // the first realtime quality entry
            }
        }

        check (allHaveIds, "every settings entry with an action has an ID (JUCE skips the action otherwise)");
        check (highest != nullptr, "the realtime quality choices are in the settings menu");

        if (highest != nullptr)
        {
            auto* quality = processor.getState().getParameter (params::quality);
            quality->setValueNotifyingHost (1.0f);
            highest->action();
            check (quality->getValue() < 0.5f, "choosing a quality in the settings menu sets it");
        }
    }

    std::printf ("Meter mode\n");
    {
        auto& meter = panel.getMeter();
        meter.toggleMode();
        check ((int) processor.getUiState().getProperty (ui::uistate::meterMode) == 1, "meter mode persisted");
    }

    std::printf ("Studio view\n");
    {
        const auto sizeBefore = editor->getWidth();
        const auto bypassBounds = editor->getMainPanel().getHeader().getBypassButton().getBounds();
        const auto settingsBounds = editor->getMainPanel().getHeader().getSettingsButton().getBounds();
        check (editor->getMainPanel().getHeader().getViewButton (ui::ViewMode::panel).isLit(), "PANEL is lit in the panel view");

        editor->setView (ui::ViewMode::studio);
        check (editor->getViewMode() == ui::ViewMode::studio, "switches to the studio view");
        check (processor.getUiState().getProperty (ui::uistate::view).toString() == "studio", "view stored in the session");
        check (myzel::ui::viewPreference::getDefault ("Umber") == ui::ViewMode::studio, "the chosen view becomes the default for new instances");
        check (editor->getWidth() == sizeBefore, "switching does not resize the window");

        auto& studio = editor->getStudioPanel();
        check (studio.getHeader().getBypassButton().getBounds() == bypassBounds
               && studio.getHeader().getSettingsButton().getBounds() == settingsBounds, "the header keys keep their places in both views");
        check (studio.getHeader().getViewButton (ui::ViewMode::studio).isLit(), "STUDIO is lit");
        frames (*editor, 2);

        // Every sound parameter (plus the listening switches) has a control.
        bool all = true;

        for (const auto* id : { params::drive, params::bias, params::emphasis, params::stereoMode, params::lowCut,
                                params::highCut, params::tone, params::mix, params::output })
            all = all && studio.findSlider (id) != nullptr;

        for (const auto* id : { params::slam, params::weight, params::autoGain, params::delta })
            all = all && studio.findToggle (id) != nullptr;

        check (all, "every parameter has a control");

        auto& toneSlider = *studio.findSlider (params::tone);
        toneSlider.setNormalisedValueAsGesture (0.2f);
        {
            GestureRecorder r;
            processor.getState().getParameter (params::tone)->addListener (&r);
            dragSlider (toneSlider, { 60.0f, 0.0f });
            processor.getState().getParameter (params::tone)->removeListener (&r);
            check (std::abs (toneSlider.getNormalisedValue() - 0.4f) < 0.01f, "60 px right moves a slider by 0.2 (300 px = full range)");
            check (r.begins == 1 && r.ends == 1, "one host gesture for the drag");
        }

        studio.getCircuitRow (4).onClick();
        check (juce::roundToInt (valueOf (processor, params::model)) == 4, "a circuit row selects the circuit");
        check (studio.getCircuitRow (4).getToggleState() && ! studio.getCircuitRow (0).getToggleState(), "only the selected row is lit");

        auto* lowCut = processor.getState().getParameter (params::lowCut);
        lowCut->setValueNotifyingHost (lowCut->convertTo0to1 (20.0f));
        frames (*editor, 7);
        check (studio.findToggle (params::weight)->isDimmed(), "Weight is greyed out while Low Cut is off");
        lowCut->setValueNotifyingHost (lowCut->convertTo0to1 (120.0f));
        frames (*editor, 7);
        check (! studio.findToggle (params::weight)->isDimmed(), "and lit normally with Low Cut on");

        studio.getMeters().setLevelsForSnapshot ({ -12.0f, -6.0f, -9.0f });
        check (studio.getMeters().getReadoutText().startsWith ("In -7.5 dB, Drive -1.5 dB, Out -4.5 dB"), "the meters read out In, Drive and Out");

        editor->setView (ui::ViewMode::panel);
        check (editor->getViewMode() == ui::ViewMode::panel, "and back to the panel");
        check (myzel::ui::viewPreference::getDefault ("Umber") == ui::ViewMode::panel, "and the panel is the default again");
    }

    processor.getState().getParameter (params::drive)->removeListener (&rec);
    editor.reset();
    myzel::ui::viewPreference::setSettingsFileOverride ({});
    scratch.deleteRecursively();

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures;
}
