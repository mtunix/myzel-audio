#include "support/Rig.h"

#include "support/Reference.h"

#include "common/dsp/FloatCompare.h"
#include "plugin/PluginProcessor.h"

#include <cstdio>
#include <map>
#include <stdexcept>

namespace basalt::measure
{

namespace
{
    constexpr const char* modelKeys[] { "clean", "vca", "fet", "opto", "vari-mu", "upward", "diode-bridge", "pwm", "pumping" };

    /** Every parameter's plain value, in the processor's units: choices as their index, switches 0 or 1, ∞ as 21. */
    std::vector<std::pair<const char*, float>> plainValues (const Settings& s)
    {
        const auto index = [] (auto e) { return (float) static_cast<int> (e); };
        return {
            { params::model, index (s.model) },
            { params::vcaDetector, index (s.vcaDetector) },
            { params::threshold, (float) s.thresholdDb },
            { params::ratio, std::isinf (s.ratio) ? params::ratioInfinity : (float) s.ratio },
            { params::knee, (float) s.kneeDb },
            { params::attack, (float) s.attackMs },
            { params::release, (float) s.releaseMs },
            { params::autoRelease, s.autoRelease ? 1.0f : 0.0f },
            { params::range, (float) s.rangeDb },
            { params::floor, (float) s.floorDb },
            { params::gain, (float) s.gainDb },
            { params::autoGain, s.autoGain ? 1.0f : 0.0f },
            { params::mix, (float) s.mixPercent },
            { params::character, index (s.character) },
            { params::drive, (float) s.driveDb },
            { params::charPost, s.charPost ? 1.0f : 0.0f },
            { params::driveLink, s.driveLink ? 1.0f : 0.0f },
            { params::target, (float) s.targetDb },
            { params::lookahead, (float) s.lookaheadMs },
            { params::stereo, index (s.stereo) },
            { params::link, (float) s.linkPercent },
            { params::key, index (s.key) },
            { params::keyHpf, (float) s.keyHpfHz },
            { params::keyLpf, (float) s.keyLpfHz },
            { params::keyBellFreq, (float) s.keyBellHz },
            { params::keyBellGain, (float) s.keyBellDb },
            { params::keyBellQ, (float) s.keyBellQ },
            { params::listen, index (s.listen) },
            { params::output, (float) s.outputDb },
            { params::bypass, s.bypass ? 1.0f : 0.0f },
        };
    }

    juce::AudioChannelSet channelSet (int channels)
    {
        return channels == 1 ? juce::AudioChannelSet::mono()
                             : (channels == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
    }

    class ProcessorDevice final : public Device
    {
    public:
        void prepare (const Settings& s, double rate, int mainChannels, int keyChannels, int maximumBlockSize) override
        {
            processor = std::make_unique<BasaltProcessor>();
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (channelSet (mainChannels));
            layout.inputBuses.add (channelSet (keyChannels));
            layout.outputBuses.add (channelSet (mainChannels));
            if (! processor->setBusesLayout (layout))
                throw std::runtime_error ("BasaltProcessor refused the bus layout");

            numMain = mainChannels;
            numKey = keyChannels;
            current = s;
            for (const auto& [id, value] : plainValues (s))
                setParameter (id, value);
            processor->setLatencyMode (s.latency);
            processor->prepareToPlay (rate, maximumBlockSize);
            buffer.setSize (std::max (processor->getTotalNumInputChannels(), processor->getTotalNumOutputChannels()), maximumBlockSize);
        }

        void set (const Settings& s) override
        {
            const auto before = plainValues (current), after = plainValues (s);
            for (size_t i = 0; i < after.size(); ++i)
                if (! myzel::dsp::isExactly (before[i].second, after[i].second))
                    setParameter (after[i].first, after[i].second);
            if (s.latency != current.latency)
                processor->setLatencyMode (s.latency);
            current = s;
        }

        void process (float* const* main, int mainChannels, const float* const* key, int keyChannels, int n) override
        {
            for (int c = 0; c < mainChannels; ++c)
                std::copy (main[c], main[c] + n, buffer.getWritePointer (c));
            for (int c = 0; c < keyChannels; ++c)
                std::copy (key[c], key[c] + n, buffer.getWritePointer (mainChannels + c));
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), n);
            processor->processBlock (view, midi);
            for (int c = 0; c < mainChannels; ++c)
                std::copy (buffer.getReadPointer (c), buffer.getReadPointer (c) + n, main[c]);
        }

        int latency() const override { return processor->getLatencySamples(); }

        Settings applied() const override
        {
            auto& state = processor->getState();
            const auto v = [&state] (const char* id) { return (double) state.getRawParameterValue (id)->load(); };
            auto s = current;
            s.model = static_cast<Model> ((int) v (params::model));
            s.vcaDetector = static_cast<VcaDetector> ((int) v (params::vcaDetector));
            s.thresholdDb = v (params::threshold);
            s.ratio = params::isRatioInfinite ((float) v (params::ratio)) ? infiniteRatio : v (params::ratio);
            s.kneeDb = v (params::knee);
            s.attackMs = v (params::attack);
            s.releaseMs = v (params::release);
            s.autoRelease = v (params::autoRelease) >= 0.5;
            s.rangeDb = v (params::range);
            s.floorDb = v (params::floor);
            s.gainDb = v (params::gain);
            s.autoGain = v (params::autoGain) >= 0.5;
            s.mixPercent = v (params::mix);
            s.character = static_cast<CharacterMode> ((int) v (params::character));
            s.driveDb = v (params::drive);
            s.charPost = v (params::charPost) >= 0.5;
            s.driveLink = v (params::driveLink) >= 0.5;
            s.targetDb = v (params::target);
            s.lookaheadMs = v (params::lookahead);
            s.stereo = static_cast<StereoMode> ((int) v (params::stereo));
            s.linkPercent = v (params::link);
            s.key = static_cast<KeySource> ((int) v (params::key));
            s.keyHpfHz = v (params::keyHpf);
            s.keyLpfHz = v (params::keyLpf);
            s.keyBellHz = v (params::keyBellFreq);
            s.keyBellDb = v (params::keyBellGain);
            s.keyBellQ = v (params::keyBellQ);
            s.listen = static_cast<Listen> ((int) v (params::listen));
            s.outputDb = v (params::output);
            s.bypass = v (params::bypass) >= 0.5;
            s.latency = processor->getLatencyMode();
            return s;
        }

    private:
        void setParameter (const char* id, float plain)
        {
            auto* p = processor->getState().getParameter (id);
            if (p == nullptr)
                throw std::runtime_error (std::string ("no parameter ") + id);
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
        }

        std::unique_ptr<BasaltProcessor> processor;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
        Settings current;
        int numMain = 2, numKey = 0;
    };

    template <typename Enum>
    bool choose (Enum& e, const std::string& value, std::initializer_list<const char*> names)
    {
        int i = 0;
        for (const auto* n : names)
        {
            if (value == n || value == std::to_string (i))
            {
                e = static_cast<Enum> (i);
                return true;
            }
            ++i;
        }
        return false;
    }
}

bool setSetting (Settings& s, const std::string& key, const std::string& value)
{
    const auto number = [&value] (double& d)
    {
        char* end = nullptr;
        const auto v = value == "inf" ? infiniteRatio : std::strtod (value.c_str(), &end);
        if (value != "inf" && (end == value.c_str() || *end != '\0'))
            return false;
        d = v;
        return true;
    };
    const auto flag = [&value] (bool& b)
    {
        if (value != "0" && value != "1" && value != "on" && value != "off")
            return false;
        b = value == "1" || value == "on";
        return true;
    };

    const std::map<std::string, double*> numbers {
        { params::threshold, &s.thresholdDb }, { params::ratio, &s.ratio }, { params::knee, &s.kneeDb },
        { params::attack, &s.attackMs }, { params::release, &s.releaseMs }, { params::range, &s.rangeDb },
        { params::floor, &s.floorDb }, { params::gain, &s.gainDb }, { params::mix, &s.mixPercent },
        { params::drive, &s.driveDb }, { params::target, &s.targetDb }, { params::lookahead, &s.lookaheadMs },
        { params::link, &s.linkPercent }, { params::keyHpf, &s.keyHpfHz }, { params::keyLpf, &s.keyLpfHz },
        { params::keyBellFreq, &s.keyBellHz }, { params::keyBellGain, &s.keyBellDb }, { params::keyBellQ, &s.keyBellQ },
        { params::output, &s.outputDb },
    };
    const std::map<std::string, bool*> flags {
        { params::autoRelease, &s.autoRelease }, { params::autoGain, &s.autoGain }, { params::charPost, &s.charPost },
        { params::driveLink, &s.driveLink }, { params::bypass, &s.bypass },
    };

    if (const auto it = numbers.find (key); it != numbers.end())
        return number (*it->second);
    if (const auto it = flags.find (key); it != flags.end())
        return flag (*it->second);
    if (key == params::model)
    {
        if (const auto m = modelFromKey (value))
        {
            s.model = *m;
            return true;
        }
        return false;
    }
    if (key == params::vcaDetector)
        return choose (s.vcaDetector, value, { "peak", "rms" });
    if (key == params::character)
        return choose (s.character, value, { "off", "warm", "iron", "edge" });
    if (key == params::stereo)
        return choose (s.stereo, value, { "linked", "unlinked", "mid", "side", "mid-side" });
    if (key == params::key)
        return choose (s.key, value, { "internal", "external" });
    if (key == params::listen)
        return choose (s.listen, value, { "output", "key", "delta" });
    if (key == "latency")
        return choose (s.latency, value, { "normal", "live" });
    return false;
}

const char* modelKey (Model m)
{
    return modelKeys[static_cast<int> (m)];
}

std::optional<Model> modelFromKey (const std::string& key)
{
    for (int i = 0; i < numModels; ++i)
        if (key == modelKeys[i])
            return static_cast<Model> (i);
    return std::nullopt;
}

const char* stereoName (StereoMode m)
{
    constexpr const char* names[] { "Linked", "Unlinked", "Mid", "Side", "Mid/Side" };
    return names[static_cast<int> (m)];
}

const char* latencyName (LatencyMode m)
{
    return m == LatencyMode::normal ? "Normal" : "Live";
}

std::string ratioText (double ratio)
{
    if (std::isinf (ratio))
        return "inf";
    char text[32];
    std::snprintf (text, sizeof (text), "%g", ratio);
    return text;
}

const char* deviceName (DeviceKind k)
{
    return k == DeviceKind::processor ? "processor" : (k == DeviceKind::reference ? "reference" : "one-pole");
}

std::unique_ptr<Device> makeDevice (DeviceKind k)
{
    if (k == DeviceKind::processor)
        return std::make_unique<ProcessorDevice>();
    return reference::makeDevice (k == DeviceKind::onePole);
}

namespace
{
    /**
        The raw samples of one render, for checking a measurement by hand: per sample and main
        channel, float32 input, output and key (0 without a key), and a text header naming them.
    */
    void dumpRender (const std::string& path, const Input& in, const Rendered& r, double rate)
    {
        const auto channels = in.main.size();
        if (auto* f = std::fopen ((path + ".f32").c_str(), "wb"))
        {
            for (size_t i = 0; i < in.main[0].size(); ++i)
                for (size_t c = 0; c < channels; ++c)
                {
                    const auto key = in.key.empty() ? 0.0f : in.key[std::min (c, in.key.size() - 1)][i];
                    const float frame[] { in.main[c][i], r.main[c][i], key };
                    std::fwrite (frame, sizeof (float), 3, f);
                }
            std::fclose (f);
        }
        if (auto* f = std::fopen ((path + ".txt").c_str(), "w"))
        {
            std::fprintf (f, "rate %.17g\nlatency %d\nsamples %zu\ncolumns", rate, r.latency, in.main[0].size());
            for (size_t c = 0; c < channels; ++c)
                std::fprintf (f, " main%zu_in main%zu_out key%zu", c, c, c);
            std::fprintf (f, "\n");
            std::fclose (f);
        }
    }
}

Rendered render (Device& device, const Settings& s, const Input& in, const RenderOptions& o)
{
    const auto n = (int) in.main[0].size();
    const auto largest = *std::max_element (o.blocks.begin(), o.blocks.end());
    device.prepare (s, o.rate, (int) in.main.size(), (int) in.key.size(), largest);

    Rendered r;
    r.main = in.main;
    r.latency = device.latency();
    r.applied = device.applied();

    auto current = s;
    std::vector<float*> main (r.main.size());
    std::vector<const float*> key (in.key.size());
    size_t k = 0;
    for (int pos = 0; pos < n;)
    {
        if (o.automate)
        {
            auto next = current;
            o.automate (pos, next);
            if (! (next == current))
            {
                device.set (next);
                current = next;
            }
        }
        const auto block = std::min (o.blocks[k++ % o.blocks.size()], n - pos);
        for (size_t c = 0; c < main.size(); ++c)
            main[c] = r.main[c].data() + pos;
        for (size_t c = 0; c < key.size(); ++c)
            key[c] = in.key[c].data() + pos;
        device.process (main.data(), (int) main.size(), key.data(), (int) key.size(), block);
        pos += block;
    }

    if (! o.dump.empty())
        dumpRender (o.dump, in, r, o.rate);
    return r;
}

std::vector<int> variableBlocks (unsigned seed, int count)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> octaves (0.0, 12.0);
    std::vector<int> sizes;
    for (int i = 0; i < count; ++i)
        sizes.push_back (std::clamp ((int) std::exp2 (octaves (rng)), 1, 4096));
    return sizes;
}

Trace traceOf (const Samples& in, const Samples& out, int latency)
{
    Trace t;
    const auto n = std::max<int> (0, (int) std::min (in.size(), out.size()) - latency);
    t.gain.resize ((size_t) n);
    t.reductionDb.resize ((size_t) n);
    for (size_t i = 0; i < (size_t) n; ++i)
    {
        const auto x = in[i];
        const auto g = ! myzel::dsp::isExactly (x, 0.0) ? out[i + (size_t) latency] / x : std::numeric_limits<float>::quiet_NaN();
        t.gain[i] = g;
        t.reductionDb[i] = g > 0.0f ? -20.0 * std::log10 ((double) g) : std::numeric_limits<double>::quiet_NaN();
    }
    return t;
}

KeyRender renderKey (DeviceKind kind, Settings s, const Channels& key, Probe probe, const RenderOptions& o)
{
    // The signal runs on past its end for the longest latency (10 ms of look-ahead and Character's
    // delay), so every sample of it has its output.
    const auto pad = samplesFor (0.02, o.rate);
    const auto n = (int) key[0].size();

    Input in;
    if (probe == Probe::external)
    {
        s.key = KeySource::external;
        for (const auto& k : key)
        {
            in.key.push_back (k);
            append (in.key.back(), silence (pad));
        }
        in.main = { carrier (n + pad, probeAmplitude), carrier (n + pad, probeAmplitude) };
    }
    else
    {
        s.key = KeySource::internal;
        in.main = { key[0], key[key.size() > 1 ? 1 : 0] };
        for (auto& ch : in.main)
        {
            append (ch, silence (pad));
            for (size_t i = 0; i < ch.size(); ++i)
                if (myzel::dsp::isExactly (ch[i], 0.0))
                    ch[i] = (i % 2 == 0) ? internalSilenceProbe : -internalSilenceProbe;
        }
    }

    // Automation sets whole Settings: the probe keeps the key source it needs through every change.
    auto options = o;
    if (o.automate)
        options.automate = [automate = o.automate, source = s.key] (int position, Settings& x)
        {
            automate (position, x);
            x.key = source;
        };

    auto device = makeDevice (kind);
    const auto rendered = render (*device, s, in, options);

    KeyRender out;
    out.latency = rendered.latency;
    out.applied = rendered.applied;
    for (size_t c = 0; c < in.main.size(); ++c)
    {
        auto t = traceOf (in.main[c], rendered.main[c], rendered.latency);
        t.gain.resize ((size_t) n, std::numeric_limits<float>::quiet_NaN());
        t.reductionDb.resize ((size_t) n, std::numeric_limits<double>::quiet_NaN());
        out.traces.push_back (std::move (t));
    }
    return out;
}

} // namespace basalt::measure
