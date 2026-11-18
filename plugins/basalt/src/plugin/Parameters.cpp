#include "plugin/Parameters.h"

#include <cmath>
#include <functional>
#include <optional>

namespace basalt
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

    /** Equal knob travel per octave (per doubling, for times and Q). */
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

    /** 1 … 20 logarithmic, then the step to ∞ (Parameters.h, ratioInfinity). */
    NormalisableRange<float> ratioRange()
    {
        const auto snap = [] (float, float, float value)
        {
            if (value <= ratioMin)
                return ratioMin;
            if (value <= ratioMax)
                return value;
            return isRatioInfinite (value) ? ratioInfinity : ratioMax;
        };

        return { ratioMin, ratioInfinity,
                 [snap] (float, float, float proportion)
                 {
                     if (proportion <= 0.0f)
                         return ratioMin;
                     if (proportion >= ratioLogTravel)
                         return snap (0.0f, 0.0f, ratioMax + (ratioInfinity - ratioMax) * (proportion - ratioLogTravel) / (1.0f - ratioLogTravel));
                     return std::exp (std::log (ratioMax) * proportion / ratioLogTravel);
                 },
                 [] (float, float, float value)
                 {
                     if (value <= ratioMin)
                         return 0.0f;
                     if (value <= ratioMax)
                         return ratioLogTravel * std::log (value) / std::log (ratioMax);
                     return ratioLogTravel + (1.0f - ratioLogTravel) * (jmin (value, ratioInfinity) - ratioMax) / (ratioInfinity - ratioMax);
                 },
                 snap };
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

    /** Milliseconds, or the unit typed: "µs" or "us", "ms", "s". */
    std::optional<float> msIn (const String& text)
    {
        const auto value = numberIn (text);
        if (! value)
            return value;
        // The micro sign or the Greek mu, as a keyboard gives either.
        const auto t = text.trimEnd();
        if (t.endsWith ("us") || t.endsWith (String::charToString ((juce_wchar) 0x00b5) + "s")
            || t.endsWith (String::charToString ((juce_wchar) 0x03bc) + "s"))
            return *value * 0.001f;
        if (t.endsWith ("ms"))
            return value;
        if (t.endsWithChar ('s'))
            return *value * 1000.0f;
        return value;
    }

    const String infinity = String::charToString ((juce_wchar) 0x221e);

    /** "4", "4:1", "4 : 1", or ∞ as "∞" or "inf". Above 20 the nearer of 20 and ∞ by slope (1 / ratio). */
    std::optional<float> ratioIn (const String& typed)
    {
        auto text = typed.trim();
        if (text.startsWith (infinity) || text.startsWith ("inf"))
            return ratioInfinity;

        if (text.containsChar (':'))
        {
            if (text.fromFirstOccurrenceOf (":", false, false).trim() != "1")
                return std::nullopt;
            text = text.upToFirstOccurrenceOf (":", false, false).trimEnd();
        }

        const auto value = numberIn (text);
        if (! value || *value <= ratioMax)
            return value;
        return *value < 2.0f * ratioMax ? ratioMax : ratioInfinity;
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

    /** Never "0.0" for a value that is not exactly 0: Output and Gain are bit-transparent only there. */
    String dbText (float db, const char* unit, bool showSign)
    {
        if (exactlyEqual (db, 0.0f))
            return "0.0 " + String (unit);
        const auto decimals = exactlyEqual (roundedTo (db, 1), 0.0f) ? 2 : 1;
        return (showSign && db > 0.0f ? "+" : "") + String (db, decimals) + " " + unit;
    }

    /** 20 µs … 999 µs, 1.0 … 9.9 ms, 10 … 999 ms, then seconds. */
    String timeText (float ms)
    {
        if (roundToInt (ms * 1000.0f) < 1000)
            return String (roundToInt (ms * 1000.0f)) + " " + String::charToString ((juce_wchar) 0x00b5) + "s";
        if (roundedTo (ms, 1) < 10.0f)
            return String (ms, 1) + " ms";
        if (roundToInt (ms) < 1000)
            return String (roundToInt (ms)) + " ms";
        return String (ms / 1000.0f, 2) + " s";
    }

    /** Never "1.00:1" for a ratio that is not exactly 1: the gain path is bit-transparent only there. */
    String ratioText (float stored)
    {
        if (isRatioInfinite (stored))
            return infinity + ":1";
        if (exactlyEqual (stored, ratioMin))
            return "1.00:1";

        // The range has no step, so a ratio just above 1 can need the float's full resolution. The
        // printed text decides, not roundedTo(): the two can round a half differently.
        auto decimals = roundedTo (stored, 2) < 10.0f ? 2 : 1;
        auto text = String (stored, decimals);
        while (decimals < 7 && exactlyEqual (text.getFloatValue(), ratioMin))
            text = String (stored, ++decimals);
        return text + ":1";
    }

    /** Whole percent, but never "0 %" or "100 %" for a value that is not exactly that: Mix and
        Link mean something exact at both ends (PLAN.md, Signal flow and the Stereo gate). */
    String percentText (float v)
    {
        const auto isEnd = [] (float x) { return exactlyEqual (x, 0.0f) || exactlyEqual (x, 100.0f); };

        if (isEnd (v) || ! isEnd ((float) roundToInt (v)))
            return String (roundToInt (v)) + " %";
        return String (v, isEnd (roundedTo (v, 1)) ? 2 : 1) + " %";
    }

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

    /** Its own words, or on/off, 1/0, true/false; anything else leaves the switch where it is. */
    void addBool (Layout& layout, const char* id, const char* name, bool defaultValue,
                  const char* offText = "Off", const char* onText = "On")
    {
        const auto on = StringArray { String (onText).toLowerCase(), "on", "1", "true" };
        const auto off = StringArray { String (offText).toLowerCase(), "off", "0", "false" };
        const auto self = std::make_shared<const AudioParameterBool*>();
        auto parameter = std::make_unique<AudioParameterBool> (
            pid (id), name, defaultValue,
            AudioParameterBoolAttributes()
                .withStringFromValueFunction ([offWord = String (offText), onWord = String (onText)] (bool v, int)
                {
                    return v ? onWord : offWord;
                })
                .withValueFromStringFunction ([on, off, self] (const String& s)
                {
                    const auto t = s.trim().toLowerCase();
                    if (on.contains (t))
                        return true;
                    if (off.contains (t))
                        return false;
                    return (*self)->get();
                }));
        *self = parameter.get();
        layout.add (std::move (parameter));
    }

    void addDb (Layout& layout, const char* id, const char* name, float min, float max, float defaultValue,
                const char* unit = "dB", bool showSign = true)
    {
        addFloat (layout, id, name, linearRange (min, max), defaultValue, unit,
                  [unit, showSign] (float v) { return dbText (v, unit, showSign); }, numberIn);
    }

    /** An amount that only ever goes one way (Knee, Range, Drive, Target): unsigned. */
    void addAmount (Layout& layout, const char* id, const char* name, float min, float max, float defaultValue)
    {
        addDb (layout, id, name, min, max, defaultValue, "dB", false);
    }

    void addHz (Layout& layout, const char* id, const char* name, float min, float max, float defaultValue)
    {
        addFloat (layout, id, name, logRange (min, max), defaultValue, "Hz", hzText, hzIn);
    }

    void addPercent (Layout& layout, const char* id, const char* name, float defaultValue)
    {
        addFloat (layout, id, name, linearRange (0.0f, 100.0f), defaultValue, "%", percentText, numberIn);
    }
}

juce::StringArray modelNames()       { return { "Clean", "VCA", "FET", "Opto", "Vari-mu", "Upward", "Diode bridge", "PWM", "Pumping" }; }
juce::StringArray vcaDetectorNames() { return { "Peak", "RMS" }; }
juce::StringArray characterNames()   { return { "Off", "Warm", "Iron", "Edge" }; }
juce::StringArray stereoModeNames()  { return { "Linked", "Unlinked", "Mid", "Side", "Mid/Side" }; }
juce::StringArray keySourceNames()   { return { "Internal", "External" }; }
juce::StringArray listenNames()      { return { "Output", "Key", "Delta" }; }

const juce::StringArray& allParameterIds()
{
    static const juce::StringArray ids {
        model, vcaDetector, threshold, ratio, knee, attack, release, autoRelease, range, floor, gain,
        autoGain, mix, character, drive, charPost, driveLink, target, lookahead, stereo, link, key,
        keyHpf, keyLpf, keyBellFreq, keyBellGain, keyBellQ, listen, output, bypass
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

    addChoice (layout, model, "Model", modelNames(), static_cast<int> (Model::clean));
    addChoice (layout, vcaDetector, "VCA Detector", vcaDetectorNames(), static_cast<int> (VcaDetector::peak));
    addDb (layout, threshold, "Threshold", thresholdMinDb, 0.0f, thresholdDefaultDb, "dBFS");
    addFloat (layout, ratio, "Ratio", ratioRange(), ratioDefault, "", ratioText, ratioIn);
    addAmount (layout, knee, "Knee", 0.0f, kneeMaxDb, kneeDefaultDb);
    addFloat (layout, attack, "Attack", logRange (attackMinMs, attackMaxMs), attackDefaultMs, "ms", timeText, msIn);
    addFloat (layout, release, "Release", logRange (releaseMinMs, releaseMaxMs), releaseDefaultMs, "ms", timeText, msIn);
    addBool (layout, autoRelease, "Auto Release", true);
    addAmount (layout, range, "Range", 0.0f, rangeMaxDb, rangeDefaultDb);
    addDb (layout, floor, "Floor", floorMinDb, floorMaxDb, floorDefaultDb, "dBFS");
    addDb (layout, gain, "Gain", -gainRangeDb, gainRangeDb, 0.0f);
    addBool (layout, autoGain, "Auto Gain", false);
    addPercent (layout, mix, "Mix", 100.0f);

    addChoice (layout, character, "Character", characterNames(), static_cast<int> (CharacterMode::off));
    addAmount (layout, drive, "Drive", 0.0f, driveMaxDb, driveDefaultDb);
    addBool (layout, charPost, "Placement", true, "Pre", "Post");
    addBool (layout, driveLink, "Drive Link", false);

    addAmount (layout, target, "Target", targetMinDb, targetMaxDb, targetDefaultDb);

    addFloat (layout, lookahead, "Look-ahead", linearRange (0.0f, lookaheadMaxMs), 0.0f, "ms",
              [] (float v) { return String (v, 2) + " ms"; }, msIn);
    addChoice (layout, stereo, "Stereo", stereoModeNames(), static_cast<int> (StereoMode::linked));
    addPercent (layout, link, "Link", 100.0f);
    addChoice (layout, key, "Key", keySourceNames(), static_cast<int> (KeySource::internal));

    addFloat (layout, keyHpf, "Key HPF", logRange (keyHpfMinHz, keyHpfMaxHz), keyHpfMinHz, "Hz",
              [] (float v) { return isKeyHpfOff (v) ? String ("Off") : hzText (v); },
              [] (const String& s) { return s == "off" ? std::optional<float> (keyHpfMinHz) : hzIn (s); });
    addFloat (layout, keyLpf, "Key LPF", logRange (keyLpfMinHz, keyLpfMaxHz), keyLpfMaxHz, "Hz",
              [] (float v) { return isKeyLpfOff (v) ? String ("Off") : hzText (v); },
              [] (const String& s) { return s == "off" ? std::optional<float> (keyLpfMaxHz) : hzIn (s); });
    addHz (layout, keyBellFreq, "Key Bell Freq", keyBellMinHz, keyBellMaxHz, keyBellDefaultHz);
    addDb (layout, keyBellGain, "Key Bell Gain", -keyBellRangeDb, keyBellRangeDb, 0.0f);
    addFloat (layout, keyBellQ, "Key Bell Q", logRange (keyBellQMin, keyBellQMax), keyBellQDefault, "",
              [] (float v) { return String (v, 2); }, numberIn);

    addChoice (layout, listen, "Listen", listenNames(), static_cast<int> (Listen::output));
    addDb (layout, output, "Output", -outputRangeDb, outputRangeDb, 0.0f);
    addBool (layout, bypass, "Bypass", false);

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
    : model       (raw (apvts, params::model)),
      vcaDetector (raw (apvts, params::vcaDetector)),
      threshold   (raw (apvts, params::threshold)),
      ratio       (raw (apvts, params::ratio)),
      knee        (raw (apvts, params::knee)),
      attack      (raw (apvts, params::attack)),
      release     (raw (apvts, params::release)),
      autoRelease (raw (apvts, params::autoRelease)),
      range       (raw (apvts, params::range)),
      floor       (raw (apvts, params::floor)),
      gain        (raw (apvts, params::gain)),
      autoGain    (raw (apvts, params::autoGain)),
      mix         (raw (apvts, params::mix)),
      character   (raw (apvts, params::character)),
      drive       (raw (apvts, params::drive)),
      charPost    (raw (apvts, params::charPost)),
      driveLink   (raw (apvts, params::driveLink)),
      target      (raw (apvts, params::target)),
      lookahead   (raw (apvts, params::lookahead)),
      stereo      (raw (apvts, params::stereo)),
      link        (raw (apvts, params::link)),
      key         (raw (apvts, params::key)),
      keyHpf      (raw (apvts, params::keyHpf)),
      keyLpf      (raw (apvts, params::keyLpf)),
      keyBellFreq (raw (apvts, params::keyBellFreq)),
      keyBellGain (raw (apvts, params::keyBellGain)),
      keyBellQ    (raw (apvts, params::keyBellQ)),
      listen      (raw (apvts, params::listen)),
      output      (raw (apvts, params::output)),
      bypass      (dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (params::bypass)))
{
    jassert (bypass != nullptr);
}

} // namespace basalt
