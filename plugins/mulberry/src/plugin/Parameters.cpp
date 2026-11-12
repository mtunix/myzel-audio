#include "plugin/Parameters.h"

#include "dsp/NoteValues.h"

namespace mulberry
{

namespace params
{

namespace
{
    using namespace juce;
    using FloatAttrs = AudioParameterFloatAttributes;

    ParameterID pid (const String& id) { return { id, versionHint }; }

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

    float parseMs (const String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.retainCharacters ("0123456789.+-").getFloatValue();
        return (t.endsWith ("s") && ! t.endsWith ("ms")) ? value * 1000.0f : value;
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

    FloatAttrs msAttrs()
    {
        return FloatAttrs().withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return formatMs (v); })
            .withValueFromStringFunction ([] (const String& s) { return parseMs (s); });
    }

    String formatMultiplier (float x) { return String (x, 2) + "x"; }
}

juce::String stepId (int index)
{
    return "step" + juce::String (index + 1);
}

juce::String formatMs (double ms)
{
    if (ms < 10.0)
        return juce::String (ms, 1) + " ms";
    if (ms < 1000.0)
        return juce::String (juce::roundToInt (ms)) + " ms";
    return juce::String (ms / 1000.0, 2) + " s";
}

juce::StringArray machineNames()
{
    juce::StringArray names;
    for (const auto& m : machineInfos)
        names.add (m.name);
    return names;
}

juce::StringArray modeNames() { return { "Single", "Dual", "Ping-Pong", "Pattern" }; }

juce::StringArray noteNames()
{
    juce::StringArray names;
    for (const auto* n : notes::names)
        names.add (n);
    return names;
}

juce::StringArray panRuleNames() { return { "Centre", "Alternate", "Sweep", "Scatter" }; }

juce::String characterText (Machine m, bool isParamB, float percent)
{
    using namespace juce;
    namespace c = character;
    const auto x = jlimit (0.0f, 1.0f, percent / 100.0f);
    const auto pct = String (roundToInt (percent)) + " %";

    if (! isParamB)
    {
        switch (m)
        {
            case Machine::clean:
                if (x < 0.02f) return "Crossfade";
                if (x > 0.98f) return "Glide";
                return pct;
            case Machine::rack:   return String (c::bits (x)) + " bits";
            case Machine::bucket: return String (c::chipStages (x)) + " stages";
            case Machine::oilCan:
                if (x < 0.02f) return "One";
                return pct;
            case Machine::loop:   return c::headNames[(size_t) c::step (x, 7)];
            case Machine::reel:
            {
                const auto ips = c::speedIps (x);
                return (ips < 10.0f ? String (ips, 1) : String (roundToInt (ips))) + " ips";
            }
        }
        return pct;
    }

    switch (m)
    {
        case Machine::clean:
        {
            const auto st = c::shiftSemitones (x);
            if (st == 0) return "Off";
            return (st > 0 ? "+" : "") + String (st) + " st";
        }
        case Machine::rack:
        {
            const auto hz = c::rateHz (x);
            return String (hz / 1000.0f, 1) + " kHz";
        }
        case Machine::bucket:
            if (x < 0.02f) return "Off";
            return pct;
        case Machine::oilCan:
            if (x < 0.02f) return "Fresh";
            if (x > 0.98f) return "Old";
            return pct;
        case Machine::loop:
            if (x < 0.02f) return "New";
            return pct;
        case Machine::reel:
        {
            const auto b = c::bias (x);
            if (std::abs (b) < 0.02f) return "Nominal";
            return (b < 0.0f ? "Under " : "Over ") + String (roundToInt (std::abs (b) * 100.0f));
        }
    }
    return pct;
}

float characterPercentFromText (Machine m, bool isParamB, const juce::String& typed)
{
    using namespace juce;
    const auto text = typed.trim();
    if (text.isEmpty())
        return -1.0f;

    // Scans the displayed values; returns the middle of the first run of positions that match.
    auto findRun = [m, isParamB] (auto&& matches) -> float
    {
        constexpr int n = 1000;
        int first = -1, last = -1;
        for (int i = 0; i <= n; ++i)
        {
            if (matches (characterText (m, isParamB, 100.0f * (float) i / (float) n)))
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

    if (const auto exact = findRun ([&text] (const String& shown) { return shown.equalsIgnoreCase (text); }); exact >= 0.0f)
        return exact;

    const auto lower = text.toLowerCase();
    if (! lower.containsAnyOf ("0123456789"))
        return -1.0f;

    const auto number = lower.retainCharacters ("0123456789.+-").getFloatValue();

    if (! lower.endsWith ("%"))
    {
        // "12" -> "12 bits", "+7" -> "+7 st", "15" -> "15 ips", "8" -> "8.0 kHz", "1+2" -> heads.
        const auto counted = findRun ([number, &lower] (const String& shown)
        {
            const auto start = shown.trimStart();
            if (start.isEmpty() || shown.endsWith ("%") || ! (CharacterFunctions::isDigit (start[0]) || start[0] == '+' || start[0] == '-'))
                return false;
            if (shown.containsChar ('+') && ! shown.startsWithChar ('+'))
                return shown.equalsIgnoreCase (lower);
            const auto value = start.getFloatValue();
            const auto typedKhz = lower.contains ("k") || number < 100.0f;
            const auto scaled = shown.contains ("kHz") && ! typedKhz ? value * 1000.0f : value;
            return std::abs (scaled - number) < 0.05f * std::max (1.0f, std::abs (number) * 0.02f);
        });
        if (counted >= 0.0f)
            return counted;
    }

    return jlimit (0.0f, 100.0f, number);
}

const juce::StringArray& soundParameterIds()
{
    static const juce::StringArray ids = []
    {
        juce::StringArray s {
            machine, mode, time, sync, note, feedback, drive, wobble, diffuse, lowCut, highCut,
            paramA, paramB, timeRight, noteRight, width, steps, mix, output, duck, wetOnly,
            groove, feel, crossFeed, panRule, wobbleRate, wobbleShape, noise, trails,
            duckAmount, duckThreshold, duckRelease
        };
        for (int i = 0; i < maxSteps; ++i)
            s.add (stepId (i));
        return s;
    }();
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = [&] (const String& id, const String& name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            pid (id), name, NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, def, percentAttrs()));
    };

    // Front panel ------------------------------------------------------------
    layout.add (std::make_unique<AudioParameterChoice> (
        pid (machine), "Machine", machineNames(), static_cast<int> (Machine::reel)));
    layout.add (std::make_unique<AudioParameterChoice> (
        pid (mode), "Mode", modeNames(), static_cast<int> (Mode::single)));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (time), "Time", logRange (maps::minTimeMs, maps::maxTimeMs), 375.0f, msAttrs()));
    layout.add (std::make_unique<AudioParameterBool> (pid (sync), "Sync", true));
    layout.add (std::make_unique<AudioParameterChoice> (pid (note), "Note", noteNames(), notes::index1_8D));
    layout.add (std::make_unique<AudioParameterBool> (pid (hold), "Hold", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (feedback), "Feedback", NormalisableRange<float> { 0.0f, 120.0f, 0.01f }, 35.0f, percentAttrs()));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (drive), "Drive", NormalisableRange<float> { driveMinDb, driveMaxDb, 0.01f }, 0.0f,
        FloatAttrs().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            return (v > 0.005f ? "+" : "") + String (v, 1) + " dB";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (wobble), "Wobble", NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 50.0f,
        FloatAttrs().withLabel ("%").withStringFromValueFunction ([] (float v, int)
        {
            if (v < 0.05f) return String ("Off");
            if (std::abs (v - 50.0f) < 0.5f) return String ("Nominal");
            return String (maps::wobbleScale (v / 100.0f), 2) + "x";
        })));

    percent (diffuse, "Diffuse", 0.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (lowCut), "Low Cut", frequencyRange (lowCutOffHz, lowCutMaxHz, 150.0f), lowCutOffHz,
        FloatAttrs().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return isLowCutOff (v) ? String ("Off") : formatHz (v); })
            .withValueFromStringFunction ([] (const String& s) { return parseHz (s, lowCutOffHz); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (highCut), "High Cut", frequencyRange (highCutMinHz, highCutOffHz, 5000.0f), highCutOffHz,
        FloatAttrs().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return isHighCutOff (v) ? String ("Off") : formatHz (v); })
            .withValueFromStringFunction ([] (const String& s) { return parseHz (s, highCutOffHz); })));

    percent (paramA, "Param A", 50.0f);
    percent (paramB, "Param B", 50.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (timeRight), "Right Time", logRange (maps::minTimeMs, maps::maxTimeMs), 500.0f, msAttrs()));
    layout.add (std::make_unique<AudioParameterChoice> (pid (noteRight), "Right Note", noteNames(), notes::index1_4));

    percent (width, "Width", 100.0f);
    layout.add (std::make_unique<AudioParameterInt> (pid (steps), "Steps", 1, maxSteps, 4));

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

    // Advanced deck ----------------------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (
        pid (groove), "Groove", NormalisableRange<float> { 50.0f, 75.0f, 0.01f }, 50.0f,
        FloatAttrs().withLabel ("%").withStringFromValueFunction ([] (float v, int)
        {
            return v < 50.05f ? String ("Straight") : String (roundToInt (v)) + " %";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (feel), "Feel", NormalisableRange<float> { -feelMaxMs, feelMaxMs, 0.01f }, 0.0f,
        FloatAttrs().withLabel ("ms").withStringFromValueFunction ([] (float v, int)
        {
            if (std::abs (v) < 0.05f) return String ("On the beat");
            return (v > 0.0f ? "Drag +" : "Push ") + String (v, 1) + " ms";
        })
        .withValueFromStringFunction ([] (const String& s)
        {
            const auto t = s.trim().toLowerCase();
            if (t.startsWith ("on"))
                return 0.0f;
            const auto magnitude = std::abs (t.retainCharacters ("0123456789.").getFloatValue());
            return (t.startsWith ("push") || t.startsWith ("-")) ? -magnitude : magnitude;
        })));

    percent (crossFeed, "Cross-feed", 0.0f);
    layout.add (std::make_unique<AudioParameterChoice> (
        pid (panRule), "Pan Rule", panRuleNames(), static_cast<int> (PanRule::alternate)));

    {
        NormalisableRange<float> range { wobbleRateMin, wobbleRateMax };
        range.setSkewForCentre (1.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            pid (wobbleRate), "Wobble Rate", range, 1.0f,
            FloatAttrs().withStringFromValueFunction ([] (float v, int) { return formatMultiplier (v); })));
    }

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (wobbleShape), "Wobble Shape", NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 50.0f,
        FloatAttrs().withLabel ("%").withStringFromValueFunction ([] (float v, int)
        {
            if (std::abs (v - 50.0f) < 0.5f) return String ("Nominal");
            if (v < 0.5f) return String ("Periodic");
            if (v > 99.5f) return String ("Random");
            return String (roundToInt (v)) + " %";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (noise), "Noise", NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 0.0f,
        FloatAttrs().withLabel ("%").withStringFromValueFunction ([] (float v, int)
        {
            return v < 0.05f ? String ("Off") : String (roundToInt (v)) + " %";
        })));

    layout.add (std::make_unique<AudioParameterBool> (pid (trails), "Trails", true));

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
            pid (duckRelease), "Duck Release", range, 300.0f, msAttrs()));
    }

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (bpm), "BPM", NormalisableRange<float> { bpmMin, bpmMax, 0.01f }, 120.0f,
        FloatAttrs().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " BPM"; })));

    layout.add (std::make_unique<AudioParameterChoice> (
        pid (holdMode), "Hold Mode", StringArray { "Latch", "Momentary" }, static_cast<int> (HoldMode::latch)));

    for (int i = 0; i < maxSteps; ++i)
        percent (stepId (i), "Step " + String (i + 1), 100.0f);

    return layout;
}

} // namespace params

namespace
{
    std::atomic<float>* raw (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    }
}

ParameterRefs::ParameterRefs (juce::AudioProcessorValueTreeState& apvts)
    : machine       (raw (apvts, params::machine)),
      mode          (raw (apvts, params::mode)),
      time          (raw (apvts, params::time)),
      sync          (raw (apvts, params::sync)),
      note          (raw (apvts, params::note)),
      hold          (raw (apvts, params::hold)),
      feedback      (raw (apvts, params::feedback)),
      drive         (raw (apvts, params::drive)),
      wobble        (raw (apvts, params::wobble)),
      diffuse       (raw (apvts, params::diffuse)),
      lowCut        (raw (apvts, params::lowCut)),
      highCut       (raw (apvts, params::highCut)),
      paramA        (raw (apvts, params::paramA)),
      paramB        (raw (apvts, params::paramB)),
      timeRight     (raw (apvts, params::timeRight)),
      noteRight     (raw (apvts, params::noteRight)),
      width         (raw (apvts, params::width)),
      steps         (raw (apvts, params::steps)),
      mix           (raw (apvts, params::mix)),
      output        (raw (apvts, params::output)),
      duck          (raw (apvts, params::duck)),
      wetOnly       (raw (apvts, params::wetOnly)),
      groove        (raw (apvts, params::groove)),
      feel          (raw (apvts, params::feel)),
      crossFeed     (raw (apvts, params::crossFeed)),
      panRule       (raw (apvts, params::panRule)),
      wobbleRate    (raw (apvts, params::wobbleRate)),
      wobbleShape   (raw (apvts, params::wobbleShape)),
      noise         (raw (apvts, params::noise)),
      trails        (raw (apvts, params::trails)),
      duckAmount    (raw (apvts, params::duckAmount)),
      duckThreshold (raw (apvts, params::duckThreshold)),
      duckRelease   (raw (apvts, params::duckRelease)),
      bpm           (raw (apvts, params::bpm)),
      holdMode      (raw (apvts, params::holdMode)),
      bypass        (dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (params::bypass)))
{
    for (int i = 0; i < params::maxSteps; ++i)
        stepLevels[(size_t) i] = raw (apvts, params::stepId (i));
    jassert (bypass != nullptr);
}

} // namespace mulberry
