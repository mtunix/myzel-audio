// CeruleanMeasure: renders every algorithm through its engine and prints the numbers the quality
// gates look at, so voicing work can be judged at a glance.
//
//   CeruleanMeasure                 all algorithms, summary table
//   CeruleanMeasure hall plate      only these
//   CeruleanMeasure --wav out/      also write each impulse response as a 32-bit float WAV
//   CeruleanMeasure --decay 4       IR measurements at this Decay (default 3 s)

#include "analysis/ReverbAnalysis.h"
#include "dsp/Algorithms.h"
#include "dsp/chain/EngineFactory.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

using namespace cerulean;

namespace
{
    const char* names[numAlgorithms] = { "room", "hall", "plate", "spring", "nimbus",
                                         "halo", "rise", "billow", "choir", "echo" };

    void writeWav (const std::string& path, const analysis::Stereo& s)
    {
        std::ofstream f (path, std::ios::binary);
        auto u32 = [&] (uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
        auto u16 = [&] (uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };
        const auto frames = (uint32_t) s.size();
        const auto dataBytes = frames * 2u * 4u;
        f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVE", 4);
        f.write ("fmt ", 4); u32 (16); u16 (3); u16 (2); u32 ((uint32_t) s.sampleRate);
        u32 ((uint32_t) s.sampleRate * 8u); u16 (8); u16 (32);
        f.write ("data", 4); u32 (dataBytes);
        for (size_t i = 0; i < s.size(); ++i)
        {
            f.write (reinterpret_cast<const char*> (&s.left[i]), 4);
            f.write (reinterpret_cast<const char*> (&s.right[i]), 4);
        }
    }
}

int main (int argc, char** argv)
{
    std::string wavDir;
    float decay = 3.0f;
    std::vector<int> selected;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--wav") == 0 && i + 1 < argc)
            wavDir = argv[++i];
        else if (std::strcmp (argv[i], "--decay") == 0 && i + 1 < argc)
            decay = std::stof (argv[++i]);
        else
            for (int a = 0; a < numAlgorithms; ++a)
                if (std::strcmp (argv[i], names[a]) == 0)
                    selected.push_back (a);
    }

    if (selected.empty())
        for (int a = 0; a < numAlgorithms; ++a)
            selected.push_back (a);

    const double fs = 48000.0;

    std::printf ("Decay %.2f s, 48 kHz, default controls\n\n", decay);
    std::printf ("%-8s %8s %8s %8s %8s %8s %8s | %8s %8s %8s %8s\n", "algo", "RT125", "RT250", "RTmid",
                 "RT4k", "RT8k", "ED0.9ms", "wet dB", "L/R rho", "mono dB", "CPU %");

    for (const auto a : selected)
    {
        auto engine = dsp::createEngine (static_cast<Algorithm> (a), 1);
        engine->prepare (fs, dsp::Engine::maxSubBlock);
        engine->reset();

        dsp::EngineParams p;
        p.decaySeconds = decay;
        const auto ir = analysis::impulseResponse (*engine, p, fs, decay * 1.4 + 1.0);

        if (! wavDir.empty())
            writeWav (wavDir + "/" + names[a] + ".wav", ir);

        // Wet level: steady pink noise at Decay 2 s.
        auto levelParams = p;
        levelParams.decaySeconds = 2.0f;
        analysis::Stereo noise;
        noise.sampleRate = fs;
        noise.left = analysis::pinkNoise ((size_t) (fs * 6.0), 99);
        noise.right = analysis::pinkNoise ((size_t) (fs * 6.0), 100);
        engine->reset();

        const auto t0 = std::chrono::steady_clock::now();
        const auto wet = analysis::renderEngine (*engine, levelParams, noise, noise.size());
        const auto cpu = 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 6.0;

        const auto start = (size_t) (fs * 3.0);
        const auto wetDb = analysis::toDb (0.5 * (analysis::rms (wet.left, start) + analysis::rms (wet.right, start)))
                         - analysis::toDb (0.5 * (analysis::rms (noise.left, start) + analysis::rms (noise.right, start)));

        const auto tailStart = (size_t) (0.2 * fs), tailEnd = (size_t) (2.0 * fs);
        const auto rho = analysis::correlation (ir.left, ir.right, tailStart, tailEnd);
        std::vector<float> mono (ir.size());
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.5f * (ir.left[i] + ir.right[i]);
        const auto monoDb = analysis::toDb (analysis::rms (mono, tailStart, tailEnd))
                          - analysis::toDb (std::sqrt (0.5 * (std::pow (analysis::rms (ir.left, tailStart, tailEnd), 2.0)
                                                             + std::pow (analysis::rms (ir.right, tailStart, tailEnd), 2.0))));

        std::printf ("%-8s %8.2f %8.2f %8.2f %8.2f %8.2f %8.1f | %8.1f %8.2f %8.1f %8.2f\n", names[a],
                     analysis::bandRt60 (ir, 125.0), analysis::bandRt60 (ir, 250.0), analysis::midBandRt60 (ir),
                     analysis::bandRt60 (ir, 4000.0), analysis::bandRt60 (ir, 8000.0),
                     1000.0 * analysis::echoDensityTime (ir.left, fs), wetDb, rho, monoDb, cpu);
    }

    return 0;
}
