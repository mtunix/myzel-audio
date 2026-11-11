#include "dsp/algorithms/nimbus/NimbusEngine.h"
#include "support/EngineGates.h"
#include "algorithms/nimbus/TailMetrics.h"
#include "dsp/algorithms/hall/HallEngine.h"

#include <iostream>

namespace
{
    cerulean::gates::Options nimbusOptions()
    {
        cerulean::gates::Options o;
        o.cpuBudgetPercent = 5.0;   // PLAN.md budget for Nimbus/Billow/Echo
        return o;
    }
}

CERULEAN_ENGINE_GATES ("[nimbus]", cerulean::dsp::NimbusEngine, nimbusOptions())

namespace
{
    constexpr double fs = 48000.0;

    cerulean::analysis::Stereo nimbusIr (cerulean::dsp::EngineParams p, double seconds)
    {
        auto e = cerulean::gates::makeEngine<cerulean::dsp::NimbusEngine> (fs);
        return cerulean::analysis::impulseResponse (*e, p, fs, seconds);
    }

    /** White noise with an exponential decay (RT60 = rt): the ideal, perfectly diffuse tail. */
    std::vector<float> idealTail (double seconds, double rt)
    {
        cerulean::dsp::Rng rng (5);
        std::vector<float> x ((size_t) (seconds * fs));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = rng.bipolar() * (float) std::pow (10.0, -3.0 * (double) i / (fs * rt));
        return x;
    }

    /** RMS of the dB difference between the Welch spectra of two segments (150 Hz - 6 kHz). */
    double spectralDistanceDb (const std::vector<float>& x, size_t a0, size_t a1, size_t b0, size_t b1)
    {
        const auto sa = cerulean::metrics::welchSpectrum (x, a0, a1);
        const auto sb = cerulean::metrics::welchSpectrum (x, b0, b1);
        const auto binHz = fs / 8192.0;
        double sum = 0.0;
        int count = 0;
        for (auto k = (size_t) (150.0 / binHz); k < (size_t) (6000.0 / binHz); ++k)
        {
            sum += std::pow (10.0 * std::log10 (std::max (sa[k], 1.0e-30) / std::max (sb[k], 1.0e-30)), 2.0);
            ++count;
        }
        return std::sqrt (sum / count);
    }
}

TEST_CASE ("Nimbus: Density goes from a sparse cloud of echoes to a thick fog", "[nimbus]")
{
    using namespace cerulean;
    auto p = dsp::EngineParams {};
    p.decaySeconds = 6.0f;

    p.paramA = 0.0f;
    const auto sparse = nimbusIr (p, 2.0);
    p.paramA = 1.0f;
    const auto fog = nimbusIr (p, 2.0);

    // Separate echoes show up as an uneven envelope (10 ms frames around the decay trend) and
    // as a low echo density early on.
    const auto roughSparse = metrics::envelopeRoughnessDb (sparse.left, fs, 4800, 72000);
    const auto roughFog = metrics::envelopeRoughnessDb (fog.left, fs, 4800, 72000);
    const auto edSparse = metrics::meanEchoDensity (sparse.left, fs, 2400, 14400);
    const auto edFog = metrics::meanEchoDensity (fog.left, fs, 2400, 14400);
    INFO ("envelope roughness 0.1-1.5 s: Density 0 " << roughSparse << " dB, Density 1 " << roughFog
          << " dB; echo density 50-300 ms: " << edSparse << " vs " << edFog);
    CHECK (roughSparse > roughFog + 1.5);
    CHECK (edFog > edSparse + 0.2);
    CHECK (edFog > 0.9);
}

TEST_CASE ("Nimbus: Distance moves the cloud later, smeared and darker", "[nimbus]")
{
    using namespace cerulean;
    auto p = dsp::EngineParams {};
    p.decaySeconds = 6.0f;

    p.paramB = 0.0f;
    const auto close = nimbusIr (p, 2.0);
    p.paramB = 1.0f;
    const auto far = nimbusIr (p, 2.0);

    const auto arriveClose = metrics::energyArrivalSeconds (close, 0.1, 2.0);
    const auto arriveFar = metrics::energyArrivalSeconds (far, 0.1, 2.0);
    const auto hiClose = metrics::highShare (close.left, fs, 0, (size_t) (0.4 * fs));
    const auto hiFar = metrics::highShare (far.left, fs, 0, (size_t) (0.4 * fs));
    INFO ("10 % energy arrival: Distance 0 " << arriveClose * 1000.0 << " ms, Distance 1 " << arriveFar * 1000.0
          << " ms; share of onset energy above 3 kHz: " << hiClose << " vs " << hiFar);
    CHECK (arriveFar > arriveClose + 0.15);
    CHECK (hiFar < 0.9 * hiClose);
}

TEST_CASE ("Nimbus: long tails honour Decay and stay smooth, without ringing", "[nimbus]")
{
    using namespace cerulean;

    // Reference: an ideal diffuse tail (decaying white noise) through the same metrics.
    const auto ideal = idealTail (16.0, 30.0);
    const auto a = (size_t) (8.0 * fs), b = (size_t) (16.0 * fs);
    const auto idealPeak = metrics::spectralPeakinessDb (ideal, fs, a, b);

    struct Case { float mod, rate; };
    for (const auto c : { Case { 0.35f, 1.0f }, Case { 1.0f, 1.0f }, Case { 1.0f, 4.0f } })
    {
        auto p = dsp::EngineParams {};
        p.decaySeconds = 30.0f;
        p.mod = c.mod;
        p.modRate = c.rate;
        const auto ir = nimbusIr (p, 16.0);
        const auto peak = metrics::spectralPeakinessDb (ir.left, fs, a, b);
        const auto ed = metrics::meanEchoDensity (ir.left, fs, a, b);
        INFO ("Decay 30 s, Mod " << c.mod << " x" << c.rate << ": spectral peakiness 8-16 s " << peak
              << " dB (ideal diffuse tail " << idealPeak << " dB), echo density " << ed);
        CHECK (peak < idealPeak + 1.5);   // Mod 0 (a static network) reads +3.4 dB here
        CHECK (ed > 0.95);
    }

    for (const float decay : { 20.0f, 60.0f })
    {
        auto p = dsp::EngineParams {};
        p.decaySeconds = decay;
        const auto ir = nimbusIr (p, decay * 0.6 + 2.0);   // T30 needs -35 dB: 0.58 x RT60
        const auto rt = analysis::midBandRt60 (ir);
        INFO ("Decay " << decay << " s -> mid-band RT60 " << rt << " s");
        CHECK (std::abs (rt - decay) < 0.1 * decay);
    }
}

TEST_CASE ("Nimbus: a frozen cloud is static (modulation stops, no drift)", "[nimbus]")
{
    using namespace cerulean;
    auto e = gates::makeEngine<dsp::NimbusEngine> (fs);

    auto p = dsp::EngineParams {};
    p.decaySeconds = 10.0f;
    p.mod = 1.0f;
    p.modRate = 4.0f;
    auto frozen = p;
    frozen.freeze = 1.0f;

    const auto in = gates::noise (fs, 1.0);
    gates::SmoothedAutomation automation (p, fs);
    const auto out = analysis::renderEngine (*e, p, in, (size_t) (fs * 24.0), [&] (size_t pos)
    {
        return automation.step (pos < (size_t) fs ? p : frozen);
    });

    // A static (lossless, unmodulated) network rings on in fixed modes: the spectrum 4-8 s and
    // 20-24 s must match. With the modulation left running in freeze the modes wander: that
    // reads ~6 dB here (and still passes the energy-only freeze gate); stopped, ~0.7 dB.
    const auto d = spectralDistanceDb (out.left, (size_t) (4 * fs), (size_t) (8 * fs), (size_t) (20 * fs), (size_t) (24 * fs));
    INFO ("frozen spectrum drift 4-8 s vs 20-24 s: " << d << " dB rms");
    CHECK (d < 2.0);
}

// Not a test: prints the numbers NOTES.md quotes (run with "[.measure]").
TEST_CASE ("Nimbus: measurement dump", "[.measure][nimbus]")
{
    using namespace cerulean;
    auto e = gates::makeEngine<dsp::NimbusEngine> (fs);
    auto hall = gates::makeEngine<dsp::HallEngine> (fs);
    for (const float decay : { 1.0f, 3.0f, 10.0f })
    {
        auto p = dsp::EngineParams {};
        p.decaySeconds = decay;
        const auto ir = analysis::impulseResponse (*e, p, fs, decay * 1.3 + 1.0);
        std::cout << "decay " << decay << ": RT125 " << analysis::bandRt60 (ir, 125.0) << " RT250 " << analysis::bandRt60 (ir, 250.0)
                  << " mid " << analysis::midBandRt60 (ir) << " RT4k " << analysis::bandRt60 (ir, 4000.0)
                  << " RT8k " << analysis::bandRt60 (ir, 8000.0) << " ED0.9 " << analysis::echoDensityTime (ir.left, fs)
                  << " rho " << analysis::correlation (ir.left, ir.right, 9600, 96000) << "\n";
    }

    auto tail = [&] (dsp::Engine& eng, const char* name, dsp::EngineParams p)
    {
        const auto ir = analysis::impulseResponse (eng, p, fs, 16.0);
        const auto a = (size_t) (8.0 * fs), b = (size_t) (16.0 * fs);
        std::cout << name << " decay " << p.decaySeconds << " mod " << p.mod << " A " << p.paramA << " B " << p.paramB
                  << ": peakiness " << metrics::spectralPeakinessDb (ir.left, fs, a, b)
                  << " dB, ED(8-16 s) " << metrics::meanEchoDensity (ir.left, fs, a, b)
                  << ", roughness " << metrics::envelopeRoughnessDb (ir.left, fs, a, b) << " dB"
                  << ", ED(50-300 ms) " << metrics::meanEchoDensity (ir.left, fs, 2400, 14400)
                  << ", arrival10 " << metrics::energyArrivalSeconds (ir, 0.1, 2.0)
                  << ", arrival50 " << metrics::energyArrivalSeconds (ir, 0.5, 2.0)
                  << ", onset hi share " << metrics::highShare (ir.left, fs, 0, (size_t) (0.4 * fs)) << "\n";
    };

    for (const float decay : { 10.0f, 30.0f })
        for (const float m : { 0.0f, 0.35f, 1.0f })
        {
            auto p = dsp::EngineParams {};
            p.decaySeconds = decay;
            p.mod = m;
            const auto ir = analysis::impulseResponse (*e, p, fs, std::min (decay * 0.7 + 1.0, 22.0));
            std::cout << "decay " << decay << " mod " << m << ": RT500 " << analysis::bandRt60 (ir, 500.0) << " RT1k " << analysis::bandRt60 (ir, 1000.0)
                      << " RT2k " << analysis::bandRt60 (ir, 2000.0) << " mid " << analysis::midBandRt60 (ir) << "\n";
        }
    for (const float diff : { 0.0f, 0.75f })
        for (const float a : { 0.0f, 0.5f, 1.0f })
        {
            auto p = dsp::EngineParams {};
            p.decaySeconds = 6.0f;
            p.diffusion = diff;
            p.paramA = a;
            const auto ir = analysis::impulseResponse (*e, p, fs, 2.0);
            std::cout << "diffusion " << diff << " density " << a << ": ED(50-300ms) " << metrics::meanEchoDensity (ir.left, fs, 2400, 14400)
                      << " ED(300-1000ms) " << metrics::meanEchoDensity (ir.left, fs, 14400, 48000)
                      << " ED0.9 " << analysis::echoDensityTime (ir.left, fs)
                      << " rough(0.1-1.5s) " << metrics::envelopeRoughnessDb (ir.left, fs, 4800, 72000) << "\n";
        }
    dsp::EngineParams p;
    p.decaySeconds = 30.0f;
    tail (*hall, "hall", p);
    tail (*e, "nimbus", p);
    for (const float m : { 0.0f, 1.0f }) { auto q = p; q.mod = m; tail (*e, "nimbus", q); }
    for (const float m : { 0.0f, 1.0f }) { auto q = p; q.mod = 1.0f; q.modRate = 4.0f; q.paramA = m; tail (*e, "nimbus rate4", q); }
    for (const float a : { 0.0f, 1.0f }) { auto q = p; q.paramA = a; tail (*e, "nimbus", q); }
    for (const float b : { 0.0f, 1.0f }) { auto q = p; q.paramB = b; tail (*e, "nimbus", q); }
}
