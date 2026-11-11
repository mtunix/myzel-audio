#include "dsp/chain/Loudness.h"
#include "dsp/chain/SaturatorChain.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>

using namespace umber;

namespace
{
    /** Pink-ish programme (white noise through a gentle -3 dB/oct approximation), -18 dBFS RMS. */
    std::vector<float> programme (double rate, double seconds)
    {
        std::mt19937 rng (99);
        std::normal_distribution<double> white (0.0, 1.0);
        std::vector<float> out ((size_t) (rate * seconds));

        double b0 = 0, b1 = 0, b2 = 0, sumSq = 0;
        for (auto& v : out)
        {
            const double w = white (rng);
            b0 = 0.99765 * b0 + w * 0.0990460;
            b1 = 0.96300 * b1 + w * 0.2965164;
            b2 = 0.57000 * b2 + w * 1.0526913;
            const double p = b0 + b1 + b2 + w * 0.1848;
            v = (float) p;
            sumSq += p * p;
        }

        const double gain = std::pow (10.0, -18.0 / 20.0) / std::sqrt (sumSq / (double) out.size());
        for (auto& v : out)
            v = (float) (v * gain);
        return out;
    }

    double loudnessDb (const float* x, int n)
    {
        return dsp::KWeightedLoudness::measureDb (x, n, 0);
    }
}

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;

    /** Loudness change (dB, K-weighted) of the programme through the chain with Auto gain on. */
    double loudnessChange (const std::vector<float>& input, dsp::ChainSettings s)
    {
        const int skip = (int) (0.5 * rate);
        const auto inputDb = loudnessDb (input.data() + skip, (int) input.size() - skip);

        dsp::SaturatorChain chain;
        chain.prepare (rate, block, 1);
        s.autoGain = true;

        auto signal = input;
        dsp::ChainMeters meters;
        for (size_t start = 0; start < signal.size(); start += block)
        {
            float* ptr = signal.data() + start;
            juce::AudioBuffer<float> view (&ptr, 1, (int) std::min ((size_t) block, signal.size() - start));
            chain.process (view, s, false, meters);
        }

        return loudnessDb (signal.data() + skip, (int) signal.size() - skip) - inputDb;
    }
}

TEST_CASE ("Auto gain holds loudness steady across the whole Drive range", "[chain][autogain]")
{
    const auto input = programme (rate, 2.0);

    for (int m = 0; m < numModels; ++m)
    {
        for (auto [knob, slam] : { std::pair { 0.0f, false }, { 3.0f, false }, { 6.0f, false },
                                   { 10.0f, false }, { 5.0f, true }, { 10.0f, true } })
        {
            dsp::ChainSettings s;
            s.model = static_cast<Model> (m);
            s.driveKnob = knob;
            s.slam = slam;

            const auto change = loudnessChange (input, s);
            INFO (params::modelNames()[m] << " drive " << knob << (slam ? " + Slam" : "")
                                         << ": loudness change " << change << " dB");
            // Perceived (K-weighted) loudness stays put. The test programme has a different
            // spectrum from the calibration noise, so allow a little slack.
            CHECK (std::abs (change) < 1.0); // measured: within ±0.7 dB
        }
    }
}

TEST_CASE ("Auto gain holds loudness steady across Bias and Emphasis", "[chain][autogain]")
{
    const auto input = programme (rate, 2.0);

    for (int m = 0; m < numModels; ++m)
        for (float knob : { 4.0f, 8.0f })
            for (auto [bias, emphasis] : { std::pair { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f },
                                           { -0.6f, 0.7f }, { 0.8f, -0.4f }, { 1.0f, 1.0f }, { -1.0f, -1.0f } })
            {
                dsp::ChainSettings s;
                s.model = static_cast<Model> (m);
                s.driveKnob = knob;
                s.bias = bias;
                s.emphasis = emphasis;

                const auto change = loudnessChange (input, s);
                INFO (params::modelNames()[m] << " drive " << knob << ", bias " << bias << ", emphasis " << emphasis
                                             << ": loudness change " << change << " dB");
                CHECK (std::abs (change) < 1.0);
            }
}
