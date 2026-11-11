#include "dsp/chain/SaturatorChain.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_dsp/juce_dsp.h>

using namespace umber;

namespace
{
    struct AliasResult
    {
        double worstAliasDbc = -200.0;  // strongest non-harmonic component relative to the fundamental
        double fundamentalDbfs = -200.0;
    };

    /**
        Drives the chain with a pure high-frequency tone and inspects the output spectrum.
        Real harmonics of f0 sit at n * f0 (< Nyquist); anything else (excluding DC) is aliasing
        or noise. The tone is placed exactly on an FFT bin; a 7-term Blackman-Harris-style window
        keeps leakage below -150 dB.
    */
    AliasResult measureAliasing (Model model, int factor, float driveKnob, double sampleRate, double freq)
    {
        constexpr int order = 16;
        constexpr int n = 1 << order;

        const double binHz = sampleRate / n;
        const double f0 = std::round (freq / binHz) * binHz;

        dsp::SaturatorChain chain;
        chain.prepare (sampleRate, 512, 1);

        dsp::ChainSettings s;
        s.model = model;
        s.driveKnob = driveKnob;
        s.realtimeFactor = factor;
        s.renderFactor = 0;
        s.autoGain = true;

        const int settle = 16384;
        juce::AudioBuffer<float> buffer (1, settle + n);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * f0 * i / sampleRate)));

        dsp::ChainMeters meters;
        for (int start = 0; start < buffer.getNumSamples(); start += 512)
        {
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 1, start, std::min (512, buffer.getNumSamples() - start));
            chain.process (view, s, false, meters);
        }

        // window (Blackman-Harris 7-term) + FFT
        static constexpr double a[] = { 0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803,
                                        0.01081174209837, 0.00077658482522, 0.00001388721735 };
        std::vector<float> fftData (2 * n, 0.0f);
        double windowSum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double w = 0.0;
            for (int k = 0; k < 7; ++k)
                w += ((k % 2 == 0) ? 1.0 : -1.0) * a[k] * std::cos (2.0 * juce::MathConstants<double>::pi * k * i / n);
            fftData[(size_t) i] = (float) (buffer.getSample (0, settle + i) * w);
            windowSum += w;
        }

        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        auto magDb = [&] (int bin) { return 20.0 * std::log10 (2.0 * fftData[(size_t) bin] / windowSum + 1.0e-20); };

        const int fundamentalBin = (int) std::lround (f0 / binHz);
        constexpr int guard = 8; // window main-lobe half-width in bins

        std::vector<bool> isHarmonic ((size_t) n / 2, false);
        for (int h = 1; h * f0 < sampleRate / 2.0; ++h)
        {
            const int centre = (int) std::lround (h * f0 / binHz);
            for (int b = centre - guard; b <= centre + guard; ++b)
                if (b >= 0 && b < n / 2)
                    isHarmonic[(size_t) b] = true;
        }

        AliasResult r;
        r.fundamentalDbfs = magDb (fundamentalBin);

        for (int b = guard + 1; b < n / 2 - guard; ++b) // skip DC region
            if (! isHarmonic[(size_t) b])
                r.worstAliasDbc = std::max (r.worstAliasDbc, magDb (b) - r.fundamentalDbfs);

        return r;
    }
}

TEST_CASE ("Aliasing report", "[.quality]")
{
    for (int factor : { 2, 4, 8, 16 })
        for (int m = 0; m < numModels; ++m)
            for (float drive : { 5.0f, 10.0f })
            {
                const auto r = measureAliasing (static_cast<Model> (m), factor, drive, 44100.0, 7000.0);
                WARN (params::modelNames()[m] << " " << factor << "x drive " << drive
                      << ": worst alias " << juce::String (r.worstAliasDbc, 1) << " dBc");
            }
}

TEST_CASE ("Standard quality keeps aliasing far below the signal", "[chain][aliasing]")
{
    // Torture test: a pure 7 kHz tone at 44.1 kHz, where everything from the 4th harmonic up
    // folds back into the audio band. Drive 5 (+15 dB into the circuit) is already heavy for
    // such a high, pure tone; musical material aliases far less.
    for (int m = 0; m < numModels; ++m)
    {
        const auto r = measureAliasing (static_cast<Model> (m), 4, 5.0f, 44100.0, 7000.0);
        INFO (params::modelNames()[m] << ": worst alias " << r.worstAliasDbc << " dBc");
        CHECK (r.worstAliasDbc < (m == static_cast<int> (Model::classA) ? -70.0 : -50.0)); // measured: Class A -75, Reel -69, Desk -74, Fuzz -76, Valve -109
    }
}
