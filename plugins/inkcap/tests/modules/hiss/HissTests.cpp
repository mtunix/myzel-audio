/*
    Hiss: exactly nothing added at Damage 0 and Off; each bed at its level, shaped by Tone and
    Follow; the same noise for any block size and from any start; seed changes, transport jumps
    and type changes without clicks; the crackle's dust repeating with the record only when Chaos
    is low.
*/

#include "dsp/modules/hiss/Crackle.h"
#include "dsp/modules/hiss/HissModule.h"

#include "support/ModuleHarness.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::testing;
using analysis::Stereo;

namespace
{
    constexpr double fs = 48000.0;

    HissSettings settings (HissType type, float tone = 0.0f, float follow = 0.0f)
    {
        HissSettings s;
        s.on = true;
        s.amount = 1.0f;
        s.type = type;
        s.tone = tone;
        s.follow = follow;
        return s;
    }

    Stereo silence (double seconds)
    {
        const auto n = (size_t) std::lround (seconds * fs);
        return { std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f) };
    }

    Stereo render (const HissSettings& s, Macros m, const Stereo& in, int block = 512, Transport t = {})
    {
        HissModule module;
        module.prepare ({ fs, block, 2 });
        return renderModule (module, s, m, in, block, fs, t);
    }

    /** The largest 3rd difference in [from, to) over the largest in the reference stretch: > 1 means something new and sharp. */
    double sharpness (const std::vector<float>& x, size_t from, size_t to, size_t refFrom, size_t refTo)
    {
        return analysis::thirdDifferencePeak (x, from, to) / std::max (analysis::thirdDifferencePeak (x, refFrom, refTo), 1.0e-12);
    }

    /** Energy above 4 kHz over energy below 1 kHz (2nd-order TPT filters): the bed's tilt. */
    double tilt (const std::vector<float>& x, size_t from)
    {
        myzel::dsp::TptSvf high, low;
        high.setCoefficients (4000.0, 0.7071, fs);
        low.setCoefficients (1000.0, 0.7071, fs);
        double h = 0.0, l = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const auto yh = high.processHighpass (x[i]), yl = low.processLowpass (x[i]);
            if (i >= from)
            {
                h += yh * yh;
                l += yl * yl;
            }
        }
        return h / std::max (l, 1.0e-30);
    }
}

TEST_CASE ("Hiss: Damage 0 and Off add exactly nothing", "[hiss]")
{
    const auto in = analysis::programme (fs, 1.0);
    for (int type = 0; type < 3; ++type)
        for (const auto chaos : { 0.0f, 1.0f })
        {
            INFO ("type " << type << ", chaos " << chaos);
            const auto zero = render (settings ((HissType) type, 0.7f, -0.5f), { 0.0f, chaos }, in);
            CHECK (analysis::identical (zero.left, in.left));
            CHECK (analysis::identical (zero.right, in.right));

            auto off = settings ((HissType) type);
            off.on = false;
            const auto o = render (off, { 1.0f, chaos }, in);
            CHECK (analysis::identical (o.left, in.left));
            CHECK (analysis::identical (o.right, in.right));
        }

    // Damage back to 0 while running: the level glides out and lands on exactly nothing.
    HissModule module;
    module.prepare ({ fs, 256, 2 });
    const auto out = renderModule<HissModule, HissSettings> (module, settings (HissType::vinyl), { 1.0f, 0.5f }, in, 256, fs, {},
                                                             [] (int64_t start, HissSettings&, Macros& m) { m.damage = start < 24000 ? 1.0f : 0.0f; });
    CHECK_FALSE (analysis::identical (out.left, in.left, 0, 24000));
    CHECK (analysis::identical (out.left, in.left, 24000 + (size_t) (0.2 * fs)));
    CHECK (module.levelGain() <= 0.0);

    // Silence in, Hiss off: silence out.
    auto off = settings (HissType::hum);
    off.on = false;
    const auto quiet = render (off, { 1.0f, 1.0f }, silence (0.5));
    CHECK (analysis::peak (quiet.left) <= 0.0);
}

TEST_CASE ("Hiss: each bed sits at its level, and Amount x Damage moves it", "[hiss]")
{
    for (int type = 0; type < 3; ++type)
        for (const auto damage : { 0.3f, 1.0f })
        {
            const auto out = render (settings ((HissType) type), { damage, 0.0f }, silence (4.0));
            const auto i = (double) moduleIntensity (Module::hiss, 1.0f, damage);
            const auto expected = hiss::bedLevelDb (i);
            const auto measured = analysis::toDb (analysis::rms (out.left, (size_t) (0.5 * fs)));
            INFO ("type " << type << ", damage " << damage << ": " << measured << " dBFS, the bed " << expected);
            // Tone's neutral corners trim the edges of the band; Vinyl adds its crackle and rumble.
            CHECK (measured > expected - 1.0);
            CHECK (measured < expected + (type == (int) HissType::vinyl ? 1.5 : 1.0));
            CHECK (analysis::rms (out.right, (size_t) (0.5 * fs)) > 0.0);
        }
}

TEST_CASE ("Hiss: the same noise for any block size, to the bit", "[hiss][determinism]")
{
    const auto in = analysis::programme (fs, 1.5, 3);
    for (int type = 0; type < 3; ++type)
    {
        INFO ("type " << type);
        const auto s = settings ((HissType) type, 0.3f, -0.6f);
        const auto reference = render (s, { 0.8f, 0.7f }, in, 4096);
        for (const auto block : { 1, 7, 64, 333 })
        {
            const auto out = render (s, { 0.8f, 0.7f }, in, block);
            CHECK (analysis::identical (out.left, reference.left));
            CHECK (analysis::identical (out.right, reference.right));
        }
    }

    // Changes at shared block boundaries: a type change, Tone, Follow and Damage moves.
    auto change = [] (int64_t start, HissSettings& s, Macros& m)
    {
        s.type = start < 24576 ? HissType::vinyl : start < 49152 ? HissType::hum : HissType::tape;
        s.tone = start < 36864 ? -0.4f : 0.6f;
        m.damage = start < 61440 ? 0.6f : 0.2f;
    };
    HissModule a, b;
    a.prepare ({ fs, 4096, 2 });
    b.prepare ({ fs, 1, 2 });
    const auto one = renderModule<HissModule, HissSettings> (a, settings (HissType::vinyl), { 0.6f, 0.4f }, in, 4096, fs, {}, change);
    const auto two = renderModule<HissModule, HissSettings> (b, settings (HissType::vinyl), { 0.6f, 0.4f }, in, 1, fs, {}, change);
    CHECK (analysis::identical (one.left, two.left));
}

TEST_CASE ("Hiss: a render from bar 17 has the noise bar 17 had", "[hiss][determinism]")
{
    // At 120 BPM bar 17 is 32 s in. Both renders follow the host; only the filters' first
    // moments differ (a few ms), and the crackle under way at the start is rebuilt.
    const auto bar17 = (size_t) std::lround (32.0 * fs);
    for (int type = 0; type < 3; ++type)
    {
        const auto s = settings ((HissType) type, 0.2f);
        const Macros m { 0.9f, 0.6f };
        const auto whole = render (s, m, silence (33.0), 512, { true, 0.0, 120.0 });
        const auto part = render (s, m, silence (1.0), 480, { true, 64.0, 120.0 });
        const std::vector<float> tail (whole.left.begin() + (std::ptrdiff_t) bar17, whole.left.end());
        const auto db = analysis::nullDb (part.left, tail, (size_t) (0.5 * fs));
        INFO ("type " << type << ": " << db << " dB after 500 ms");
        CHECK (db < -100.0);
    }
}

TEST_CASE ("Hiss: the crackle under way at a restart is the same crackle", "[hiss][determinism]")
{
    hiss::Crackle a, b;
    a.prepare (fs);
    b.prepare (fs);
    a.setSeed (0x1234'5678u);
    b.setSeed (0x1234'5678u);
    const hiss::Crackle::Wear wear { 900.0, 0.4, 0.2, 2 };

    constexpr int64_t from = 1'000'000, n = 96000, restart = from + 40'000;
    a.restartAt (from, wear);
    std::vector<float> continuous ((size_t) n), resumed ((size_t) n);
    for (int64_t k = 0; k < n; ++k)
    {
        float l = 0.0f, r = 0.0f;
        a.next (from + k, wear, l, r);
        continuous[(size_t) k] = l;
        if (from + k == restart)
            b.restartAt (restart, wear);
        if (from + k >= restart)
        {
            b.next (from + k, wear, l, r);
            resumed[(size_t) k] = l;
        }
    }
    CHECK (analysis::peak (continuous) > 0.0);
    CHECK (analysis::identical (continuous, resumed, (size_t) (restart - from)));
}

TEST_CASE ("Hiss: at Chaos 0 the dust comes round with the record; at Chaos 1 it is fresh every turn", "[hiss]")
{
    auto repeatShare = [] (double chaos)
    {
        hiss::Crackle c;
        c.prepare (fs);
        c.setSeed (77u);
        const hiss::Crackle::Wear wear { 120.0, chaos, 0.03, 0 };
        const auto turn = c.samplesPerTurn();
        c.restartAt (0, wear);
        std::vector<float> x ((size_t) turn * 6);
        for (size_t k = 0; k < x.size(); ++k)
        {
            float l = 0.0f, r = 0.0f;
            c.next ((int64_t) k, wear, l, r);
            x[k] = l;
        }

        // Of the clicks starting in turns 2 .. 5, the share with a click starting at the same sample one turn earlier.
        auto starts = [&x] (size_t k) { return std::abs (x[k]) > 0.0f && std::abs (x[k - 1]) <= 0.0f; };
        int clicks = 0, repeats = 0;
        for (size_t k = (size_t) turn * 2; k < x.size(); ++k)
            if (starts (k))
            {
                ++clicks;
                if (starts (k - (size_t) turn))
                    ++repeats;
            }
        INFO ("chaos " << chaos << ": " << clicks << " clicks, " << repeats << " repeats");
        CHECK (clicks > 100);
        return (double) repeats / std::max (clicks, 1);
    };

    CHECK (repeatShare (0.0) > 0.4);
    CHECK (repeatShare (1.0) < 0.02);
}

TEST_CASE ("Hiss: Tone darkens and brightens the bed", "[hiss]")
{
    for (int type = 0; type < 3; ++type)
    {
        const auto dark = render (settings ((HissType) type, -1.0f), { 1.0f, 0.0f }, silence (1.5));
        const auto neutral = render (settings ((HissType) type, 0.0f), { 1.0f, 0.0f }, silence (1.5));
        const auto bright = render (settings ((HissType) type, 1.0f), { 1.0f, 0.0f }, silence (1.5));
        const auto from = (size_t) (0.3 * fs);
        INFO ("type " << type);
        INFO ("tilt dark " << analysis::toDb (std::sqrt (tilt (dark.left, from))) << " dB, neutral "
              << analysis::toDb (std::sqrt (tilt (neutral.left, from))) << " dB, bright " << analysis::toDb (std::sqrt (tilt (bright.left, from))) << " dB");
        CHECK (tilt (dark.left, from) < 0.25 * tilt (neutral.left, from));
        CHECK (tilt (bright.left, from) > 3.0 * tilt (neutral.left, from));
    }

    // Hum: dark is the transformer's hum, bright the rectifier's buzz (the 1 kHz harmonic).
    const auto dark = render (settings (HissType::hum, -1.0f), { 1.0f, 0.0f }, silence (1.0));
    const auto bright = render (settings (HissType::hum, 1.0f), { 1.0f, 0.0f }, silence (1.0));
    const auto from = (size_t) (0.3 * fs), n = (size_t) (0.6 * fs);
    CHECK (analysis::toneAmplitude (dark.left, 50.0, fs, from, n) > 4.0 * analysis::toneAmplitude (dark.left, 1000.0, fs, from, n));
    CHECK (analysis::toneAmplitude (bright.left, 1000.0, fs, from, n) > 3.0 * analysis::toneAmplitude (dark.left, 1000.0, fs, from, n));
}

TEST_CASE ("Hiss: Follow lets the noise ride on the input, or ducks it under", "[hiss]")
{
    // A loud second, then silence.
    auto in = silence (3.0);
    const auto tone = analysis::sine (440.0, fs, 1.0, 0.5);
    std::copy (tone.begin(), tone.end(), in.left.begin());
    std::copy (tone.begin(), tone.end(), in.right.begin());

    auto noiseIn = [] (const Stereo& out, const Stereo& input, double from, double to)
    {
        std::vector<float> d (out.left.size());
        for (size_t k = 0; k < d.size(); ++k)
            d[k] = out.left[k] - input.left[k];
        return analysis::rms (d, (size_t) (from * fs), (size_t) (to * fs));
    };

    const auto follows = render (settings (HissType::tape, 0.0f, 1.0f), { 1.0f, 0.0f }, in);
    CHECK (noiseIn (follows, in, 0.5, 0.95) > 10.0 * noiseIn (follows, in, 2.0, 3.0));
    CHECK (noiseIn (follows, in, 2.0, 3.0) <= 1.0e-6);

    const auto ducks = render (settings (HissType::tape, 0.0f, -1.0f), { 1.0f, 0.0f }, in);
    const auto ratio = noiseIn (ducks, in, 0.5, 0.95) / noiseIn (ducks, in, 2.0, 3.0);
    INFO ("ducked by " << analysis::toDb (ratio) << " dB");
    CHECK (ratio < 0.15);
    CHECK (ratio > 0.05);
}

TEST_CASE ("Hiss: seed changes, transport jumps and type changes do not click", "[hiss]")
{
    // On Hum, whose waveform is smooth enough for the 3rd difference to see a step.
    auto s = settings (HissType::hum, -0.6f);

    SECTION ("a new seed")
    {
        HissModule module;
        module.prepare ({ fs, 256, 2 });
        const auto out = renderModule<HissModule, HissSettings> (module, s, { 1.0f, 1.0f }, silence (2.0), 256, fs, {},
                                                                 [] (int64_t start, HissSettings&, Macros& m) { m.seed = start < 48000 ? 1u : 2u; });
        CHECK (sharpness (out.left, 47000, 62000, 10000, 46000) < 1.5);
    }

    SECTION ("a transport jump (a loop back to bar 1)")
    {
        HissModule module;
        module.prepare ({ fs, 256, 2 });
        auto in = silence (2.0);
        auto out = in;
        GridClock clock;
        clock.prepare (fs);
        for (int64_t start = 0; start < (int64_t) in.size(); start += 256)
        {
            const auto t = start < 48000 ? start : start - 48000 + 1237;     // jump back to an odd place
            const Transport transport { true, 0.0, 120.0 };
            clock.advance (transport.at (t, fs), 120.0, 256);
            const BlockContext context { clock, 1.0f, 1.0f, 99u };
            float* channels[] = { out.left.data() + start, out.right.data() + start };
            module.process ({ channels, 2, 256, clock.blockStart() }, s, context);
        }
        CHECK (sharpness (out.left, 47000, 62000, 10000, 46000) < 1.5);
    }

    SECTION ("On/Off and a type change")
    {
        // Hum to Tape and back to Hum: the hum's fade, the crossfade and the key fade, judged
        // against the steady hum before.
        HissModule module;
        module.prepare ({ fs, 256, 2 });
        const auto out = renderModule<HissModule, HissSettings> (module, s, { 1.0f, 0.0f }, silence (3.0), 256, fs, {},
                                                                 [] (int64_t start, HissSettings& h, Macros&)
        {
            h.on = ! (start >= 48000 && start < 72000);
            h.type = start >= 96000 && start < 120000 ? HissType::tape : HissType::hum;
        });
        CHECK (sharpness (out.left, 46000, 76000, 4000, 46000) < 1.5);
        CHECK (analysis::peak (out.left, 50000, 72000) <= 0.0);
        CHECK (analysis::peak (out.left, 72000, 74000) > 0.0);
    }

    SECTION ("Tone moves")
    {
        HissModule module;
        module.prepare ({ fs, 256, 2 });
        const auto out = renderModule<HissModule, HissSettings> (module, s, { 1.0f, 0.0f }, silence (2.0), 256, fs, {},
                                                                 [] (int64_t start, HissSettings& h, Macros&) { h.tone = start < 48000 ? -0.6f : 0.4f; });
        // Brighter after the move; the move itself no sharper than where it lands.
        CHECK (sharpness (out.left, 47000, 52000, 60000, 96000) < 1.2);
    }
}

TEST_CASE ("Hiss: mono, and every sample rate", "[hiss]")
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int type = 0; type < 3; ++type)
        {
            HissModule module;
            module.prepare ({ rate, 256, 2 });
            Stereo mono { std::vector<float> ((size_t) rate, 0.0f), {} };
            const auto out = renderModule (module, settings ((HissType) type), { 1.0f, 0.5f }, mono, 256, rate);
            const auto db = analysis::toDb (analysis::rms (out.left, (size_t) (0.3 * rate)));
            INFO ("rate " << rate << ", type " << type << ": " << db << " dBFS");
            bool finite = true;
            for (const auto x : out.left)
                finite = finite && std::isfinite (x);
            CHECK (finite);
            CHECK (std::abs (db - hiss::bedLevelDb (1.0)) < 4.0);
        }
}
