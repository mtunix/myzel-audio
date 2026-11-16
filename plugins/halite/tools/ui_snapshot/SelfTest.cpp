#include "SelfTest.h"

#include "Programme.h"

#include "common/testing/UiTestKit.h"
#include "plugin/PluginProcessor.h"
#include "ui/DisplayScales.h"
#include "ui/HaliteEditor.h"
#include "ui/UiState.h"
#include "ui/UserSettings.h"

#include <map>
#include <set>
#include <thread>

namespace
{
using namespace myzel::testing;
using namespace halite;
using ui::Band;

void frames (HaliteEditor& editor, int n)
{
    static double t = 0.0;
    for (int i = 0; i < n; ++i)
        editor.animateFrame (t += 1.0 / 60.0);
}

void pump (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

bool near (float a, float b, float tolerance = 0.002f) { return std::abs (a - b) <= tolerance; }

/** A mouse event whose drag started at `start` (the kit's makeEvent starts every event where it is). */
juce::MouseEvent dragEvent (juce::Component& c, juce::Point<float> start, juce::Point<float> pos, juce::ModifierKeys mods, bool dragged)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &c, &c, now, start, now, 1, dragged };
}

void dragBy (juce::Component& c, juce::Point<float> delta, juce::ModifierKeys mods = {})
{
    const auto from = c.getLocalBounds().toFloat().getCentre();
    c.mouseDown (dragEvent (c, from, from, mods, false));
    for (int i = 1; i <= 4; ++i)
        c.mouseDrag (dragEvent (c, from, from + delta * ((float) i / 4.0f), mods, true));
    c.mouseUp (dragEvent (c, from, from + delta, mods, true));
}

void wheel (juce::Component& c, float deltaY, juce::ModifierKeys mods = {})
{
    juce::MouseWheelDetails w {};
    w.deltaY = deltaY;
    c.mouseWheelMove (makeEvent (c, c.getLocalBounds().toFloat().getCentre(), mods), w);
}

void click (juce::Button& b)
{
    b.triggerClick();
    pump (20);
}

/** Gestures on several parameters at once. */
struct Gestures
{
    Gestures (HaliteProcessor& p, const juce::StringArray& ids)
    {
        for (const auto& id : ids)
        {
            auto* param = p.getState().getParameter (id);
            param->addListener (&recorder);
            listened.push_back (param);
        }
    }

    ~Gestures()
    {
        for (auto* param : listened)
            param->removeListener (&recorder);
    }

    bool onePer (int parameters) const { return recorder.begins == parameters && recorder.ends == parameters; }

    std::vector<juce::AudioProcessorParameter*> listened;
    GestureRecorder recorder;
};

int menuItemTicked (const juce::PopupMenu& menu, int id)
{
    for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
        if (it.getItem().itemID == id)
            return it.getItem().isTicked ? 1 : 0;
    return -1;
}

juce::String stateXml (HaliteProcessor& p)
{
    juce::MemoryBlock block;
    p.getStateInformation (block);
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize()))
        return xml->toString();
    return {};
}

/** Collects pass or fail per invariant over many cases, and prints one line per invariant. */
struct Invariants
{
    struct Result
    {
        int cases = 0, failed = 0;
        juce::String first;
    };

    void expect (const char* what, bool ok, const juce::String& where)
    {
        auto& r = results[what];
        ++r.cases;
        if (! ok && r.failed++ == 0)
            r.first = where;
    }

    void report()
    {
        for (const auto& [what, r] : results)
        {
            const auto line = what + juce::String (" (") + juce::String (r.cases) + " cases"
                            + (r.failed > 0 ? ", " + juce::String (r.failed) + " failed, first: " + r.first : juce::String()) + ")";
            check (r.failed == 0, line.toRawUTF8());
        }
    }

    std::map<juce::String, Result> results;
};

//==============================================================================
void checkWindow (HaliteProcessor& p, HaliteEditor& editor)
{
    std::printf ("Window\n");
    check (editor.getWidth() == 1200 && editor.getHeight() == 720, "opens at 1200 x 720 (M) with no stored size");

    auto* constrainer = editor.getConstrainer();
    check (constrainer != nullptr && constrainer->getMinimumWidth() == 720 && constrainer->getMinimumHeight() == 420,
           "free resizing from 720 x 420");
    check (constrainer != nullptr && constrainer->getFixedAspectRatio() <= 0.0, "no fixed aspect");
    check (menuItemTicked (editor.settingsMenu(), HaliteEditor::sizeItem + 1) == 1, "the settings menu ticks M at 1200 x 720");

    editor.performSettingsItem (HaliteEditor::sizeItem + 0);
    check (editor.getWidth() == 960 && editor.getHeight() == 576, "S is 960 x 576");
    const auto stored = ui::userSettings::windowSize();
    check (stored.getWidth() == 960 && stored.getHeight() == 576, "the size is a user setting");

    {
        auto second = std::make_unique<HaliteEditor> (p);
        check (second->getWidth() == 960 && second->getHeight() == 576, "a new editor opens at the user's size");
    }

    const auto xml = stateXml (p);
    check (! xml.contains ("windowWidth") && ! p.getUiState().hasProperty (myzel::plugin::uistate::scale),
           "the window size is not in the session");

    editor.performSettingsItem (HaliteEditor::sizeItem + 3);
    check (editor.getWidth() == 1680 && editor.getHeight() == 1008, "XL is 1680 x 1008");
    editor.setWindowSize (100, 100);
    check (editor.getWidth() == 720 && editor.getHeight() == 420, "setWindowSize stops at the minimum");
    editor.setWindowSize (1200, 720);
}

//==============================================================================
std::vector<juce::Component*> directChildren (juce::Component& c)
{
    std::vector<juce::Component*> v;
    for (auto* child : c.getChildren())
        if (child->isVisible() && ! child->getBounds().isEmpty())
            v.push_back (child);
    return v;
}

bool overlaps (juce::Rectangle<int> a, juce::Rectangle<int> b)
{
    return a.getIntersection (b).getWidth() > 0 && a.getIntersection (b).getHeight() > 0;
}

void checkPart (Invariants& inv, const char* what, juce::Component& part, const juce::String& where)
{
    const auto local = part.getLocalBounds();
    const auto kids = directChildren (part);
    auto inside = true, apart = true;
    juce::String detail;

    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (! local.contains (kids[i]->getBounds()))
        {
            inside = false;
            detail = kids[i]->getTitle() + " " + kids[i]->getBounds().toString();
        }

        for (size_t j = i + 1; j < kids.size(); ++j)
            if (overlaps (kids[i]->getBounds(), kids[j]->getBounds()))
            {
                apart = false;
                detail = kids[i]->getTitle() + " / " + kids[j]->getTitle();
            }
    }

    inv.expect ((juce::String (what) + ": every control inside, none overlapping").toRawUTF8(), inside && apart, where + " " + detail);
}

void checkLayout (HaliteProcessor& p, HaliteEditor& editor)
{
    std::printf ("Layout at every size\n");
    auto& c = editor.getController();
    Invariants inv;

    const std::vector<std::pair<int, int>> sizes {
        { 720, 420 }, { 760, 480 }, { 860, 600 }, { 899, 620 }, { 900, 600 }, { 960, 576 }, { 999, 700 }, { 1000, 700 },
        { 1017, 640 }, { 1200, 720 }, { 1440, 864 }, { 1680, 1008 }, { 900, 1100 }, { 2560, 1440 }, { 3840, 2400 }
    };

    for (const auto& [w, h] : sizes)
        for (const auto advancedOpen : { false, true })
            for (const auto eventsShown : { true, false })
                for (const auto band : { Band::ess, Band::harsh })
                    for (const auto overlay : { false, true })
                    {
                        if (overlay && w >= HaliteEditor::dockWidth)
                            continue;

                        editor.setSize (1200, 720);   // closes an overlay left from the case before
                        c.setAdvancedOpen (advancedOpen);
                        c.setEventsShown (eventsShown);
                        c.setBand (band);
                        editor.setSize (w, h);
                        if (overlay)
                            editor.toggleSidebar();
                        frames (editor, 1);

                        const auto& l = editor.getLayout();
                        const auto where = juce::String (w) + "x" + juce::String (h) + (advancedOpen ? " advanced" : "")
                                         + (eventsShown ? "" : " no-sidebar") + (band == Band::harsh ? " harsh" : "") + (overlay ? " overlay" : "");

                        const auto docked = l.sidebarDocked ? l.sidebar.getWidth() : 0;
                        inv.expect ("the parts tile the window",
                                    l.header.getY() == 0 && l.header.getHeight() + l.display.getHeight() + l.advanced.getHeight()
                                                                + l.controlBar.getHeight() + l.status.getHeight() == h
                                        && l.display.getWidth() + docked == w
                                        && l.toolbar.getHeight() + l.main.getHeight() + l.spectrum.getHeight() == l.display.getHeight()
                                        && l.main.getWidth() + l.meters.getWidth() == l.display.getWidth(),
                                    where);

                        std::vector<juce::Rectangle<int>> parts { l.header, l.toolbar, l.main, l.spectrum, l.meters, l.controlBar, l.status };
                        if (advancedOpen)
                            parts.push_back (l.advanced);
                        if (l.sidebarDocked)
                            parts.push_back (l.sidebar);

                        auto apart = true;
                        for (size_t i = 0; i < parts.size(); ++i)
                            for (size_t j = i + 1; j < parts.size(); ++j)
                                apart = apart && ! overlaps (parts[i], parts[j]);
                        inv.expect ("no two parts overlap", apart, where);

                        if (! advancedOpen)
                        {
                            inv.expect ("the spectrum keeps 120 px with the Advanced row closed", l.spectrum.getHeight() >= 120, where);
                            inv.expect ("the main view keeps 72 px with the Advanced row closed", l.main.getHeight() >= 72, where);
                        }
                        inv.expect ("the main view keeps 48 px and the spectrum 72 px in every state",
                                    l.main.getHeight() >= 48 && l.spectrum.getHeight() >= 72, where);
                        inv.expect ("the spectrum never takes more than 320 px", l.spectrum.getHeight() <= 320, where);

                        inv.expect ("the sidebar docks from 1000 px wide when it is shown",
                                    l.sidebarDocked == (w >= HaliteEditor::dockWidth && eventsShown), where);
                        inv.expect ("below 1000 px the toggle opens the sidebar as an overlay", l.sidebarOverlay == overlay, where);
                        inv.expect ("the sidebar is 260 px wide", ! editor.isSidebarShowing() || l.sidebar.getWidth() == 260, where);

                        const auto wrapped = l.barMode == ui::ControlBar::Mode::wrapped;
                        inv.expect ("the control bar is one row from 900 px wide", w < 900 || ! wrapped, where);
                        inv.expect ("the control bar wraps into two rows below 870 px", w >= 870 || wrapped, where);
                        inv.expect ("compact rails below 600 px tall", l.compact == (h < HaliteEditor::compactHeight), where);

                        checkPart (inv, "header", editor.getHeader(), where);
                        checkPart (inv, "toolbar", editor.getToolbar(), where);
                        checkPart (inv, "control bar", editor.getControlBar(), where);
                        if (advancedOpen)
                            checkPart (inv, "Advanced row", editor.getAdvanced(), where);

                        // The preset field sits between the wordmark and the right cluster.
                        auto& header = editor.getHeader();
                        const auto box = header.presetArea();
                        inv.expect ("the preset field clears the wordmark and the buttons",
                                    box.getX() >= 14 + 92 && box.getRight() <= header.getAb().getX() - 6 && box.getWidth() >= 200, where);

                        // Every value control of the band on show is visible, with its whole ring or box.
                        auto shownOk = true;
                        for (auto* v : editor.getControlBar().valueControls())
                            if (v->isVisible())
                                shownOk = shownOk && editor.getControlBar().getLocalBounds().contains (v->getBounds()) && v->getHeight() >= 22;
                        inv.expect ("the selected band's controls are whole", shownOk, where);
                    }

    inv.report();

    c.setAdvancedOpen (false);
    c.setEventsShown (true);
    c.setBand (Band::ess);
    editor.setSize (1200, 720);
    frames (editor, 1);
    juce::ignoreUnused (p);
}

//==============================================================================
void checkContrast()
{
    std::printf ("Contrast (PLAN.md, Colours and themes: 4.5:1 for text and read-outs, 3:1 for focus and graphics)\n");

    for (const auto name : { ui::ThemeName::dark, ui::ThemeName::light })
    {
        const auto& t = ui::theme::tokens (name);
        auto failed = 0, pairs = 0;
        juce::String worst;
        double worstMargin = 1.0e9;

        for (const auto& pair : ui::theme::contrastPairs (t))
        {
            const auto ratio = ui::theme::contrast (pair.foreground, pair.background);
            ++pairs;
            if (ratio < pair.minimum)
            {
                ++failed;
                std::printf ("    %s: %.2f < %.1f\n", pair.what, ratio, pair.minimum);
            }
            if (ratio - pair.minimum < worstMargin)
            {
                worstMargin = ratio - pair.minimum;
                worst = juce::String (pair.what) + " " + juce::String (ratio, 2) + ":1";
            }
        }

        const auto themeName = juce::String (name == ui::ThemeName::dark ? "Dark" : "Light");
        const auto line = themeName + ": " + juce::String (pairs) + " pairs meet their ratio (closest: " + worst + ")";
        check (failed == 0, line.toRawUTF8());

        // The waveform's core only has to part from its peak, so it is noted, not gated (PLAN.md, UI restyle: Navy).
        std::printf ("    %s: the waveform's RMS core against its peak %.2f:1 (noted)\n", themeName.toRawUTF8(),
                     ui::theme::contrast (t.signalCore, t.signal));
    }

    const auto& dark = ui::theme::tokens (ui::ThemeName::dark);
    const auto& light = ui::theme::tokens (ui::ThemeName::light);
    check (dark.caution != dark.essFill && light.caution != light.essFill
               && std::abs (dark.caution.getHue() - dark.essFill.getHue()) > 0.05f,
           "Bypass's caution colour is apart from the accent");
    check (light.essText != light.essFill, "Light: the accent's text differs from its fill");
}

//==============================================================================
juce::StringArray attachedIds (HaliteEditor& editor)
{
    juce::StringArray ids;
    auto& bar = editor.getControlBar();

    for (auto* v : bar.valueControls())
        ids.addArray (v->getBinding().parameterIds());
    for (auto* v : editor.getAdvanced().valueControls())
        ids.addArray (v->getBinding().parameterIds());

    ids.addArray ({ params::shape, params::detection, params::stereo, params::key, params::harshOn, params::bypass, params::listen });
    ids.removeDuplicates (false);
    return ids;
}

void checkAttachments (HaliteProcessor& p, HaliteEditor& editor)
{
    std::printf ("Every parameter has a control\n");
    auto& c = editor.getController();
    auto& bar = editor.getControlBar();
    auto& advanced = editor.getAdvanced();

    resetParams (p);
    frames (editor, 1);
    auto ids = attachedIds (editor);
    setParam (p, params::detection, 1.0f);
    frames (editor, 1);
    ids.addArray (attachedIds (editor));
    ids.removeDuplicates (false);
    setParam (p, params::detection, 0.0f);

    juce::StringArray missing;
    for (const auto& id : params::allParameterIds())
        if (! ids.contains (id))
            missing.add (id);
    check (missing.isEmpty() && ids.size() == params::allParameterIds().size(),
           ("all 25 parameters are attached" + (missing.isEmpty() ? juce::String() : ": missing " + missing.joinIntoString (", "))).toRawUTF8());

    // Each value control moves its own parameters, as one gesture and one undo step.
    c.setAdvancedOpen (true);
    std::vector<std::pair<ui::ValueControl*, Band>> controls;
    for (auto* v : bar.valueControls())
        controls.push_back ({ v, (v == &bar.getThreshold (Band::harsh) || v == &bar.getRange (Band::harsh) || v == &bar.getHarshSpeed()
                                  || v == &bar.getHarshFreq() || v == &bar.getHarshWidth()) ? Band::harsh : Band::ess });
    for (auto* v : advanced.valueControls())
        controls.push_back ({ v, Band::ess });

    auto moved = 0, gestures = 0, undone = 0;
    juce::StringArray failures;

    for (auto [v, band] : controls)
    {
        resetParams (p);
        c.setBand (band);
        frames (editor, 1);
        c.getUndoManager().clearUndoHistory();

        auto& binding = v->getBinding();
        const auto edited = binding.parameterIds();
        const auto before = binding.position();
        {
            Gestures g (p, edited);
            dragBy (*v, { 0.0f, -40.0f });
            const auto delta = binding.position() - before;
            const auto ok = near (delta, 40.0f / ui::interaction::dragPixels, 0.01f);
            moved += ok ? 1 : 0;
            gestures += g.onePer (edited.size()) ? 1 : 0;
            if (! ok || ! g.onePer (edited.size()))
                failures.add (v->getTitle() + " moved " + juce::String (delta, 3) + ", gestures " + juce::String (g.recorder.begins));
        }

        c.getUndoManager().undo();
        undone += near (binding.position(), before, 0.001f) ? 1 : 0;
    }

    const auto n = (int) controls.size();
    check (moved == n, ("a drag of 40 px moves each of the " + juce::String (n) + " value controls by a sixth of its travel"
                        + (failures.isEmpty() ? juce::String() : " (" + failures.joinIntoString ("; ") + ")")).toRawUTF8());
    check (gestures == n, "each drag is one gesture per parameter it edits");
    check (undone == n, "one undo step takes each drag back");

    // The segmented controls, the pills, the switch, Bypass and Listen.
    resetParams (p);
    c.getUndoManager().clearUndoHistory();
    click (bar.getShape().segment (2));
    check (c.choice (params::shape) == 2, "a click on Wide sets Shape");
    c.getUndoManager().undo();
    check (c.choice (params::shape) == 0, "and one undo step takes it back");

    click (bar.getDetection().segment (1));
    frames (editor, 1);
    check (c.choice (params::detection) == 1, "a click on Allround sets Detection");
    check (bar.getThreshold (Band::ess).getBinding().parameterIds()[0] == params::thresholdAbs,
           "in Allround the Threshold knob edits the absolute threshold");
    check (bar.getFocus().isDimmed(), "Focus dims in Allround (it is Voice's)");
    click (bar.getDetection().segment (0));
    frames (editor, 1);

    click (advanced.getStereo().segment (3));
    click (advanced.getKey().segment (1));
    check (c.choice (params::stereo) == 3 && c.choice (params::key) == 1, "Stereo and Key take their segments");

    click (bar.getHarshPill());
    frames (editor, 1);
    check (c.getBand() == Band::harsh && p.getUiState()[ui::uistate::selectedBand].toString() == "harsh"
               && bar.getThreshold (Band::harsh).isVisible() && ! bar.getThreshold (Band::ess).isVisible(),
           "the Harsh pill selects the band and shows its controls");
    click (*bar.getHarshPill().getSwitch());
    check (c.flag (params::harshOn), "the pill's switch turns Harsh on");
    frames (editor, 1);
    check (! bar.getThreshold (Band::harsh).isDimmed(), "Harsh's controls are drawn full once it is on");
    click (bar.getEssPill());

    c.getUndoManager().clearUndoHistory();
    click (editor.getHeader().getBypass());
    frames (editor, 1);
    check (c.flag (params::bypass) && editor.getHeader().getBypass().isOn(), "Bypass engages, in its caution colour");
    check (! c.getUndoManager().canUndo(), "Bypass is not an undo step");
    click (editor.getHeader().getBypass());

    bar.chooseListen (ui::ControlBar::listenItemBase + 1);
    frames (editor, 1);
    check (c.choice (params::listen) == 1 && bar.getListen().getText() == "Listen: Removed", "Listen: Removed");
    check (menuItemTicked (bar.listenMenu(), ui::ControlBar::listenItemBase + 1) == 1, "the Listen menu ticks it");
    check (! c.getUndoManager().canUndo(), "Listen is not an undo step");
    bar.chooseListen (ui::ControlBar::listenItemBase);

    c.setAdvancedOpen (false);
    resetParams (p);
    frames (editor, 1);
}

//==============================================================================
void checkInteraction (HaliteProcessor& p, HaliteEditor& editor)
{
    std::printf ("Interaction rules\n");
    auto& c = editor.getController();
    auto& bar = editor.getControlBar();
    auto& threshold = bar.getThreshold (Band::ess);
    resetParams (p);
    frames (editor, 1);
    c.getUndoManager().clearUndoHistory();

    wheel (threshold, 0.2f);
    pump (500);
    check (near (valueOf (p, params::threshold), -16.5f, 0.011f), "a wheel notch: +0.5 dB on Threshold");
    wheel (threshold, 0.2f, juce::ModifierKeys::shiftModifier);
    pump (500);
    check (near (valueOf (p, params::threshold), -16.4f, 0.011f), "Shift: a fifth of the step (+0.1 dB)");

    c.getUndoManager().clearUndoHistory();
    for (int i = 0; i < 5; ++i)
        wheel (threshold, -0.2f);
    pump (500);
    check (near (valueOf (p, params::threshold), -18.9f, 0.011f), "five notches down: -2.5 dB");
    c.getUndoManager().undo();
    check (near (valueOf (p, params::threshold), -16.4f, 0.011f), "a burst of wheel steps is one undo step");

    dragBy (threshold, { 0.0f, -40.0f }, juce::ModifierKeys::shiftModifier);
    check (near (valueOf (p, params::threshold), -16.4f + 60.0f * 40.0f / 240.0f * 0.2f, 0.06f), "Shift drags at a fifth of the speed");

    threshold.mouseDoubleClick (makeEvent (threshold, threshold.getLocalBounds().toFloat().getCentre(), {}, 2));
    check (near (valueOf (p, params::threshold), -17.0f), "double-click resets a knob");

    threshold.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    check (threshold.isEntryOpen(), "Return opens typed entry on a knob");
    threshold.commitEntryForTest (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "20 dB")));
    check (near (valueOf (p, params::threshold), -20.0f), "typed \"-20 dB\" (with the minus sign) sets it");

    auto& low = bar.getDetectLow();
    low.mouseDoubleClick (makeEvent (low, low.boxArea().getCentre(), {}, 2));
    check (low.isEntryOpen(), "double-click opens typed entry on a field");
    low.commitEntryForTest ("6 kHz");
    check (near (valueOf (p, params::detectLow), 6000.0f, 1.0f), "typed \"6 kHz\" sets Detect Low");
    low.openTextEntry();
    low.commitEntryForTest ("lots");
    check (near (valueOf (p, params::detectLow), 6000.0f, 1.0f), "text it does not understand changes nothing");

    threshold.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    pump (500);
    check (near (valueOf (p, params::threshold), -19.5f, 0.011f), "the up arrow steps +0.5 dB");
    threshold.keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0));
    pump (500);
    check (near (valueOf (p, params::threshold), -19.4f, 0.011f), "Shift with an arrow takes a fifth");
    threshold.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    check (near (valueOf (p, params::threshold), -17.0f), "Delete returns it to the default");
    threshold.keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    check (near (valueOf (p, params::threshold), -36.0f), "Home: the bottom of the range");

    // Another key inside an arrow burst closes the burst first, so the steps after it still count.
    c.getUndoManager().clearUndoHistory();
    threshold.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    threshold.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    threshold.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    pump (500);
    check (near (valueOf (p, params::threshold), -16.5f, 0.011f), "Up, Delete, Up within 400 ms: the default and one step");
    c.getUndoManager().undo();
    check (near (valueOf (p, params::threshold), -17.0f, 0.011f), "each its own undo step");

    // Speed: Auto, then a fixed release, as one control.
    c.setAdvancedOpen (true);
    frames (editor, 1);
    auto& speed = editor.getAdvanced().getSpeed();
    check (speed.getBinding().text() == "Auto", "Ess speed reads Auto by default");
    speed.openTextEntry();
    speed.commitEntryForTest ("40 ms");
    check (! c.flag (params::speedAuto) && near (valueOf (p, params::release), 40.0f, 0.3f), "typed \"40 ms\": Auto off, a 40 ms release");
    c.getUndoManager().clearUndoHistory();
    speed.openTextEntry();
    speed.commitEntryForTest ("auto");
    check (c.flag (params::speedAuto), "typed \"auto\" turns Auto back on");
    c.getUndoManager().undo();
    check (! c.flag (params::speedAuto), "one undo step");
    speed.keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    check (c.flag (params::speedAuto) && speed.getBinding().text() == "Auto", "the bottom of Speed's travel is Auto");

    auto& output = editor.getAdvanced().getOutput();
    output.mouseDown (makeEvent (output, output.boxArea().getCentre(), juce::ModifierKeys::altModifier));
    check (output.isEntryOpen(), "Alt-click opens typed entry");
    output.commitEntryForTest ("-3");
    check (near (valueOf (p, params::output), -3.0f), "Output takes a typed value in dB");

    c.setAdvancedOpen (false);
    resetParams (p);
    frames (editor, 1);
}

//==============================================================================
void checkFocusAndAccessibility (HaliteProcessor& p, HaliteEditor& editor)
{
    std::printf ("Keyboard focus and accessibility\n");
    auto& c = editor.getController();
    auto& bar = editor.getControlBar();
    c.setAdvancedOpen (true);
    frames (editor, 1);

    auto traverser = editor.createKeyboardFocusTraverser();
    const auto order = traverser->getAllComponents (&editor);
    auto indexOf = [&] (juce::Component& comp)
    {
        const auto it = std::find (order.begin(), order.end(), &comp);
        return it == order.end() ? -1 : (int) std::distance (order.begin(), it);
    };

    const auto preset = indexOf (editor.getHeader().getPresetName());
    const auto settings = indexOf (editor.getHeader().getSettings());
    const auto tabs = indexOf (editor.getToolbar().getTabs().segment (0));
    const auto list = indexOf (editor.getSidebar().getList());
    const auto histogram = indexOf (editor.getSidebar().getHistogram());
    const auto speed = indexOf (editor.getAdvanced().getSpeed());
    const auto essPill = indexOf (bar.getEssPill());
    const auto threshold = indexOf (bar.getThreshold (Band::ess));
    const auto learn = indexOf (bar.getLearn());
    const auto more = indexOf (bar.getMore());

    check ((int) order.size() >= 30, ("the focus order holds every control (" + juce::String ((int) order.size()) + ")").toRawUTF8());
    check (preset >= 0 && preset < settings && settings < tabs && tabs < list && list < histogram && histogram < speed && speed < essPill
               && essPill < threshold && threshold < learn && learn <= more,
           "Tab runs header, toolbar, events sidebar, Advanced row, control bar, left to right");

    auto untitled = 0;
    juce::StringArray unexplained;
    for (auto* comp : order)
    {
        untitled += comp->getTitle().isEmpty() ? 1 : 0;
        if (comp->getDescription().isEmpty())
            unexplained.add (comp->getTitle());
    }
    check (untitled == 0, "every focusable control has an accessible title");
    check (unexplained.isEmpty(), ("every focusable control carries its help as its description" + (unexplained.isEmpty() ? juce::String()
                                       : " (missing: " + unexplained.joinIntoString (", ") + ")")).toRawUTF8());

    juce::StringArray notOneSentence;
    for (const auto& id : ui::helpIds())
    {
        const auto h = ui::helpFor (id);
        const auto body = h.text.trimEnd();
        if (h.title.isEmpty() || ! body.endsWithChar ('.') || body.dropLastCharacters (1).contains (". ") || body.contains ("! "))
            notOneSentence.add (id);
    }
    check (ui::helpIds().size() >= 40 && notOneSentence.isEmpty(),
           ("every help text is a name and one plain sentence (" + juce::String (ui::helpIds().size()) + " texts"
            + (notOneSentence.isEmpty() ? juce::String() : ", not: " + notOneSentence.joinIntoString (", ")) + ")").toRawUTF8());

    {
        auto& sidebar = editor.getSidebar();
        const auto listHandler = sidebar.getList().createAccessibilityHandler();
        const auto histogramHandler = sidebar.getHistogram().createAccessibilityHandler();
        const auto totalsHandler = sidebar.getTotals().createAccessibilityHandler();
        check (listHandler != nullptr && listHandler->getRole() == juce::AccessibilityRole::list && listHandler->getValueInterface() != nullptr
                   && histogramHandler != nullptr && histogramHandler->getValueInterface() != nullptr
                   && histogramHandler->getValueInterface()->getCurrentValueAsString().contains ("threshold")
                   && totalsHandler != nullptr && totalsHandler->getValueInterface() != nullptr,
               "the event list is a list that reads its marked row; the histogram and the totals read as text");
    }

    auto sliders = 0, values = 0;
    std::vector<ui::ValueControl*> all = bar.valueControls();
    for (auto* v : editor.getAdvanced().valueControls())
        all.push_back (v);
    for (auto* v : all)
    {
        // Built directly: off screen (no peer) JUCE hands out no handlers.
        if (const auto handler = v->createAccessibilityHandler())
        {
            sliders += handler->getRole() == juce::AccessibilityRole::slider ? 1 : 0;
            if (auto* value = handler->getValueInterface())
                values += value->getCurrentValueAsString() == v->getBinding().text() ? 1 : 0;
        }
    }
    check (sliders == (int) all.size() && values == (int) all.size(), "knobs and fields are sliders that read their value");
    check (editor.getHeader().getAb().segment (1).getTitle() == "A/B: B" && bar.getShape().segment (1).getTitle() == "Shape: Spectral",
           "segments are named with their group");
    check (bar.getHarshPill().getSwitch()->getTitle() == "Harsh on" && editor.getHeader().getBypass().getTitle() == "Bypass",
           "switches say what they switch");

    auto& k = bar.getThreshold (Band::ess);
    k.focusGained (juce::Component::focusChangedByTabKey);
    frames (editor, 1);
    check (editor.getStatusLine().shownHelp().title == "Threshold", "the status line explains the control in focus");
    k.focusLost (juce::Component::focusChangedByTabKey);

    c.setAdvancedOpen (false);
    frames (editor, 1);
    juce::ignoreUnused (p);
}

//==============================================================================
void checkUiState (HaliteProcessor& p, HaliteEditor& editor)
{
    std::printf ("UI state keys (stored with the session)\n");
    auto& c = editor.getController();
    auto ui = p.getUiState();
    c.getUndoManager().clearUndoHistory();

    c.setDisplayView (ui::DisplayView::cloud);
    c.setBand (Band::harsh);
    c.setEventsShown (false);
    c.setAdvancedOpen (true);
    c.setZoom (ui::DisplayView::timeline, 8.0f);
    c.setZoom (ui::DisplayView::cloud, 45.0f);
    frames (editor, 1);

    check (ui["displayView"].toString() == "cloud" && ui["selectedBand"].toString() == "harsh" && ! (bool) ui["eventsShown"]
               && (bool) ui["advancedOpen"] && near ((float) ui["timelineZoom"], 8.0f) && near ((float) ui["cloudZoom"], 45.0f),
           "displayView, selectedBand, eventsShown, advancedOpen, timelineZoom, cloudZoom are stored by those names");
    check (! c.getUndoManager().canUndo(), "UI state is never an undo step");
    check (editor.getToolbar().getTabs().segment (1).getToggleState() && editor.getToolbar().getZoom().getText() == "45 s",
           "the toolbar shows the Cloud and its zoom");
    check (! editor.getLayout().sidebarDocked && editor.getAdvanced().isVisible(), "the sidebar hides and the Advanced row opens");
    check (menuItemTicked (editor.zoomMenu(), 4) == 1, "the zoom menu ticks 45 s");

    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    {
        auto other = std::make_unique<HaliteProcessor>();
        other->setStateInformation (saved.getData(), (int) saved.getSize());
        auto second = std::make_unique<HaliteEditor> (*other);
        frames (*second, 1);
        check (second->getController().getDisplayView() == ui::DisplayView::cloud && second->getControlBar().getHarshPill().isSelected()
                   && ! second->getLayout().sidebarDocked && second->getAdvanced().isVisible(),
               "a session restored in a new instance opens as it was left");
    }

    // A host restoring a session into an open editor.
    {
        auto fresh = std::make_unique<HaliteProcessor>();
        juce::MemoryBlock defaults;
        fresh->getStateInformation (defaults);
        p.setStateInformation (defaults.getData(), (int) defaults.getSize());
        frames (editor, 1);
        check (editor.getController().getDisplayView() == ui::DisplayView::timeline && editor.getLayout().sidebarDocked
                   && ! editor.getAdvanced().isVisible() && editor.getControlBar().getEssPill().isSelected(),
               "a session restored while open: the editor follows it");

        // Restored off the message thread (JUCE's LV2 wrapper does): the editor follows on the message thread.
        std::thread host ([&] { p.setStateInformation (saved.getData(), (int) saved.getSize()); });
        host.join();
        const auto untouched = editor.getLayout().sidebarDocked && ! editor.getAdvanced().isVisible();
        pump (50);
        frames (editor, 1);
        check (untouched && ! editor.getLayout().sidebarDocked && editor.getAdvanced().isVisible(),
               "a session restored off the message thread: the editor follows it there, not on the host's thread");
        p.setStateInformation (defaults.getData(), (int) defaults.getSize());
        frames (editor, 1);
    }

    c.setZoom (ui::DisplayView::timeline, 99.0f);
    check (near (c.getZoom (ui::DisplayView::timeline), 10.0f), "Timeline zoom stops at 10 s");
    c.setZoom (ui::DisplayView::timeline, 6.0f);
}

//==============================================================================
void checkTheme (HaliteProcessor& p, HaliteEditor& editor)
{
    std::printf ("Theme (a user setting)\n");
    check (ui::theme::currentName() == ui::ThemeName::dark && ui::userSettings::theme() == ui::ThemeChoice::dark, "Dark by default");

    editor.performSettingsItem (HaliteEditor::themeItem + (int) ui::ThemeChoice::light);
    check (ui::theme::currentName() == ui::ThemeName::light && ui::userSettings::theme() == ui::ThemeChoice::light, "⚙ › Theme › Light");
    check (menuItemTicked (editor.settingsMenu(), HaliteEditor::themeItem + 1) == 1, "the menu ticks Light");
    check (! stateXml (p).contains ("theme"), "the theme is not in the session");

    {
        auto second = std::make_unique<HaliteEditor> (p);
        check (ui::theme::currentName() == ui::ThemeName::light, "a new editor opens in the user's theme");
    }

    editor.performSettingsItem (HaliteEditor::themeItem + (int) ui::ThemeChoice::system);
    const auto expected = juce::Desktop::getInstance().isDarkModeActive() ? ui::ThemeName::dark : ui::ThemeName::light;
    check (ui::theme::currentName() == expected, "System follows the desktop where JUCE can read it");

    editor.performSettingsItem (HaliteEditor::themeItem + (int) ui::ThemeChoice::dark);
}

//==============================================================================
void checkPresets (HaliteProcessor& p, HaliteEditor& editor, const ScratchSession& scratch)
{
    std::printf ("Presets, A/B, undo\n");
    auto& c = editor.getController();
    auto& pm = c.getPresets();
    auto& dialog = editor.getDialog();
    auto& header = editor.getHeader();

    check (pm.getUserPresetDirectory() == scratch.getPresetDirectory(), "user presets live in the scratch folder");
    check (pm.getEntries().size() >= 1 && pm.getEntries().front().name == "Init", "the factory list starts with Init");

    resetParams (p);
    frames (editor, 1);
    check (header.getPresetName().getTitle() == "Preset: Utility / Init", "the header names the preset with its category");

    setParam (p, params::range, 12.0f);
    frames (editor, 1);
    check (header.getPresetName().getTitle().endsWith ("modified"), "a changed sound is marked modified");

    editor.performPresetItem (HaliteEditor::saveAsItem);
    check (dialog.isOpen(), "Save as asks for a name");
    dialog.confirmForTest ("Selftest");
    frames (editor, 1);
    check (! dialog.isOpen() && scratch.getPresetDirectory().getChildFile ("Selftest.xml").existsAsFile(), "and saves the file");
    check (pm.isCurrentUserPreset() && header.getPresetName().getTitle() == "Preset: User / Selftest", "which becomes the current preset");

    editor.performPresetItem (HaliteEditor::saveAsItem);
    dialog.confirmForTest ("Selftest");
    check (dialog.isOpen(), "saving over an existing name asks first");
    dialog.confirmForTest ({});
    check (! dialog.isOpen(), "and replaces it on confirmation");

    click (header.getPrevious());
    frames (editor, 1);
    check (pm.getCurrentName() == "Centre vocal" && pm.getCurrentCategory() == "Mix" && near (valueOf (p, params::range), 4.0f),
           "‹ steps to the preset before (the last factory preset)");
    click (header.getNext());
    check (pm.getCurrentName() == "Selftest" && near (valueOf (p, params::range), 12.0f), "› steps back");

    editor.performPresetItem (HaliteEditor::renameItem);
    dialog.confirmForTest ("Selftest B");
    check (scratch.getPresetDirectory().getChildFile ("Selftest B.xml").existsAsFile(), "rename renames the file");
    editor.performPresetItem (HaliteEditor::deleteItem);
    dialog.confirmForTest ({});
    check (! scratch.getPresetDirectory().getChildFile ("Selftest B.xml").existsAsFile(), "delete removes it");

    resetParams (p);
    c.getUndoManager().clearUndoHistory();
    frames (editor, 1);
    check (! header.getUndo().isEnabled(), "undo is greyed with nothing to undo");
    dragBy (editor.getControlBar().getRange (Band::ess), { 0.0f, -24.0f });
    frames (editor, 1);
    check (header.getUndo().isEnabled(), "an edit enables undo");
    click (header.getUndo());
    frames (editor, 1);
    check (near (valueOf (p, params::range), 8.0f) && header.getRedo().isEnabled(), "undo takes it back, redo is offered");
    click (header.getRedo());
    check (! near (valueOf (p, params::range), 8.0f), "redo does it again");

    click (header.getAb().segment (1));
    frames (editor, 1);
    check (c.getAb().getActiveSlot() == 1 && header.getAb().segment (1).getToggleState(), "B is selected");
    click (header.getAb().segment (0));
    resetParams (p);

    // A right-click on A/B offers the copy (the help promises it) and chooses no slot.
    {
        auto menus = 0;
        const auto shown = std::exchange (header.onAbMenu, [&menus] { ++menus; });
        juce::Component& b = header.getAb().segment (1);
        const auto at = b.getLocalBounds().toFloat().getCentre();
        b.mouseDown (makeEvent (b, at, juce::ModifierKeys::rightButtonModifier));
        b.mouseUp (makeEvent (b, at, juce::ModifierKeys::rightButtonModifier));
        pump (20);
        header.onAbMenu = shown;
        check (menus == 1 && c.getAb().getActiveSlot() == 0, "a right-click on B opens the A/B menu and leaves A selected");

        setParam (p, params::range, 3.0f);
        click (header.getAb().segment (1));
        setParam (p, params::range, 15.0f);
        frames (editor, 1);
        juce::String item;
        const auto menu = editor.abMenu();
        for (juce::PopupMenu::MenuItemIterator it (menu, false); it.next();)
            if (it.getItem().itemID == HaliteEditor::abCopyItem)
                item = it.getItem().text;
        editor.performAbItem (HaliteEditor::abCopyItem);
        click (header.getAb().segment (0));
        check (item == "Copy B to A" && near (valueOf (p, params::range), 15.0f), "Copy B to A: A recalls B's sound");
        click (header.getAb().segment (0));
        resetParams (p);
    }

    // The factory set (PLAN.md, Presets): the plan's categories in its order, chosen from the menu, one undo step,
    // never Bypass, Listen or the latency.
    {
        juce::StringArray sections;
        const auto menu = editor.presetMenu();
        for (juce::PopupMenu::MenuItemIterator it (menu, false); it.next();)
            if (it.getItem().isSectionHeader)
                sections.add (it.getItem().text);
        check (sections.joinIntoString (",").startsWith ("Utility,Vocals,Speech,Instruments,Mix"),
               ("the preset menu lists the plan's categories in order (" + sections.joinIntoString (", ") + ")").toRawUTF8());

        auto factory = 0;
        for (const auto& e : pm.getEntries())
            factory += e.isFactory ? 1 : 0;
        check (factory >= 22 && factory <= 28, ("about 25 factory presets (" + juce::String (factory) + ")").toRawUTF8());

        int harshLead = -1;
        for (int i = 0; i < (int) pm.getEntries().size(); ++i)
            if (pm.getEntries()[(size_t) i].name == "Harsh lead")
                harshLead = i;
        setParam (p, params::listen, 2.0f);
        setParam (p, params::bypass, 1.0f);
        p.setLatencyMode (LatencyMode::live);
        c.getUndoManager().clearUndoHistory();
        editor.performPresetItem (harshLead + 1);
        frames (editor, 1);
        check (c.flag (params::harshOn) && near (valueOf (p, params::harshThreshold), -11.0f, 0.01f)
                   && header.getPresetName().getTitle() == "Preset: Vocals / Harsh lead",
               "choosing Vocals / Harsh lead loads it and the header names it");
        check (c.choice (params::listen) == 2 && c.flag (params::bypass) && p.getLatencyMode() == LatencyMode::live,
               "a preset leaves Listen, Bypass and the latency alone");
        c.getUndoManager().undo();
        check (! c.flag (params::harshOn) && near (valueOf (p, params::harshThreshold), params::harshThresholdDefaultDb, 0.01f),
               "one undo step takes the whole preset back");
        p.setLatencyMode (LatencyMode::normal);
        resetParams (p);
        pm.load (0);
    }
}

//==============================================================================
/** A drag from one point to another in four steps, as one press. */
void dragFromTo (juce::Component& c, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
{
    c.mouseDown (dragEvent (c, from, from, mods, false));
    for (int i = 1; i <= 4; ++i)
        c.mouseDrag (dragEvent (c, from, from + (to - from) * ((float) i / 4.0f), mods, true));
    c.mouseUp (dragEvent (c, from, to, mods, true));
}

void clickAt (juce::Component& c, juce::Point<float> at)
{
    c.mouseDown (dragEvent (c, at, at, {}, false));
    c.mouseUp (dragEvent (c, at, at, {}, false));
}

juce::Image snapshotOf (juce::Component& c)
{
    return c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
}

/** Pixels whose channels differ by more than `tolerance` levels. */
int pixelsApart (const juce::Image& a, const juce::Image& b, int tolerance = 2)
{
    if (a.getBounds() != b.getBounds())
        return a.getWidth() * a.getHeight() + 1;

    auto apart = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
        {
            const auto p = a.getPixelAt (x, y), q = b.getPixelAt (x, y);
            if (std::abs (p.getRed() - q.getRed()) > tolerance || std::abs (p.getGreen() - q.getGreen()) > tolerance
                || std::abs (p.getBlue() - q.getBlue()) > tolerance || std::abs (p.getAlpha() - q.getAlpha()) > tolerance)
                ++apart;
        }
    return apart;
}

juce::String percent (float scale)
{
    return juce::String (juce::roundToInt (scale * 100.0f)) + " %";
}

/**
    A part repainted only where the frame clock asked, frame after frame, as JUCE's Linux window does (each asked
    rectangle's physical pixels cleared, then painted under a clip of them), against the same part painted whole at
    the end. The part sits a point in from the image's corner, on a half pixel at 150 %, as a part of the editor can.
    Returns the pixels apart (exactly) and the share of the part the frames asked for.
*/
template <typename Part>
std::pair<int, double> repaintedWhereAsked (Part& part, int n, const std::function<void()>& frame, float scale)
{
    const auto bounds = part.getLocalBounds();
    const auto toImage = juce::AffineTransform::translation (1.0f, 1.0f).scaled (scale);
    juce::Image shown (juce::Image::ARGB, juce::roundToInt ((float) (bounds.getWidth() + 2) * scale),
                       juce::roundToInt ((float) (bounds.getHeight() + 2) * scale), true);

    auto paint = [&] (juce::Image& into, const juce::RectangleList<int>& area)
    {
        juce::RectangleList<int> pixels;
        for (const auto& r : area)
            pixels.add (r.toFloat().transformedBy (toImage).getSmallestIntegerContainer());
        for (const auto& r : pixels)
            into.clear (r);
        juce::Graphics g (into);
        g.reduceClipRegion (pixels);
        g.addTransform (toImage);
        part.paintEntireComponent (g, true);
    };

    // Twice whole first: a paint that builds a part's cached layers is followed by a whole repaint.
    for (int i = 0; i < 2; ++i)
    {
        frame();
        part.takeAskedArea();
        paint (shown, bounds);
    }

    auto asked = 0.0;
    for (int i = 0; i < n; ++i)
    {
        frame();
        const auto area = part.takeAskedArea();
        for (const auto& r : area)
            asked += (double) r.getWidth() * r.getHeight();
        paint (shown, area);
    }

    juce::Image whole (juce::Image::ARGB, shown.getWidth(), shown.getHeight(), true);
    paint (whole, bounds);
    return { pixelsApart (shown, whole, 0), asked / ((double) n * bounds.getWidth() * bounds.getHeight()) };
}

void checkDisplay (HaliteProcessor& p, HaliteEditor& editor)
{
    std::printf ("Display: Timeline, Cloud, spectrum, meters\n");
    using ui::DisplayView;
    using T = ui::scales::SpectrumTarget;
    auto& c = editor.getController();
    auto& model = editor.getModel();
    auto& undo = c.getUndoManager();

    resetParams (p);
    editor.setWindowSize (1200, 720);
    c.setDisplayView (DisplayView::timeline);
    c.setBand (Band::ess);
    c.setZoom (DisplayView::timeline, 6.0f);
    c.setZoom (DisplayView::cloud, 30.0f);

    Programme programme;
    programme.prepare (p);
    programme.play (p, 14.0);
    frames (editor, 2);

    // Frame by frame from here: a sixtieth of a second of audio, then a frame.
    auto maxCut = 0.0f;
    auto run = [&] (int n, juce::Component* render)
    {
        for (int i = 0; i < n; ++i)
        {
            programme.play (p, 1.0 / 60.0);
            frames (editor, 1);
            maxCut = std::max (maxCut, model.essCut());
            if (render != nullptr)
                juce::ignoreUnused (snapshotOf (*render));
        }
    };

    auto cut = 0, left = 0;
    for (int i = 0; i < model.esses().size(); ++i)
        (model.esses()[i].cutDb >= ui::DisplayModel::cutDrawnDb ? cut : left)++;
    check (model.newest() - model.oldest() > (std::int64_t) (13.0 * model.recordsPerSecond()), "the model mirrors the feed's history");
    check (cut > 3 && left > 0 && model.events().size() >= cut - 1,
           ("the programme's esses: " + juce::String (cut) + " cut, " + juce::String (left) + " left alone, "
            + juce::String (model.events().size()) + " events").toRawUTF8());

    // The Timeline scrolls by blitting, and a scrolled image is the image a full redraw makes.
    auto& timeline = editor.getMainView().getTimeline();
    check (timeline.isVisible() && ! editor.getMainView().getCloud().isVisible(), "displayView timeline shows the Timeline");
    // In both themes: Light's removed part has an edge that joins the column before it.
    for (const auto theme : { ui::ThemeChoice::dark, ui::ThemeChoice::light })
    {
        editor.performSettingsItem (HaliteEditor::themeItem + (int) theme);
        const auto in = juce::String (theme == ui::ThemeChoice::dark ? " (Dark)" : " (Light)");
        snapshotOf (timeline);
        const auto rebuilds = timeline.fullRedraws();
        run (30, &timeline);
        check (timeline.fullRedraws() == rebuilds, ("half a second of frames scrolls the Timeline without a full redraw" + in).toRawUTF8());
        const auto scrolled = timeline.cachedImage().createCopy();
        timeline.invalidate();
        snapshotOf (timeline);
        const auto apart = pixelsApart (scrolled, timeline.cachedImage());
        check (timeline.fullRedraws() == rebuilds + 1 && apart == 0,
               ("a scrolled Timeline matches a full redraw (" + juce::String (apart) + " pixels apart)" + in).toRawUTF8());
        for (const auto scale : { 2.0f, 1.5f })
        {
            const auto [differ, share] = repaintedWhereAsked (timeline, 30, [&] { run (1, nullptr); }, scale);
            check (differ == 0 && share < 0.95,
                   ("a scrolling Timeline repaints its plot alone and matches a whole repaint at " + percent (scale) + " (" + juce::String (differ)
                    + " pixels apart, " + juce::String (juce::roundToInt (100.0 * share)) + " % of the view asked)" + in).toRawUTF8());
        }
    }
    editor.performSettingsItem (HaliteEditor::themeItem + (int) ui::ThemeChoice::dark);
    check (maxCut > 1.0f, "the cut follows the history (the Cut meter and the curves)");
    auto rebuilds = timeline.fullRedraws();

    // Freeze holds the views; the audio runs on.
    click (editor.getToolbar().getFreeze());
    check (model.isFrozen(), "the toolbar's freeze holds the display");
    const auto held = model.viewEnd();
    run (20, nullptr);
    check (model.viewEnd() == held && editor.getToolbar().getFreeze().isOn(), "frozen: the view's right edge stays put");
    check (model.secondsBehindLive() > 0.25, "and the axis knows how far behind live it is");
    click (editor.getToolbar().getFreeze());
    frames (editor, 1);
    check (! model.isFrozen() && model.viewEnd() > held && model.viewEnd() == model.newest(), "unfreezing returns to live");

    // An event older than the zoom window: revealing it freezes and scrolls back to it.
    const auto window = (std::int64_t) (6.0 * model.recordsPerSecond());
    std::optional<std::uint64_t> old;
    for (int i = 0; i < model.events().size() && ! old; ++i)
        if (model.peakRecord (model.events()[i]) < model.viewEnd() - window - (std::int64_t) model.recordsPerSecond())
            old = model.events()[i].number;
    check (old.has_value(), "the programme has an event older than the Timeline's window");
    if (old)
    {
        const auto span = ui::DisplayModel::spanOf (*model.findEvent (*old));
        model.reveal (span);
        frames (editor, 1);
        const auto peak = model.peakRecord (*model.findEvent (*old));
        check (model.isFrozen() && peak > model.viewEnd() - window && peak <= model.viewEnd() && model.selected() == span
                   && editor.getToolbar().getFreeze().isOn(),
               "selecting an older event freezes the Timeline and scrolls back to it");
        snapshotOf (timeline);
        check (! timeline.tagArea (*old).isEmpty(), "its tag is in view");
        click (editor.getToolbar().getFreeze());
        frames (editor, 1);
        check (! model.isFrozen() && model.selected() == span, "unfreezing returns to live; the mark stays");
        model.select (std::nullopt);
    }

    // The threshold line: drag it (one gesture, one undo step), double-click it to type.
    snapshotOf (timeline);
    auto& lines = timeline.getThresholds();
    const auto geo = timeline.geometry();
    const auto y0 = lines.lineY (Band::ess);
    check (std::abs (y0 - geo.sibilance.yOf (params::thresholdDefaultDb)) <= 1.0f, "the Ess threshold line sits at its value");
    {
        undo.clearUndoHistory();
        Gestures g (p, { params::threshold });
        dragFromTo (timeline, { 300.0f, y0 }, { 300.0f, y0 + 30.0f });
        const auto expected = geo.sibilance.dbAt (y0 + 30.0f);
        check (near (valueOf (p, params::threshold), expected, 0.02f) && g.onePer (1),
               ("dragging the line down 30 px lowers Threshold to " + juce::String (valueOf (p, params::threshold), 2) + " dB in one gesture").toRawUTF8());
        undo.undo();
        check (near (valueOf (p, params::threshold), params::thresholdDefaultDb, 0.02f), "and it is one undo step");
    }
    {
        Gestures g (p, { params::threshold });
        timeline.mouseDown (makeEvent (timeline, { 300.0f, y0 }, {}));
        timeline.mouseUp (makeEvent (timeline, { 300.0f, y0 }, {}));
        check (g.recorder.begins == 0, "a click on the line is no edit");
    }
    snapshotOf (timeline);
    timeline.mouseDoubleClick (makeEvent (timeline, lines.chipArea (Band::ess).getCentre(), {}, 2));
    check (lines.isEntryOpen(), "double-clicking the chip opens typed entry");
    lines.commitEntryForTest ("-20");
    check (near (valueOf (p, params::threshold), -20.0f, 0.01f), "typing −20 sets Threshold");

    setParam (p, params::detection, 1.0f);
    frames (editor, 1);
    snapshotOf (timeline);
    {
        const auto before = valueOf (p, params::threshold);
        const auto y = lines.lineY (Band::ess);
        check (std::abs (y - timeline.geometry().sibilance.yOf (params::absThresholdDefaultDb)) <= 1.0f, "Allround: the line shows Absolute in dBFS");
        dragFromTo (timeline, { 300.0f, y }, { 300.0f, y - 20.0f });
        check (valueOf (p, params::thresholdAbs) > params::absThresholdDefaultDb + 1.0f && near (valueOf (p, params::threshold), before),
               "and dragging it edits thresholdAbs alone");
    }
    setParam (p, params::detection, 0.0f);

    setParam (p, params::harshOn, 1.0f);
    c.setBand (Band::harsh);
    frames (editor, 1);
    snapshotOf (timeline);
    {
        const auto y = lines.lineY (Band::harsh);
        check (std::abs (y - timeline.geometry().sibilance.yOf (params::harshThresholdDefaultDb)) <= 1.0f && ! lines.chipArea (Band::harsh).isEmpty(),
               "Harsh selected: its line carries the chip");
        Gestures g (p, { params::harshThreshold });
        dragFromTo (timeline, { 300.0f, y }, { 300.0f, y + 16.0f });
        check (valueOf (p, params::harshThreshold) < params::harshThresholdDefaultDb - 1.0f && g.onePer (1), "dragging it edits Harsh threshold");
        snapshotOf (timeline);
        const auto essY = lines.lineY (Band::ess);
        timeline.mouseDown (makeEvent (timeline, { 300.0f, essY }, {}));
        timeline.mouseUp (makeEvent (timeline, { 300.0f, essY }, {}));
        check (c.getBand() == Band::ess, "grabbing the Ess line selects the Ess band");
    }

    c.setZoom (DisplayView::timeline, 2.0f);
    rebuilds = timeline.fullRedraws();
    frames (editor, 1);
    snapshotOf (timeline);
    check (timeline.fullRedraws() == rebuilds + 1, "a new zoom redraws the Timeline once");
    c.setZoom (DisplayView::timeline, 6.0f);

    // The Cloud: a dot per ess in its window, added as they come (the Detection changes above cleared the candidates).
    run (300, nullptr);
    click (editor.getToolbar().getTabs().segment (1));
    frames (editor, 1);
    auto& cloud = editor.getMainView().getCloud();
    check (cloud.isVisible() && p.getUiState()[ui::uistate::displayView].toString() == "cloud", "the Cloud tab shows the Cloud (displayView cloud)");
    snapshotOf (cloud);
    check (cloud.dotsDrawn() > 0 && cloud.dotsDrawn() == model.esses().size(),
           ("every ess of the window is a dot (" + juce::String (cloud.dotsDrawn()) + ")").toRawUTF8());
    rebuilds = cloud.fullRedraws();
    const auto found = model.essesFound();
    run (90, &cloud);
    check (cloud.fullRedraws() == rebuilds && model.essesFound() > found && cloud.dotsDrawn() == model.esses().size(),
           "new esses are added as dots without a full redraw");
    for (const auto scale : { 2.0f, 1.5f })
    {
        const auto [differ, share] = repaintedWhereAsked (cloud, 60, [&] { run (1, nullptr); }, scale);
        check (differ == 0 && share < 0.95,
               ("a scrolling Cloud repaints its plot alone and matches a whole repaint at " + percent (scale) + " (" + juce::String (differ)
                + " pixels apart, " + juce::String (juce::roundToInt (100.0 * share)) + " % of the view asked)").toRawUTF8());
    }
    {
        auto& cloudLines = cloud.getThresholds();
        snapshotOf (cloud);
        const auto y = cloudLines.lineY (Band::ess);
        dragFromTo (cloud, { 400.0f, y }, { 400.0f, y - 25.0f });
        check (valueOf (p, params::threshold) > -20.0f + 2.0f, "the threshold drags in the Cloud too");
    }
    click (editor.getToolbar().getTabs().segment (0));
    frames (editor, 1);
    check (p.getUiState()[ui::uistate::displayView].toString() == "timeline", "and back to the Timeline");

    // The spectrum: 30 fps, handles that drag their parameters, bands that select on a click.
    auto& spectrum = editor.getSpectrum();
    const auto spectrumFrames = model.spectrumFrame();
    run (60, nullptr);
    const auto updates = (int) (model.spectrumFrame() - spectrumFrames);
    check (updates >= 28 && updates <= 31, ("the spectrum updates at 30 fps (" + juce::String (updates) + " in 60 frames)").toRawUTF8());
    for (const auto scale : { 2.0f, 1.5f })
    {
        const auto [differ, share] = repaintedWhereAsked (spectrum, 60, [&] { run (1, nullptr); }, scale);
        check (differ == 0 && share < 0.6,
               ("the spectrum repaints the rows its curves moved in and matches a whole repaint at " + percent (scale) + " ("
                + juce::String (differ) + " pixels apart, " + juce::String (juce::roundToInt (100.0 * share)) + " % of the view asked a frame)")
                   .toRawUTF8());
    }

    resetParams (p);
    setParam (p, params::harshOn, 1.0f);
    c.setBand (Band::ess);
    frames (editor, 1);
    {
        const auto sg = spectrum.geometry();
        const auto marks = spectrum.marks();
        undo.clearUndoHistory();
        Gestures g (p, { params::detectLow, params::detectHigh });
        dragFromTo (spectrum, ui::scales::handlePosition (sg, marks, T::essLow), { sg.xOf (3000.0), sg.zeroY + 2.0f });
        check (std::abs (valueOf (p, params::detectLow) / 3000.0f - 1.0f) < 0.01f && near (valueOf (p, params::detectHigh), 12000.0f, 1.0f) && g.onePer (1),
               ("Detect Low's handle drags to " + juce::String (valueOf (p, params::detectLow), 0) + " Hz in one gesture").toRawUTF8());
        undo.undo();
        check (near (valueOf (p, params::detectLow), 4500.0f, 1.0f), "and it is one undo step");
    }
    frames (editor, 1);
    {
        const auto sg = spectrum.geometry();
        const auto marks = spectrum.marks();
        dragFromTo (spectrum, ui::scales::handlePosition (sg, marks, T::harshCentre), { sg.xOf (2500.0), sg.zeroY });
        check (std::abs (valueOf (p, params::harshFreq) / 2500.0f - 1.0f) < 0.01f && c.getBand() == Band::harsh,
               "Harsh's centre handle drags Freq, and selects the Harsh band");
    }
    frames (editor, 1);
    {
        const auto sg = spectrum.geometry();
        const auto marks = spectrum.marks();
        const auto low = sg.plot.getBottom() - 12.0f;
        const auto edge = juce::Point<float> (sg.xOf (marks.bands.harshHigh), low);
        check (ui::scales::hitTest (sg, marks, edge, true) == T::harshHigh, "Harsh's edge grabs along its height");
        dragFromTo (spectrum, edge, { sg.xOf (2500.0 * std::pow (2.0, 0.75)), low });
        check (near (valueOf (p, params::harshWidth), 1.5f, 0.02f) && near (valueOf (p, params::harshFreq), 2500.0f, 5.0f),
               ("an edge drags Width around the centre (" + juce::String (valueOf (p, params::harshWidth), 2) + " oct)").toRawUTF8());
    }
    frames (editor, 1);
    {
        const auto sg = spectrum.geometry();
        clickAt (spectrum, { sg.xOf (8000.0), sg.plot.getBottom() - 12.0f });
        frames (editor, 1);
        check (c.getBand() == Band::ess && editor.getControlBar().getEssPill().isSelected(), "a click in the Ess band selects it, and the bar follows");
        clickAt (spectrum, { sg.xOf (2500.0), sg.plot.getBottom() - 12.0f });
        frames (editor, 1);
        check (c.getBand() == Band::harsh, "a click in the Harsh band selects it");
    }

    // An editor closed mid-drag ends the drag's gesture, so later edits of the parameter are still undoable.
    resetParams (p);
    c.setBand (Band::ess);
    frames (editor, 1);
    {
        Gestures g (p, { params::detectLow });
        {
            auto second = std::make_unique<HaliteEditor> (p);
            second->setSize (1200, 720);
            frames (*second, 1);
            auto& sv = second->getSpectrum();
            const auto from = ui::scales::handlePosition (sv.geometry(), sv.marks(), T::essLow);
            sv.mouseDown (dragEvent (sv, from, from, {}, false));
            sv.mouseDrag (dragEvent (sv, from, from + juce::Point<float> (-30.0f, 0.0f), {}, true));
        }
        check (g.onePer (1), ("closed mid-drag on Detect Low: the gesture ends (" + juce::String (g.recorder.begins) + " begun, "
                              + juce::String (g.recorder.ends) + " ended)").toRawUTF8());
        undo.clearUndoHistory();
        editor.getControlBar().getDetectLow().getBinding().setAsEdit (0.8f);
        const auto edited = valueOf (p, params::detectLow);
        undo.undo();
        check (! near (edited, valueOf (p, params::detectLow), 1.0f), "and a later edit of it is still one undo step");
    }

    // Meters: levels, holds, a click resets the hold.
    auto& meters = editor.getMeters();
    run (30, nullptr);
    const auto in = model.meter (0), out = model.meter (1);
    check (in.peakDb > -40.0f && in.rmsDb < in.peakDb && in.holdDb >= in.peakDb && out.peakDb > -40.0f, "In and Out show peak, RMS and hold");
    meters.mouseDown (makeEvent (meters, meters.track (0).getCentre(), {}));
    check (near (model.meter (0).holdDb, model.meter (0).peakDb), "a click on In resets its hold");
    check (meters.meterAt (meters.track (2).getCentreX()) == 2, "the Cut track is the third meter");

    resetParams (p);
    c.setBand (Band::ess);
    frames (editor, 1);
}

//==============================================================================
void checkSidebar (HaliteProcessor& p, HaliteEditor& editor)
{
    std::printf ("Events sidebar and Learn\n");
    using ui::DisplayView;
    using Mode = ui::LearnHistogram::Mode;
    auto& c = editor.getController();
    auto& model = editor.getModel();
    auto& sidebar = editor.getSidebar();
    auto& list = sidebar.getList();
    auto& histogram = sidebar.getHistogram();
    auto& undo = c.getUndoManager();
    auto& learner = p.getLearn();

    resetParams (p);
    editor.setWindowSize (1200, 720);
    c.setEventsShown (true);
    c.setDisplayView (DisplayView::timeline);
    c.setZoom (DisplayView::timeline, 6.0f);
    model.setFrozen (false);
    model.select (std::nullopt);

    // The host plays from 0:42 for a little over a minute, so every listed ess carries a song position.
    Programme programme;
    programme.prepare (p);
    programme.playTransport (p, 42.0);
    auto run = [&] (double seconds)
    {
        for (int i = 0; i < juce::roundToInt (seconds * 60.0); ++i)
        {
            programme.play (p, 1.0 / 60.0);
            frames (editor, 1);
        }
        sidebar.refresh();
        frames (editor, 1);
    };
    auto now = [&]
    {
        sidebar.refresh();
        frames (editor, 1);
    };
    run (62.0);

    check (sidebar.isVisible() && ! sidebar.isOverlay() && sidebar.getWidth() == ui::EventsSidebar::width, "the sidebar docks at 1200 px, 260 px wide");

    const auto& rows = model.rows();
    const auto rps = model.recordsPerSecond();
    auto sorted = true, recent = true, songTimes = true, spacing = true, aloneClean = true;
    auto caught = 0, alone = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& r = rows[i];
        caught += r.caught ? 1 : 0;
        alone += r.caught ? 0 : 1;
        recent = recent && r.span.last >= model.newest() - (std::int64_t) (60.5 * rps);
        songTimes = songTimes && r.songPosition && r.seconds >= 42.0 && r.seconds < 42.0 + 64.0;
        if (! r.caught)
            for (int k = 0; k < model.events().size(); ++k)
                aloneClean = aloneClean && juce::exactlyEqual (r.cutDb, 0.0f) && ! r.span.overlaps (model.events()[k].first, model.events()[k].last);
        if (i > 0)
        {
            const auto& q = rows[i - 1];
            sorted = sorted && r.span.first > q.span.first;
            // Times step as the records do: a candidate's stamp from the transport log agrees with the events' own.
            spacing = spacing && std::abs ((r.seconds - q.seconds) - (double) (r.span.first - q.span.first) / rps) < 0.005;
        }
    }
    check (! rows.empty() && (int) rows.size() <= ui::DisplayModel::maxRows && sorted && recent,
           ("the list holds the last minute's esses, oldest first (" + juce::String ((int) rows.size()) + " rows)").toRawUTF8());
    check (caught == model.events().size() && alone > 0 && aloneClean,
           ("every event is a caught row, and esses left alone are rows too (" + juce::String (caught) + " caught, " + juce::String (alone)
            + " left alone)").toRawUTF8());
    check (songTimes && spacing, "while the host plays every row carries its song position, a candidate's stamped as an event's");

    const auto& totals = model.totals();
    auto sum = 0.0f, largest = 0.0f;
    for (int k = 0; k < model.events().size(); ++k)
    {
        sum += model.events()[k].ess.peakReductionDb;
        largest = std::max (largest, model.events()[k].ess.peakReductionDb);
    }
    check (totals.caught == model.events().size() && near (totals.averageCutDb, sum / (float) std::max (1, totals.caught), 0.001f)
               && near (totals.largestCutDb, largest),
           "the totals count the events of the last minute, their average cut and the largest");

    const auto r0 = rows.empty() ? ui::DisplayModel::Row {} : rows.front();
    const auto texts = ui::EventList::textsOf (r0);
    check (ui::EventList::clockText (72.345) == "1:12.34" && ui::EventList::clockText (5.0) == "0:05.00" && ! texts[0].startsWith ("~"),
           ("times read m:ss.cc (" + texts[0] + ")").toRawUTF8());
    auto stopped = r0;
    stopped.songPosition = false;
    stopped.caught = false;
    const auto stoppedTexts = ui::EventList::textsOf (stopped);
    check (stoppedTexts[0].startsWith ("~") && stoppedTexts[2] == juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
           "elapsed time reads with a leading ~, and an ess left alone has an em dash for its cut");

    // A click marks a row in the views (at once, between the sidebar's data reads); an old one freezes and scrolls back to it.
    now();
    const auto last = (int) rows.size() - 1;
    list.mouseDown (makeEvent (list, list.rowArea (last).toFloat().getCentre(), {}));
    frames (editor, 1);
    check (model.selected().has_value() && model.selected()->overlaps (rows[(size_t) last].span.first, rows[(size_t) last].span.last)
               && list.markedRow() == last && ! model.isFrozen(),
           "a click on the newest row marks it, and the display stays live");

    list.keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    frames (editor, 1);
    {
        const auto window = (std::int64_t) (6.0 * rps);
        const auto peak = model.peakOf (rows.front().span);
        check (list.markedRow() == 0 && model.isFrozen() && peak > model.viewEnd() - window && peak <= model.viewEnd()
                   && editor.getToolbar().getFreeze().isOn() && ! list.rowArea (0).isEmpty(),
               "Home marks the oldest row: the display freezes and scrolls back to it, and the list shows it");
    }
    list.keyPressed (juce::KeyPress (juce::KeyPress::endKey));
    list.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    check (list.markedRow() == last - 1, "End, then Up, marks the row before the newest");
    list.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    check (! model.selected().has_value(), "Escape clears the mark");
    click (editor.getToolbar().getFreeze());
    frames (editor, 1);
    check (! model.isFrozen(), "unfreezing returns to live");

    // The wheel scrolls back; arriving rows leave the rows in view where they are.
    wheel (list, 0.25f);
    frames (editor, 1);
    const auto firstShown = list.firstShown();
    const auto shownSpan = rows[(size_t) firstShown].span.first;
    check (list.scrolled() >= 1, ("the wheel scrolls the list back (" + juce::String (list.scrolled()) + " rows)").toRawUTF8());
    run (2.0);
    check (rows[(size_t) list.firstShown()].span.first == shownSpan, "rows arriving below leave the rows in view where they are");
    wheel (list, -2.0f);
    frames (editor, 1);
    check (list.scrolled() == 0, "and the wheel brings it back to the newest");

    list.mouseEnter (makeEvent (list, list.getLocalBounds().toFloat().getCentre(), {}));
    frames (editor, 1);
    check (editor.getStatusLine().shownHelp().title == "Events", "the status line explains the list under the pointer");
    list.mouseExit (makeEvent (list, list.getLocalBounds().toFloat().getCentre(), {}));

    // The live histogram: the last minute's esses as Learn counts them.
    now();
    const auto& learn = p.getChain().learn();
    auto barsTotal = 0;
    for (const auto b : histogram.bars())
        barsTotal += b;
    check (histogram.mode() == Mode::live && histogram.total() == (int) learn.live().total() && barsTotal == histogram.total()
               && ! std::isnan (histogram.medianDb()) && histogram.readout().startsWith ("median"),
           ("the live histogram holds Learn's last minute (" + juce::String (histogram.total()) + " esses, " + histogram.readout() + ")").toRawUTF8());

    // Learn: listening fills the histogram with the capture; the result is one undo step, marked Learned.
    undo.clearUndoHistory();
    const auto before = valueOf (p, params::threshold);
    click (editor.getControlBar().getLearn());
    run (4.0);
    check (learner.state() == LearnState::listening && histogram.mode() == Mode::listening && histogram.readout().contains ("s left")
               && editor.getControlBar().getLearn().getButtonText().isNotEmpty(),
           ("Learn listens, and the histogram shows its capture filling (" + histogram.readout() + ")").toRawUTF8());
    run (4.5);
    learner.service();
    now();
    const auto learnedDb = valueOf (p, params::threshold);
    check (learner.state() == LearnState::learned && histogram.mode() == Mode::learned && std::abs (learnedDb - before) > 0.05f
               && editor.getStatusLine().shownHelp().text.startsWith ("Threshold set to"),
           ("Learn sets Threshold (" + juce::String (learnedDb, 1) + " dB), marks the histogram Learned and says so").toRawUTF8());
    undo.undo();
    now();
    check (near (valueOf (p, params::threshold), before, 0.01f) && ! undo.canUndo() && histogram.mode() == Mode::live,
           "one undo step takes Learn back, and the live histogram returns");

    auto learnAgain = [&]
    {
        click (editor.getControlBar().getLearn());
        run (8.5);
        learner.service();
        now();
    };
    learnAgain();
    histogram.mouseDown (makeEvent (histogram, histogram.getLocalBounds().toFloat().getCentre(), {}));
    now();
    check (histogram.mode() == Mode::live && learner.state() == LearnState::idle, "a click on the Learned histogram returns to the live one");
    learnAgain();
    dragBy (editor.getControlBar().getRange (Band::ess), { 0.0f, -12.0f });
    now();
    check (histogram.mode() == Mode::live, "so does the next edit");

    programme.setEsses (false);
    run (1.0);
    learnAgain();
    check (learner.state() == LearnState::tooFew && histogram.mode() == Mode::tooFew && histogram.readout() == "too few esses"
               && editor.getStatusLine().shownHelp().text.contains ("Threshold stays"),
           ("too few esses: Threshold stays and the histogram says so (" + juce::String (learner.result().esses) + " esses)").toRawUTF8());
    {
        const auto n = learner.result().esses;
        const auto heard = n == 0 ? juce::String ("No esses") : "Only " + juce::String (n) + (n == 1 ? " ess" : " esses");
        const auto said = editor.getStatusLine().shownHelp().text;
        check (said.startsWith (heard + " in 8 s: Threshold stays"), ("the status line words it as the histogram does: " + said).toRawUTF8());
    }
    histogram.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    check (histogram.mode() == Mode::live, "Return on the histogram returns to the live one");
    programme.setEsses (true);

    // Collapsed, the sidebar reads nothing; at 960 px the toggle opens it over the display.
    c.setEventsShown (false);
    frames (editor, 1);
    const auto repaints = sidebar.frameRepaints();
    run (2.0);
    check (! sidebar.isVisible() && sidebar.frameRepaints() == repaints, "collapsed, the sidebar is hidden and asks for no repaints");
    c.setEventsShown (true);
    editor.setWindowSize (960, 640);
    editor.toggleSidebar();
    run (0.5);
    check (sidebar.isVisible() && sidebar.isOverlay() && ! model.rows().empty() && ! list.rowArea ((int) model.rows().size() - 1).isEmpty(),
           "at 960 px the toggle opens it as an overlay, its newest row in view");
    editor.toggleSidebar();
    editor.setWindowSize (1200, 720);

    p.setPlayHead (nullptr);
    resetParams (p);
    undo.clearUndoHistory();
    frames (editor, (int) (ui::HaliteController::flashSeconds * 60.0f) + 10);   // Learn's message goes
}

//==============================================================================
void checkStatusAndModes (HaliteProcessor& p, HaliteEditor& editor)
{
    std::printf ("Status line, Learn, latency\n");
    auto& c = editor.getController();
    auto& bar = editor.getControlBar();
    resetParams (p);
    frames (editor, 1);

    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    check (editor.getStatusLine().shownSummary() == "Voice" + dot + "Split" + dot + "Linked" + dot + "look-ahead 5 ms",
           ("the mode summary: " + editor.getStatusLine().shownSummary()).toRawUTF8());

    auto& range = bar.getRange (Band::ess);
    range.mouseEnter (makeEvent (range, range.getLocalBounds().toFloat().getCentre(), {}));
    frames (editor, 1);
    check (editor.getStatusLine().shownHelp().title == "Range" && editor.getStatusLine().shownHelp().text.isNotEmpty(),
           "the control under the pointer: its name and one sentence");
    range.mouseExit (makeEvent (range, range.getLocalBounds().toFloat().getCentre(), {}));
    frames (editor, 1);
    check (editor.getStatusLine().shownHelp().title.isEmpty(), "and it clears when the pointer leaves");

    click (bar.getLearn());
    frames (editor, 1);
    check (p.getLearn().state() == LearnState::listening, "Learn starts listening");

    editor.performSettingsItem (HaliteEditor::latencyItem + 1);
    frames (editor, 1);
    check (p.getLatencyMode() == LatencyMode::live && editor.getHeader().getLatency().isEnabled(), "⚙ › Latency › Live");
    check (bar.getShape().isItemDimmed (1) && editor.getStatusLine().shownSummary().contains ("Live"), "Spectral dims; the summary says Live");
    c.setAdvancedOpen (true);
    frames (editor, 1);
    check (editor.getAdvanced().getLookahead().isDimmed(), "Look-ahead dims in Live");
    c.setAdvancedOpen (false);
    editor.performSettingsItem (HaliteEditor::latencyItem);
    frames (editor, 1);
    check (p.getLatencyMode() == LatencyMode::normal, "and back to Normal");

    c.flash ("Learn", "Threshold set");
    frames (editor, 1);
    check (editor.getStatusLine().shownHelp().text == "Threshold set", "a message shows in the status line");
    for (int i = 0; i < 200; ++i)
        frames (editor, 1);
    check (editor.getStatusLine().shownHelp().text.isEmpty(), "and goes after a moment");
}

} // namespace

int runUiSelfTest (const ScratchSession& scratch)
{
    failures = 0;

    auto processor = std::make_unique<HaliteProcessor>();
    auto& p = *processor;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    auto editor = std::make_unique<HaliteEditor> (p);
    editor->setVisible (true);
    frames (*editor, 2);

    checkWindow (p, *editor);
    checkContrast();
    checkLayout (p, *editor);
    checkAttachments (p, *editor);
    checkInteraction (p, *editor);
    checkFocusAndAccessibility (p, *editor);
    checkUiState (p, *editor);
    checkTheme (p, *editor);
    checkPresets (p, *editor, scratch);
    checkDisplay (p, *editor);
    checkSidebar (p, *editor);
    checkStatusAndModes (p, *editor);

    editor.reset();
    std::printf ("%s: %d failure%s\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return failures;
}
