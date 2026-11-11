// Measurements behind NOTES.md (hidden: run with "[.halo-measure]").
#include "dsp/algorithms/halo/HaloEngine.h"
#include "support/EngineGates.h"
#include "HaloSpectrum.h"

#include <cstdio>

using namespace cerulean;

TEST_CASE ("Halo measure: mid-band RT60 per interval and amount", "[.halo-measure]")
{
    auto e = gates::makeEngine<dsp::HaloEngine> (48000.0);
    for (int idx = 0; idx < 5; ++idx)
        for (float amount : { 0.0f, 0.5f, 1.0f })
        {
            std::printf ("interval %d amount %.2f:", idx, amount);
            for (float decay : { 1.0f, 2.5f, 6.0f })
            {
                dsp::EngineParams p;
                p.paramA = (float) idx / 4.0f;
                p.paramB = amount;
                p.decaySeconds = decay;
                const auto ir = analysis::impulseResponse (*e, p, 48000.0, decay * 1.3 + 0.5);
                std::printf ("  %.1f -> %.2f (%+.0f%%)", decay, analysis::midBandRt60 (ir),
                             100.0 * (analysis::midBandRt60 (ir) / decay - 1.0));
            }
            std::printf ("\n");
        }
}

TEST_CASE ("Halo measure: centroid over time for a 220 Hz note", "[.halo-measure]")
{
    auto e = gates::makeEngine<dsp::HaloEngine> (48000.0);
    const auto note = gates::sine (48000.0, 0.5, 220.0, 0.25f);
    for (int idx = 0; idx < 5; ++idx)
        for (float amount : { 0.0f, 0.5f, 1.0f })
        {
            dsp::EngineParams p;
            p.paramA = (float) idx / 4.0f;
            p.paramB = amount;
            p.decaySeconds = 6.0f;
            e->reset();
            const auto out = analysis::renderEngine (*e, p, note, 48000 * 7);
            std::printf ("interval %d amount %.2f centroid:", idx, amount);
            for (double t = 0.5; t < 6.5; t += 1.0)
                std::printf (" %6.0f", halotest::centroidHz (out.left, 48000.0, (size_t) (t * 48000), (size_t) ((t + 1.0) * 48000)));
            std::printf ("   level dB:");
            for (double t = 0.5; t < 6.5; t += 1.0)
                std::printf (" %6.1f", analysis::toDb (analysis::rms (out.left, (size_t) (t * 48000), (size_t) ((t + 1.0) * 48000))));
            std::printf ("\n");
        }
}

namespace
{
    // Envelope modulation of one band: RMS envelope at 1 kHz, then the fraction of its
    // (mean-removed) variance in 8-60 Hz (where grain-rate flutter would sit), and its depth.
    struct Flutter { double modIndex, fastShare; };

    Flutter flutter (const std::vector<float>& x, double fs, double fc, size_t start, size_t end)
    {
        auto f1 = analysis::Biquad::bandpass (fc, 6.0, fs);
        auto f2 = f1;
        auto lp1 = analysis::Biquad::lowpass (150.0, 0.7071, fs);
        auto lp2 = lp1;
        std::vector<double> env;
        const auto hop = (size_t) (fs / 1000.0);
        for (size_t i = 0; i < end; ++i)
        {
            const auto y = f2.process (f1.process (x[i]));
            const auto e = lp2.process (lp1.process (y * y));
            if (i >= start && (i - start) % hop == 0)
                env.push_back (std::sqrt (std::max (e, 0.0)));
        }
        double mean = 0.0;
        for (auto v : env) mean += v;
        mean /= (double) env.size();
        // Split the variance: slow part (< 8 Hz, moving average over 125 ms) vs the rest.
        double total = 0.0, fast = 0.0;
        const size_t w = 62;
        for (size_t i = w; i + w < env.size(); ++i)
        {
            double m = 0.0;
            for (size_t k = i - w; k <= i + w; ++k) m += env[k];
            m /= (double) (2 * w + 1);
            total += (env[i] - mean) * (env[i] - mean);
            fast += (env[i] - m) * (env[i] - m);
        }
        const auto n = (double) (env.size() - 2 * w);
        return { std::sqrt (total / n) / mean, fast / std::max (total, 1.0e-30) };
    }
}

TEST_CASE ("Halo measure: flutter of a held note's shifted partial", "[.halo-measure]")
{
    auto e = gates::makeEngine<dsp::HaloEngine> (48000.0);
    const auto note = gates::sine (48000.0, 6.0, 440.0, 0.25f);
    for (int idx : { 0, 3, 4 })
        for (float amount : { 0.5f, 1.0f })
        {
            dsp::EngineParams p;
            p.paramA = (float) idx / 4.0f;
            p.paramB = amount;
            p.decaySeconds = 4.0f;
            e->reset();
            const auto out = analysis::renderEngine (*e, p, note, 48000 * 6);
            const auto band = idx == 0 ? 220.0 : 880.0;
            const auto f = flutter (out.left, 48000.0, band, 48000 * 2, 48000 * 6);
            const auto ref = flutter (out.left, 48000.0, 440.0, 48000 * 2, 48000 * 6);
            std::printf ("interval %d amount %.1f: shifted band %4.0f Hz mod index %.2f (fast share %.2f); "
                         "unshifted 440 Hz mod index %.2f (fast %.2f)\n",
                         idx, amount, band, f.modIndex, f.fastShare, ref.modIndex, ref.fastShare);
        }
}

TEST_CASE ("Halo measure: click crest per automated control", "[.halo-measure]")
{
    const double fs = 48000.0;
    const auto in = gates::sine (fs, 6.0, 220.0, 0.25f);
    auto e = gates::makeEngine<dsp::HaloEngine> (fs);
    const dsp::EngineParams base;
    const auto still = analysis::renderEngine (*e, base, in, in.size());
    const auto stillCrest = gates::thirdDifferencePeak (still.left, 48000) / analysis::rms (still.left, 48000);
    std::printf ("static crest %.1f dB\n", analysis::toDb (stillCrest));

    const char* names[] = { "decay", "size", "mod", "tone", "lowEnd", "diffusion", "A", "B", "all" };
    for (int which = 0; which < 9; ++which)
    {
        e->reset();
        dsp::Rng rng (31);
        auto target = base;
        gates::SmoothedAutomation automation (base, fs);
        size_t nextJump = 0;
        const auto moving = analysis::renderEngine (*e, base, in, in.size(), [&] (size_t pos)
        {
            if (pos >= nextJump)
            {
                target = base;
                const auto v = std::array<float, 9> { 0.3f + 8.0f * rng.uniform(), 0.3f + 1.7f * rng.uniform(), rng.uniform(),
                                                      rng.bipolar(), 0.5f + 1.5f * rng.uniform(), rng.uniform(),
                                                      rng.uniform(), rng.uniform(), 0.0f };
                if (which == 0 || which == 8) target.decaySeconds = v[0];
                if (which == 1 || which == 8) target.sizeScale = v[1];
                if (which == 2 || which == 8) target.mod = v[2];
                if (which == 3 || which == 8) target.tone = v[3];
                if (which == 4 || which == 8) target.lowEnd = v[4];
                if (which == 5 || which == 8) target.diffusion = v[5];
                if (which == 6 || which == 8) target.paramA = v[6];
                if (which == 7 || which == 8) target.paramB = v[7];
                nextJump += 12000;
            }
            return automation.step (target);
        });
        const auto crest = gates::thirdDifferencePeak (moving.left, 48000) / analysis::rms (moving.left, 48000);
        size_t at = 0; double pk = 0;
        for (size_t i = 48000; i < moving.size(); ++i)
        {
            const auto d = std::abs ((double) moving.left[i] - 3.0 * moving.left[i - 1] + 3.0 * moving.left[i - 2] - moving.left[i - 3]);
            if (d > pk) { pk = d; at = i; }
        }
        std::printf ("%-10s crest %.1f dB (peak at %.3f s)\n", names[which], analysis::toDb (crest), (double) at / fs);
    }
}

TEST_CASE ("Halo measure: static 3rd-difference crest per interval", "[.halo-measure]")
{
    const double fs = 48000.0;
    const auto in = gates::sine (fs, 6.0, 220.0, 0.25f);
    auto e = gates::makeEngine<dsp::HaloEngine> (fs);
    for (int idx = 0; idx < 5; ++idx)
        for (float amount : { 0.0f, 0.5f, 1.0f })
        {
            dsp::EngineParams p;
            p.paramA = (float) idx / 4.0f;
            p.paramB = amount;
            e->reset();
            const auto out = analysis::renderEngine (*e, p, in, in.size());
            const auto crest = gates::thirdDifferencePeak (out.left, 48000) / analysis::rms (out.left, 48000);
            const auto hf = analysis::highBandRms (out.left, fs, 4000.0, 48000, out.size()) / analysis::rms (out.left, 48000);
            std::printf ("interval %d amount %.1f: static crest %.1f dB, >4 kHz share %.1f dB\n", idx, amount,
                         analysis::toDb (crest), analysis::toDb (hf));
        }
}

TEST_CASE ("Halo measure: echo density profile per rate", "[.halo-measure]")
{
    for (float amount : { 0.0f, 0.5f })
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            auto e = gates::makeEngine<dsp::HaloEngine> (fs);
            dsp::EngineParams p;
            p.paramA = 0.75f;
            p.paramB = amount;
            p.decaySeconds = 2.5f;
            const auto ir = analysis::impulseResponse (*e, p, fs, 1.0);
            const auto band = analysis::decimateTo48k (ir.left, fs);
            const auto rate = fs > 48000.0 ? 48000.0 : fs;
            const auto prof = analysis::echoDensityProfile (band, rate);
            std::printf ("amount %.1f fs %6.0f: t0.9 = %.3f  profile 40..120 ms:", amount, fs, analysis::echoDensityTime (band, rate));
            for (size_t i = 30; i < 110 && i < prof.size(); i += 5)
                std::printf (" %.2f", prof[i]);
            std::printf ("\n");
        }
}

TEST_CASE ("Halo measure: high-band click detector", "[.halo-measure]")
{
    const double fs = 48000.0;
    const auto in = gates::sine (fs, 6.0, 220.0, 0.25f);
    auto e = gates::makeEngine<dsp::HaloEngine> (fs);
    auto hfPeak = [&] (const analysis::Stereo& s)
    {
        auto f1 = analysis::Biquad::highpass (14000.0, 0.5412, fs), f2 = analysis::Biquad::highpass (14000.0, 1.3066, fs);
        double pk = 0.0;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const auto y = f2.process (f1.process (s.left[i]));
            if (i > 48000) pk = std::max (pk, std::abs (y));
        }
        return analysis::toDb (pk / analysis::rms (s.left, 48000));
    };
    for (int idx = 0; idx < 5; ++idx)
    {
        dsp::EngineParams p;
        p.paramA = (float) idx / 4.0f;
        p.paramB = 1.0f;
        e->reset();
        std::printf ("static interval %d: HF peak %.1f dB\n", idx, hfPeak (analysis::renderEngine (*e, p, in, in.size())));
    }
    e->reset();
    dsp::Rng rng (31);
    dsp::EngineParams base, target;
    base.paramB = 1.0f;
    gates::SmoothedAutomation automation (base, fs);
    size_t nextJump = 0;
    const auto moving = analysis::renderEngine (*e, base, in, in.size(), [&] (size_t pos)
    {
        if (pos >= nextJump) { target = base; target.paramA = rng.uniform(); nextJump += 6000; }
        return automation.step (target);
    });
    std::printf ("interval automation: HF peak %.1f dB\n", hfPeak (moving));
}

TEST_CASE ("Halo measure: CPU worst case (+12/+19, Amount 1)", "[.halo-measure]")
{
    auto e = gates::makeEngine<dsp::HaloEngine> (48000.0);
    const auto in = gates::noise (48000.0, 20.0);
    dsp::EngineParams p;
    p.paramA = 1.0f;
    p.paramB = 1.0f;
    p.decaySeconds = 4.0f;
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = analysis::renderEngine (*e, p, in, in.size());
    const auto s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("worst case CPU %.2f %% (peak %.2f)\n", 100.0 * s / 20.0, analysis::peak (out));
}

TEST_CASE ("Halo measure: mid-band RT60 vs Size", "[.halo-measure]")
{
    auto e = gates::makeEngine<dsp::HaloEngine> (48000.0);
    for (float size : { 0.3f, 2.0f })
        for (int idx : { 0, 3, 4 })
            for (float amount : { 0.0f, 0.5f, 1.0f })
            {
                std::printf ("size %.1f interval %d amount %.2f:", size, idx, amount);
                for (float decay : { 1.0f, 2.5f, 6.0f })
                {
                    dsp::EngineParams p;
                    p.paramA = (float) idx / 4.0f;
                    p.paramB = amount;
                    p.decaySeconds = decay;
                    p.sizeScale = size;
                    const auto ir = analysis::impulseResponse (*e, p, 48000.0, decay * 1.3 + 0.5);
                    std::printf ("  %.1f -> %.2f (%+.0f%%)", decay, analysis::midBandRt60 (ir),
                                 100.0 * (analysis::midBandRt60 (ir) / decay - 1.0));
                }
                std::printf ("\n");
            }
}
