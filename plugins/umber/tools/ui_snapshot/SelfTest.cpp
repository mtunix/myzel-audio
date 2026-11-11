#include "SelfTest.h"

#include "common/testing/EditorSelfTest.h"

#include "ui/Theme.h"

#include "plugin/PluginProcessor.h"
#include "ui/FactoryPresets.h"
#include "ui/Header.h"
#include "ui/UmberEditor.h"
#include "ui/UiState.h"

namespace
{
using namespace myzel::testing;
using namespace umber;

void flushUndo (UmberProcessor& p)
{
    // APVTS writes parameter values into its ValueTree (and so the undo history) on a timer;
    // copyState() forces that flush.
    juce::ignoreUnused (p.getState().copyState());
}

void frames (UmberEditor& editor, int count)
{
    for (int i = 0; i < count; ++i)
        editor.animateFrame ((double) i / 60.0);
}
}

int runUiSelfTest (const ScratchSession& scratch)
{
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
        drag (*drive, -64.0f);
        const auto after = valueOf (processor, params::drive);
        check (after > before + 1.5f && after < before + 2.5f, "64 px up raises Drive by ~2 (320 px = full range)");
        check (rec.begins == 1 && rec.ends == 1, "one host gesture for the drag");

        const auto mid = valueOf (processor, params::drive);
        drag (*drive, -64.0f, juce::ModifierKeys::shiftModifier);
        const auto fine = valueOf (processor, params::drive) - mid;
        check (fine > 0.1f && fine < 0.4f, "Shift-drag is ~8x finer");

        const auto c = drive->getLocalBounds().toFloat().getCentre();
        drive->mouseDoubleClick (makeEvent (*drive, c, {}, 2));
        check (std::abs (valueOf (processor, params::drive) - 3.0f) < 0.01f, "double-click resets to default (3.0)");

        const auto beforeWheel = valueOf (processor, params::drive);
        juce::MouseWheelDetails wheel { 0.0f, 0.1f, false, false, false };
        drive->mouseWheelMove (makeEvent (*drive, c, {}), wheel);
        check (valueOf (processor, params::drive) > beforeWheel, "wheel up raises the value");
    }

    std::printf ("Tone centre detent\n");
    {
        setParam (processor, params::tone, -1.0f);
        drag (*tone, -15.0f);
        check (std::abs (valueOf (processor, params::tone)) < 0.001f, "dragging through centre sticks at Flat");
        check (processor.getState().getParameter (params::tone)->getCurrentValueAsText() == "Flat", "Tone shows 'Flat' at 12 o'clock");
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

    std::printf ("Factory presets\n");
    {
        auto& presets = editor->getController().getPresets();
        check (presets.getEntries().size() >= 20, "at least 20 factory presets");

        const auto& entries = presets.getEntries();
        int fuzzIndex = -1;

        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == "Scorched Earth")
                fuzzIndex = (int) i;

        presets.load (fuzzIndex);
        check (juce::roundToInt (valueOf (processor, params::model)) == 4 && valueOf (processor, params::slam) > 0.5f,
               "loading a preset sets the parameters");
        presets.load (0);
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
        editor->setView (ui::ViewMode::studio);
        auto& studio = editor->getStudioPanel();
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

        setParam (processor, params::lowCut, 20.0f);
        frames (*editor, 7);
        check (studio.findToggle (params::weight)->isDimmed(), "Weight is greyed out while Low Cut is off");
        setParam (processor, params::lowCut, 120.0f);
        frames (*editor, 7);
        check (! studio.findToggle (params::weight)->isDimmed(), "and lit normally with Low Cut on");

        studio.getMeters().setLevelsForSnapshot ({ -12.0f, -6.0f, -9.0f });
        check (studio.getMeters().getReadoutText().startsWith ("In -7.5 dB, Drive -1.5 dB, Out -4.5 dB"), "the meters read out In, Drive and Out");

        editor->setView (ui::ViewMode::panel);
        check (editor->getViewMode() == ui::ViewMode::panel, "and back to the panel");
    }

    processor.getState().getParameter (params::drive)->removeListener (&rec);
    runEditorChecks (processor, *editor, scratch, { .pluginName = "Umber", .parameter = params::drive,
                                                    .lockableIds = ui::presetFormat().lockableIds });
    editor.reset();

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures;
}
