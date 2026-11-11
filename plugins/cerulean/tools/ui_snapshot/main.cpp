/*
    CeruleanUiSnapshot: renders the editor to PNG files for visual review (headless with xvfb-run).

    Usage: CeruleanUiSnapshot <output dir> [--leakcheck] [--selftest]
*/

#include "plugin/PluginProcessor.h"
#include "ui/CeruleanEditor.h"
#include "ui/FactoryPresets.h"
#include "ui/UiState.h"

#include "SelfTest.h"

#include "common/testing/SnapshotTool.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
using namespace myzel::testing;
using cerulean::CeruleanEditor;
using cerulean::CeruleanProcessor;
namespace params = cerulean::params;
namespace uistate = cerulean::ui::uistate;
using cerulean::ui::ViewMode;

void runFrames (CeruleanEditor& editor, int frames, double start = 0.0)
{
    for (int i = 0; i < frames; ++i)
        editor.animateFrame (start + (double) i / 60.0);
}

struct Scene
{
    const char* name;
    std::function<void (CeruleanProcessor&)> setup;
    float livePeakDb = -14.0f;     // synthetic phrase in the tail display (below -66 = none)
    float editorScale = 1.0f;
    std::function<void (CeruleanEditor&)> after;
    bool alsoRetina = false;
    bool studio = false;
    float inDb = -18.0f, outDb = -14.0f, reductionDb = 0.0f;   // studio meters
};

void runScene (const Scene& scene, const juce::File& dir)
{
    CeruleanProcessor processor;
    processor.getUiState().setProperty (uistate::scale, scene.editorScale, nullptr);
    processor.getUiState().setProperty (uistate::view, toString (scene.studio ? ViewMode::studio : ViewMode::panel), nullptr);
    resetParams (processor);

    if (scene.setup != nullptr)
        scene.setup (processor);

    auto editor = std::make_unique<CeruleanEditor> (processor);
    editor->setVisible (true);

    runFrames (*editor, 120);

    if (scene.studio)
    {
        auto& studio = editor->getStudioPanel();

        if (scene.livePeakDb > -66.0f)
            studio.getTail().fillHistoryForSnapshot (scene.livePeakDb, 0.4f);

        studio.getMeters().setLevelsForSnapshot ({ scene.inDb, scene.livePeakDb - 3.0f, scene.outDb }, scene.reductionDb);
    }
    else if (scene.livePeakDb > -66.0f)
        editor->getMainPanel().getTail().fillHistoryForSnapshot (scene.livePeakDb, 0.4f);

    if (scene.after != nullptr)
        scene.after (*editor);

    saveScene (*editor, dir, scene.name, scene.alsoRetina);
    editor.reset();
}

using A = cerulean::Algorithm;

void algorithm (CeruleanProcessor& p, A a)
{
    setParam (p, params::algorithm, (float) (int) a);
}

const std::vector<Scene> scenes {
    { "01-default", nullptr, -15.0f, 1.0f, nullptr, true },

    { "02-halo-shimmer", [] (CeruleanProcessor& p)
      {
          algorithm (p, A::halo);
          setParam (p, params::decay, 9.0f);
          setParam (p, params::preDelay, 45.0f);
          setParam (p, params::size, 72.0f);
          setParam (p, params::mod, 48.0f);
          setParam (p, params::tone, 2.0f);
          setParam (p, params::paramA, 75.0f);
          setParam (p, params::paramB, 62.0f);
          setParam (p, params::mix, 42.0f);
          setParam (p, params::lowEnd, 1.4f);
      },
      -12.0f, 1.0f, nullptr, true },

    { "03-frozen", [] (CeruleanProcessor& p)
      {
          algorithm (p, A::nimbus);
          setParam (p, params::decay, 14.0f);
          setParam (p, params::size, 80.0f);
          setParam (p, params::paramA, 80.0f);
          setParam (p, params::freeze, 1.0f);
          setParam (p, params::mix, 55.0f);
      },
      -10.0f, 1.0f, nullptr },

    { "04-echo-synced", [] (CeruleanProcessor& p)
      {
          algorithm (p, A::echo);
          setParam (p, params::echoSync, 1.0f);
          setParam (p, params::echoNote, 6.0f);
          setParam (p, params::decay, 2.6f);
          setParam (p, params::paramB, 58.0f);
          setParam (p, params::duck, 1.0f);
          setParam (p, params::preDelaySync, 1.0f);
          setParam (p, params::preDelayNote, 2.0f);
      },
      -16.0f, 1.0f, nullptr },

    { "05-room", [] (CeruleanProcessor& p)
      {
          p.getPresets().setMixLocked (true);   // shows the padlock by MIX
          algorithm (p, A::room);
          setParam (p, params::decay, 0.8f);
          setParam (p, params::preDelay, 4.0f);
          setParam (p, params::size, 30.0f);
          setParam (p, params::paramA, 30.0f);
          setParam (p, params::paramB, 70.0f);
          setParam (p, params::tone, 3.0f);
          setParam (p, params::wetOnly, 1.0f);
      },
      -18.0f, 1.0f, nullptr },

    { "06-rise-swell", [] (CeruleanProcessor& p)
      {
          algorithm (p, A::rise);
          setParam (p, params::decay, 5.0f);
          setParam (p, params::paramA, 62.0f);
          setParam (p, params::paramB, 0.0f);
          setParam (p, params::tone, -3.5f);
          setParam (p, params::lowEnd, 0.7f);
      },
      -15.0f, 1.0f, nullptr },

    { "07-advanced", [] (CeruleanProcessor& p)
      {
          setParam (p, params::preDelaySync, 1.0f);
          setParam (p, params::inLowCut, 120.0f);
          setParam (p, params::duck, 1.0f);
          p.getUiState().setProperty (uistate::advancedOpen, true, nullptr);
      },
      -15.0f, 1.0f, nullptr, true },

    { "08-scale-75", nullptr, -15.0f, 0.75f, nullptr },

    { "09-advanced-75", [] (CeruleanProcessor& p) { p.getUiState().setProperty (uistate::advancedOpen, true, nullptr); },
      -15.0f, 0.75f, nullptr },

    { "10-scale-200", [] (CeruleanProcessor& p)
      {
          algorithm (p, A::plate);
          setParam (p, params::decay, 2.2f);
          setParam (p, params::tone, 3.5f);
          setParam (p, params::paramA, 70.0f);
      },
      -14.0f, 2.0f, nullptr },

    { "11-hover-readout", [] (CeruleanProcessor& p)
      {
          algorithm (p, A::choir);
          setParam (p, params::paramA, 37.0f);
          setParam (p, params::paramB, 70.0f);
          setParam (p, params::decay, 6.0f);
      },
      -15.0f, 1.0f,
      [] (CeruleanEditor& e)
      {
          if (auto* k = e.getMainPanel().findKnob (params::paramA))
              k->setHoverForSnapshot (true);
      } },

    { "12-relabel-crossfade", [] (CeruleanProcessor& p) { algorithm (p, A::spring); },
      -66.0f, 1.0f,
      [] (CeruleanEditor& e)
      {
          // Switch algorithm (Spring -> Halo) and stop half way through the legend crossfade.
          auto& key = e.getMainPanel().getAlgorithmKey (5);
          key.onClick();
          runFrames (e, 5, 2.0);
      } },

    { "14-bypass", [] (CeruleanProcessor& p)
      {
          setParam (p, params::bypass, 1.0f);
          setParam (p, params::size, 64.0f);
      },
      -24.0f, 1.0f, nullptr },

    { "13-save-dialog", nullptr, -15.0f, 1.0f,
      [] (CeruleanEditor& e)
      {
          cerulean::ui::InlineDialog::Options o;
          o.title = "Save preset";
          o.message = "Name your preset. It is stored as an XML file in your user preset folder.";
          o.withTextField = true;
          o.initialText = "Glass Cathedral";
          o.confirmLabel = "Save";
          e.getDialog().open (o);
      } },

    // The studio view.
    { "20-studio-default", nullptr, -15.0f, 1.0f, nullptr, true, true },

    { "21-studio-halo", [] (CeruleanProcessor& p)
      {
          algorithm (p, A::halo);
          setParam (p, params::decay, 9.0f);
          setParam (p, params::preDelay, 45.0f);
          setParam (p, params::size, 72.0f);
          setParam (p, params::mod, 48.0f);
          setParam (p, params::tone, 2.0f);
          setParam (p, params::paramA, 75.0f);
          setParam (p, params::paramB, 62.0f);
          setParam (p, params::mix, 42.0f);
          setParam (p, params::lowEnd, 1.4f);
      },
      -12.0f, 1.0f, nullptr, true, true, -14.0f, -9.0f },

    { "22-studio-frozen", [] (CeruleanProcessor& p)
      {
          algorithm (p, A::nimbus);
          setParam (p, params::decay, 14.0f);
          setParam (p, params::size, 80.0f);
          setParam (p, params::paramA, 80.0f);
          setParam (p, params::freeze, 1.0f);
          setParam (p, params::freezeMode, 1.0f);
          setParam (p, params::freezeLayer, 1.0f);
          setParam (p, params::mix, 55.0f);
      },
      -10.0f, 1.0f, nullptr, false, true },

    { "23-studio-echo-duck", [] (CeruleanProcessor& p)
      {
          p.getPresets().setMixLocked (true);   // shows the padlock by MIX
          algorithm (p, A::echo);
          setParam (p, params::echoSync, 1.0f);
          setParam (p, params::echoNote, 6.0f);
          setParam (p, params::decay, 2.6f);
          setParam (p, params::paramB, 58.0f);
          setParam (p, params::duck, 1.0f);
          setParam (p, params::preDelaySync, 1.0f);
          setParam (p, params::preDelayNote, 2.0f);
          setParam (p, params::inLowCut, 140.0f);
          setParam (p, params::inHighCut, 6500.0f);
      },
      -16.0f, 1.0f, nullptr, false, true, -9.0f, -7.5f, 8.5f },

    { "24-studio-room-hot", [] (CeruleanProcessor& p)
      {
          algorithm (p, A::room);
          setParam (p, params::decay, 0.8f);
          setParam (p, params::preDelay, 4.0f);
          setParam (p, params::size, 30.0f);
          setParam (p, params::paramA, 30.0f);
          setParam (p, params::paramB, 70.0f);
          setParam (p, params::tone, -3.0f);
          setParam (p, params::wetOnly, 1.0f);
          setParam (p, params::output, 4.5f);
      },
      -18.0f, 1.0f, nullptr, false, true, -3.0f, -2.0f },

    { "25-studio-scale-75", [] (CeruleanProcessor& p) { algorithm (p, A::spring); }, -15.0f, 0.75f, nullptr, false, true },

    { "26-studio-scale-200", [] (CeruleanProcessor& p)
      {
          algorithm (p, A::choir);
          setParam (p, params::paramA, 37.0f);
          setParam (p, params::paramB, 70.0f);
          setParam (p, params::decay, 6.0f);
      },
      -14.0f, 2.0f, nullptr, false, true },

    { "27-studio-text-entry", [] (CeruleanProcessor& p) { algorithm (p, A::plate); }, -15.0f, 1.0f,
      [] (CeruleanEditor& e)
      {
          if (auto* s = e.getStudioPanel().findSlider (params::decay))
              s->showTextEntry();
      }, false, true },

    { "29-studio-bypass", [] (CeruleanProcessor& p)
      {
          setParam (p, params::bypass, 1.0f);
          setParam (p, params::size, 64.0f);
      },
      -24.0f, 1.0f, nullptr, false, true },

    { "28-studio-save-dialog", nullptr, -15.0f, 1.0f,
      [] (CeruleanEditor& e)
      {
          cerulean::ui::InlineDialog::Options o;
          o.title = "Save preset";
          o.message = "Name your preset. It is stored as an XML file in your user preset folder.";
          o.withTextField = true;
          o.initialText = "Glass Cathedral";
          o.confirmLabel = "Save";
          e.getDialog().open (o);
      }, false, true },
};

void timeRendering (ViewMode view)
{
    std::printf ("-- %s view --\n", toString (view).toRawUTF8());
    CeruleanProcessor processor;
    processor.getUiState().setProperty (uistate::view, toString (view), nullptr);
    auto editor = std::make_unique<CeruleanEditor> (processor);
    editor->setVisible (true);

    timeRepaints (*editor);

    // Tail display alone, with a live trace.
    if (view == ViewMode::panel)
    {
        auto& tail = editor->getMainPanel().getTail();
        tail.fillHistoryForSnapshot (-12.0f, 0.2f);
        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int i = 0; i < 20; ++i)
            juce::ignoreUnused (tail.createComponentSnapshot (tail.getLocalBounds(), true, 1.0f));

        std::printf ("tail display (live): %.2f ms per repaint\n", (juce::Time::getMillisecondCounterHiRes() - start) / 20.0);
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
        .pluginName = "Cerulean",
        .makeProcessor = [] { return std::make_unique<CeruleanProcessor>(); },
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
