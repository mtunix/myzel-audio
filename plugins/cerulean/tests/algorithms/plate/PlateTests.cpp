#include "dsp/algorithms/plate/PlateEngine.h"
#include "support/EngineGates.h"

namespace
{
    cerulean::gates::Options plateOptions()
    {
        cerulean::gates::Options o;
        // A plate is the fastest-diffusing algorithm: the cross-rotated input diffusers and the
        // four-point tank injection make the tail dense within ~20 ms, so hold it to 50 ms
        // (tighter than the 150 ms default).
        o.echoDensityTargetS = 0.05;
        return o;
    }

    using cerulean::analysis::Stereo;

    /** Std (dB) of the 5 ms RMS envelope around its linear trend over [t0, t1): graininess. */
    double envelopeRoughness (const std::vector<float>& x, double fs, double t0, double t1)
    {
        const auto hop = (size_t) (0.005 * fs);
        std::vector<double> db;
        for (auto i = (size_t) (t0 * fs); i + hop <= (size_t) (t1 * fs); i += hop)
            db.push_back (cerulean::analysis::toDb (cerulean::analysis::rms (x, i, i + hop)));

        const auto n = (double) db.size();
        double st = 0, sy = 0, stt = 0, sty = 0;
        for (size_t i = 0; i < db.size(); ++i)
        {
            st += (double) i; sy += db[i]; stt += (double) i * (double) i; sty += (double) i * db[i];
        }
        const auto slope = (n * sty - st * sy) / (n * stt - st * st);
        const auto icpt = (sy - slope * st) / n;
        double var = 0.0;
        for (size_t i = 0; i < db.size(); ++i)
            var += std::pow (db[i] - (icpt + slope * (double) i), 2.0);
        return std::sqrt (var / n);
    }

    double roughness (const Stereo& ir)
    {
        return 0.5 * (envelopeRoughness (ir.left, ir.sampleRate, 0.02, 0.3)
                      + envelopeRoughness (ir.right, ir.sampleRate, 0.02, 0.3));
    }

    double meanEchoDensity (const std::vector<float>& x, double fs, double t0, double t1)
    {
        const auto profile = cerulean::analysis::echoDensityProfile (x, fs);   // 1 ms hop, 20 ms window
        double sum = 0.0;
        int count = 0;
        for (auto i = (size_t) (t0 * 1000.0); i < (size_t) (t1 * 1000.0) && i < profile.size(); ++i, ++count)
            sum += profile[i];
        return count > 0 ? sum / count : 0.0;
    }
}

CERULEAN_ENGINE_GATES ("[plate]", cerulean::dsp::PlateEngine, plateOptions())

TEST_CASE ("Plate: Density goes from grainy to smooth", "[plate]")
{
    using namespace cerulean;
    auto e = gates::makeEngine<dsp::PlateEngine> (48000.0);
    auto p = dsp::EngineParams {};
    p.decaySeconds = 3.0f;

    auto at = [&] (float density)
    {
        p.paramA = density;
        return analysis::impulseResponse (*e, p, 48000.0, 1.0);
    };
    const auto grainy = at (0.0f), middle = at (0.5f), smooth = at (1.0f);

    // Grainy: the onset is a spray of separate echoes (echo density well below a diffuse 1).
    const auto edGrainy = meanEchoDensity (grainy.left, 48000.0, 0.01, 0.05);
    const auto edMiddle = meanEchoDensity (middle.left, 48000.0, 0.01, 0.05);
    INFO ("echo density 20-60 ms: Density 0 -> " << edGrainy << ", 0.5 -> " << edMiddle);
    CHECK (edGrainy < 0.75);
    CHECK (edMiddle > 0.85);

    // Smoothness: the envelope's fluctuation around its decay over the first 300 ms.
    const auto r0 = roughness (grainy), r5 = roughness (middle), r1 = roughness (smooth);
    INFO ("envelope roughness (dB): Density 0 -> " << r0 << ", 0.5 -> " << r5 << ", 1 -> " << r1);
    CHECK (r0 > 2.0 * r5);
    CHECK (r1 < r5);
}

TEST_CASE ("Plate: Metal lets the highs ring", "[plate]")
{
    using namespace cerulean;
    auto e = gates::makeEngine<dsp::PlateEngine> (48000.0);
    auto p = dsp::EngineParams {};
    p.decaySeconds = 3.0f;

    auto at = [&] (float metal)
    {
        p.paramB = metal;
        return analysis::impulseResponse (*e, p, 48000.0, 4.4);
    };
    const auto damped = at (0.0f), ringing = at (1.0f);

    const auto ratioDamped = analysis::bandRt60 (damped, 8000.0) / analysis::midBandRt60 (damped);
    const auto ratioRinging = analysis::bandRt60 (ringing, 8000.0) / analysis::midBandRt60 (ringing);
    INFO ("RT60(8 kHz)/RT60(mid): Metal 0 -> " << ratioDamped << ", Metal 1 -> " << ratioRinging);
    CHECK (ratioRinging > 0.8);
    CHECK (ratioRinging > 1.6 * ratioDamped);

    // The sheen: high-band energy left in the tail, relative to the whole tail.
    auto sheenDb = [] (const analysis::Stereo& ir)
    {
        const auto a = (size_t) (1.0 * ir.sampleRate), b = (size_t) (2.0 * ir.sampleRate);
        return analysis::toDb (analysis::highBandRms (ir.left, ir.sampleRate, 6000.0, a, b))
             - analysis::toDb (analysis::rms (ir.left, a, b));
    };
    INFO ("high band (> 6 kHz) re tail, 1-2 s: Metal 0 -> " << sheenDb (damped) << " dB, Metal 1 -> " << sheenDb (ringing) << " dB");
    CHECK (sheenDb (ringing) > sheenDb (damped) + 6.0);
}

TEST_CASE ("Plate: instantly at full level, no bloom", "[plate]")
{
    using namespace cerulean;
    auto e = gates::makeEngine<dsp::PlateEngine> (48000.0);
    auto p = dsp::EngineParams {};
    p.decaySeconds = 3.0f;
    const auto ir = analysis::impulseResponse (*e, p, 48000.0, 0.5);

    auto windowDb = [&] (double t0, double t1)
    {
        const auto a = (size_t) (t0 * 48000.0), b = (size_t) (t1 * 48000.0);
        return analysis::toDb (std::hypot (analysis::rms (ir.left, a, b), analysis::rms (ir.right, a, b)));
    };

    double loudest = -300.0;
    for (double t = 0.0; t < 0.3; t += 0.01)
        loudest = std::max (loudest, windowDb (t, t + 0.01));

    // 20-40 ms already within 3 dB of the loudest 10 ms of the first 300 ms.
    INFO ("20-40 ms: " << windowDb (0.02, 0.04) << " dB, loudest 10 ms window: " << loudest << " dB");
    CHECK (windowDb (0.02, 0.04) > loudest - 3.0);
}

TEST_CASE ("Plate: short decays are honoured too", "[plate]")
{
    // Every delay element (input diffusers and in-tank allpasses included) carries its share of
    // the decay, so even a 0.3 s plate decays at the set rate instead of the diffusers' own ring.
    using namespace cerulean;
    auto e = gates::makeEngine<dsp::PlateEngine> (48000.0);
    auto p = dsp::EngineParams {};
    for (const auto decay : { 0.3f, 0.5f })
    {
        p.decaySeconds = decay;
        const auto rt = analysis::midBandRt60 (analysis::impulseResponse (*e, p, 48000.0, 1.0));
        INFO ("Decay " << decay << " s -> mid-band RT60 " << rt << " s");
        CHECK (std::abs (rt - decay) < 0.1 * decay);
    }
}
