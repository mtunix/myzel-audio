#include "plugin/Parameters.h"

#include "dsp/Mappings.h"

namespace cerulean
{

double noteValueInBeats (NoteValue v) noexcept
{
    constexpr double beats[numNoteValues] = {
        0.125,              // 1/32
        0.25 * 2.0 / 3.0,   // 1/16T
        0.25,               // 1/16
        0.375,              // 1/16D
        0.5 * 2.0 / 3.0,    // 1/8T
        0.5,                // 1/8
        0.75,               // 1/8D
        2.0 / 3.0,          // 1/4T
        1.0,                // 1/4
        1.5,                // 1/4D
        4.0 / 3.0,          // 1/2T
        2.0,                // 1/2
        3.0,                // 1/2D
        4.0                 // 1/1
    };
    return beats[juce::jlimit (0, numNoteValues - 1, static_cast<int> (v))];
}

namespace params
{

namespace
{
    using namespace juce;
    using FloatAttrs = AudioParameterFloatAttributes;

    ParameterID pid (const char* id) { return { id, versionHint }; }

    String formatHz (float hz)
    {
        return hz >= 1000.0f ? String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz"
                             : String (roundToInt (hz)) + " Hz";
    }

    float parseHz (const String& text, float offValue)
    {
        const auto t = text.trim().toLowerCase();
        if (t == "off")
            return offValue;
        const auto value = t.getFloatValue();
        return t.contains ("k") ? value * 1000.0f : value;
    }

    NormalisableRange<float> frequencyRange (float min, float max, float centre)
    {
        NormalisableRange<float> range { min, max };
        range.setSkewForCentre (centre);
        return range;
    }

    /** Exponential range: equal knob travel = equal ratio. */
    NormalisableRange<float> logRange (float min, float max)
    {
        return { min, max,
                 [] (float lo, float hi, float x) { return lo * std::pow (hi / lo, x); },
                 [] (float lo, float hi, float v) { return std::log (v / lo) / std::log (hi / lo); },
                 [] (float lo, float hi, float v) { return jlimit (lo, hi, v); } };
    }

    FloatAttrs percentAttrs()
    {
        return FloatAttrs().withLabel ("%").withStringFromValueFunction ([] (float v, int)
        {
            return String (roundToInt (v)) + " %";
        });
    }

    String formatSeconds (float s)
    {
        if (s < 1.0f)
            return String (roundToInt (s * 1000.0f)) + " ms";
        return String (s, s < 10.0f ? 2 : 1) + " s";
    }

    float parseSeconds (const String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.getFloatValue();
        return t.endsWith ("ms") ? value / 1000.0f : value;
    }

    String formatMs (float ms)
    {
        return ms < 1000.0f ? String (ms, ms < 10.0f ? 1 : 0) + " ms" : String (ms / 1000.0f, 2) + " s";
    }

    String formatMultiplier (float x) { return String (x, 2) + "x"; }
}

juce::StringArray algorithmNames()
{
    return { "Room", "Hall", "Plate", "Spring", "Nimbus", "Halo", "Rise", "Billow", "Choir", "Echo" };
}

juce::StringArray noteValueNames()
{
    return { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1/1" };
}

std::pair<juce::String, juce::String> characterLegends (Algorithm a)
{
    switch (a)
    {
        case Algorithm::room:   return { "EARLY/LATE", "WALLS" };
        case Algorithm::hall:   return { "EARLY", "BUILD" };
        case Algorithm::plate:  return { "DENSITY", "METAL" };
        case Algorithm::spring: return { "SPRINGS", "DRIP" };
        case Algorithm::nimbus: return { "DENSITY", "DISTANCE" };
        case Algorithm::halo:   return { "INTERVAL", "AMOUNT" };
        case Algorithm::rise:   return { "RISE TIME", "BLEND" };
        case Algorithm::billow: return { "BLOOM", "FEEDBACK" };
        case Algorithm::choir:  return { "VOWEL", "VOICES" };
        case Algorithm::echo:   return { "ECHO TIME", "REPEATS" };
    }
    return { "PARAM A", "PARAM B" };
}

juce::String characterText (Algorithm a, bool isParamB, float percent)
{
    using namespace juce;
    const auto x = jlimit (0.0f, 1.0f, percent / 100.0f);
    const auto pct = String (roundToInt (percent)) + " %";

    if (! isParamB)
    {
        switch (a)
        {
            case Algorithm::halo:
                return dsp::maps::haloIntervalNames[(size_t) dsp::maps::haloIntervalIndex (x)];
            case Algorithm::spring:
            {
                const auto n = dsp::maps::springCount (x);
                return String (n) + (n == 1 ? " spring" : " springs");
            }
            case Algorithm::rise:   return formatMs (dsp::maps::riseTimeMs (x));
            case Algorithm::billow: return formatMs (dsp::maps::bloomLengthMs (x));
            case Algorithm::echo:   return formatMs (dsp::maps::echoTimeMs (x));
            case Algorithm::choir:
            {
                const auto pos = dsp::maps::vowelPosition (x);
                const auto lo = jlimit (0, 4, (int) std::floor (pos));
                const auto frac = pos - (float) lo;
                if (frac < 0.15f || lo == 4)
                    return dsp::maps::vowelNames[(size_t) lo];
                if (frac > 0.85f)
                    return dsp::maps::vowelNames[(size_t) lo + 1];
                return String (dsp::maps::vowelNames[(size_t) lo]) + "-" + dsp::maps::vowelNames[(size_t) lo + 1];
            }
            case Algorithm::hall:
            case Algorithm::plate:
            case Algorithm::nimbus:
                return pct;
            case Algorithm::room:
                if (std::abs (x - 0.5f) < 0.02f)
                    return "Even";
                return x < 0.5f ? "Early " + String (roundToInt ((0.5f - x) * 200.0f))
                                : "Late " + String (roundToInt ((x - 0.5f) * 200.0f));
        }
        return pct;
    }

    switch (a)
    {
        case Algorithm::choir:
            return String (dsp::maps::voiceCount (x), 1) + " voices";
        case Algorithm::rise:
            if (x < 0.02f) return "Wet only";
            if (x > 0.98f) return "Both";
            return pct;
        case Algorithm::room:
            if (x < 0.25f) return "Soft " + pct;
            if (x > 0.75f) return "Hard " + pct;
            return pct;
        case Algorithm::hall:
        case Algorithm::plate:
        case Algorithm::spring:
        case Algorithm::nimbus:
        case Algorithm::halo:
        case Algorithm::billow:
        case Algorithm::echo:
            return pct;
    }
    return pct;
}

float characterPercentFromText (Algorithm a, bool isParamB, const juce::String& typed)
{
    using namespace juce;
    const auto text = typed.trim();

    if (text.isEmpty())
        return -1.0f;

    // Scans the displayed values; returns the middle of the first run of positions that match.
    auto findRun = [a, isParamB] (auto&& matches) -> float
    {
        constexpr int n = 1000;
        int first = -1, last = -1;

        for (int i = 0; i <= n; ++i)
        {
            const auto pct = 100.0f * (float) i / (float) n;

            if (matches (characterText (a, isParamB, pct)))
            {
                if (first < 0)
                    first = i;
                last = i;
            }
            else if (first >= 0)
            {
                break;
            }
        }

        return first < 0 ? -1.0f : 100.0f * 0.5f * (float) (first + last) / (float) n;
    };

    // 1. A displayed value, exactly.
    if (const auto exact = findRun ([&text] (const String& shown) { return shown.equalsIgnoreCase (text); }); exact >= 0.0f)
        return exact;

    const auto lower = text.toLowerCase();
    const auto hasNumber = lower.containsAnyOf ("0123456789");

    if (! hasNumber)
        return -1.0f;

    const auto number = lower.retainCharacters ("0123456789.+-").getFloatValue();

    // 2. Times, for the time-valued controls (no unit = ms).
    if (! isParamB && (a == Algorithm::rise || a == Algorithm::billow || a == Algorithm::echo))
    {
        if (lower.endsWith ("%"))
            return jlimit (0.0f, 100.0f, number);

        const auto ms = (lower.endsWith ("s") && ! lower.endsWith ("ms")) ? number * 1000.0f : number;

        if (ms <= 0.0f)
            return 0.0f;

        // Inverses of dsp::maps::riseTimeMs / bloomLengthMs / echoTimeMs.
        const auto lo = a == Algorithm::rise ? 50.0f : a == Algorithm::billow ? 150.0f : 20.0f;
        const auto ratio = a == Algorithm::rise ? 80.0f : a == Algorithm::billow ? 4000.0f / 150.0f : 75.0f;

        return 100.0f * jlimit (0.0f, 1.0f, std::log (ms / lo) / std::log (ratio));
    }

    // 3. A counted or stepped value by its number ("2" -> "2 springs", "12" -> "+12", "3" -> "3.0 voices").
    if (! lower.endsWith ("%"))
    {
        const auto counted = findRun ([number] (const String& shown)
        {
            const auto start = shown.trimStart();

            if (start.isEmpty() || shown.endsWith ("%") || ! (CharacterFunctions::isDigit (start[0]) || start[0] == '+' || start[0] == '-'))
                return false;

            return std::abs (start.getFloatValue() - number) < 0.05f;
        });

        if (counted >= 0.0f)
            return counted;
    }

    // 4. A percentage.
    return jlimit (0.0f, 100.0f, number);
}

const juce::StringArray& soundParameterIds()
{
    static const juce::StringArray ids {
        algorithm, decay, preDelay, size, mod, tone, paramA, paramB, mix, output, duck, wetOnly,
        lowEnd, diffusion, modRate, width, inLowCut, inHighCut, duckAmount, duckThreshold, duckRelease,
        preDelaySync, preDelayNote, echoSync, echoNote, trails
    };
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            pid (id), name, NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, def, percentAttrs()));
    };

    // Front panel ------------------------------------------------------------
    layout.add (std::make_unique<AudioParameterChoice> (
        pid (algorithm), "Algorithm", algorithmNames(), static_cast<int> (Algorithm::hall)));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (decay), "Decay", logRange (decayMinS, decayMaxS), 3.0f,
        FloatAttrs().withLabel ("s")
            .withStringFromValueFunction ([] (float v, int) { return formatSeconds (v); })
            .withValueFromStringFunction ([] (const String& s) { return parseSeconds (s); })));

    layout.add (std::make_unique<AudioParameterBool> (pid (freeze), "Freeze", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (preDelay), "Pre-delay", frequencyRange (0.0f, preDelayMaxMs, 80.0f), 20.0f,
        FloatAttrs().withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return formatMs (v); })));

    percent (size, "Size", 50.0f);
    percent (mod, "Mod", 35.0f);

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
            return (t.startsWith ("dark") || t.startsWith ("-")) ? -magnitude : magnitude;
        })));

    percent (paramA, "Param A", 50.0f);
    percent (paramB, "Param B", 50.0f);
    percent (mix, "Mix", 35.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (output), "Output",
        NormalisableRange<float> { -outputRangeDb, outputRangeDb, 0.01f }, 0.0f,
        FloatAttrs().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            return (v > 0.005f ? "+" : "") + String (v, 1) + " dB";
        })));

    layout.add (std::make_unique<AudioParameterBool> (pid (duck), "Duck", false));
    layout.add (std::make_unique<AudioParameterBool> (pid (wetOnly), "Wet Only", false));
    layout.add (std::make_unique<AudioParameterBool> (pid (bypass), "Bypass", false));

    // Advanced drawer ---------------------------------------------------------
    {
        NormalisableRange<float> range { lowEndMin, lowEndMax };
        range.setSkewForCentre (1.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            pid (lowEnd), "Low End", range, 1.0f,
            FloatAttrs().withStringFromValueFunction ([] (float v, int) { return formatMultiplier (v); })));
    }

    percent (diffusion, "Diffusion", 75.0f);

    {
        NormalisableRange<float> range { modRateMin, modRateMax };
        range.setSkewForCentre (1.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            pid (modRate), "Mod Rate", range, 1.0f,
            FloatAttrs().withStringFromValueFunction ([] (float v, int) { return formatMultiplier (v); })));
    }

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (width), "Width", NormalisableRange<float> { 0.0f, widthMax, 0.01f }, 100.0f, percentAttrs()));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (inLowCut), "Input Low Cut", frequencyRange (inLowCutMinHz, inLowCutMaxHz, 100.0f), inLowCutMinHz,
        FloatAttrs().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return isInLowCutOff (v) ? String ("Off") : formatHz (v); })
            .withValueFromStringFunction ([] (const String& s) { return parseHz (s, inLowCutMinHz); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (inHighCut), "Input High Cut", frequencyRange (inHighCutMinHz, inHighCutMaxHz, 5000.0f), inHighCutMaxHz,
        FloatAttrs().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return isInHighCutOff (v) ? String ("Off") : formatHz (v); })
            .withValueFromStringFunction ([] (const String& s) { return parseHz (s, inHighCutMaxHz); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (duckAmount), "Duck Amount", NormalisableRange<float> { 0.0f, duckAmountMaxDb, 0.01f }, 12.0f,
        FloatAttrs().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            return v < 0.05f ? String ("0 dB") : "-" + String (v, 1) + " dB";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (duckThreshold), "Duck Threshold", NormalisableRange<float> { duckThresholdMin, 0.0f, 0.01f }, -40.0f,
        FloatAttrs().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    {
        NormalisableRange<float> range { duckReleaseMinMs, duckReleaseMaxMs };
        range.setSkewForCentre (300.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            pid (duckRelease), "Duck Release", range, 300.0f,
            FloatAttrs().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return formatMs (v); })));
    }

    layout.add (std::make_unique<AudioParameterBool> (pid (preDelaySync), "Pre-delay Sync", false));
    layout.add (std::make_unique<AudioParameterChoice> (
        pid (preDelayNote), "Pre-delay Note", noteValueNames(), static_cast<int> (NoteValue::n1_16)));
    layout.add (std::make_unique<AudioParameterBool> (pid (echoSync), "Echo Sync", false));
    layout.add (std::make_unique<AudioParameterChoice> (
        pid (echoNote), "Echo Note", noteValueNames(), static_cast<int> (NoteValue::n1_8D)));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (bpm), "BPM", NormalisableRange<float> { bpmMin, bpmMax, 0.01f }, 120.0f,
        FloatAttrs().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " BPM"; })));

    layout.add (std::make_unique<AudioParameterChoice> (
        pid (freezeMode), "Freeze Mode", StringArray { "Latch", "Momentary" }, static_cast<int> (FreezeMode::latch)));

    layout.add (std::make_unique<AudioParameterBool> (pid (trails), "Trails", true));

    // Added after v0.1 ---------------------------------------------------------
    layout.add (std::make_unique<AudioParameterBool> (pid (freezeLayer), "Freeze Layer", false));

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
    : algorithm     (raw (apvts, params::algorithm)),
      decay         (raw (apvts, params::decay)),
      freeze        (raw (apvts, params::freeze)),
      preDelay      (raw (apvts, params::preDelay)),
      size          (raw (apvts, params::size)),
      mod           (raw (apvts, params::mod)),
      tone          (raw (apvts, params::tone)),
      paramA        (raw (apvts, params::paramA)),
      paramB        (raw (apvts, params::paramB)),
      mix           (raw (apvts, params::mix)),
      output        (raw (apvts, params::output)),
      duck          (raw (apvts, params::duck)),
      wetOnly       (raw (apvts, params::wetOnly)),
      lowEnd        (raw (apvts, params::lowEnd)),
      diffusion     (raw (apvts, params::diffusion)),
      modRate       (raw (apvts, params::modRate)),
      width         (raw (apvts, params::width)),
      inLowCut      (raw (apvts, params::inLowCut)),
      inHighCut     (raw (apvts, params::inHighCut)),
      duckAmount    (raw (apvts, params::duckAmount)),
      duckThreshold (raw (apvts, params::duckThreshold)),
      duckRelease   (raw (apvts, params::duckRelease)),
      preDelaySync  (raw (apvts, params::preDelaySync)),
      preDelayNote  (raw (apvts, params::preDelayNote)),
      echoSync      (raw (apvts, params::echoSync)),
      echoNote      (raw (apvts, params::echoNote)),
      bpm           (raw (apvts, params::bpm)),
      trails        (raw (apvts, params::trails)),
      freezeLayer   (raw (apvts, params::freezeLayer)),
      bypass        (dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (params::bypass)))
{
    jassert (bypass != nullptr);
}

} // namespace cerulean
