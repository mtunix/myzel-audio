/*
    MulberryUiSnapshot: renders the editor to PNG files for visual review (headless with xvfb-run).

    Usage: MulberryUiSnapshot <output dir> [--leakcheck] [--selftest]
*/

#include "plugin/PluginProcessor.h"
#include "ui/FactoryPresets.h"
#include "ui/MulberryEditor.h"
#include "ui/UiState.h"

#include "SelfTest.h"

#include "common/testing/SnapshotTool.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
using namespace myzel::testing;
using mulberry::MulberryEditor;
using mulberry::MulberryProcessor;
namespace params = mulberry::params;
namespace uistate = mulberry::ui::uistate;
using mulberry::ui::ViewMode;
using M = mulberry::Machine;
using Mode = mulberry::Mode;

void runFrames (MulberryEditor& editor, int frames, double start = 0.0)
{
    for (int i = 0; i < frames; ++i)
        editor.animateFrame (start + (double) i / 60.0);
}

struct Scene
{
    const char* name;
    std::function<void (MulberryProcessor&)> setup;
    float livePeakDb = -14.0f;     // synthetic wet level behind the echo map (below -66 = none)
    float editorScale = 1.0f;
    std::function<void (MulberryEditor&)> after;
    bool alsoRetina = false;
    bool studio = false;
    float inDb = -18.0f, outDb = -14.0f, reductionDb = 0.0f;   // studio meters
};

void runScene (const Scene& scene, const juce::File& dir)
{
    MulberryProcessor processor;
    processor.getUiState().setProperty (uistate::scale, scene.editorScale, nullptr);
    processor.getUiState().setProperty (uistate::view, toString (scene.studio ? ViewMode::studio : ViewMode::panel), nullptr);
    resetParams (processor);

    if (scene.setup != nullptr)
        scene.setup (processor);

    auto editor = std::make_unique<MulberryEditor> (processor);
    editor->setVisible (true);
    runFrames (*editor, 120);

    if (scene.studio)
        editor->getStudioPanel().getMeters().setLevelsForSnapshot ({ scene.inDb, scene.livePeakDb - 3.0f, scene.outDb }, scene.reductionDb);
    else if (scene.livePeakDb > -66.0f)
        editor->getMainPanel().getEchoMap().fillHistoryForSnapshot (scene.livePeakDb);

    if (scene.after != nullptr)
        scene.after (*editor);

    saveScene (*editor, dir, scene.name, scene.alsoRetina);
    editor.reset();
}

void machine (MulberryProcessor& p, M m) { setParam (p, params::machine, (float) (int) m); }
void mode (MulberryProcessor& p, Mode m) { setParam (p, params::mode, (float) (int) m); }

void threeHeads (MulberryProcessor& p)
{
    machine (p, M::loop);
    setParam (p, params::note, 10.0f);
    setParam (p, params::feedback, 45.0f);
    setParam (p, params::paramA, 92.9f);
    setParam (p, params::paramB, 40.0f);
    setParam (p, params::wobble, 62.0f);
}

void gallop (MulberryProcessor& p)
{
    mode (p, Mode::pattern);
    setParam (p, params::note, 16.0f);
    setParam (p, params::steps, 6.0f);
    setParam (p, params::feedback, 25.0f);
    const float levels[] = { 100.0f, 25.0f, 70.0f, 0.0f, 90.0f, 45.0f };
    for (int i = 0; i < 6; ++i)
        setParam (p, params::stepId (i).toRawUTF8(), levels[i]);
    setParam (p, params::groove, 60.0f);
}

void dualBucket (MulberryProcessor& p)
{
    machine (p, M::bucket);
    mode (p, Mode::dual);
    setParam (p, params::note, 11.0f);
    setParam (p, params::noteRight, 13.0f);
    setParam (p, params::feedback, 55.0f);
    setParam (p, params::paramA, 87.5f);
    setParam (p, params::crossFeed, 30.0f);
    setParam (p, params::diffuse, 35.0f);
}

void runaway (MulberryProcessor& p)
{
    setParam (p, params::feedback, 108.0f);
    setParam (p, params::drive, 6.0f);
    setParam (p, params::lowCut, 180.0f);
    setParam (p, params::highCut, 4000.0f);
}

const std::vector<Scene> scenes {
    { "01-default", nullptr, -15.0f, 1.0f, nullptr, true },
    { "02-loop-three-heads", threeHeads, -13.0f, 1.0f, nullptr, true },
    { "03-pattern", gallop, -14.0f, 1.0f, nullptr },
    { "04-dual-bucket", dualBucket, -14.0f, 1.0f, nullptr },
    { "05-runaway", runaway, -8.0f, 1.0f, nullptr },
    { "06-hold-oilcan", [] (MulberryProcessor& p)
      {
          machine (p, M::oilCan);
          setParam (p, params::sync, 0.0f);
          setParam (p, params::time, 320.0f);
          setParam (p, params::feedback, 60.0f);
          setParam (p, params::hold, 1.0f);
      },
      -12.0f, 1.0f, nullptr },
    { "07-advanced", [] (MulberryProcessor& p)
      {
          mode (p, Mode::pingPong);
          setParam (p, params::groove, 62.0f);
          setParam (p, params::duck, 1.0f);
          setParam (p, params::noise, 30.0f);
          p.getUiState().setProperty (uistate::advancedOpen, true, nullptr);
      },
      -15.0f, 1.0f, nullptr, true },
    { "08-scale-75", nullptr, -15.0f, 0.75f, nullptr },
    { "09-advanced-75", [] (MulberryProcessor& p) { p.getUiState().setProperty (uistate::advancedOpen, true, nullptr); },
      -15.0f, 0.75f, nullptr },
    { "10-scale-200", [] (MulberryProcessor& p)
      {
          machine (p, M::rack);
          setParam (p, params::sync, 0.0f);
          setParam (p, params::time, 250.0f);
          setParam (p, params::paramA, 5.6f);
      },
      -14.0f, 2.0f, nullptr },
    { "11-hover-readout", [] (MulberryProcessor& p)
      {
          machine (p, M::clean);
          setParam (p, params::paramB, 78.0f);
      },
      -15.0f, 1.0f,
      [] (MulberryEditor& e)
      {
          if (auto* k = e.getMainPanel().findKnob (params::paramB))
              k->setHoverForSnapshot (true);
      } },
    { "12-relabel-crossfade", [] (MulberryProcessor& p) { machine (p, M::bucket); },
      -66.0f, 1.0f,
      [] (MulberryEditor& e)
      {
          // Switch machine (Bucket -> Loop) and stop half way through the legend crossfade.
          e.getMainPanel().getMachineKey ((int) M::loop).onClick();
          runFrames (e, 5, 2.0);
      } },
    { "13-save-dialog", nullptr, -15.0f, 1.0f,
      [] (MulberryEditor& e)
      {
          myzel::ui::InlineDialog::Options o;
          o.title = "Save preset";
          o.message = "Name your preset. It is stored as an XML file in your user preset folder.";
          o.withTextField = true;
          o.initialText = "Night Bus Dub";
          o.confirmLabel = "Save";
          e.getDialog().open (o);
      } },
    { "14-bypass", [] (MulberryProcessor& p) { setParam (p, params::bypass, 1.0f); }, -24.0f, 1.0f, nullptr },
    { "15-free-time", [] (MulberryProcessor& p)
      {
          machine (p, M::clean);
          setParam (p, params::sync, 0.0f);
          setParam (p, params::time, 480.0f);
          setParam (p, params::feedback, 62.0f);
      },
      -15.0f, 1.0f, nullptr },

    // The studio view.
    { "20-studio-default", nullptr, -15.0f, 1.0f, nullptr, true, true },
    { "21-studio-pattern", gallop, -14.0f, 1.0f, nullptr, false, true, -14.0f, -9.0f },
    { "22-studio-dual", dualBucket, -12.0f, 1.0f, nullptr, true, true },
    { "23-studio-hold-duck", [] (MulberryProcessor& p)
      {
          threeHeads (p);
          setParam (p, params::hold, 1.0f);
          setParam (p, params::duck, 1.0f);
          setParam (p, params::holdMode, 1.0f);
      },
      -12.0f, 1.0f, nullptr, false, true, -9.0f, -7.5f, 8.5f },
    { "24-studio-scale-75", nullptr, -15.0f, 0.75f, nullptr, false, true },
    { "25-studio-scale-200", threeHeads, -14.0f, 2.0f, nullptr, false, true },
    { "26-studio-text-entry", nullptr, -15.0f, 1.0f,
      [] (MulberryEditor& e)
      {
          if (auto* s = e.getStudioPanel().findSlider (params::feedback))
              s->showTextEntry();
      }, false, true },
    { "27-studio-bypass", [] (MulberryProcessor& p) { setParam (p, params::bypass, 1.0f); }, -24.0f, 1.0f, nullptr, false, true },
    { "28-studio-runaway", runaway, -8.0f, 1.0f, nullptr, false, true, -12.0f, -2.0f },
};

void timeRendering (ViewMode view)
{
    std::printf ("-- %s view --\n", toString (view).toRawUTF8());
    MulberryProcessor processor;
    processor.getUiState().setProperty (uistate::view, toString (view), nullptr);
    auto editor = std::make_unique<MulberryEditor> (processor);
    editor->setVisible (true);
    timeRepaints (*editor);

    if (view == ViewMode::panel)
    {
        auto& map = editor->getMainPanel().getEchoMap();
        map.fillHistoryForSnapshot (-12.0f);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i)
            juce::ignoreUnused (map.createComponentSnapshot (map.getLocalBounds(), true, 1.0f));
        std::printf ("echo map (live): %.2f ms per repaint\n", (juce::Time::getMillisecondCounterHiRes() - start) / 20.0);
    }

    auto& meters = processor.getMeters();
    timeFrames ([&] (int i)
    {
        meters.wetRms.store (0.1f + 0.08f * std::sin ((float) i * 0.1f));
        editor->animateFrame ((double) i / 60.0);
    });
}

} // namespace

int main (int argc, char* argv[])
{
    return runSnapshotTool (argc, argv, {
        .pluginName = "Mulberry",
        .makeProcessor = [] { return std::make_unique<MulberryProcessor>(); },
        .runSelfTest = runUiSelfTest,
        .renderScenes = [] (const juce::File& dir)
        {
            for (const auto& scene : scenes)
                runScene (scene, dir);
        },
        .timeRendering = []
        {
            timeRendering (ViewMode::panel);
            timeRendering (ViewMode::studio);
        },
    });
}
