#include "plugin/Parameters.h"

#include <optional>

namespace inkcap
{

namespace params
{

namespace
{
    using namespace juce;
    using FloatAttrs = AudioParameterFloatAttributes;
    using Layout = AudioProcessorValueTreeState::ParameterLayout;

    ParameterID pid (const String& id) { return { id, versionHint }; }

    /** The number in a typed value, locale-free ("-40", "dark 40" -> 40, "62 %" -> 62); nothing without a digit. */
    std::optional<float> numberIn (const String& text)
    {
        if (! text.containsAnyOf ("0123456789"))
            return std::nullopt;
        return text.retainCharacters ("0123456789.+-").getFloatValue();
    }

    using Format = std::function<String (float)>;
    using Parse = std::function<std::optional<float> (const String&)>;

    /** A float parameter whose typed entry leaves it where it is when the text does not parse. */
    std::unique_ptr<AudioParameterFloat> number (const char* id, const String& name, NormalisableRange<float> range,
                                                 float def, const String& label, Format format, Parse parse)
    {
        const auto self = std::make_shared<const AudioParameterFloat*>();
        auto parameter = std::make_unique<AudioParameterFloat> (
            pid (id), name, range, def,
            FloatAttrs().withLabel (label)
                .withStringFromValueFunction ([format] (float v, int) { return format (v); })
                .withValueFromStringFunction ([parse, self] (const String& s)
                {
                    if (const auto v = parse (s))
                        return *v;
                    return (*self)->get();
                }));
        *self = parameter.get();
        return parameter;
    }

    std::unique_ptr<AudioParameterFloat> percent (const char* id, const String& name, float def)
    {
        return number (id, name, { 0.0f, 100.0f, 0.01f }, def, "%",
                       [] (float v) { return String (roundToInt (v)) + " %"; }, numberIn);
    }

    /** -100 .. +100 around a neutral middle: "Dark 40", "Neutral", "Bright 40" (or another pair of words). */
    std::unique_ptr<AudioParameterFloat> bipolar (const char* id, const String& name, const char* below,
                                                  const char* middle, const char* above)
    {
        const String low (below), mid (middle), high (above);

        return number (id, name, { -100.0f, 100.0f, 0.01f }, 0.0f, "%",
                       [low, mid, high] (float v)
                       {
                           if (std::abs (v) < 0.5f)
                               return mid;
                           return (v < 0.0f ? low : high) + " " + String (roundToInt (std::abs (v)));
                       },
                       [low, mid] (const String& s) -> std::optional<float>
                       {
                           const auto t = s.trim();
                           if (t.equalsIgnoreCase (mid))
                               return 0.0f;
                           const auto magnitude = numberIn (t);
                           if (! magnitude)
                               return std::nullopt;
                           return (t.startsWithIgnoreCase (low) || t.startsWith ("-")) ? -std::abs (*magnitude) : std::abs (*magnitude);
                       });
    }

    /**
        A control read out through its module's own Type (and Warble's Sync): the text functions
        hold the sibling parameters, which live exactly as long as this one.
    */
    std::unique_ptr<AudioParameterFloat> typed (const char* id, const String& name, TypedControl control,
                                                const AudioParameterChoice& type, const AudioParameterBool* sync = nullptr)
    {
        const auto* typeParam = &type;
        auto synced = [sync] { return sync != nullptr && sync->get(); };

        return number (id, name, { 0.0f, 100.0f, 0.01f }, 50.0f, "%",
                       [control, typeParam, synced] (float v)
                       {
                           return String (typedControlText (control, typeParam->getIndex(), synced(), v));
                       },
                       [control, typeParam, synced] (const String& s) -> std::optional<float>
                       {
                           const auto v = typedControlFromText (control, typeParam->getIndex(), synced(), s.toStdString());
                           if (v < 0.0f)
                               return std::nullopt;
                           return v;
                       });
    }

    /**
        A choice whose typed entry matches a name without regard to case or surrounding space,
        and leaves the choice where it is for anything else (JUCE's own parser picks the first).
    */
    std::unique_ptr<AudioParameterChoice> choice (const String& id, const String& name, const StringArray& names, int def)
    {
        const auto self = std::make_shared<const AudioParameterChoice*>();
        auto parameter = std::make_unique<AudioParameterChoice> (
            pid (id), name, names, def,
            AudioParameterChoiceAttributes().withValueFromStringFunction ([names, self] (const String& s)
            {
                const auto t = s.trim();
                for (int i = 0; i < names.size(); ++i)
                    if (t.equalsIgnoreCase (names[i]))
                        return i;
                return (*self)->getIndex();
            }));
        *self = parameter.get();
        return parameter;
    }

    /** On/Off, yes/no, true/false, 1/0 in any case; anything else leaves the switch where it is. */
    std::unique_ptr<AudioParameterBool> toggle (const String& id, const String& name, bool def)
    {
        const auto self = std::make_shared<const AudioParameterBool*>();
        auto parameter = std::make_unique<AudioParameterBool> (
            pid (id), name, def,
            AudioParameterBoolAttributes().withValueFromStringFunction ([self] (const String& s)
            {
                const auto t = s.trim();
                for (const auto* word : { "on", "yes", "true", "1" })
                    if (t.equalsIgnoreCase (word))
                        return true;
                for (const auto* word : { "off", "no", "false", "0" })
                    if (t.equalsIgnoreCase (word))
                        return false;
                return (*self)->get();
            }));
        *self = parameter.get();
        return parameter;
    }

    std::unique_ptr<AudioParameterBool> moduleOn (Module m)
    {
        const auto& info = moduleInfo (m);
        return toggle (onIds[(size_t) m], String (info.name) + " On", info.defaultOn);
    }

    std::unique_ptr<AudioParameterChoice> moduleType (Module m)
    {
        const auto& info = moduleInfo (m);
        return choice (typeIds[(size_t) m], String (info.name) + " Type", typeNames (m), info.defaultType);
    }

    std::unique_ptr<AudioParameterFloat> moduleAmount (Module m)
    {
        return percent (amountIds[(size_t) m], String (moduleInfo (m).name) + " Amount", 50.0f);
    }

    /** A module's parameters as one group (hosts list them together): On, Amount, Type, then its own. */
    template <typename... Own>
    std::unique_ptr<AudioProcessorParameterGroup> group (Module m, std::unique_ptr<AudioParameterBool> on,
                                                         std::unique_ptr<AudioParameterFloat> amount,
                                                         std::unique_ptr<AudioParameterChoice> type, Own&&... own)
    {
        const auto& info = moduleInfo (m);
        return std::make_unique<AudioProcessorParameterGroup> (info.key, info.name, "|", std::move (on),
                                                               std::move (amount), std::move (type),
                                                               std::forward<Own> (own)...);
    }
}

juce::StringArray typeNames (Module m)
{
    juce::StringArray names;
    for (const auto* name : moduleInfo (m).types)
        names.add (name);
    return names;
}

juce::StringArray glitchRateNames()
{
    juce::StringArray names;
    for (const auto& r : glitchRates)
        names.add (r.name);
    return names;
}

const juce::StringArray& settingParameterIds()
{
    static const juce::StringArray ids { bypass, match, bpm };
    return ids;
}

const juce::StringArray& allParameterIds()
{
    static const juce::StringArray ids {
        damage, chaos, mix, output, bypass, match, bpm,
        glitchOn, glitchAmount, glitchType, glitchRate, glitchLength, glitchGroove,
        warbleOn, warbleAmount, warbleType, warbleRate, warbleSync, warbleSpread,
        crushOn, crushAmount, crushType, crushDrive, crushTone,
        bitsOn, bitsAmount, bitsType, bitsRate, bitsDepth, bitsJitter,
        hissOn, hissAmount, hissType, hissTone, hissFollow,
        roomOn, roomAmount, roomType, roomSize, roomDuck
    };
    return ids;
}

const juce::StringArray& soundParameterIds()
{
    static const juce::StringArray ids = []
    {
        juce::StringArray s;
        for (const auto& id : allParameterIds())
            if (! settingParameterIds().contains (id))
                s.add (id);
        return s;
    }();
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;

    Layout layout;

    layout.add (percent (damage, "Damage", 50.0f),
                percent (chaos, "Chaos", 25.0f),
                percent (mix, "Mix", 100.0f),
                number (output, "Output", { -outputRangeDb, outputRangeDb, 0.01f }, 0.0f, "dB",
                        [] (float v) { return (v > 0.005f ? "+" : "") + String (v, 1) + " dB"; }, numberIn),
                toggle (bypass, "Bypass", false),
                toggle (match, "Match", true),
                number (bpm, "BPM", { bpmMin, bpmMax, 0.01f }, 120.0f, "BPM",
                        [] (float v) { return String (v, 1) + " BPM"; }, numberIn));

    {
        auto type = moduleType (Module::glitch);
        auto length = typed (glitchLength, "Glitch Length", TypedControl::glitchLength, *type);

        layout.add (group (Module::glitch, moduleOn (Module::glitch), moduleAmount (Module::glitch), std::move (type),
                           choice (glitchRate, "Glitch Rate", glitchRateNames(), defaultGlitchRate),
                           std::move (length),
                           number (glitchGroove, "Glitch Groove", { grooveMin, grooveMax, 0.01f }, grooveMin, "%",
                                   [] (float v) { return v < grooveMin + 0.05f ? String ("Straight") : String (roundToInt (v)) + " %"; },
                                   [] (const String& s) -> std::optional<float>
                                   {
                                       if (s.trim().equalsIgnoreCase ("straight"))
                                           return grooveMin;
                                       return numberIn (s);
                                   })));
    }

    {
        auto type = moduleType (Module::warble);
        auto sync = toggle (warbleSync, "Warble Sync", false);
        auto rate = typed (warbleRate, "Warble Rate", TypedControl::warbleRate, *type, sync.get());

        layout.add (group (Module::warble, moduleOn (Module::warble), moduleAmount (Module::warble), std::move (type),
                           std::move (rate), std::move (sync), percent (warbleSpread, "Warble Spread", 0.0f)));
    }

    {
        auto type = moduleType (Module::crush);
        auto drive = typed (crushDrive, "Crush Drive", TypedControl::crushDrive, *type);

        layout.add (group (Module::crush, moduleOn (Module::crush), moduleAmount (Module::crush), std::move (type),
                           std::move (drive), bipolar (crushTone, "Crush Tone", "Dark", "Neutral", "Bright")));
    }

    {
        auto type = moduleType (Module::bits);
        auto rate = typed (bitsRate, "Bits Rate", TypedControl::bitsRate, *type);
        auto depth = typed (bitsDepth, "Bits Depth", TypedControl::bitsDepth, *type);

        layout.add (group (Module::bits, moduleOn (Module::bits), moduleAmount (Module::bits), std::move (type),
                           std::move (rate), std::move (depth), percent (bitsJitter, "Bits Jitter", 0.0f)));
    }

    layout.add (group (Module::hiss, moduleOn (Module::hiss), moduleAmount (Module::hiss), moduleType (Module::hiss),
                       bipolar (hissTone, "Hiss Tone", "Dark", "Neutral", "Bright"),
                       bipolar (hissFollow, "Hiss Follow", "Duck", "Steady", "Follow")));

    {
        auto type = moduleType (Module::room);
        auto size = typed (roomSize, "Room Size", TypedControl::roomSize, *type);

        layout.add (group (Module::room, moduleOn (Module::room), moduleAmount (Module::room), std::move (type),
                           std::move (size), percent (roomDuck, "Room Duck", 0.0f)));
    }

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
    : damage       (raw (apvts, params::damage)),
      chaos        (raw (apvts, params::chaos)),
      mix          (raw (apvts, params::mix)),
      output       (raw (apvts, params::output)),
      match        (raw (apvts, params::match)),
      bpm          (raw (apvts, params::bpm)),
      glitchRate   (raw (apvts, params::glitchRate)),
      glitchLength (raw (apvts, params::glitchLength)),
      glitchGroove (raw (apvts, params::glitchGroove)),
      warbleRate   (raw (apvts, params::warbleRate)),
      warbleSync   (raw (apvts, params::warbleSync)),
      warbleSpread (raw (apvts, params::warbleSpread)),
      crushDrive   (raw (apvts, params::crushDrive)),
      crushTone    (raw (apvts, params::crushTone)),
      bitsRate     (raw (apvts, params::bitsRate)),
      bitsDepth    (raw (apvts, params::bitsDepth)),
      bitsJitter   (raw (apvts, params::bitsJitter)),
      hissTone     (raw (apvts, params::hissTone)),
      hissFollow   (raw (apvts, params::hissFollow)),
      roomSize     (raw (apvts, params::roomSize)),
      roomDuck     (raw (apvts, params::roomDuck)),
      bypass       (dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (params::bypass)))
{
    for (size_t m = 0; m < (size_t) numModules; ++m)
        modules[m] = { raw (apvts, params::onIds[m]), raw (apvts, params::amountIds[m]), raw (apvts, params::typeIds[m]) };

    jassert (bypass != nullptr);
}

} // namespace inkcap
