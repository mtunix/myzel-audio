#include "SelfTest.h"

#include "ui/Theme.h"

#include "plugin/PluginProcessor.h"
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
}

int runUiSelfTest()
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
        ui::PresetManager presets (processor);
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
        ui::PresetManager presets (processor);
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

    std::printf ("Meter mode\n");
    {
        auto& meter = panel.getMeter();
        meter.toggleMode();
        check ((int) processor.getUiState().getProperty (ui::uistate::meterMode) == 1, "meter mode persisted");
    }

    processor.getState().getParameter (params::drive)->removeListener (&rec);
    editor.reset();

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures;
}
