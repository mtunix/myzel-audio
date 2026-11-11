// Measurements behind NOTES.md (hidden: run with "[.choir-measure]").
#include "dsp/algorithms/choir/ChoirEngine.h"
#include "support/EngineGates.h"
#include "ChoirSpectrum.h"

#include <cstdio>

using namespace cerulean;

namespace
{
    /** 1/12-octave spectrum (dB) of the steady output to pink noise, 150 Hz .. 5 kHz. */
    std::vector<std::pair<double, double>> spectrum (const analysis::Stereo& s, double t0, double t1)
    {
        std::vector<std::pair<double, double>> out;
        for (double f = 150.0; f < 5000.0; f *= std::exp2 (1.0 / 12.0))
            out.push_back ({ f, choirtest::bandDb (s, f, t0, t1, 17.0) });
        return out;
    }
}

TEST_CASE ("Choir measure: spectra per vowel", "[.choir-measure]")
{
    auto e = gates::makeEngine<dsp::ChoirEngine> (48000.0);
    const auto in = gates::noise (48000.0, 4.0);
    for (float b : { 0.0f, 1.0f })
        for (int v = 0; v < 5; ++v)
        {
            dsp::EngineParams p;
            p.paramA = (float) v / 4.0f;
            p.paramB = b;
            p.mod = 0.0f;
            p.decaySeconds = 2.0f;
            e->reset();
            const auto out = analysis::renderEngine (*e, p, in, in.size());
            const auto sp = spectrum (out, 1.5, 4.0);
            double ref = 0.0;
            for (auto& [f, db] : sp) ref = std::max (ref, db);
            std::printf ("voices %.0f vowel %s:", 1 + 3 * b, dsp::maps::vowelNames[(size_t) v]);
            // Local maxima more than 3 dB above both neighbours 2 bins away.
            for (size_t i = 2; i + 2 < sp.size(); ++i)
                if (sp[i].second > sp[i - 1].second && sp[i].second >= sp[i + 1].second
                    && sp[i].second > sp[i - 2].second + 1.0 && sp[i].second > sp[i + 2].second + 1.0)
                    std::printf (" %4.0f Hz (%+.0f)", sp[i].first, sp[i].second - ref);
            const auto inDb = analysis::toDb (analysis::rms (in.left, 48000 * 2));
            std::printf ("  | level %+.1f dB, rho %.2f\n", analysis::toDb (analysis::rms (out.left, 48000 * 2)) - inDb,
                         analysis::correlation (out.left, out.right, 48000, out.size()));
        }
}

TEST_CASE ("Choir measure: echo density profile per rate", "[.choir-measure]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto e = gates::makeEngine<dsp::ChoirEngine> (fs);
        dsp::EngineParams p;
        p.decaySeconds = 2.5f;
        const auto ir = analysis::impulseResponse (*e, p, fs, 1.0);
        const auto band = analysis::decimateTo48k (ir.left, fs);
        const auto rate = fs > 48000.0 ? 48000.0 : fs;
        const auto prof = analysis::echoDensityProfile (band, rate);
        std::printf ("fs %6.0f: t0.9 = %.3f  profile 20..110 ms:", fs, analysis::echoDensityTime (band, rate));
        for (size_t i = 10; i < 100 && i < prof.size(); i += 5)
            std::printf (" %.2f", prof[i]);
        std::printf ("\n");
    }
}

TEST_CASE ("Choir measure: CPU worst case (4 voices, Mod 1)", "[.choir-measure]")
{
    auto e = gates::makeEngine<dsp::ChoirEngine> (48000.0);
    const auto in = gates::noise (48000.0, 20.0);
    dsp::EngineParams p;
    p.paramB = 1.0f;
    p.mod = 1.0f;
    p.decaySeconds = 4.0f;
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = analysis::renderEngine (*e, p, in, in.size());
    const auto s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("worst case CPU %.2f %% (peak %.2f)\n", 100.0 * s / 20.0, analysis::peak (out));
}
