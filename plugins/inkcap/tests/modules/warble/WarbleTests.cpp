/*
    Warble: the input to the bit at Damage 0 and Off; at Chaos 0 a pure LFO of the type's depth
    and rate (free or synced); Spread decorrelates only when asked; no clicks on any change; the
    same render for any block size and from any start.
*/

#include "dsp/modules/warble/WarbleControls.h"
#include "dsp/modules/warble/WarbleModule.h"
#include "dsp/modules/warble/WarbleShape.h"

#include "support/ModuleHarness.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::testing;
using analysis::Stereo;

namespace
{
    constexpr double fs = 48000.0;

    WarbleSettings settings (WarbleType type, float rate = 0.5f, bool sync = false, float spread = 0.0f)
    {
        WarbleSettings s;
        s.on = true;
        s.amount = 1.0f;
        s.type = type;
        s.rate = rate;
        s.sync = sync;
        s.spread = spread;
        return s;
    }

    Stereo sineInput (double hz, double seconds, double amplitude = 0.5)
    {
        const auto x = analysis::sine (hz, fs, seconds, amplitude);
        return { x, x };
    }

    Stereo render (const WarbleSettings& s, Macros m, const Stereo& in, int block = 512, Transport t = {})
    {
        WarbleModule module;
        module.prepare ({ fs, block, 2 });
        return renderModule (module, s, m, in, block, fs, t);
    }
}

namespace
{
    analysis::Stereo delayed (const analysis::Stereo& x, int samples)
    {
        auto d = x;
        for (auto* channel : { &d.left, &d.right })
        {
            channel->insert (channel->begin(), (size_t) samples, 0.0f);
            channel->resize (x.size());
        }
        return d;
    }
}

TEST_CASE ("Warble: Damage 0 and Off are the input at the head's rest, to the bit", "[warble]")
{
    const auto in = analysis::programme (fs, 2.0);
    const auto rested = delayed (in, WarbleModule::restSamples);
    for (int type = 0; type < 3; ++type)
        for (const auto chaos : { 0.0f, 1.0f })
        {
            INFO ("type " << type << ", chaos " << chaos);
            const auto zero = render (settings ((WarbleType) type, 0.3f, false, 1.0f), { 0.0f, chaos }, in);
            CHECK (analysis::identical (zero.left, rested.left));
            CHECK (analysis::identical (zero.right, rested.right));

            auto off = settings ((WarbleType) type);
            off.on = false;
            CHECK (analysis::identical (render (off, { 1.0f, chaos }, in).left, rested.left));
        }

    // Damage back to 0 while running: the centre glides home at its speed limit and the output is
    // the input again (Wow at full: a 5.8 ms centre, 1.2 s at 0.5 % of the speed).
    WarbleModule module;
    module.prepare ({ fs, 256, 2 });
    const auto s = settings (WarbleType::wow);
    const auto longer = analysis::programme (fs, 2.5);
    const auto home = 0.5 + warble::shapeOf (WarbleType::wow).maxDeviation / (2.0 * std::numbers::pi * warble::freeHz (WarbleType::wow, 0.5f))
                                / WarbleModule::centreSlope;
    const auto out = renderModule<WarbleModule, WarbleSettings> (module, s, { 1.0f, 0.3f }, longer, 256, fs, {},
                                                                 [] (int64_t start, WarbleSettings&, Macros& m) { m.damage = start < 24000 ? 1.0f : 0.0f; });
    CHECK (home < 2.0);
    const auto longerRested = delayed (longer, WarbleModule::restSamples);
    CHECK_FALSE (analysis::identical (out.left, longerRested.left, (size_t) (0.6 * fs), (size_t) (home * fs)));
    CHECK (analysis::identical (out.left, longerRested.left, (size_t) ((home + 0.1) * fs)));
}

TEST_CASE ("Warble: at Chaos 0 each type is a pure LFO at its rate and depth", "[warble]")
{
    for (int type = 0; type < 3; ++type)
    {
        const auto t = (WarbleType) type;
        const auto& shape = warble::shapeOf (t);
        const auto lfo = warble::freeHz (t, 0.5f);
        const auto seconds = std::max (3.0, 4.0 / lfo);
        const auto in = sineInput (1000.0, seconds + 1.0);
        const auto out = render (settings (t), { 1.0f, 0.0f }, in);

        const auto track = analysis::frequencyTrack (out.left, fs, (size_t) (0.5 * fs));
        const auto [amplitude, residual] = analysis::fitSine (track, lfo, fs);

        // The delay's swing is capped (longestSwingSeconds): Drift's slow rate reaches the cap first.
        const auto swing = std::min (shape.maxDeviation / (2.0 * std::numbers::pi * lfo), WarbleModule::longestSwingSeconds);
        const auto expected = 1000.0 * swing * 2.0 * std::numbers::pi * lfo;
        INFO ("type " << type << ": " << lfo << " Hz LFO, deviation " << amplitude << " Hz (expected " << expected
                      << "), residual " << residual << " Hz");
        CHECK (std::abs (amplitude - expected) < 0.03 * expected);
        CHECK (residual < 0.03 * expected);
    }

    // Chaos 1: the wander is no longer a sine.
    const auto in = sineInput (1000.0, 6.0);
    const auto out = render (settings (WarbleType::wow), { 1.0f, 1.0f }, in);
    const auto track = analysis::frequencyTrack (out.left, fs, (size_t) (0.5 * fs));
    const auto [amplitude, residual] = analysis::fitSine (track, warble::freeHz (WarbleType::wow, 0.5f), fs);
    CHECK (residual > amplitude * 0.3);
}

TEST_CASE ("Warble: Sync follows the tempo; Rate reads out per type and Sync", "[warble][readout]")
{
    WarbleModule module;
    module.prepare ({ fs, 512, 2 });
    const auto in = sineInput (500.0, 0.5);
    renderModule (module, settings (WarbleType::flutter, 0.5f, true), Macros {}, in, 512, fs, { true, 0.0, 90.0 });
    CHECK (std::abs (module.lfoHz() - 90.0 / 60.0 / 0.25) < 1.0e-9);     // 1/16 at 90 BPM: 6 Hz
    renderModule (module, settings (WarbleType::wow, 0.5f, true), Macros {}, in, 512, fs, { true, 0.0, 120.0 });
    CHECK (std::abs (module.lfoHz() - 0.5) < 1.0e-9);                     // 1/1 at 120 BPM

    CHECK (warble::rateText ((int) WarbleType::wow, false, 50.0f) == "0.55 Hz");
    CHECK (warble::rateText ((int) WarbleType::flutter, false, 50.0f) == "7.0 Hz");
    CHECK (warble::rateText ((int) WarbleType::flutter, false, 100.0f) == "28 Hz");
    CHECK (warble::rateText ((int) WarbleType::wow, true, 50.0f) == "1/1");
    CHECK (warble::rateText ((int) WarbleType::flutter, true, 50.0f) == "1/16");
    CHECK (warble::rateText ((int) WarbleType::drift, true, 50.0f) == "4/1");
    CHECK (warble::rateText ((int) WarbleType::drift, true, 0.0f) == "16/1");

    CHECK (std::abs (warble::rateFromText ((int) WarbleType::wow, false, "0.55 hz") - 50.0f) < 0.01f);
    CHECK (std::abs (warble::rateFromText ((int) WarbleType::flutter, true, "1/32") - 100.0f) < 0.01f);
    CHECK (std::abs (warble::rateFromText ((int) WarbleType::wow, true, "1/4d") - 100.0f) < 0.01f);
    CHECK (warble::rateFromText ((int) WarbleType::wow, true, "1/64") < 0.0f);
    CHECK (warble::rateFromText ((int) WarbleType::wow, false, "fast") < 0.0f);

    for (int type = 0; type < 3; ++type)
        for (const auto synced : { false, true })
            for (float p = 0.0f; p <= 100.0f; p += 12.5f)
            {
                const auto text = warble::rateText (type, synced, p);
                const auto back = warble::rateFromText (type, synced, text);
                INFO (type << (synced ? " synced" : " free") << " at " << p << ": " << text);
                CHECK (warble::rateText (type, synced, back) == text);
            }
}

TEST_CASE ("Warble: Spread 0 keeps a mono source mono; Spread decorrelates the right side", "[warble]")
{
    const auto in = analysis::programme (fs, 2.0);
    const Stereo mono { in.left, in.left };

    const auto narrow = render (settings (WarbleType::wow, 0.7f, false, 0.0f), { 1.0f, 0.6f }, mono);
    CHECK (analysis::identical (narrow.left, narrow.right));

    const auto wide = render (settings (WarbleType::wow, 0.7f, false, 1.0f), { 1.0f, 0.6f }, mono);
    CHECK (analysis::nullDb (wide.right, wide.left, (size_t) fs) > -30.0);
    CHECK (analysis::identical (wide.left, narrow.left));     // the left side is the same
}

TEST_CASE ("Warble: no clicks when it comes on, goes off, changes type, rate, sync or seed, or Damage moves", "[warble]")
{
    const auto in = sineInput (330.0, 6.0);
    const auto steady = analysis::clickCrestDb (in.left, 100);

    WarbleModule module;
    module.prepare ({ fs, 256, 2 });
    const auto out = renderModule<WarbleModule, WarbleSettings> (
        module, settings (WarbleType::wow), { 0.6f, 0.2f }, in, 256, fs, {},
        [] (int64_t start, WarbleSettings& s, Macros& m)
        {
            const auto t = (double) start / fs;
            s.on = ! (t > 0.5 && t < 1.0);
            s.type = t < 2.0 ? WarbleType::wow : (t < 3.0 ? WarbleType::flutter : WarbleType::drift);
            s.rate = t < 2.5 ? 0.5f : 0.8f;
            s.sync = t > 4.0 && t < 5.0;
            m.damage = t > 1.0 && t < 1.9 ? 0.0f : (t < 3.5 ? 0.6f : 1.0f);     // home and resting, then out again
            m.seed = t < 4.5 ? 11u : 12u;
            m.chaos = t < 5.5 ? 0.2f : 0.9f;
        });

    // From 0.1 s: the sine's own start, delayed by the head, is not the module's click.
    const auto crest = analysis::clickCrestDb (out.left, (size_t) (0.1 * fs));
    size_t worst = (size_t) (0.1 * fs);
    for (size_t i = worst; i < out.left.size(); ++i)
        if (std::abs (out.left[i] - 3.0 * out.left[i - 1] + 3.0 * out.left[i - 2] - out.left[i - 3])
            > std::abs (out.left[worst] - 3.0 * out.left[worst - 1] + 3.0 * out.left[worst - 2] - out.left[worst - 3]))
            worst = i;
    INFO ("crest " << crest << " dB, the sine's own " << steady << " dB, worst at " << (double) worst / fs << " s");
    CHECK (crest <= -60.0);
}

TEST_CASE ("Warble: the same render for any block size, to the bit, and from any start", "[warble][determinism]")
{
    const auto in = analysis::programme (fs, 3.0, 3);
    for (const auto sync : { false, true })
    {
        const auto s = settings (WarbleType::wow, 0.6f, sync, 0.5f);
        const Macros m { 0.8f, 0.5f };
        const auto reference = render (s, m, in, 4096);
        for (const auto block : { 1, 7, 64, 511 })
        {
            const auto r = render (s, m, in, block);
            INFO ("block " << block << (sync ? ", synced" : ", free"));
            CHECK (analysis::identical (r.left, reference.left));
            CHECK (analysis::identical (r.right, reference.right));
        }

        // A render from bar 9 (16 s at 120 BPM) against the same stretch of one from bar 1.
        const auto bar9 = (size_t) (16.0 * fs);
        const auto long_ = analysis::programme (fs, 19.0, 3);
        Stereo late;
        late.left.assign (long_.left.begin() + (std::ptrdiff_t) bar9, long_.left.end());
        late.right.assign (long_.right.begin() + (std::ptrdiff_t) bar9, long_.right.end());
        const auto whole = render (s, m, long_, 512, { true, 0.0, 120.0 });
        const auto part = render (s, m, late, 512, { true, 32.0, 120.0 });
        std::vector<float> wholeTail (whole.left.begin() + (std::ptrdiff_t) bar9, whole.left.end());
        const auto db = analysis::nullDb (part.left, wholeTail, (size_t) (0.5 * fs));
        INFO ("from bar 9: " << db << " dB after 500 ms");
        CHECK (db < -100.0);
    }
}

TEST_CASE ("Warble: the top octave keeps its level through every cycle", "[warble]")
{
    // The head never reads closer than its rest, where the windowed sinc is flat: an interpolator
    // at half a sample would dip 12 kHz by 3 dB and 18 kHz by 8 dB once per cycle. The 12-tap sinc
    // itself varies by 0.8 dB over the fractions at 18 kHz.
    for (int type = 0; type < 3; ++type)
        for (const auto amount : { 0.1f, 1.0f })
            for (const auto hz : { 12000.0, 18000.0 })
            {
                const auto x = analysis::sine (hz, fs, 3.0, 0.5);
                const auto out = render (settings ((WarbleType) type, amount), { 1.0f, 0.0f }, { x, x });
                double lo = 1.0e9, hi = 0.0;
                constexpr size_t window = 96;
                for (size_t k = (size_t) fs; k + window < out.size(); k += window / 2)
                {
                    const auto level = analysis::rms (out.left, k, k + window);
                    lo = std::min (lo, level);
                    hi = std::max (hi, level);
                }
                INFO ("type " << type << " at " << amount << ", " << hz << " Hz: " << analysis::toDb (lo / 0.35355) << " .. "
                              << analysis::toDb (hi / 0.35355) << " dB");
                const auto tolerance = hz < 15000.0 ? 0.1 : 1.0;
                CHECK (analysis::toDb (lo / 0.35355) > -tolerance);
                CHECK (analysis::toDb (hi / 0.35355) < tolerance);
            }
}

TEST_CASE ("Warble: the LFO's sine is the library's within 1e-9", "[warble]")
{
    double worst = 0.0;
    for (int k = -20000; k <= 20000; ++k)
    {
        const auto phase = (double) k * 0.000731 + 0.13;
        worst = std::max (worst, std::abs (warble::lfoSine (phase) - std::sin (2.0 * std::numbers::pi * phase)));
    }
    CHECK (worst < 1.0e-9);
}
