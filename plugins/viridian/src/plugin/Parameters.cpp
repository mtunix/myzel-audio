#include "plugin/Parameters.h"

namespace viridian
{

namespace params
{

namespace
{
    using namespace juce;
    using FloatAttrs = AudioParameterFloatAttributes;
    using BoolAttrs = AudioParameterBoolAttributes;

    ParameterID pid (const char* id) { return { id, versionHint }; }

    constexpr std::array<const char*, numControls> controlNames {
        "HPF Freq",
        "Low Freq", "Low Gain", "Low Q", "Low Shape",
        "Low-Mid Freq", "Low-Mid Gain", "Low-Mid Q",
        "High-Mid Freq", "High-Mid Gain", "High-Mid Q",
        "High Freq", "High Gain", "High Q", "High Shape",
        "LPF Freq"
    };

    /** Trailing zeros dropped: "1.5", "12", "12.5". */
    String compact (float v, int decimals)
    {
        auto s = String (v, decimals);
        if (s.containsChar ('.'))
            s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        return s;
    }

    String formatHz (float hz, bool exact)
    {
        if (hz < 1000.0f)
            return String (roundToInt (hz)) + " Hz";
        const auto khz = hz / 1000.0f;
        if (exact)
            return compact (khz, 2) + " kHz";
        return String (khz, khz < 10.0f ? 2 : 1) + " kHz";
    }

    String formatDb (float db, bool exact)
    {
        if (exact)
            return (db > 0.0f ? "+" : "") + String (roundToInt (db)) + " dB";
        if (std::abs (db) < 0.05f)
            return "0.0 dB";
        return (db > 0.0f ? "+" : "") + String (db, 1) + " dB";
    }

    String formatValue (const ControlInfo& c, float value, bool exact)
    {
        switch (c.unit)
        {
            case Unit::hz:   return formatHz (value, exact);
            case Unit::db:   return formatDb (value, exact);
            case Unit::q:    return String (value, 2);
            case Unit::dial: return String (value, 1);
            case Unit::none: break;
        }
        return String (value, 2);
    }

    /** The Model and Alt parameters of the same layout, which the slot read-outs follow. Owned
        with the slot parameters by one processor, so they live exactly as long. */
    struct Selection
    {
        const AudioParameterChoice* model = nullptr;
        const AudioParameterBool* alt = nullptr;

        Model currentModel() const { return static_cast<Model> (jlimit (0, numModels - 1, model->getIndex())); }
        bool altOn() const { return alt->get(); }
    };

    String dbText (float v, int)
    {
        return (v > 0.005f ? "+" : "") + String (v, 1) + " dB";
    }

    float numberIn (const String& text)
    {
        return text.retainCharacters ("0123456789.+-").getFloatValue();
    }
}

juce::StringArray modelNames()
{
    juce::StringArray names;
    for (const auto& m : modelInfos())
        names.add (m.name);
    return names;
}

juce::StringArray stereoModeNames()
{
    return { "Stereo", "Mid", "Side" };
}

juce::String controlText (Model m, bool altOn, Control control, float position)
{
    const auto& c = controlInfo (m, altOn, control);

    switch (c.kind)
    {
        case Kind::dim:
            return "Unused";

        case Kind::fixed:
            return formatValue (c, c.min, true);

        case Kind::toggle:
            return positionIndex (c, position) == 0 ? c.offName : c.onName;

        case Kind::stepped:
        case Kind::continuous:
        {
            const auto v = map (c, position);
            if (v.off)
                return "Off";
            return formatValue (c, v.value, c.kind == Kind::stepped);
        }
    }

    return {};
}

float controlPositionFromText (Model m, bool altOn, Control control, const juce::String& typed)
{
    const auto& c = controlInfo (m, altOn, control);
    const auto text = typed.trim().toLowerCase();

    if (text.isEmpty() || c.kind == Kind::dim || c.kind == Kind::fixed)
        return -1.0f;

    if (c.kind == Kind::toggle)
    {
        if (text == juce::String (c.offName).toLowerCase())
            return 0.0f;
        if (text == juce::String (c.onName).toLowerCase())
            return 1.0f;
        return -1.0f;
    }

    if (text == "off")
        return offPosition (c);

    if (! text.containsAnyOf ("0123456789"))
        return -1.0f;

    auto value = numberIn (text);
    if (c.unit == Unit::hz && text.containsChar ('k'))
        value *= 1000.0f;

    return positionOf (c, value);
}

const juce::StringArray& soundParameterIds()
{
    static const juce::StringArray ids = []
    {
        juce::StringArray s { model };
        for (const auto* id : controlIds)
            s.add (id);
        s.addArray (juce::StringArray { alt, drive, eqIn, output, stereo, tolerance, noise });
        return s;
    }();
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;
    const auto selection = std::make_shared<Selection>();

    auto modelParameter = std::make_unique<AudioParameterChoice> (
        pid (model), "Model", modelNames(), static_cast<int> (Model::inductor));
    selection->model = modelParameter.get();
    layout.add (std::move (modelParameter));

    // Text that does not parse (or a dimmed slot's "Unused") leaves the knob where it is: the
    // position is shared, and on another model it means something.
    for (int i = 0; i < numControls; ++i)
    {
        const auto control = static_cast<Control> (i);
        const auto* id = controlId (control);
        const String name (controlNames[(size_t) i]);
        const auto defaultValue = defaultPosition (control);

        if (control == Control::lowShape || control == Control::highShape)
        {
            const auto self = std::make_shared<const AudioParameterBool*>();
            auto parameter = std::make_unique<AudioParameterBool> (
                pid (id), name, defaultValue >= 0.5f,
                BoolAttrs()
                    .withStringFromValueFunction ([selection, control] (bool on, int)
                    {
                        return controlText (selection->currentModel(), selection->altOn(), control, on ? 1.0f : 0.0f);
                    })
                    .withValueFromStringFunction ([selection, control, self] (const String& s)
                    {
                        if (const auto p = controlPositionFromText (selection->currentModel(), selection->altOn(), control, s); p >= 0.0f)
                            return p >= 0.5f;
                        const auto t = s.trim().toLowerCase();
                        if (t == "bell" || t == "peak" || t == "on" || t == "1")
                            return true;
                        if (t == "shelf" || t == "off" || t == "0")
                            return false;
                        return (*self)->get();
                    }));
            *self = parameter.get();
            layout.add (std::move (parameter));
            continue;
        }

        const auto self = std::make_shared<const AudioParameterFloat*>();
        auto parameter = std::make_unique<AudioParameterFloat> (
            pid (id), name, NormalisableRange<float> { 0.0f, 1.0f }, defaultValue,
            FloatAttrs()
                .withStringFromValueFunction ([selection, control] (float v, int)
                {
                    return controlText (selection->currentModel(), selection->altOn(), control, v);
                })
                .withValueFromStringFunction ([selection, control, self] (const String& s)
                {
                    if (s.trim().endsWithChar ('%') && s.containsAnyOf ("0123456789"))
                        return jlimit (0.0f, 1.0f, numberIn (s) / 100.0f);
                    if (const auto p = controlPositionFromText (selection->currentModel(), selection->altOn(), control, s); p >= 0.0f)
                        return p;
                    return (*self)->get();
                }));
        *self = parameter.get();
        layout.add (std::move (parameter));
    }

    auto altParameter = std::make_unique<AudioParameterBool> (pid (alt), "Alt", false);
    selection->alt = altParameter.get();
    layout.add (std::move (altParameter));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (drive), "Drive", NormalisableRange<float> { driveMinDb, driveMaxDb, 0.01f }, 0.0f,
        FloatAttrs().withLabel ("dB").withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<AudioParameterBool> (pid (eqIn), "EQ In", true));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (output), "Output", NormalisableRange<float> { -outputRangeDb, outputRangeDb, 0.01f }, 0.0f,
        FloatAttrs().withLabel ("dB").withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<AudioParameterChoice> (
        pid (stereo), "Stereo", stereoModeNames(), static_cast<int> (StereoMode::stereo)));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (tolerance), "Tolerance", NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 0.0f,
        FloatAttrs().withLabel ("%").withStringFromValueFunction ([] (float v, int)
        {
            return v < 0.05f ? String ("Matched") : String (v, 1) + " %";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (noise), "Noise", NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 0.0f,
        FloatAttrs().withLabel ("%").withStringFromValueFunction ([] (float v, int)
        {
            return v < 0.05f ? String ("Off") : String (v, 1) + " %";
        })));

    layout.add (std::make_unique<AudioParameterBool> (pid (bypass), "Bypass", false));

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
    : model     (raw (apvts, params::model)),
      alt       (raw (apvts, params::alt)),
      drive     (raw (apvts, params::drive)),
      eqIn      (raw (apvts, params::eqIn)),
      output    (raw (apvts, params::output)),
      stereo    (raw (apvts, params::stereo)),
      tolerance (raw (apvts, params::tolerance)),
      noise     (raw (apvts, params::noise)),
      bypass    (dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (params::bypass)))
{
    for (int i = 0; i < numControls; ++i)
        controls[(size_t) i] = raw (apvts, params::controlId (static_cast<Control> (i)));
    jassert (bypass != nullptr);
}

} // namespace viridian
