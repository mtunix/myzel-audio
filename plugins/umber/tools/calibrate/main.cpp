// UmberCalibrate — measures every circuit model through the full signal chain and writes the
// Auto gain tables (src/dsp/chain/AutoGainTables.h).
//
// Method: deterministic pink noise at -18 dBFS RMS is processed with Auto gain OFF, Mix 100 %,
// filters off and Tone flat, on a grid of drive levels, Bias and Emphasis settings. Input and output
// loudness are measured with the ITU-R BS.1770 K-weighting filter; the compensation is the
// loudness difference. Runs the measurements in parallel on all cores.
//
// Usage: UmberCalibrate [output-header-path]
//        UmberCalibrate --report      (no file written: prints how far Emphasis and Bias move
//                                         the loudness with the current tables)

#include "dsp/chain/AutoGain.h"
#include "dsp/chain/Loudness.h"
#include "dsp/chain/SaturatorChain.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cmath>
#include <iostream>
#include <random>
#include <thread>

using namespace umber;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr double seconds = 4.0;
    constexpr double settleSeconds = 0.5;
    constexpr float programmeDb = -18.0f;

    double kWeightedRmsDb (const std::vector<float>& x, int skip)
    {
        return dsp::KWeightedLoudness::measureDb (x, skip);
    }

    /** Deterministic pink noise (Paul Kellet's refined filter), normalised to programmeDb RMS. */
    std::vector<float> pinkNoise (int numSamples)
    {
        std::mt19937 rng (20261110);
        std::normal_distribution<double> white (0.0, 1.0);

        std::vector<float> out ((size_t) numSamples);
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        double sumSq = 0.0;

        for (auto& v : out)
        {
            const double w = white (rng);
            b0 = 0.99886 * b0 + w * 0.0555179;
            b1 = 0.99332 * b1 + w * 0.0750759;
            b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856;
            b4 = 0.55000 * b4 + w * 0.5329522;
            b5 = -0.7616 * b5 - w * 0.0168980;
            const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
            b6 = w * 0.115926;
            v = (float) pink;
            sumSq += pink * pink;
        }

        const double rms = std::sqrt (sumSq / numSamples);
        const double target = std::pow (10.0, programmeDb / 20.0);
        for (auto& v : out)
            v = (float) (v * target / rms);

        return out;
    }

    struct Point
    {
        Model model;
        float driveDb;
        float bias;
        float emphasis = 0.0f;
        bool autoGain = false;
    };

    /** Output loudness minus input loudness (dB) for one setting. */
    float measureGainDb (const Point& pt, const std::vector<float>& programme, double inputLoudness)
    {
        dsp::SaturatorChain chain;
        chain.prepare (sampleRate, blockSize, 1);

        dsp::ChainSettings s;
        s.model = pt.model;
        s.autoGain = pt.autoGain;
        s.bias = pt.bias;
        s.emphasis = pt.emphasis;
        s.slam = pt.driveDb > dsp::SaturatorChain::driveDb (params::driveMax, false);
        // invert the drive law: driveDb = -6 + 4.2 * knob (+20 with Slam)
        s.driveKnob = juce::jlimit (0.0f, params::driveMax, (pt.driveDb + 6.0f - (s.slam ? 20.0f : 0.0f)) / 4.2f);
        s.realtimeFactor = 4;
        s.renderFactor = 0;

        auto signal = programme;
        juce::AudioBuffer<float> buffer (1, blockSize);
        dsp::ChainMeters meters;

        for (size_t start = 0; start < signal.size(); start += blockSize)
        {
            const int len = (int) std::min ((size_t) blockSize, signal.size() - start);
            buffer.setSize (1, len, false, false, true);
            buffer.copyFrom (0, 0, signal.data() + start, len);
            chain.process (buffer, s, false, meters);
            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + len, signal.begin() + (long) start);
        }

        return (float) (kWeightedRmsDb (signal, (int) (settleSeconds * sampleRate)) - inputLoudness);
    }

    /** Measures every point on all cores. */
    std::vector<float> measureAll (const std::vector<Point>& points, const std::vector<float>& programme, double inputLoudness)
    {
        std::vector<float> results (points.size(), 0.0f);
        std::atomic<size_t> next { 0 };
        std::atomic<size_t> done { 0 };

        auto worker = [&]
        {
            for (size_t i = next++; i < points.size(); i = next++)
            {
                results[i] = measureGainDb (points[i], programme, inputLoudness);
                const auto d = ++done;
                if (d % 50 == 0 || d == points.size())
                    std::cerr << "\r  " << d << " / " << points.size() << std::flush;
            }
        };

        std::vector<std::thread> threads;
        const auto numThreads = std::max (1u, std::thread::hardware_concurrency());
        for (unsigned t = 0; t < numThreads; ++t)
            threads.emplace_back (worker);
        for (auto& t : threads)
            t.join();

        std::cerr << std::endl;
        return results;
    }

    float driveAt (int i)
    {
        return dsp::AutoGain::minDriveDb
               + (dsp::AutoGain::maxDriveDb - dsp::AutoGain::minDriveDb) * (float) i / (float) (dsp::AutoGain::tableSize - 1);
    }

    /** With Auto gain ON: how far the output loudness strays from the input, over a grid of
        drive, Bias and Emphasis settings. Prints the worst deviation per model. */
    int report (const std::vector<float>& programme, double inputLoudness)
    {
        // Between the calibration grid points (drive steps of 2 dB, bias/emphasis steps of 0.5),
        // so the report includes the interpolation error.
        const float drives[] = { -5.0f, 7.0f, 19.0f, 31.0f, 43.0f, 55.0f };
        const float biases[] = { -0.75f, -0.25f, 0.0f, 0.25f, 0.75f };
        const float emphases[] = { -0.75f, -0.25f, 0.0f, 0.25f, 0.75f };

        std::vector<Point> points;
        for (int m = 0; m < numModels; ++m)
            for (auto d : drives)
                for (auto b : biases)
                    for (auto e : emphases)
                        points.push_back ({ static_cast<Model> (m), d, b, e, true });

        const auto results = measureAll (points, programme, inputLoudness);

        std::printf ("\nAuto gain ON, loudness out - in (dB), between calibration points. Worst per model:\n");
        std::printf ("%-8s %14s %14s %14s %14s\n", "model", "bias=0,emph=0", "any bias", "any emphasis", "any of both");

        for (int m = 0; m < numModels; ++m)
        {
            float nominal = 0.0f, anyBias = 0.0f, anyEmph = 0.0f, all = 0.0f;
            for (size_t i = 0; i < points.size(); ++i)
            {
                if ((int) points[i].model != m)
                    continue;
                const auto dev = std::abs (results[i]);
                if (points[i].bias == 0.0f && points[i].emphasis == 0.0f) nominal = std::max (nominal, dev);
                if (points[i].emphasis == 0.0f) anyBias = std::max (anyBias, dev);
                if (points[i].bias == 0.0f) anyEmph = std::max (anyEmph, dev);
                all = std::max (all, dev);
            }
            std::printf ("%-8s %14.2f %14.2f %14.2f %14.2f\n", params::modelNames()[m].toRawUTF8(), nominal, anyBias, anyEmph, all);
        }

        return 0;
    }
}

int main (int argc, char* argv[])
{
    const auto programme = pinkNoise ((int) (seconds * sampleRate));
    const auto inputLoudness = kWeightedRmsDb (programme, (int) (settleSeconds * sampleRate));

    if (argc > 1 && juce::String (argv[1]) == "--report")
        return report (programme, inputLoudness);

    const juce::File output (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                      : juce::File::getCurrentWorkingDirectory().getChildFile ("src/dsp/chain/AutoGainTables.h"));

    using dsp::AutoGain;

    std::vector<Point> points;
    for (int m = 0; m < numModels; ++m)
        for (int e = 0; e < AutoGain::emphasisPoints; ++e)
            for (int b = 0; b < AutoGain::biasPoints; ++b)
                for (int i = 0; i < AutoGain::tableSize; ++i)
                    points.push_back ({ static_cast<Model> (m), driveAt (i), AutoGain::biasAt (b), AutoGain::emphasisAt (e) });

    std::cerr << "Measuring " << points.size() << " settings" << std::endl;
    const auto results = measureAll (points, programme, inputLoudness);

    juce::String header;
    header << "#pragma once\n\n"
           << "// GENERATED by tools/calibrate (UmberCalibrate). Do not edit by hand:\n"
           << "// re-run the tool whenever a model's sound changes.\n"
           << "//\n"
           << "// Per model: " << AutoGain::emphasisPoints << " Emphasis blocks x " << AutoGain::biasPoints
           << " Bias rows (each from -1 to +1); columns: drive from "
           << AutoGain::minDriveDb << " dB to " << AutoGain::maxDriveDb << " dB in " << AutoGain::tableSize << " steps.\n"
           << "// Values: gain (dB) that restores the K-weighted loudness of a -18 dBFS RMS pink-noise programme.\n\n"
           << "#include \"dsp/chain/AutoGain.h\"\n\n"
           << "namespace umber::dsp\n{\n\n"
           << "inline constexpr std::array<AutoGain::ModelTables, " << numModels << "> autoGainTables {{\n";

    size_t k = 0;
    for (int m = 0; m < numModels; ++m)
    {
        header << "    // " << params::modelNames()[m] << "\n    {{\n";

        for (int e = 0; e < AutoGain::emphasisPoints; ++e)
        {
            header << "      // emphasis " << juce::String (AutoGain::emphasisAt (e), 2) << "\n      {{\n";

            for (int b = 0; b < AutoGain::biasPoints; ++b)
            {
                header << "        {{ ";

                for (int i = 0; i < AutoGain::tableSize; ++i)
                {
                    const auto comp = -results[k++];
                    header << juce::String (comp, 3) << "f" << (i + 1 < AutoGain::tableSize ? ", " : "");
                    if (i % 8 == 7 && i + 1 < AutoGain::tableSize)
                        header << "\n           ";
                }

                header << " }},   // bias " << juce::String (AutoGain::biasAt (b), 2) << "\n";
            }

            header << "      }},\n";
        }

        header << "    }},\n";
    }

    header << "}};\n\n} // namespace umber::dsp\n";

    if (! output.replaceWithText (header))
    {
        std::cerr << "Could not write " << output.getFullPathName() << std::endl;
        return 1;
    }

    std::cout << "Wrote " << output.getFullPathName() << std::endl;
    return 0;
}
