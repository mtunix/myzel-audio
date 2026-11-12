/*
    Every factory preset: valid values, the coverage PLAN.md asks for, and a sane result on
    programme material.
*/

#include "plugin/PluginProcessor.h"
#include "ui/FactoryPresets.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <random>
#include <set>

using namespace mulberry;

namespace
{
    float valueOf (MulberryProcessor& p, const char* id)
    {
        return p.getState().getRawParameterValue (id)->load();
    }

    /** Stereo pink-ish programme with drum-like bursts, -18 dBFS RMS. */
    juce::AudioBuffer<float> programme (double rate, double seconds)
    {
        const int n = (int) (rate * seconds);
        juce::AudioBuffer<float> b (2, n);
        std::mt19937 rng (5);
        std::normal_distribution<double> white (0.0, 1.0);
        for (int ch = 0; ch < 2; ++ch)
        {
            double b0 = 0, b1 = 0, b2 = 0, sumSq = 0;
            for (int i = 0; i < n; ++i)
            {
                const double w = white (rng);
                b0 = 0.99765 * b0 + w * 0.0990460;
                b1 = 0.96300 * b1 + w * 0.2965164;
                b2 = 0.57000 * b2 + w * 1.0526913;
                const double hit = (i % 12000) < 400 ? 3.0 * std::exp (-(i % 12000) / 80.0) : 0.0;
                const double v = b0 + b1 + b2 + w * 0.1848 + hit * std::sin (i * 0.02);
                b.setSample (ch, i, (float) v);
                sumSq += v * v;
            }
            b.applyGain (ch, 0, n, (float) (std::pow (10.0, -18.0 / 20.0) / std::sqrt (sumSq / n)));
        }
        return b;
    }
}

TEST_CASE ("Presets: Init is the parameter defaults", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    MulberryProcessor p;
    const auto format = ui::presetFormat();
    REQUIRE (format.factory.front().name == "Init");
    for (const auto& id : format.parameterIds)
    {
        INFO (id);
        auto* param = p.getState().getParameter (id);
        REQUIRE (param != nullptr);
        const auto stored = (float) format.factory.front().values[juce::Identifier (id)];
        CHECK (std::abs (param->convertTo0to1 (stored) - param->getDefaultValue()) < 1.0e-4f);
    }
}

TEST_CASE ("Presets: every machine at least 4, every mode at least 3, the plan's categories", "[presets]")
{
    std::map<int, int> machines, modes;
    std::set<juce::String> names;
    const std::set<juce::String> categories { "Utility", "Slapback", "Tape Echoes", "Analog", "Digital & Lo-Fi",
                                              "Rhythmic", "Dub & Runaway", "Ambient & Diffuse" };
    std::set<juce::String> used;
    for (const auto& preset : ui::factoryPresets())
    {
        INFO (preset.name);
        CHECK (names.insert (preset.name).second);
        CHECK (categories.count (preset.category) == 1);
        used.insert (preset.category);
        ++machines[preset.machine];
        ++modes[preset.mode];
    }
    CHECK (names.size() >= 38);
    CHECK (used == categories);
    for (int m = 0; m < numMachines; ++m)
        CHECK (machines[m] >= 4);
    for (int m = 0; m < numModes; ++m)
        CHECK (modes[m] >= 3);
}

TEST_CASE ("Presets: each loads exactly and sounds sane", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    constexpr double rate = 48000.0;
    constexpr int block = 512;
    const auto input = programme (rate, 3.0);
    const auto format = ui::presetFormat();
    const auto inputPeak = std::max (input.getMagnitude (0, 0, input.getNumSamples()), input.getMagnitude (1, 0, input.getNumSamples()));

    MulberryProcessor p;
    p.setPlayConfigDetails (2, 2, rate, block);
    p.prepareToPlay (rate, block);

    for (int index = 0; index < (int) format.factory.size(); ++index)
    {
        const auto& snapshot = format.factory[(size_t) index];
        INFO (snapshot.name);
        p.getPresets().load (index);

        for (const auto& id : format.parameterIds)
        {
            auto* param = p.getState().getParameter (id);
            const auto stored = (float) snapshot.values[juce::Identifier (id)];
            INFO (id << " stored " << stored << " loaded " << valueOf (p, id.toRawUTF8()));
            CHECK (std::abs (param->convertTo0to1 (stored) - param->getValue()) < 1.0e-3f);
        }

        auto buffer = input;
        juce::MidiBuffer midi;
        for (int start = 0; start < buffer.getNumSamples(); start += block)
        {
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, start, std::min (block, buffer.getNumSamples() - start));
            p.processBlock (view, midi);
        }

        bool finite = true;
        float peak = 0.0f;
        double wet = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                finite = finite && std::isfinite (buffer.getSample (ch, i));
                peak = std::max (peak, std::abs (buffer.getSample (ch, i)));
                const auto d = (double) (buffer.getSample (ch, i) - input.getSample (ch, i));
                wet += d * d;
            }

        const auto runaway = (float) snapshot.values[juce::Identifier (params::feedback)] > 100.0f;
        INFO ("peak " << peak << ", input peak " << inputPeak);
        CHECK (finite);
        // Echoes add to a full-level dry: at most +6 dB over the input's peak, +12 dB into a runaway.
        CHECK (peak < inputPeak * (runaway ? 4.0f : 2.0f));
        CHECK (wet > 1.0e-3);                     // every preset audibly echoes
    }
}
