/*
    UmberUiSnapshot: renders the editor to PNG files for visual review (headless with xvfb-run).

    Usage: UmberUiSnapshot <output dir> [--leakcheck] [--selftest]
*/

#include "plugin/PluginProcessor.h"
#include "ui/UmberEditor.h"

#include "SelfTest.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
using umber::UmberEditor;
using umber::UmberProcessor;
namespace params = umber::params;

struct Levels
{
    float driveRms = 0.0f, outputRms = 0.0f, outputPeak = 0.0f;
};

void setParam (UmberProcessor& p, const char* id, float value)
{
    if (auto* param = p.getState().getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void resetParams (UmberProcessor& p)
{
    for (auto* param : p.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
}

void runFrames (UmberProcessor& p, UmberEditor& editor, Levels levels, int frames)
{
    auto& meters = p.getMeters();

    for (int i = 0; i < frames; ++i)
    {
        meters.driveRms.store (levels.driveRms);
        meters.outputRms.store (levels.outputRms);
        meters.outputPeak.store (levels.outputPeak);
        editor.getMainPanel().animateFrame ((double) i / 60.0);
    }
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
    std::function<void (UmberProcessor&)> setup;
    Levels levels;
    float editorScale = 1.0f;
    std::function<void (UmberEditor&)> after;
    bool alsoRetina = false;
};

void runScene (const Scene& scene, const juce::File& dir)
{
    UmberProcessor processor;
    processor.getUiState().setProperty ("scale", scene.editorScale, nullptr);
    resetParams (processor);

    if (scene.setup != nullptr)
        scene.setup (processor);

    auto editor = std::make_unique<UmberEditor> (processor);
    editor->setVisible (true);

    runFrames (processor, *editor, scene.levels, 120);

    if (scene.after != nullptr)
        scene.after (*editor);

    save (*editor, dir.getChildFile (juce::String (scene.name) + ".png"), 1.0f);

    if (scene.alsoRetina)
        save (*editor, dir.getChildFile (juce::String (scene.name) + "@2x.png"), 2.0f);

    editor.reset();
}

void timeRendering (const juce::File&)
{
    UmberProcessor processor;
    auto editor = std::make_unique<UmberEditor> (processor);
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

    // Frame clock cost alone (lamp/meter state; repaints are only scheduled).
    {
        auto& meters = processor.getMeters();
        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int i = 0; i < 600; ++i)
        {
            meters.driveRms.store (0.5f + 0.4f * std::sin ((float) i * 0.1f));
            meters.outputRms.store (0.1f);
            editor->getMainPanel().animateFrame ((double) i / 60.0);
        }

        std::printf ("animateFrame: %.4f ms per frame\n", (juce::Time::getMillisecondCounterHiRes() - start) / 600.0);
    }
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

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
        UmberProcessor processor;

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
              p.getUiState().setProperty ("meterMode", 1, nullptr);
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
              e.getMainPanel().getDialog().open (o);
          }, false },

    };

    for (const auto& scene : scenes)
        runScene (scene, outDir);

    timeRendering (outDir);
    return 0;
}
