/*
    CeruleanUiSnapshot: renders the editor to PNG files for visual review (headless with xvfb-run).

    Usage: CeruleanUiSnapshot <output dir> [--leakcheck] [--selftest]
*/

#include "plugin/PluginProcessor.h"
#include "ui/CeruleanEditor.h"
#include "ui/PresetManager.h"
#include "ui/UiState.h"

#include "SelfTest.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
using cerulean::CeruleanEditor;
using cerulean::CeruleanProcessor;
namespace params = cerulean::params;

void setParam (CeruleanProcessor& p, const char* id, float value)
{
    if (auto* param = p.getState().getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void resetParams (CeruleanProcessor& p)
{
    for (auto* param : p.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
}

void runFrames (CeruleanEditor& editor, int frames, double start = 0.0)
{
    for (int i = 0; i < frames; ++i)
        editor.animateFrame (start + (double) i / 60.0);
}

void save (juce::Component& c, const juce::File& file, float scale)
{
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    file.deleteFile();
    juce::FileOutputStream out (file);

    if (out.openedOk())
    {
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("wrote %s (%dx%d)\n", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    }
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
    processor.getUiState().setProperty ("scale", scene.editorScale, nullptr);
    processor.getUiState().setProperty (cerulean::ui::uistate::view, scene.studio ? "studio" : "panel", nullptr);
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

        studio.getMeters().setLevelsForSnapshot (scene.inDb, scene.livePeakDb - 3.0f, scene.outDb, scene.reductionDb);
    }
    else if (scene.livePeakDb > -66.0f)
        editor->getMainPanel().getTail().fillHistoryForSnapshot (scene.livePeakDb, 0.4f);

    if (scene.after != nullptr)
        scene.after (*editor);

    save (*editor, dir.getChildFile (juce::String (scene.name) + ".png"), 1.0f);

    if (scene.alsoRetina)
        save (*editor, dir.getChildFile (juce::String (scene.name) + "@2x.png"), 2.0f);

    editor.reset();
}

void timeRendering (bool studio)
{
    std::printf ("-- %s view --\n", studio ? "studio" : "panel");
    CeruleanProcessor processor;
    processor.getUiState().setProperty (cerulean::ui::uistate::view, studio ? "studio" : "panel", nullptr);
    auto editor = std::make_unique<CeruleanEditor> (processor);
    editor->setVisible (true);

    auto measure = [&] (const char* label, float scale, int runs)
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int i = 0; i < runs; ++i)
            juce::ignoreUnused (editor->createComponentSnapshot (editor->getLocalBounds(), true, scale));

        std::printf ("%s: %.2f ms per full repaint\n", label, (juce::Time::getMillisecondCounterHiRes() - start) / runs);
    };

    measure ("cold 1x (builds caches)", 1.0f, 1);
    measure ("warm 1x", 1.0f, 10);
    measure ("cold 2x (builds caches)", 2.0f, 1);
    measure ("warm 2x", 2.0f, 10);

    // Tail display alone, with a live trace.
    if (! studio)
    {
        auto& tail = editor->getMainPanel().getTail();
        tail.fillHistoryForSnapshot (-12.0f, 0.2f);
        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int i = 0; i < 20; ++i)
            juce::ignoreUnused (tail.createComponentSnapshot (tail.getLocalBounds(), true, 1.0f));

        std::printf ("tail display (live): %.2f ms per repaint\n", (juce::Time::getMillisecondCounterHiRes() - start) / 20.0);
    }

    // Frame clock cost alone (lamps, legends, tail state; repaints are only scheduled).
    {
        auto& meters = processor.getMeters();
        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int i = 0; i < 600; ++i)
        {
            meters.wetRms.store (0.1f + 0.08f * std::sin ((float) i * 0.1f));
            editor->animateFrame ((double) i / 60.0);
        }

        std::printf ("animateFrame: %.4f ms per frame\n", (juce::Time::getMillisecondCounterHiRes() - start) / 600.0);
    }
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    // Never touch the user's real default-view setting.
    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cerulean-ui-snapshot.settings");
    cerulean::ui::viewPreference::setSettingsFileOverride (settings);

    juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("ui-snapshots");
    bool leakCheck = false;

    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--selftest")
            return runUiSelfTest() == 0 ? 0 : 1;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg == "--leakcheck")
            leakCheck = true;
        else
            outDir = juce::File::getCurrentWorkingDirectory().getChildFile (arg);
    }

    outDir.createDirectory();

    if (leakCheck)
    {
        // Open and close the editor repeatedly; the JUCE leak detector (Debug builds) reports leaks at exit.
        CeruleanProcessor processor;

        for (int i = 0; i < 8; ++i)
        {
            auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor.createEditorAndMakeActive());
            editor->setVisible (true);
            juce::ignoreUnused (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f + (float) (i % 2)));
            processor.editorBeingDeleted (editor.get());
            editor.reset();
        }

        std::printf ("leak check: opened and closed the editor 8 times\n");
        return 0;
    }

    using A = cerulean::Algorithm;
    auto algorithm = [] (CeruleanProcessor& p, A a) { setParam (p, params::algorithm, (float) (int) a); };

    const std::vector<Scene> scenes {
        { "01-default", nullptr, -15.0f, 1.0f, nullptr, true },

        { "02-halo-shimmer", [&] (CeruleanProcessor& p)
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

        { "03-frozen", [&] (CeruleanProcessor& p)
          {
              algorithm (p, A::nimbus);
              setParam (p, params::decay, 14.0f);
              setParam (p, params::size, 80.0f);
              setParam (p, params::paramA, 80.0f);
              setParam (p, params::freeze, 1.0f);
              setParam (p, params::mix, 55.0f);
          },
          -10.0f, 1.0f, nullptr },

        { "04-echo-synced", [&] (CeruleanProcessor& p)
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

        { "05-room", [&] (CeruleanProcessor& p)
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

        { "06-rise-swell", [&] (CeruleanProcessor& p)
          {
              algorithm (p, A::rise);
              setParam (p, params::decay, 5.0f);
              setParam (p, params::paramA, 62.0f);
              setParam (p, params::paramB, 0.0f);
              setParam (p, params::tone, -3.5f);
              setParam (p, params::lowEnd, 0.7f);
          },
          -15.0f, 1.0f, nullptr },

        { "07-advanced", [&] (CeruleanProcessor& p)
          {
              setParam (p, params::preDelaySync, 1.0f);
              setParam (p, params::inLowCut, 120.0f);
              setParam (p, params::duck, 1.0f);
              p.getUiState().setProperty ("advancedOpen", true, nullptr);
          },
          -15.0f, 1.0f, nullptr, true },

        { "08-scale-75", nullptr, -15.0f, 0.75f, nullptr },

        { "09-advanced-75", [] (CeruleanProcessor& p) { p.getUiState().setProperty ("advancedOpen", true, nullptr); },
          -15.0f, 0.75f, nullptr },

        { "10-scale-200", [&] (CeruleanProcessor& p)
          {
              algorithm (p, A::plate);
              setParam (p, params::decay, 2.2f);
              setParam (p, params::tone, 3.5f);
              setParam (p, params::paramA, 70.0f);
          },
          -14.0f, 2.0f, nullptr },

        { "11-hover-readout", [&] (CeruleanProcessor& p)
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

        { "12-relabel-crossfade", [&] (CeruleanProcessor& p) { algorithm (p, A::spring); },
          -66.0f, 1.0f,
          [] (CeruleanEditor& e)
          {
              // Switch algorithm (Spring -> Halo) and stop half way through the legend crossfade.
              auto& key = e.getMainPanel().getAlgorithmKey (5);
              key.onClick();
              runFrames (e, 5, 2.0);
          } },

        { "14-bypass", [&] (CeruleanProcessor& p)
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
              e.getMainPanel().getDialog().open (o);
          } },

        // Studio view ---------------------------------------------------------
        { "20-studio-default", nullptr, -15.0f, 1.0f, nullptr, true, true },

        { "21-studio-halo", [&] (CeruleanProcessor& p)
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

        { "22-studio-frozen", [&] (CeruleanProcessor& p)
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

        { "23-studio-echo-duck", [&] (CeruleanProcessor& p)
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

        { "24-studio-room-hot", [&] (CeruleanProcessor& p)
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

        { "25-studio-scale-75", [&] (CeruleanProcessor& p) { algorithm (p, A::spring); }, -15.0f, 0.75f, nullptr, false, true },

        { "26-studio-scale-200", [&] (CeruleanProcessor& p)
          {
              algorithm (p, A::choir);
              setParam (p, params::paramA, 37.0f);
              setParam (p, params::paramB, 70.0f);
              setParam (p, params::decay, 6.0f);
          },
          -14.0f, 2.0f, nullptr, false, true },

        { "27-studio-text-entry", [&] (CeruleanProcessor& p) { algorithm (p, A::plate); }, -15.0f, 1.0f,
          [] (CeruleanEditor& e)
          {
              if (auto* s = e.getStudioPanel().findSlider (params::decay))
                  s->showTextEntry();
          }, false, true },

        { "29-studio-bypass", [&] (CeruleanProcessor& p)
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
              e.getView().getDialog().open (o);
          }, false, true },
    };

    for (const auto& scene : scenes)
        runScene (scene, outDir);

    timeRendering (false);
    timeRendering (true);
    return 0;
}
