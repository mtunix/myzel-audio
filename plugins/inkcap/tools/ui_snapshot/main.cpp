/*
    InkcapUiSnapshot: renders the glass face to PNG files for visual review, and runs the editor
    self-test (headless with xvfb-run).

    Usage: InkcapUiSnapshot <output dir> [--leakcheck] [--selftest]
*/

#include "plugin/PluginProcessor.h"
#include "ui/InkcapEditor.h"
#include "ui/UiState.h"

#include "SelfTest.h"

#include "common/testing/SnapshotTool.h"

namespace
{
using namespace myzel::testing;
using inkcap::InkcapEditor;
using inkcap::InkcapProcessor;
using inkcap::Module;
namespace params = inkcap::params;
namespace uistate = inkcap::ui::uistate;

void runFrames (InkcapEditor& editor, int frames, double start = 0.0)
{
    for (int i = 0; i < frames; ++i)
        editor.animateFrame (start + (double) i / 60.0);
}

/** Plays a few seconds of a drum-like programme through the processor, so the telemetry is real. */
void playAudio (InkcapProcessor& p, double seconds)
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;
    p.prepareToPlay (rate, block);
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    int64_t n = 0;

    for (int b = 0; b < (int) (seconds * rate / block); ++b)
    {
        for (int i = 0; i < block; ++i, ++n)
        {
            const auto t = (double) n / rate;
            const auto beat = std::fmod (t * 2.0, 1.0);       // 120 BPM
            const auto kick = std::exp (-beat * 18.0) * std::sin (juce::MathConstants<double>::twoPi * 55.0 * t);
            const auto tone = 0.2 * std::sin (juce::MathConstants<double>::twoPi * 330.0 * t);
            const auto s = (float) (0.5 * kick + tone);
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }
        p.processBlock (buffer, midi);
    }
}

struct Scene
{
    juce::String name;
    std::function<void (InkcapProcessor&)> setup;
    std::function<void (InkcapEditor&)> after;
    float editorScale = 1.0f;
    bool alsoRetina = false;
};

void runScene (const Scene& scene, const juce::File& dir)
{
    // On the heap: the chain's buffers are too large for the stack.
    auto owned = std::make_unique<InkcapProcessor>();
    auto& processor = *owned;
    processor.getUiState().setProperty (uistate::scale, scene.editorScale, nullptr);
    resetParams (processor);

    if (scene.setup != nullptr)
        scene.setup (processor);

    playAudio (processor, 2.2);

    auto editor = std::make_unique<InkcapEditor> (processor);
    editor->setVisible (true);
    runFrames (*editor, 30);

    if (scene.after != nullptr)
        scene.after (*editor);

    runFrames (*editor, 2, 1.0);
    saveScene (*editor, dir, scene.name.toRawUTF8(), scene.alsoRetina);
    editor.reset();
}

void damage (InkcapProcessor& p, float d) { setParam (p, params::damage, d); }

/** The mock's setting: worn tape with a chewing malfunction. */
void thriftStore (InkcapProcessor& p)
{
    damage (p, 62.0f);
    setParam (p, params::chaos, 38.0f);
    setParam (p, params::glitchOn, 1.0f);
    setParam (p, params::glitchType, 1.0f);
    setParam (p, params::glitchAmount, 62.0f);
    setParam (p, params::glitchRate, 4.0f);
    setParam (p, params::hissAmount, 55.0f);
    setParam (p, params::warbleAmount, 40.0f);
    setParam (p, params::crushType, 1.0f);
    setParam (p, params::crushAmount, 70.0f);
    setParam (p, params::bitsAmount, 20.0f);
    setParam (p, params::roomType, 0.0f);
    setParam (p, params::roomAmount, 30.0f);
    p.getSeeds().paste ("thrift");
}

void withPresets (InkcapProcessor&);

/** The mock's setting, saved as the current preset (after any scene-specific change). */
auto thrift (std::function<void (InkcapProcessor&)> more = nullptr)
{
    return [more] (InkcapProcessor& p)
    {
        thriftStore (p);
        if (more != nullptr)
            more (p);
        withPresets (p);
    };
}

/**
    A preset list to browse: a few user presets in the scratch session (each with its own seed).
    The header then names a factory preset, Thrift Store Find, with the scene's own changes on it.
*/
void withPresets (InkcapProcessor& p)
{
    auto& pm = p.getPresets();
    const auto scene = pm.capture();

    struct Sketch { const char* name; const char* word; float damage, chaos; int crushType; };
    static constexpr Sketch sketches[] {
        { "Basement Tape", "basement", 48.0f, 20.0f, 0 },  { "Chewed Mixtape", "chewed", 70.0f, 55.0f, 1 },
        { "Skipping Disc", "skip", 64.0f, 72.0f, 3 },      { "Phone Booth", "booth", 58.0f, 30.0f, 2 },
        { "Seasick Keys", "seasick", 66.0f, 48.0f, 0 },    { "Sampler Dust", "dust", 52.0f, 18.0f, 1 },
        { "Warm Dub Room", "dub", 40.0f, 12.0f, 2 },
    };

    for (const auto& sk : sketches)
    {
        if (pm.userPresetExists (sk.name))
            continue;
        setParam (p, params::damage, sk.damage);
        setParam (p, params::chaos, sk.chaos);
        setParam (p, params::crushType, (float) sk.crushType);
        p.getSeeds().paste (sk.word);
        pm.saveUserPreset (sk.name);
    }

    const auto& entries = pm.getEntries();
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].isFactory && entries[i].name == "Thrift Store Find")
            pm.load ((int) i);
    pm.apply (scene, "scene");
}

void page (InkcapProcessor& p, Module m) { p.getUiState().setProperty (uistate::modulePage, (int) m, nullptr); }

void presets (InkcapEditor& e) { e.getDisplay().setPresetsOpen (true); }

std::vector<Scene> scenes()
{
    std::vector<Scene> s;

    // Each page at Damage 0, 62 and 100.
    for (const auto d : { 0, 62, 100 })
    {
        const auto suffix = juce::String (d).paddedLeft ('0', 3);
        auto name = [&] (const char* base) { return juce::String (base) + "-d" + suffix; };
        const auto dd = (float) d;

        s.push_back ({ name ("01-main"), thrift ([dd] (InkcapProcessor& p) { damage (p, dd); }), nullptr, 1.0f, d == 62 });
        s.push_back ({ name ("02-glitch-page"), thrift ([dd] (InkcapProcessor& p) { damage (p, dd); page (p, Module::glitch); }),
                       nullptr, 1.0f, d == 62 });
        s.push_back ({ name ("03-presets"), thrift ([dd] (InkcapProcessor& p) { damage (p, dd); }), presets, 1.0f, false });
    }

    s.push_back ({ "10-default", nullptr, nullptr, 1.0f, true });
    s.push_back ({ "11-warble-page", thrift ([] (InkcapProcessor& p) { setParam (p, params::warbleSync, 1.0f); page (p, Module::warble); }), nullptr });
    s.push_back ({ "12-crush-page", thrift ([] (InkcapProcessor& p) { setParam (p, params::crushTone, -40.0f); page (p, Module::crush); }), nullptr });
    s.push_back ({ "13-bits-page", thrift ([] (InkcapProcessor& p) { setParam (p, params::bitsOn, 1.0f); page (p, Module::bits); }), nullptr });
    s.push_back ({ "14-hiss-page", thrift ([] (InkcapProcessor& p) { setParam (p, params::hissFollow, 60.0f); page (p, Module::hiss); }), nullptr });
    s.push_back ({ "15-room-page", thrift ([] (InkcapProcessor& p) { setParam (p, params::roomOn, 1.0f); page (p, Module::room); }), nullptr });
    s.push_back ({ "16-bypass", thrift ([] (InkcapProcessor& p) { setParam (p, params::bypass, 1.0f); }), nullptr });
    s.push_back ({ "17-locks", [] (InkcapProcessor& p)
                   {
                       thriftStore (p);
                       p.getPresets().setMixLocked (true);
                       p.setSeedLocked (true);
                       setParam (p, params::mix, 70.0f);
                   }, nullptr });
    s.push_back ({ "18-all-off", [] (InkcapProcessor& p)
                   {
                       thriftStore (p);
                       for (const auto* id : params::onIds)
                           setParam (p, id, 0.0f);
                   }, nullptr });
    s.push_back ({ "19-save-dialog", thrift(), [] (InkcapEditor& e)
                   {
                       presets (e);
                       myzel::ui::InlineDialog::Options o;
                       o.title = "Save preset";
                       o.message = "Name your preset. It keeps the seed, and is stored as an XML file in your user preset folder.";
                       o.withTextField = true;
                       o.initialText = "Thrift Store Choir";
                       o.confirmLabel = "Save";
                       e.getDialog().open (o);
                   } });
    s.push_back ({ "20-typed-entry", thrift ([] (InkcapProcessor& p) { page (p, Module::glitch); }), [] (InkcapEditor& e)
                   {
                       if (auto* row = e.getDisplay().getModulePage().findRow (params::glitchLength))
                           row->openTextEntry();
                   } });
    s.push_back ({ "21-hint", thrift(), [] (InkcapEditor& e) { e.getController().setHint ("mix 100 %"); } });
    s.push_back ({ "22-flash", thrift(), [] (InkcapEditor& e) { e.getController().flash ("seed 8719-8E02 copied"); } });
    s.push_back ({ "30-scale-75", thrift(), nullptr, 0.75f });
    s.push_back ({ "31-scale-200", thrift(), nullptr, 2.0f });
    return s;
}

void timeRendering()
{
    auto owned = std::make_unique<InkcapProcessor>();
    auto& processor = *owned;
    thriftStore (processor);
    playAudio (processor, 1.0);
    auto editor = std::make_unique<InkcapEditor> (processor);
    editor->setVisible (true);
    timeRepaints (*editor);
    timeFrames ([&] (int i) { editor->animateFrame ((double) i / 60.0); });
}

} // namespace

int main (int argc, char* argv[])
{
    return runSnapshotTool (argc, argv, {
        .pluginName = "Inkcap",
        .makeProcessor = [] { return std::make_unique<InkcapProcessor>(); },
        .runSelfTest = runUiSelfTest,
        .renderScenes = [] (const juce::File& dir)
        {
            for (const auto& scene : scenes())
                runScene (scene, dir);
        },
        .timeRendering = timeRendering,
    });
}
