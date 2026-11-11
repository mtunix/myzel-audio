#pragma once

#include "common/plugin/PresetManager.h"
#include "common/ui/Knob.h"
#include "common/ui/StudioControls.h"

#include <cstdio>

/**
    Helpers for the plugins' UI tools: the editor self-tests (--selftest) and the snapshot
    renderer. Header-only; each tool includes it next to its own scenes and checks.
*/
namespace myzel::testing
{

/** Failed checks so far (the self-test's exit code). */
inline int failures = 0;

/** Prints "[ ok ]" or "[FAIL]" with the description, and counts failures. */
inline void check (bool condition, const char* what)
{
    std::printf ("  [%s] %s\n", condition ? " ok " : "FAIL", what);

    if (! condition)
        ++failures;
}

inline juce::MouseEvent makeEvent (juce::Component& c, juce::Point<float> pos, juce::ModifierKeys mods, int clicks = 1,
                                   bool dragged = false)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             &c, &c, now, pos, now, clicks, dragged };
}

/** A parameter's value in its own units. */
inline float valueOf (plugin::PresetHost& host, const char* id)
{
    auto* param = host.getState().getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

/** Sets a parameter in its own units, as a host would (no gesture). */
inline void setParam (plugin::PresetHost& host, const char* id, float value)
{
    if (auto* param = host.getState().getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

/** Every parameter back to its default. */
inline void resetParams (plugin::PresetHost& host)
{
    for (auto* param : host.getState().processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
}

/** Records host gestures, to check that every edit is wrapped in begin/end. */
struct GestureRecorder final : juce::AudioProcessorParameter::Listener
{
    int begins = 0, ends = 0, changes = 0;
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }
};

/** A mouse drag over a knob, from its centre. */
inline void drag (ui::Knob& k, float dy, juce::ModifierKeys mods = {})
{
    const auto c = k.getLocalBounds().toFloat().getCentre();
    k.mouseDown (makeEvent (k, c, {}));
    k.mouseDrag (makeEvent (k, c.translated (0.0f, dy), mods, 1, true));
    k.mouseUp (makeEvent (k, c.translated (0.0f, dy), {}, 1, true));
}

/** A mouse drag over a studio slider, from the middle of its bar. */
inline void dragSlider (ui::StudioSlider& s, juce::Point<float> delta, juce::ModifierKeys mods = {})
{
    const auto c = s.barArea().getCentre();
    s.mouseDown (makeEvent (s, c, {}));
    s.mouseDrag (makeEvent (s, c + delta, mods, 1, true));
    s.mouseUp (makeEvent (s, c + delta, {}, 1, true));
}

/** Renders a component to a PNG file. */
inline void saveSnapshot (juce::Component& c, const juce::File& file, float scale)
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

} // namespace myzel::testing
