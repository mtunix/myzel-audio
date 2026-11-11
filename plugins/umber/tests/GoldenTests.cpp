// Golden-file regression tests: the full chain's output for every model is compared with a stored
// reference, so DSP refactors can never change the sound silently.
//
// Re-record after an INTENDED sound change:
//     UMBER_UPDATE_GOLDEN=1 build/plugins/umber/tests/UmberTests "[golden]"
// and commit the files in tests/golden/.

#include "dsp/chain/SaturatorChain.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <random>

#ifndef UMBER_TEST_DATA_DIR
 #error "UMBER_TEST_DATA_DIR must point at tests/"
#endif

using namespace umber;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 480;

    /** Deterministic stimulus: log sweep, decaying low-frequency bursts, and band-limited noise. */
    std::vector<float> stimulus()
    {
        std::vector<float> x;
        const auto twoPi = juce::MathConstants<double>::twoPi;

        // 1 s log sweep 30 Hz -> 16 kHz at -12 dBFS
        const int sweepLength = (int) sampleRate;
        double phase = 0.0;
        for (int i = 0; i < sweepLength; ++i)
        {
            const double t = (double) i / sweepLength;
            const double f = 30.0 * std::pow (16000.0 / 30.0, t);
            phase += twoPi * f / sampleRate;
            x.push_back ((float) (0.25 * std::sin (phase)));
        }

        // kick-like bursts: 60 Hz, fast attack, exponential decay, 0 dBFS peaks
        for (int burst = 0; burst < 4; ++burst)
            for (int i = 0; i < (int) (0.25 * sampleRate); ++i)
            {
                const double t = i / sampleRate;
                x.push_back ((float) (std::exp (-t * 18.0) * std::sin (twoPi * 60.0 * t)));
            }

        // 0.5 s of white noise at -18 dBFS RMS. Box-Muller on raw mt19937 output: unlike
        // std::normal_distribution, it gives the same samples with every standard library.
        std::mt19937 rng (7);
        const auto uniform = [&rng] { return ((double) rng() + 0.5) / 4294967296.0; };
        const int noiseLength = (int) (0.5 * sampleRate);
        for (int i = 0; i < noiseLength; i += 2)
        {
            const auto radius = 0.126 * std::sqrt (-2.0 * std::log (uniform()));
            const auto angle = twoPi * uniform();
            x.push_back ((float) (radius * std::cos (angle)));
            if (i + 1 < noiseLength)
                x.push_back ((float) (radius * std::sin (angle)));
        }

        return x;
    }

    std::vector<float> render (Model model)
    {
        dsp::SaturatorChain chain;
        chain.prepare (sampleRate, blockSize, 1);

        dsp::ChainSettings s;
        s.model = model;
        s.driveKnob = 6.5f;
        s.tone = 0.2f;
        s.lowCutHz = 40.0f;
        s.highCutHz = 15000.0f;
        s.realtimeFactor = 4;
        s.renderFactor = 0;

        auto signal = stimulus();
        juce::AudioBuffer<float> buffer;
        dsp::ChainMeters meters;

        for (size_t start = 0; start < signal.size(); start += blockSize)
        {
            const int len = (int) std::min ((size_t) blockSize, signal.size() - start);
            float* ptr = signal.data() + start;
            juce::AudioBuffer<float> view (&ptr, 1, len);
            chain.process (view, s, false, meters);
        }

        return signal;
    }

    juce::File goldenFile (Model model)
    {
        auto name = params::modelNames()[static_cast<int> (model)].removeCharacters (" ").toLowerCase();
        return juce::File (UMBER_TEST_DATA_DIR).getChildFile ("golden").getChildFile (name + ".f32");
    }
}

TEST_CASE ("Every model matches its golden reference", "[golden]")
{
    const bool update = juce::SystemStats::getEnvironmentVariable ("UMBER_UPDATE_GOLDEN", {}).isNotEmpty();

    for (int m = 0; m < numModels; ++m)
    {
        const auto model = static_cast<Model> (m);
        const auto file = goldenFile (model);
        const auto output = render (model);
        INFO (params::modelNames()[m] << " -> " << file.getFullPathName());

        if (update)
        {
            file.getParentDirectory().createDirectory();
            REQUIRE (file.replaceWithData (output.data(), output.size() * sizeof (float)));
            continue;
        }

        if (! file.existsAsFile())
        {
            WARN ("No golden file yet for " << params::modelNames()[m] << " (record with UMBER_UPDATE_GOLDEN=1)");
            continue;
        }

        juce::MemoryBlock data;
        REQUIRE (file.loadFileAsData (data));
        REQUIRE (data.getSize() == output.size() * sizeof (float));

        const auto* reference = static_cast<const float*> (data.getData());
        double errorSq = 0.0, signalSq = 0.0;
        for (size_t i = 0; i < output.size(); ++i)
        {
            const double e = (double) output[i] - reference[i];
            errorSq += e * e;
            signalSq += (double) reference[i] * reference[i];
        }

        const auto errorDb = 10.0 * std::log10 ((errorSq + 1.0e-30) / (signalSq + 1.0e-30));
        INFO ("difference " << errorDb << " dB relative to the reference");
        CHECK (errorDb < -100.0);
    }
}
