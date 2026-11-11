/*
    Every factory preset: valid values, and a sane result on real-ish programme material.
*/

#include "dsp/chain/Loudness.h"
#include "plugin/PluginProcessor.h"
#include "ui/FactoryPresets.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>
#include <set>

using namespace umber;

namespace
{
    void set (UmberProcessor& p, const char* id, float value)
    {
        auto* param = p.getState().getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** Stereo pink-ish programme with a little transient content, -18 dBFS RMS. */
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
                const double hit = (i % 12000) < 400 ? 3.0 * std::exp (-(i % 12000) / 80.0) : 0.0;   // kick-like bursts
                const double v = b0 + b1 + b2 + w * 0.1848 + hit * std::sin (i * 0.02);
                b.setSample (ch, i, (float) v);
                sumSq += v * v;
            }
            b.applyGain (ch, 0, n, (float) (std::pow (10.0, -18.0 / 20.0) / std::sqrt (sumSq / n)));
        }
        return b;
    }
}

TEST_CASE ("Factory presets are valid and sound sane", "[presets]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    constexpr double rate = 48000.0;
    constexpr int block = 512;
    const auto input = programme (rate, 2.0);
    const int skip = (int) (0.5 * rate);

    auto loudness = [&] (const juce::AudioBuffer<float>& b)
    {
        std::vector<float> mono ((size_t) b.getNumSamples());
        for (int i = 0; i < b.getNumSamples(); ++i)
            mono[(size_t) i] = 0.5f * (b.getSample (0, i) + b.getSample (1, i));
        return dsp::KWeightedLoudness::measureDb (mono, skip);
    };
    const auto inputDb = loudness (input);

    std::set<juce::String> names;
    int usingStereo = 0, usingEmphasis = 0, usingBias = 0;

    for (const auto& preset : ui::factoryPresets())
    {
        INFO (preset.name);
        CHECK (names.insert (preset.name).second);   // unique names

        CHECK (preset.drive >= 0.0f);
        CHECK (preset.drive <= params::driveMax);
        CHECK (preset.model >= 0);
        CHECK (preset.model < numModels);
        CHECK (std::abs (preset.tone) <= params::toneRange);
        CHECK (std::abs (preset.emphasis) <= params::emphasisRange);
        CHECK (std::abs (preset.bias) <= params::biasRange);
        CHECK (preset.stereoMode >= 0);
        CHECK (preset.stereoMode <= 2);

        usingStereo += preset.stereoMode != 0;
        usingEmphasis += preset.emphasis != 0.0f;
        usingBias += preset.bias != 0.0f;

        UmberProcessor p;
        p.setPlayConfigDetails (2, 2, rate, block);
        p.prepareToPlay (rate, block);

        set (p, params::drive, preset.drive);
        set (p, params::model, (float) preset.model);
        set (p, params::slam, preset.slam ? 1.0f : 0.0f);
        set (p, params::tone, preset.tone);
        set (p, params::lowCut, preset.lowCutHz);
        set (p, params::weight, preset.weight ? 1.0f : 0.0f);
        set (p, params::highCut, preset.highCutHz);
        set (p, params::autoGain, preset.autoGain ? 1.0f : 0.0f);
        set (p, params::mix, preset.mix);
        set (p, params::output, preset.outputDb);
        set (p, params::stereoMode, (float) preset.stereoMode);
        set (p, params::emphasis, preset.emphasis);
        set (p, params::bias, preset.bias);

        auto buffer = input;
        juce::MidiBuffer midi;
        for (int start = 0; start < buffer.getNumSamples(); start += block)
        {
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, start, std::min (block, buffer.getNumSamples() - start));
            p.processBlock (view, midi);
        }

        bool finite = true;
        float peak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = skip; i < buffer.getNumSamples(); ++i)
            {
                finite = finite && std::isfinite (buffer.getSample (ch, i));
                peak = std::max (peak, std::abs (buffer.getSample (ch, i)));
            }

        const auto change = loudness (buffer) - inputDb;
        INFO ("loudness change " << change << " dB, peak " << peak);
        CHECK (finite);
        CHECK (peak < 1.0f);                // never clips a -18 dBFS programme
        CHECK (std::abs (change) < 6.0);    // filters and tone move the level, but within reason
    }

    // the new controls are actually shown off
    CHECK (usingStereo >= 4);
    CHECK (usingEmphasis >= 6);
    CHECK (usingBias >= 6);
}
