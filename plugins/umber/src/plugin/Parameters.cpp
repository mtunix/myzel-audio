#include "plugin/Parameters.h"

namespace umber::params
{

namespace
{
    juce::ParameterID pid (const char* id) { return { id, versionHint }; }
    juce::ParameterID pid2 (const char* id) { return { id, versionHintV2 }; }

    /** "Flat" at the centre, otherwise the side's name and the amount (e.g. "Highs 3.5"). */
    juce::AudioParameterFloatAttributes bipolarText (const char* centre, const char* negative, const char* positive)
    {
        const juce::String c (centre), n (negative), p (positive);

        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([c, n, p] (float v, int)
            {
                if (std::abs (v) < 0.05f)
                    return c;
                return (v < 0.0f ? n : p) + " " + juce::String (std::abs (v), 1);
            })
            .withValueFromStringFunction ([c, n, p] (const juce::String& s)
            {
                const auto t = s.trim().toLowerCase();
                if (t == c.toLowerCase())
                    return 0.0f;
                const auto magnitude = std::abs (t.retainCharacters ("0123456789.").getFloatValue());
                if (t.startsWith (n.toLowerCase()) || t.startsWith ("-"))
                    return -magnitude;
                return magnitude;
            });
    }

    using FloatAttrs = juce::AudioParameterFloatAttributes;

    juce::String formatHz (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    float parseHz (const juce::String& text, float offValue)
    {
        const auto t = text.trim().toLowerCase();

        if (t == "off")
            return offValue;

        const auto value = t.getFloatValue();
        return t.contains ("k") ? value * 1000.0f : value;
    }

    juce::NormalisableRange<float> frequencyRange (float min, float max, float centre)
    {
        juce::NormalisableRange<float> range { min, max };
        range.setSkewForCentre (centre);
        return range;
    }
}

juce::StringArray modelNames()
{
    return { "Reel", "Desk", "Class A", "Valve", "Fuzz" };
}

juce::StringArray stereoModeNames()
{
    return { "Stereo", "Mid", "Side" };
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (drive), "Drive",
        NormalisableRange<float> { 0.0f, driveMax, 0.01f }, 3.0f,
        FloatAttrs().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));

    layout.add (std::make_unique<AudioParameterChoice> (
        pid (model), "Model", modelNames(), static_cast<int> (Model::classA)));

    layout.add (std::make_unique<AudioParameterBool> (pid (slam), "Slam", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (tone), "Tone",
        NormalisableRange<float> { -toneRange, toneRange, 0.01f }, 0.0f,
        FloatAttrs().withStringFromValueFunction ([] (float v, int)
        {
            if (std::abs (v) < 0.05f)
                return String ("Flat");
            return (v < 0.0f ? "Dark " : "Bright ") + String (std::abs (v), 1);
        })
        .withValueFromStringFunction ([] (const String& s)
        {
            const auto t = s.trim().toLowerCase();
            if (t == "flat")
                return 0.0f;
            const auto magnitude = std::abs (t.retainCharacters ("0123456789.").getFloatValue());
            if (t.startsWith ("dark") || t.startsWith ("-"))
                return -magnitude;
            return magnitude;
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (lowCut), "Low Cut",
        frequencyRange (lowCutMinHz, lowCutMaxHz, 100.0f), lowCutMinHz,
        FloatAttrs().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return isLowCutOff (v) ? String ("Off") : formatHz (v); })
            .withValueFromStringFunction ([] (const String& s) { return parseHz (s, lowCutMinHz); })));

    layout.add (std::make_unique<AudioParameterBool> (pid (weight), "Weight", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (highCut), "High Cut",
        frequencyRange (highCutMinHz, highCutMaxHz, 6000.0f), highCutMaxHz,
        FloatAttrs().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return isHighCutOff (v) ? String ("Off") : formatHz (v); })
            .withValueFromStringFunction ([] (const String& s) { return parseHz (s, highCutMaxHz); })));

    layout.add (std::make_unique<AudioParameterBool> (pid (autoGain), "Auto Gain", true));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (mix), "Mix",
        NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f,
        FloatAttrs().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v)) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (output), "Output",
        NormalisableRange<float> { -outputRangeDb, outputRangeDb, 0.01f }, 0.0f,
        FloatAttrs().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int)
            {
                return (v > 0.005f ? "+" : "") + String (v, 1) + " dB";
            })));

    layout.add (std::make_unique<AudioParameterChoice> (
        pid (quality), "Quality",
        StringArray { "Eco (2x)", "Standard (4x)", "High (8x)" },
        static_cast<int> (Quality::standard)));

    layout.add (std::make_unique<AudioParameterChoice> (
        pid (renderQuality), "Render Quality",
        StringArray { "Same as realtime", "High (8x)", "Ultra (16x)" },
        static_cast<int> (RenderQuality::ultra)));

    layout.add (std::make_unique<AudioParameterBool> (pid (bypass), "Bypass", false));

    layout.add (std::make_unique<AudioParameterBool> (pid2 (delta), "Delta", false));

    layout.add (std::make_unique<AudioParameterChoice> (
        pid2 (stereoMode), "Stereo Mode", stereoModeNames(), static_cast<int> (StereoMode::stereo)));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid2 (emphasis), "Emphasis",
        NormalisableRange<float> { -emphasisRange, emphasisRange, 0.01f }, 0.0f,
        bipolarText ("Flat", "Lows", "Highs")));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid2 (bias), "Bias",
        NormalisableRange<float> { -biasRange, biasRange, 0.01f }, 0.0f,
        bipolarText ("Nominal", "Cold", "Hot")));

    return layout;
}

} // namespace umber::params

namespace umber
{

namespace
{
    std::atomic<float>* raw (juce::AudioProcessorValueTreeState& apvts, const char* id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    }
}

ParameterRefs::ParameterRefs (juce::AudioProcessorValueTreeState& apvts)
    : drive         (raw (apvts, params::drive)),
      model         (raw (apvts, params::model)),
      slam          (raw (apvts, params::slam)),
      tone          (raw (apvts, params::tone)),
      lowCut        (raw (apvts, params::lowCut)),
      weight        (raw (apvts, params::weight)),
      highCut       (raw (apvts, params::highCut)),
      autoGain      (raw (apvts, params::autoGain)),
      mix           (raw (apvts, params::mix)),
      output        (raw (apvts, params::output)),
      quality       (raw (apvts, params::quality)),
      renderQuality (raw (apvts, params::renderQuality)),
      delta         (raw (apvts, params::delta)),
      stereoMode    (raw (apvts, params::stereoMode)),
      emphasis      (raw (apvts, params::emphasis)),
      bias          (raw (apvts, params::bias)),
      bypass        (dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (params::bypass)))
{
    jassert (bypass != nullptr);
}

} // namespace umber
