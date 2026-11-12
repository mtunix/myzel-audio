#include "SelfTest.h"

#include "common/testing/EditorSelfTest.h"

#include "dsp/NoteValues.h"
#include "plugin/PluginProcessor.h"
#include "ui/FactoryPresets.h"
#include "ui/MulberryEditor.h"
#include "ui/Theme.h"
#include "ui/UiState.h"

namespace
{
using namespace myzel::testing;
using namespace mulberry;

void frames (MulberryEditor& editor, int n)
{
    static double t = 0.0;
    for (int i = 0; i < n; ++i)
        editor.animateFrame (t += 1.0 / 60.0);
}

int roundedValue (MulberryProcessor& p, const char* id) { return juce::roundToInt (valueOf (p, id)); }
}

int runUiSelfTest (const ScratchSession& scratch)
{
    failures = 0;

    MulberryProcessor processor;
    auto editor = std::make_unique<MulberryEditor> (processor);
    editor->setVisible (true);
    auto& panel = editor->getMainPanel();

    auto* feedback = panel.findKnob (params::feedback);
    auto* wobble = panel.findKnob (params::wobble);
    check (feedback != nullptr && wobble != nullptr, "knobs exist");

    std::printf ("Knob drag\n");
    {
        GestureRecorder rec;
        auto* p = processor.getState().getParameter (params::feedback);
        p->addListener (&rec);
        const auto before = p->getValue();
        drag (*feedback, -52.0f);
        check (p->getValue() > before + 0.15f && p->getValue() < before + 0.25f, "52 px up moves Feedback by ~0.2 (260 px = full range)");
        check (rec.begins == 1 && rec.ends == 1, "one host gesture for the drag");
        p->removeListener (&rec);
        feedback->mouseDoubleClick (makeEvent (*feedback, feedback->getLocalBounds().toFloat().getCentre(), {}, 2));
        check (std::abs (valueOf (processor, params::feedback) - 35.0f) < 0.01f, "double-click resets to default (35 %)");
    }

    std::printf ("Wobble centre detent\n");
    {
        setParam (processor, params::wobble, 45.0f);
        drag (*wobble, -18.0f);
        check (std::abs (valueOf (processor, params::wobble) - 50.0f) < 0.001f, "dragging through centre sticks at nominal");
    }

    std::printf ("Machine keys and Param A/B legends\n");
    {
        for (int i = 0; i < numMachines; ++i)
        {
            const auto m = static_cast<Machine> (i);
            panel.getMachineKey (i).onClick();
            frames (*editor, 2);
            check (roundedValue (processor, params::machine) == i, (juce::String ("key selects ") + info (m).name).toRawUTF8());
            check (panel.findKnob (params::paramA)->getStyle().title == info (m).paramA
                   && panel.findKnob (params::paramB)->getStyle().title == info (m).paramB, "Param A/B relabel");
        }
        check (panel.getMachineKey ((int) Machine::reel).getToggleState() && ! panel.getMachineKey ((int) Machine::loop).getToggleState(),
               "only the selected key is lit");

        setParam (processor, params::machine, (float) (int) Machine::loop);
        setParam (processor, params::paramA, 92.9f);
        frames (*editor, 1);
        check (panel.findKnob (params::paramA)->getValueText() == "1+2+3", "Loop's Heads read out the head combination");
        auto* heads = panel.findKnob (params::paramA);
        setParam (processor, params::paramA, 7.1f);
        heads->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        check (heads->getValueText() == "2", "arrow key steps one head combination");
        setParam (processor, params::machine, (float) (int) Machine::reel);
        setParam (processor, params::paramA, 50.0f);
        frames (*editor, 1);
    }

    std::printf ("Sync binds Time to the note\n");
    {
        check (&panel.getTimeKnob().getParameter() == processor.getState().getParameter (params::note), "synced by default: Time is the note");
        check (panel.getTimeKnob().getStyle().steps == notes::count, "a 21-position switch");
        check (panel.getTimeKnob().getValueText().startsWith ("1/8D") && panel.getTimeKnob().getValueText().contains ("ms"),
               "the read-out names the note and its length");
        panel.getSyncKey().setToggleState (false, juce::sendNotificationSync);
        frames (*editor, 1);
        check (valueOf (processor, params::sync) < 0.5f, "the SYNC key turns Sync off");
        check (&panel.getTimeKnob().getParameter() == processor.getState().getParameter (params::time), "Time is in ms again");
        panel.getSyncKey().setToggleState (true, juce::sendNotificationSync);
        frames (*editor, 1);
    }

    std::printf ("The mode knob follows Mode\n");
    {
        auto boundTo = [&] (const char* id) { return &panel.getModeAmountKnob().getParameter() == processor.getState().getParameter (id); };
        check (boundTo (params::width) && panel.getModeAmountKnob().getStyle().title == "WIDTH", "Single: Width");
        setParam (processor, params::mode, (float) (int) Mode::dual);
        frames (*editor, 1);
        check (boundTo (params::noteRight) && panel.getModeAmountKnob().getStyle().title == "RIGHT TIME", "Dual, synced: the right note");
        setParam (processor, params::sync, 0.0f);
        frames (*editor, 1);
        check (boundTo (params::timeRight), "Dual, free: the right time in ms");
        setParam (processor, params::mode, (float) (int) Mode::pattern);
        frames (*editor, 1);
        check (boundTo (params::steps) && panel.getModeAmountKnob().getStyle().steps == params::maxSteps, "Pattern: Steps");
        setParam (processor, params::mode, (float) (int) Mode::pingPong);
        frames (*editor, 1);
        check (boundTo (params::width), "Ping-Pong: Width");
        setParam (processor, params::mode, 0.0f);
        setParam (processor, params::sync, 1.0f);
        frames (*editor, 1);
    }

    std::printf ("Tap\n");
    {
        auto& c = editor->getController();
        check (! c.tap (10.0), "one tap does nothing");
        check (c.tap (10.5), "a second tap sets the time");
        check (std::abs (valueOf (processor, params::time) - 500.0f) < 0.5f && valueOf (processor, params::sync) < 0.5f,
               "two taps 500 ms apart: Time 500 ms, Sync off");
        c.tap (10.9);
        c.tap (11.3);
        check (std::abs (valueOf (processor, params::time) - 433.3f) < 0.5f, "the average of the intervals");
        check (! c.tap (14.0), "a pause over 2 s starts over");
        setParam (processor, params::sync, 1.0f);
        frames (*editor, 1);
    }

    std::printf ("Hold\n");
    {
        auto& key = panel.getHoldKey();
        setParam (processor, params::holdMode, 0.0f);
        frames (*editor, 1);
        key.onClick();
        check (valueOf (processor, params::hold) > 0.5f, "latching: a click holds");
        frames (*editor, 1);
        check (panel.getEchoMap().getShape().hold, "the echo map says HOLD");
        key.onClick();
        check (valueOf (processor, params::hold) < 0.5f, "latching: a second click releases");

        setParam (processor, params::holdMode, 1.0f);
        frames (*editor, 1);
        check (key.isMomentary(), "Hold Mode Momentary makes the key momentary");
        const auto c = key.getLocalBounds().toFloat().getCentre();
        static_cast<juce::Component&> (key).mouseDown (makeEvent (key, c, juce::ModifierKeys::leftButtonModifier));
        check (valueOf (processor, params::hold) > 0.5f, "momentary: held while pressed");
        static_cast<juce::Component&> (key).mouseUp (makeEvent (key, c, {}));
        check (valueOf (processor, params::hold) < 0.5f, "momentary: released on mouse up");
        setParam (processor, params::holdMode, 0.0f);
        frames (*editor, 1);
    }

    std::printf ("Pattern steps in the echo map\n");
    {
        setParam (processor, params::mode, (float) (int) Mode::pattern);
        setParam (processor, params::steps, 4.0f);
        frames (*editor, 2);
        auto& map = panel.getEchoMap();
        const auto area = map.stepArea (1);
        check (! area.isEmpty() && map.stepArea (4).isEmpty(), "four step handles");
        GestureRecorder r;
        processor.getState().getParameter (params::stepId (1))->addListener (&r);
        map.mouseDown (makeEvent (map, { area.getCentreX(), area.getY() + area.getHeight() * 0.75f }, {}));
        map.mouseDrag (makeEvent (map, { area.getCentreX(), area.getBottom() - 1.0f }, {}, 1, true));
        map.mouseUp (makeEvent (map, { area.getCentreX(), area.getBottom() - 1.0f }, {}, 1, true));
        processor.getState().getParameter (params::stepId (1))->removeListener (&r);
        check (valueOf (processor, params::stepId (1).toRawUTF8()) < 2.0f, "dragging a step to the floor silences it");
        check (r.begins == 1 && r.ends == 1, "one host gesture per drag");
        map.mouseDoubleClick (makeEvent (map, area.getCentre(), {}, 2));
        check (std::abs (valueOf (processor, params::stepId (1).toRawUTF8()) - 100.0f) < 0.01f, "double-click resets it to full");
        setParam (processor, params::mode, 0.0f);
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
        check (panel.getDrawer().getY() >= ui::metrics::baseHeight, "the deck sits below the panel");
        for (const auto* id : { params::groove, params::feel, params::width, params::crossFeed, params::panRule, params::wobbleRate,
                                params::wobbleShape, params::noise, params::duckAmount, params::duckThreshold, params::duckRelease,
                                params::bpm, params::holdMode })
            if (panel.getDrawer().findKnob (id) == nullptr)
                check (false, (juce::String ("deck knob for ") + id).toRawUTF8());
        check (panel.getDrawer().findKey (params::trails) != nullptr, "Trails key exists");
        setParam (processor, params::groove, 66.0f);
        frames (*editor, 1);
        check (panel.getDrawer().getValueTextFor (params::groove) == "66 %", "deck values follow the parameters");

        processor.getMeters().hostHasTempo.store (true);
        processor.getMeters().bpm.store (97.0);
        frames (*editor, 1);
        check (! panel.getDrawer().findKnob (params::bpm)->isEnabled(), "BPM is greyed out while the host gives the tempo");
        processor.getMeters().hostHasTempo.store (false);
        processor.getMeters().bpm.store (120.0);
        frames (*editor, 1);

        panel.setAdvancedOpen (false);
        frames (*editor, 2);
        check (! panel.getDrawer().isVisible() && editor->getHeight() == closedHeight, "closing shrinks the window back");
        panel.getAdvancedTab().onClick();
        frames (*editor, 2);
        check (panel.isAdvancedOpen(), "the ADVANCED tab opens it");
        panel.getAdvancedTab().onClick();
        frames (*editor, 2);
        check (! panel.isAdvancedOpen(), "and closes it");
    }

    std::printf ("Bypass\n");
    {
        setParam (processor, params::bypass, 1.0f);
        frames (*editor, 1);
        check (panel.getEchoMap().getShape().bypassed && editor->getHeader().getBypassButton().isLit(), "Bypass shows on the echo map");
        setParam (processor, params::bypass, 0.0f);
        frames (*editor, 1);
        check (! panel.getEchoMap().getShape().bypassed, "and goes away");
    }

    std::printf ("Undo\n");
    {
        auto& um = processor.getUndoManager();
        um.clearUndoHistory();
        const auto before = valueOf (processor, params::drive);
        panel.findKnob (params::drive)->setNormalisedValueAsGesture (0.9f);
        check (um.canUndo(), "edit is undoable");
        um.undo();
        check (std::abs (valueOf (processor, params::drive) - before) < 0.05f, "undo restores the value");
        um.clearUndoHistory();
        panel.getHoldKey().onClick();
        panel.getHoldKey().onClick();
        check (! um.canUndo(), "Hold is not recorded");
    }

    std::printf ("Factory presets and A/B restart the engine\n");
    {
        auto& presets = panel.getPresets();
        check (presets.getEntries().size() >= 38, "about 40 factory presets");
        int index = -1;
        for (size_t i = 0; i < presets.getEntries().size(); ++i)
            if (presets.getEntries()[i].name == "Three Heads")
                index = (int) i;
        auto generation = processor.readSettings().presetGeneration;
        presets.load (index);
        check (roundedValue (processor, params::machine) == (int) Machine::loop, "loading a preset sets the parameters");
        check (processor.readSettings().presetGeneration != generation, "loading a preset starts a fresh engine (trails)");
        auto& ab = panel.getAb();
        ab.selectSlot (1);
        setParam (processor, params::feedback, 70.0f);
        generation = processor.readSettings().presetGeneration;
        ab.selectSlot (0);
        check (processor.readSettings().presetGeneration != generation, "switching A/B starts a fresh engine (trails)");
        presets.load (0);
        frames (*editor, 1);
    }

    std::printf ("Scale\n");
    {
        editor->setScale (1.5f);
        check (editor->getWidth() == juce::roundToInt (ui::metrics::baseWidth * 1.5f)
               && editor->getHeight() == juce::roundToInt (ui::metrics::baseHeight * 1.5f), "150 % size");
        editor->setScale (5.0f);
        check (editor->getWidth() == 2 * ui::metrics::baseWidth, "scale is clamped to 200 %");
        editor->setScale (1.0f);
    }

    std::printf ("Character text parsing\n");
    {
        auto textOf = [] (Machine m, bool b, const char* typed)
        {
            const auto pct = params::characterPercentFromText (m, b, typed);
            return pct < 0.0f ? juce::String ("?") : params::characterText (m, b, pct);
        };
        check (textOf (Machine::clean, true, "+7 st") == "+7 st", "Clean shift by its read-out");
        check (textOf (Machine::clean, true, "-12") == "-12 st", "Clean shift by number");
        check (textOf (Machine::rack, false, "12") == "12 bits", "Rack bits by number");
        check (textOf (Machine::bucket, false, "4096") == "4096 stages", "Bucket stages");
        check (textOf (Machine::loop, false, "1+3") == "1+3", "Loop heads by name");
        check (textOf (Machine::reel, false, "7.5 ips") == "7.5 ips", "Reel speed");
        check (textOf (Machine::reel, true, "loud") == "?", "nonsense is rejected");
    }

    std::printf ("Studio view\n");
    {
        editor->setView (ui::ViewMode::studio);
        auto& studio = editor->getStudioPanel();
        frames (*editor, 2);

        for (const auto* id : { params::feedback, params::drive, params::wobble, params::wobbleRate, params::wobbleShape, params::paramA,
                                params::paramB, params::diffuse, params::lowCut, params::highCut, params::width, params::crossFeed,
                                params::panRule, params::steps, params::groove, params::feel, params::noise, params::bpm,
                                params::duckAmount, params::duckThreshold, params::duckRelease, params::mix, params::output })
            if (studio.findSlider (id) == nullptr)
                check (false, (juce::String ("slider for ") + id).toRawUTF8());
        check (true, "every parameter has a control (no hidden drawer)");

        auto* time = studio.findSlider (params::note);
        check (time != nullptr && time->getValueText() == "1/8D" && time->getCaption().contains ("ms"),
               "synced Time shows the note, and its length under it");
        check (time != nullptr && time->commitText ("1/4") && roundedValue (processor, params::note) == 13, "type a note value");
        setParam (processor, params::sync, 0.0f);
        frames (*editor, 1);
        time = studio.findSlider (params::time);
        check (time != nullptr && time->commitText ("0.25 s") && std::abs (valueOf (processor, params::time) - 250.0f) < 0.5f, "type seconds");
        check (time != nullptr && time->commitText ("375 ms") && std::abs (valueOf (processor, params::time) - 375.0f) < 0.5f, "type ms");
        check (time != nullptr && ! time->commitText ("soon"), "nonsense is rejected");
        setParam (processor, params::sync, 1.0f);
        frames (*editor, 1);

        studio.getMachineRow ((int) Machine::clean).onClick();
        frames (*editor, 1);
        check (roundedValue (processor, params::machine) == (int) Machine::clean, "clicking Clean selects it");
        auto* b = studio.findSlider (params::paramB);
        check (b != nullptr && b->getLabel().contains ("SHIFT"), "Param B relabels to SHIFT");
        check (b != nullptr && b->commitText ("+7 st") && b->getValueText() == "+7 st", "type an interval");
        studio.getModeRow ((int) Mode::pattern).onClick();
        frames (*editor, 1);
        check (roundedValue (processor, params::mode) == (int) Mode::pattern && studio.getGraph().getShape().plan.mode == Mode::pattern,
               "the mode list sets Pattern, and the graph follows");

        auto& key = studio.getHoldBlock();
        key.onClick();
        check (valueOf (processor, params::hold) > 0.5f, "HOLD holds");
        key.onClick();
        check (valueOf (processor, params::hold) < 0.5f, "and releases");

        auto& um = processor.getUndoManager();
        um.clearUndoHistory();
        const auto mixValue = valueOf (processor, params::diffuse);
        studio.findSlider (params::diffuse)->setNormalisedValueAsGesture (0.9f);
        check (um.canUndo(), "studio edits are undoable");
        um.undo();
        check (std::abs (valueOf (processor, params::diffuse) - mixValue) < 0.05f, "undo restores the value");

        setParam (processor, params::mode, 0.0f);
        setParam (processor, params::machine, (float) (int) Machine::reel);
        editor->setView (ui::ViewMode::panel);
        check (editor->getViewMode() == ui::ViewMode::panel, "and back to the panel");
    }

    runEditorChecks (processor, *editor, scratch, { .pluginName = "Mulberry", .parameter = params::feedback,
                                                    .lockableIds = ui::presetFormat().lockableIds });
    editor.reset();

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures;
}
