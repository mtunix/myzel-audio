#pragma once

#include "common/testing/ScratchSession.h"
#include "common/testing/UiTestKit.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace myzel::testing
{

/** What a plugin's snapshot tool brings to runSnapshotTool(); the rest of main() is shared. */
struct SnapshotToolSpec
{
    juce::String pluginName;                                                // "Umber": names the scratch files
    std::function<std::unique_ptr<juce::AudioProcessor>()> makeProcessor;   // --leakcheck
    std::function<int (const ScratchSession&)> runSelfTest;                 // --selftest, returns the failures
    std::function<void (const juce::File& outDir)> renderScenes;
    std::function<void()> timeRendering;
};

/**
    The snapshot tool's main(). Usage: <tool> [<output dir>] [--leakcheck] [--selftest]

    Every mode runs inside a ScratchSession, so the user's presets and default view are never
    touched. --selftest returns 1 on any failure; the other modes return 0.
*/
inline int runSnapshotTool (int argc, char* argv[], const SnapshotToolSpec& spec)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const ScratchSession scratch (spec.pluginName);

    juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("ui-snapshots");
    bool leakCheck = false;

    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--selftest")
            return spec.runSelfTest (scratch) == 0 ? 0 : 1;

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
        auto processor = spec.makeProcessor();

        for (int i = 0; i < 8; ++i)
        {
            auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor->createEditorAndMakeActive());
            editor->setVisible (true);
            juce::ignoreUnused (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f + (float) (i % 2)));
            processor->editorBeingDeleted (editor.get());
            editor.reset();
        }

        std::printf ("leak check: opened and closed the editor 8 times\n");
        return 0;
    }

    spec.renderScenes (outDir);
    spec.timeRendering();
    return 0;
}

/** Writes <name>.png at 1x, and <name>@2x.png too when asked. */
inline void saveScene (juce::Component& editor, const juce::File& dir, const char* name, bool alsoRetina)
{
    saveSnapshot (editor, dir.getChildFile (juce::String (name) + ".png"), 1.0f);

    if (alsoRetina)
        saveSnapshot (editor, dir.getChildFile (juce::String (name) + "@2x.png"), 2.0f);
}

/** Prints the cost of a full repaint, cold (building the caches) and warm, at 1x and 2x. */
inline void timeRepaints (juce::Component& editor)
{
    auto measure = [&] (const char* label, float scale, int runs)
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (int i = 0; i < runs; ++i)
            juce::ignoreUnused (editor.createComponentSnapshot (editor.getLocalBounds(), true, scale));

        std::printf ("%s: %.2f ms per full repaint\n", label, (juce::Time::getMillisecondCounterHiRes() - start) / runs);
    };

    measure ("cold 1x (builds caches)", 1.0f, 1);
    measure ("warm 1x", 1.0f, 10);
    measure ("cold 2x (builds caches)", 2.0f, 1);
    measure ("warm 2x", 2.0f, 10);
}

/** Prints the frame clock's cost alone (repaints are only scheduled): `frame (i)` runs frame i of 600. */
inline void timeFrames (const std::function<void (int)>& frame)
{
    constexpr int count = 600;
    const auto start = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < count; ++i)
        frame (i);

    std::printf ("animateFrame: %.4f ms per frame\n", (juce::Time::getMillisecondCounterHiRes() - start) / count);
}

} // namespace myzel::testing
