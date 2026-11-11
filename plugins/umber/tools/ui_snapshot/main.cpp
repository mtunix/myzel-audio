/*
    UmberUiSnapshot: renders the editor to PNG files for visual review (headless with xvfb-run).

    Usage: UmberUiSnapshot <output dir> [--leakcheck] [--selftest]
*/

#include "plugin/PluginProcessor.h"
#include "ui/UmberEditor.h"
#include "ui/UiState.h"

#include "SelfTest.h"

#include "common/testing/SnapshotTool.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
using namespace myzel::testing;
using umber::UmberEditor;
using umber::UmberProcessor;
namespace params = umber::params;
namespace uistate = umber::ui::uistate;

const auto studioView = umber::ui::toString (umber::ui::ViewMode::studio);

struct Levels
{
    float driveRms = 0.0f, outputRms = 0.0f, outputPeak = 0.0f;
};

void runFrames (UmberProcessor& p, UmberEditor& editor, Levels levels, int frames)
{
    auto& meters = p.getMeters();

    for (int i = 0; i < frames; ++i)
    {
        meters.driveRms.store (levels.driveRms);
        meters.outputRms.store (levels.outputRms);
        meters.outputPeak.store (levels.outputPeak);
        editor.animateFrame ((double) i / 60.0);
    }
}

struct Scene
{
    const char* name;
    std::function<void (UmberProcessor&)> setup;
    Levels levels;
    float editorScale = 1.0f;
    std::function<void (UmberEditor&)> after;
    bool alsoRetina = false;
};

void runScene (const Scene& scene, const juce::File& dir)
{
    UmberProcessor processor;
    processor.getUiState().setProperty (uistate::scale, scene.editorScale, nullptr);
    resetParams (processor);

    if (scene.setup != nullptr)
        scene.setup (processor);

    auto editor = std::make_unique<UmberEditor> (processor);
    editor->setVisible (true);

    runFrames (processor, *editor, scene.levels, 120);

    if (scene.after != nullptr)
        scene.after (*editor);

    saveScene (*editor, dir, scene.name, scene.alsoRetina);
    editor.reset();
}

const std::vector<Scene> scenes {
    { "01-default", nullptr, { 0.06f, 0.09f, 0.0f }, 1.0f, nullptr, true },

    { "02-valve-slam-hot", [] (UmberProcessor& p)
      {
          setParam (p, params::model, 3.0f);
          setParam (p, params::slam, 1.0f);
          setParam (p, params::drive, 8.6f);
      },
      { 1.6f, 0.17f, 1.0f }, 1.0f, nullptr, true },

    { "03-fuzz", [] (UmberProcessor& p)
      {
          setParam (p, params::model, 4.0f);
          setParam (p, params::drive, 6.2f);
          setParam (p, params::mix, 72.0f);
          setParam (p, params::tone, 2.5f);
      },
      { 0.42f, 0.11f, 0.0f }, 1.0f, nullptr },

    { "04-filters", [] (UmberProcessor& p)
      {
          setParam (p, params::model, 0.0f);
          setParam (p, params::lowCut, 160.0f);
          setParam (p, params::weight, 1.0f);
          setParam (p, params::highCut, 6500.0f);
          setParam (p, params::tone, -4.0f);
          setParam (p, params::drive, 4.5f);
          setParam (p, params::autoGain, 0.0f);
          setParam (p, params::output, -3.5f);
      },
      { 0.2f, 0.12f, 0.0f }, 1.0f, nullptr },

    { "05-scale-75", nullptr, { 0.1f, 0.1f, 0.0f }, 0.75f, nullptr },
    { "06-scale-200", [] (UmberProcessor& p)
      {
          setParam (p, params::model, 3.0f);
          setParam (p, params::drive, 6.8f);
      },
      { 0.8f, 0.14f, 0.0f }, 2.0f, nullptr },

    { "07-hover-drag", [] (UmberProcessor& p)
      {
          setParam (p, params::model, 1.0f);
          setParam (p, params::tone, 3.2f);
          setParam (p, params::drive, 5.0f);
      },
      { 0.3f, 0.12f, 0.0f }, 1.0f,
      [] (UmberEditor& e)
      {
          if (auto* k = e.getMainPanel().findKnob (params::tone))
              k->setHoverForSnapshot (true);
      }, false },

    { "08-weight-dimmed-drive-meter", [] (UmberProcessor& p)
      {
          setParam (p, params::weight, 1.0f);
          setParam (p, params::model, 2.0f);
          setParam (p, params::drive, 3.0f);
          p.getUiState().setProperty (uistate::meterMode, 1, nullptr);
      },
      { 0.25f, 0.1f, 0.0f }, 1.0f, nullptr },

    { "09-save-dialog", nullptr, { 0.0f, 0.0f, 0.0f }, 1.0f,
      [] (UmberEditor& e)
      {
          umber::ui::InlineDialog::Options o;
          o.title = "Save preset";
          o.message = "Name your preset. It is stored as an XML file in your user preset folder.";
          o.withTextField = true;
          o.initialText = "Snare Fire";
          o.confirmLabel = "Save";
          e.getDialog().open (o);
      }, false },

    { "10-bypass", [] (UmberProcessor& p) { setParam (p, params::bypass, 1.0f); },
      { 0.06f, 0.09f, 0.0f }, 1.0f, nullptr },

    // The studio view.
    { "11-studio", [] (UmberProcessor& p) { p.getUiState().setProperty (uistate::view, studioView, nullptr); },
      { 0.0f, 0.0f, 0.0f }, 1.0f, [] (UmberEditor& e) { e.getStudioPanel().getMeters().setLevelsForSnapshot ({ -16.0f, -11.0f, -13.0f }); }, true },

    { "12-studio-fuzz-weight", [] (UmberProcessor& p)
      {
          p.getUiState().setProperty (uistate::view, studioView, nullptr);
          setParam (p, params::model, 4.0f);
          setParam (p, params::drive, 7.5f);
          setParam (p, params::slam, 1.0f);
          setParam (p, params::lowCut, 120.0f);
          setParam (p, params::weight, 1.0f);
          setParam (p, params::bias, 3.0f);
          setParam (p, params::emphasis, -2.5f);
          setParam (p, params::stereoMode, 1.0f);
          setParam (p, params::highCut, 9000.0f);
          setParam (p, params::tone, 2.0f);
          setParam (p, params::mix, 80.0f);
      },
      { 0.0f, 0.0f, 0.0f }, 1.0f, [] (UmberEditor& e) { e.getStudioPanel().getMeters().setLevelsForSnapshot ({ -14.0f, 1.5f, -4.0f }); } },

    { "13-studio-bypass-mixlock", [] (UmberProcessor& p)
      {
          p.getUiState().setProperty (uistate::view, studioView, nullptr);
          p.getUiState().setProperty (uistate::mixLock, true, nullptr);
          setParam (p, params::bypass, 1.0f);
      },
      { 0.0f, 0.0f, 0.0f }, 1.0f, [] (UmberEditor& e) { e.getStudioPanel().getMeters().setLevelsForSnapshot ({ -16.0f, -11.0f, -13.0f }); } },
};

void timeRendering()
{
    UmberProcessor processor;
    auto editor = std::make_unique<UmberEditor> (processor);
    editor->setVisible (true);

    timeRepaints (*editor);

    auto& meters = processor.getMeters();
    timeFrames ([&] (int i)
    {
        meters.driveRms.store (0.5f + 0.4f * std::sin ((float) i * 0.1f));
        meters.outputRms.store (0.1f);
        editor->animateFrame ((double) i / 60.0);
    });
}
} // namespace

int main (int argc, char* argv[])
{
    return runSnapshotTool (argc, argv, {
        .pluginName = "Umber",
        .makeProcessor = [] { return std::make_unique<UmberProcessor>(); },
        .runSelfTest = runUiSelfTest,
        .renderScenes = [] (const juce::File& dir)
        {
            for (const auto& scene : scenes)
                runScene (scene, dir);
        },
        .timeRendering = timeRendering,
    });
}
