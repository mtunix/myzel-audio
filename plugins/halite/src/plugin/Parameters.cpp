#include "plugin/Parameters.h"

#include <cmath>
#include <functional>
#include <optional>

namespace halite
{

namespace params
{

namespace
{
    using namespace juce;
    using Layout = AudioProcessorValueTreeState::ParameterLayout;
    using ToText = std::function<String (float)>;
    using FromText = std::function<std::optional<float> (const String&)>;

    ParameterID pid (const char* id) { return { id, versionHint }; }

    /** Equal knob travel per octave (per doubling, for times), as on the display's spectrum. */
    NormalisableRange<float> logRange (float min, float max)
    {
        return { min, max,
                 [] (float start, float end, float proportion)
                 {
                     if (proportion <= 0.0f)
                         return start;
                     if (proportion >= 1.0f)
                         return end;
                     return start * std::exp (std::log (end / start) * proportion);
                 },
                 [] (float start, float end, float value)
                 {
                     return std::log (jlimit (start, end, value) / start) / std::log (end / start);
                 },
                 [] (float start, float end, float value) { return jlimit (start, end, value); } };
    }

    NormalisableRange<float> linearRange (float min, float max) { return { min, max, 0.01f }; }

    /** An optional sign (ASCII or U+2212, as the docs print it), digits with one decimal point or
        comma, then only a unit. A comma before exactly three digits ("4,500") could be a thousands
        separator, so it does not parse: text that could be misread leaves the value where it is. */
    std::optional<float> numberIn (const String& typed)
    {
        const auto text = typed.replaceCharacter ((juce_wchar) 0x2212, '-');
        const auto length = text.length();
        auto i = 0;
        String number;

        if (i < length && (text[i] == '+' || text[i] == '-'))
            number << (text[i++] == '-' ? "-" : "");

        auto digits = 0, decimals = 0;
        juce_wchar separator = 0;

        for (; i < length; ++i)
        {
            const auto c = text[i];

            if (CharacterFunctions::isDigit (c))
            {
                number << String::charToString (c);
                ++digits;
                decimals += separator != 0 ? 1 : 0;
            }
            else if ((c == '.' || c == ',') && separator == 0)
            {
                separator = c;
                number << ".";
            }
            else
            {
                break;
            }
        }

        if (digits == 0 || (separator == ',' && decimals == 3) || text.substring (i).containsAnyOf ("0123456789+-.,"))
            return std::nullopt;

        return number.getFloatValue();
    }

    std::optional<float> hzIn (const String& text)
    {
        const auto value = numberIn (text);
        if (value && text.containsChar ('k'))
            return *value * 1000.0f;
        return value;
    }

    // The read-outs pick their precision from the value as rounded, not as stored, so that what
    // they show parses back to the same text ("9.996 kHz" reads "10.0 kHz", not "10.00 kHz").
    float roundedTo (float value, int decimals)
    {
        const auto scale = std::pow (10.0f, (float) decimals);
        return std::round (value * scale) / scale;
    }

    String hzText (float hz)
    {
        if (roundToInt (hz) < 1000)
            return String (roundToInt (hz)) + " Hz";
        const auto khz = hz / 1000.0f;
        return String (khz, roundedTo (khz, 2) < 10.0f ? 2 : 1) + " kHz";
    }

    /** Never "0.0" for a value that is not exactly 0: Output is bit-transparent only there. */
    String dbText (float db, const char* unit, bool showSign)
    {
        if (exactlyEqual (db, 0.0f))
            return "0.0 " + String (unit);
        const auto decimals = exactlyEqual (roundedTo (db, 1), 0.0f) ? 2 : 1;
        return (showSign && db > 0.0f ? "+" : "") + String (db, decimals) + " " + unit;
    }

    String msText (float ms) { return String (ms, roundedTo (ms, 1) < 10.0f ? 1 : 0) + " ms"; }

    /** Typed text that does not parse leaves the parameter where it is. */
    void addFloat (Layout& layout, const char* id, const char* name, NormalisableRange<float> range,
                   float defaultValue, const char* label, ToText toText, FromText fromText)
    {
        const auto self = std::make_shared<const AudioParameterFloat*>();
        auto parameter = std::make_unique<AudioParameterFloat> (
            pid (id), name, range, defaultValue,
            AudioParameterFloatAttributes()
                .withLabel (label)
                .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                .withValueFromStringFunction ([fromText, self] (const String& s)
                {
                    if (const auto v = fromText (s.trim().toLowerCase()))
                        return *v;
                    return (*self)->get();
                }));
        *self = parameter.get();
        layout.add (std::move (parameter));
    }

    void addChoice (Layout& layout, const char* id, const char* name, const StringArray& choices, int defaultIndex)
    {
        layout.add (std::make_unique<AudioParameterChoice> (pid (id), name, choices, defaultIndex));
    }

    void addBool (Layout& layout, const char* id, const char* name, bool defaultValue,
                  const char* offText = "Off", const char* onText = "On")
    {
        const String off (offText), on (onText);
        layout.add (std::make_unique<AudioParameterBool> (
            pid (id), name, defaultValue,
            AudioParameterBoolAttributes()
                .withStringFromValueFunction ([off, on] (bool v, int) { return v ? on : off; })
                .withValueFromStringFunction ([off, on] (const String& s)
                {
                    const auto t = s.trim().toLowerCase();
                    return t == on.toLowerCase() || t == "on" || t == "1" || t == "true";
                })));
    }

    void addDb (Layout& layout, const char* id, const char* name, float min, float max, float defaultValue,
                const char* unit = "dB", bool showSign = true)
    {
        addFloat (layout, id, name, linearRange (min, max), defaultValue, unit,
                  [unit, showSign] (float v) { return dbText (v, unit, showSign); }, numberIn);
    }

    /** A reduction amount: unsigned, it only ever pulls down. */
    void addRange (Layout& layout, const char* id, const char* name, float max, float defaultValue)
    {
        addDb (layout, id, name, 0.0f, max, defaultValue, "dB", false);
    }

    void addHz (Layout& layout, const char* id, const char* name, float min, float max, float defaultValue)
    {
        addFloat (layout, id, name, logRange (min, max), defaultValue, "Hz", hzText, hzIn);
    }

    void addMs (Layout& layout, const char* id, const char* name, NormalisableRange<float> range, float defaultValue,
                ToText toText = msText)
    {
        addFloat (layout, id, name, range, defaultValue, "ms", std::move (toText), numberIn);
    }
}

juce::StringArray shapeNames()      { return { "Split", "Spectral", "Wide" }; }
juce::StringArray detectionNames()  { return { "Voice", "Allround" }; }
juce::StringArray stereoModeNames() { return { "Linked", "Unlinked", "Mid", "Side" }; }
juce::StringArray keySourceNames()  { return { "Internal", "External" }; }
juce::StringArray listenNames()     { return { "Output", "Removed", "Ess removed", "Harsh removed", "Ess trigger", "Harsh trigger" }; }

const juce::StringArray& allParameterIds()
{
    static const juce::StringArray ids {
        threshold, thresholdAbs, range, detectLow, detectHigh, shape, detection, focus, speedAuto,
        release, lookahead, stereo, key, keyHpf, output, bypass, listen, harshOn, harshThreshold,
        harshThresholdAbs, harshRange, harshFreq, harshWidth, harshSpeedAuto, harshRelease
    };
    return ids;
}

const juce::StringArray& soundParameterIds()
{
    static const juce::StringArray ids = []
    {
        auto s = allParameterIds();
        s.removeString (bypass);
        s.removeString (listen);
        return s;
    }();
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;

    Layout layout;

    addDb (layout, threshold, "Threshold", thresholdMinDb, thresholdMaxDb, thresholdDefaultDb);
    addDb (layout, thresholdAbs, "Threshold (Absolute)", absThresholdMinDb, 0.0f, absThresholdDefaultDb, "dBFS");
    addRange (layout, range, "Range", rangeMaxDb, rangeDefaultDb);
    addHz (layout, detectLow, "Detect Low", detectMinHz, detectMaxHz, detectLowDefaultHz);
    addHz (layout, detectHigh, "Detect High", detectMinHz, detectMaxHz, detectHighDefaultHz);
    addChoice (layout, shape, "Shape", shapeNames(), static_cast<int> (Shape::split));
    addChoice (layout, detection, "Detection", detectionNames(), static_cast<int> (Detection::voice));

    addFloat (layout, focus, "Focus", linearRange (0.0f, 100.0f), focusDefault, "%",
              [] (float v) { return String (roundToInt (v)) + " %"; }, numberIn);

    addBool (layout, speedAuto, "Speed Auto", true, "Manual", "Auto");
    addMs (layout, release, "Release", logRange (releaseMinMs, releaseMaxMs), releaseDefaultMs);
    addMs (layout, lookahead, "Look-ahead", linearRange (0.0f, lookaheadMaxMs), lookaheadDefaultMs,
           [] (float v) { return String (v, 1) + " ms"; });

    addChoice (layout, stereo, "Stereo", stereoModeNames(), static_cast<int> (StereoMode::linked));
    addChoice (layout, key, "Key", keySourceNames(), static_cast<int> (KeySource::internal));

    addFloat (layout, keyHpf, "Key HPF", logRange (keyHpfMinHz, keyHpfMaxHz), keyHpfMinHz, "Hz",
              [] (float v) { return isKeyHpfOff (v) ? String ("Off") : hzText (v); },
              [] (const String& s) { return s == "off" ? std::optional<float> (keyHpfMinHz) : hzIn (s); });

    addDb (layout, output, "Output", -outputRangeDb, outputRangeDb, 0.0f);
    addBool (layout, bypass, "Bypass", false);
    addChoice (layout, listen, "Listen", listenNames(), static_cast<int> (Listen::output));

    addBool (layout, harshOn, "Harsh", false);
    addDb (layout, harshThreshold, "Harsh Threshold", harshThresholdMinDb, harshThresholdMaxDb, harshThresholdDefaultDb);
    addDb (layout, harshThresholdAbs, "Harsh Threshold (Absolute)", absThresholdMinDb, 0.0f, harshAbsThresholdDefaultDb, "dBFS");
    addRange (layout, harshRange, "Harsh Range", harshRangeMaxDb, harshRangeDefaultDb);
    addHz (layout, harshFreq, "Harsh Freq", harshFreqMinHz, harshFreqMaxHz, harshFreqDefaultHz);

    // Octaves are already logarithmic in frequency: a linear knob.
    addFloat (layout, harshWidth, "Harsh Width", linearRange (harshWidthMinOct, harshWidthMaxOct), harshWidthDefaultOct, "oct",
              [] (float v) { return String (v, 2) + " oct"; }, numberIn);

    addBool (layout, harshSpeedAuto, "Harsh Speed Auto", true, "Manual", "Auto");
    addMs (layout, harshRelease, "Harsh Release", logRange (harshReleaseMinMs, harshReleaseMaxMs), harshReleaseDefaultMs);

    return layout;
}

} // namespace params

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
    : threshold         (raw (apvts, params::threshold)),
      thresholdAbs      (raw (apvts, params::thresholdAbs)),
      range             (raw (apvts, params::range)),
      detectLow         (raw (apvts, params::detectLow)),
      detectHigh        (raw (apvts, params::detectHigh)),
      shape             (raw (apvts, params::shape)),
      detection         (raw (apvts, params::detection)),
      focus             (raw (apvts, params::focus)),
      speedAuto         (raw (apvts, params::speedAuto)),
      release           (raw (apvts, params::release)),
      lookahead         (raw (apvts, params::lookahead)),
      stereo            (raw (apvts, params::stereo)),
      key               (raw (apvts, params::key)),
      keyHpf            (raw (apvts, params::keyHpf)),
      output            (raw (apvts, params::output)),
      listen            (raw (apvts, params::listen)),
      harshOn           (raw (apvts, params::harshOn)),
      harshThreshold    (raw (apvts, params::harshThreshold)),
      harshThresholdAbs (raw (apvts, params::harshThresholdAbs)),
      harshRange        (raw (apvts, params::harshRange)),
      harshFreq         (raw (apvts, params::harshFreq)),
      harshWidth        (raw (apvts, params::harshWidth)),
      harshSpeedAuto    (raw (apvts, params::harshSpeedAuto)),
      harshRelease      (raw (apvts, params::harshRelease)),
      bypass            (dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (params::bypass)))
{
    jassert (bypass != nullptr);
}

} // namespace halite
