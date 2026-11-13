/*
    Room: the input to the bit at Damage 0 and Off; each space's decay where its Size puts it;
    silence decays to exact silence (and the module stops working); the same render for any block
    size and from any start once the space has rung down; no clicks on any change; Duck; mono.
*/

#include "dsp/modules/room/RoomControls.h"
#include "dsp/modules/room/RoomModule.h"
#include "dsp/modules/room/RoomVoicing.h"

#include "common/dsp/Filters.h"
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
    constexpr std::array<const char*, 4> names { "Closet", "Room", "Spring", "Hall" };

    RoomSettings settings (int type, float size = 0.5f, float duck = 0.0f)
    {
        RoomSettings s;
        s.on = true;
        s.amount = 1.0f;
        s.type = (RoomType) type;
        s.size = size;
        s.duck = duck;
        return s;
    }

    Stereo render (const RoomSettings& s, Macros m, const Stereo& in, int block = 512, Transport t = {})
    {
        RoomModule module;
        module.prepare ({ fs, block, 2 });
        return renderModule (module, s, m, in, block, fs, t);
    }

    /** A burst of programme, then silence. */
    Stereo burstThenSilence (double burst, double total, uint32_t salt = 1)
    {
        auto x = analysis::programme (fs, total, salt);
        for (auto* channel : { &x.left, &x.right })
            std::fill (channel->begin() + (std::ptrdiff_t) (burst * fs), channel->end(), 0.0f);
        return x;
    }

    /** The wet alone (output minus the stepped-back dry), at full intensity. */
    std::vector<float> wetOf (const std::vector<float>& out, const std::vector<float>& in)
    {
        std::vector<float> w (out.size());
        for (size_t k = 0; k < w.size(); ++k)
            w[k] = out[k] - (1.0f - RoomModule::dryStepBack) * in[k];
        return w;
    }

    /** Mid-band RT60 (the octave around 1 kHz, as Size reads), from the energy decay curve fitted between -5 and -25 dB. */
    double rt60 (const std::vector<float>& wet, size_t from)
    {
        myzel::dsp::TptSvf band;
        band.setCoefficients (1000.0, 1.41, fs);
        std::vector<double> x (wet.size());
        for (size_t k = 0; k < wet.size(); ++k)
            x[k] = band.process (wet[k]).bp;

        std::vector<double> edc (x.size() - from);
        double sum = 0.0;
        for (size_t k = x.size(); k-- > from;)
        {
            sum += x[k] * x[k];
            edc[k - from] = sum;
        }
        const auto total = edc[0];
        double t5 = -1.0, t25 = -1.0;
        for (size_t k = 0; k < edc.size(); ++k)
        {
            const auto db = 10.0 * std::log10 (std::max (edc[k] / total, 1.0e-30));
            if (t5 < 0.0 && db <= -5.0)
                t5 = (double) k / fs;
            if (t25 < 0.0 && db <= -25.0)
            {
                t25 = (double) k / fs;
                break;
            }
        }
        return 3.0 * (t25 - t5);
    }
}

TEST_CASE ("Room: Damage 0 and Off are the input, to the bit", "[room]")
{
    const auto in = analysis::programme (fs, 1.0);
    for (int type = 0; type < 4; ++type)
        for (const auto chaos : { 0.0f, 1.0f })
        {
            INFO (names[(size_t) type] << ", chaos " << chaos);
            const auto zero = render (settings (type, 0.8f, 0.5f), { 0.0f, chaos }, in);
            CHECK (analysis::identical (zero.left, in.left));
            CHECK (analysis::identical (zero.right, in.right));

            auto off = settings (type);
            off.on = false;
            const auto o = render (off, { 1.0f, chaos }, in);
            CHECK (analysis::identical (o.left, in.left));
            CHECK (analysis::identical (o.right, in.right));
        }

    // Damage back to 0 while a hall rings: the room fades out and the input is back to the bit.
    RoomModule module;
    module.prepare ({ fs, 256, 2 });
    const auto out = renderModule<RoomModule, RoomSettings> (module, settings (3), { 1.0f, 0.5f }, in, 256, fs, {},
                                                             [] (int64_t start, RoomSettings&, Macros& m) { m.damage = start < 24000 ? 1.0f : 0.0f; });
    CHECK_FALSE (analysis::identical (out.left, in.left, 0, 24000));
    CHECK (analysis::identical (out.left, in.left, 24000 + (size_t) (0.1 * fs)));
}

TEST_CASE ("Room: each space decays as its Size says", "[room]")
{
    for (const auto type : { 1, 2, 3 })
        for (const auto size : { 0.0f, 0.5f, 1.0f })
        {
            const auto metres = room::sizeAt (type, size);
            const auto expected = room::rt60Of (type, metres);
            const auto in = burstThenSilence (0.05, 0.05 + 2.0 * expected + 0.3, 7);
            const auto out = render (settings (type, size), { 1.0f, 0.0f }, in);
            const auto measured = rt60 (wetOf (out.left, in.left), (size_t) (0.05 * fs));
            INFO (names[(size_t) type] << " at " << metres << ": RT60 " << measured << " s, expected " << expected << " s");
            CHECK (measured > 0.85 * expected);
            CHECK (measured < 1.15 * expected);
        }

    // Closet: no tail to speak of; 60 dB down within 0.15 s of the input stopping.
    const auto in = burstThenSilence (0.3, 0.6, 3);
    const auto out = render (settings (0, 1.0f), { 1.0f, 0.0f }, in);
    const auto wet = wetOf (out.left, in.left);
    CHECK (analysis::rms (wet, (size_t) (0.45 * fs)) < 1.0e-3 * analysis::rms (wet, (size_t) (0.1 * fs), (size_t) (0.3 * fs)));
}

TEST_CASE ("Room: silence decays to exact silence, never through denormals, and the module stops working", "[room]")
{
    for (int type = 0; type < 4; ++type)
    {
        RoomModule module;
        module.prepare ({ fs, 512, 2 });
        const auto ring = room::ringSeconds (type, room::sizeAt (type, 1.0f));
        const auto in = burstThenSilence (0.5, 0.5 + ring + 0.5, 11);
        const auto out = renderModule (module, settings (type, 1.0f), { 1.0f, 1.0f }, in, 512, fs);

        bool normal = true;
        for (const auto* channel : { &out.left, &out.right })
            for (const auto x : *channel)
                normal = normal && std::fpclassify (x) != FP_SUBNORMAL && std::isfinite (x);
        const auto quiet = (size_t) ((0.5 + ring) * fs) + 512;
        INFO (names[(size_t) type] << ": rings " << ring << " s");
        CHECK (normal);
        CHECK (analysis::peak (out.left, quiet) <= 0.0);
        CHECK (analysis::peak (out.right, quiet) <= 0.0);
        CHECK (module.isAsleep());
    }
}

TEST_CASE ("Room: the same render for any block size, to the bit", "[room][determinism]")
{
    const auto in = burstThenSilence (1.0, 1.6, 5);
    for (int type = 0; type < 4; ++type)
    {
        INFO (names[(size_t) type]);
        const auto s = settings (type, 0.7f, 0.4f);
        const auto reference = render (s, { 0.9f, 0.6f }, in, 4096);
        for (const auto block : { 1, 7, 64, 333 })
        {
            const auto out = render (s, { 0.9f, 0.6f }, in, block);
            CHECK (analysis::identical (out.left, reference.left));
            CHECK (analysis::identical (out.right, reference.right));
        }
    }

    // Changes on shared block boundaries: type, Size and Damage.
    auto change = [] (int64_t start, RoomSettings& s, Macros& m)
    {
        s.type = start < 24576 ? RoomType::room : start < 49152 ? RoomType::hall : RoomType::spring;
        s.size = start < 36864 ? 0.3f : 0.8f;
        m.damage = start < 61440 ? 0.9f : 0.4f;
    };
    const auto program = analysis::programme (fs, 1.8, 6);
    RoomModule a, b;
    a.prepare ({ fs, 4096, 2 });
    b.prepare ({ fs, 1, 2 });
    const auto one = renderModule<RoomModule, RoomSettings> (a, settings (1), { 0.9f, 0.5f }, program, 4096, fs, {}, change);
    const auto two = renderModule<RoomModule, RoomSettings> (b, settings (1), { 0.9f, 0.5f }, program, 1, fs, {}, change);
    CHECK (analysis::identical (one.left, two.left));
    CHECK (analysis::identical (one.right, two.right));
}

TEST_CASE ("Room: a render from a later bar matches, once what came before has rung down", "[room][determinism]")
{
    // Bar 9 at 120 BPM is 16 s in. The space's tail of the music before bar 9 is the only difference.
    const auto bar9 = (size_t) std::lround (16.0 * fs);
    for (int type = 0; type < 4; ++type)
    {
        const auto s = settings (type, 0.5f);
        const Macros m { 1.0f, 0.7f };
        const auto ring = room::ringSeconds (type, room::sizeAt (type, 0.5f));
        const auto in = analysis::programme (fs, 16.0 + ring + 1.0, 9);
        Stereo late;
        late.left.assign (in.left.begin() + (std::ptrdiff_t) bar9, in.left.end());
        late.right.assign (in.right.begin() + (std::ptrdiff_t) bar9, in.right.end());

        const auto whole = render (s, m, in, 512, { true, 0.0, 120.0 });
        const auto part = render (s, m, late, 480, { true, 32.0, 120.0 });
        const std::vector<float> tail (whole.left.begin() + (std::ptrdiff_t) bar9, whole.left.end());
        const auto settle = (size_t) (0.6 * ring * fs);     // 90 dB down
        const auto db = analysis::nullDb (part.left, tail, settle);
        INFO (names[(size_t) type] << ": " << db << " dB after " << 0.6 * ring << " s");
        CHECK (db < -80.0);
    }
}

TEST_CASE ("Room: no clicks from On/Off, type, Size, Duck, seed or Damage changes", "[room]")
{
    const auto sine = analysis::sine (330.0, fs, 6.0, 0.5);
    const Stereo in { sine, sine };
    RoomModule module;
    module.prepare ({ fs, 256, 2 });
    const auto out = renderModule<RoomModule, RoomSettings> (module, settings (1), { 0.8f, 0.4f }, in, 256, fs, {},
                                                             [] (int64_t start, RoomSettings& s, Macros& m)
    {
        const auto t = (double) start / fs;
        s.on = ! (t > 0.5 && t < 0.9);
        s.type = t < 1.3 ? RoomType::room : t < 2.0 ? RoomType::hall : t < 2.7 ? RoomType::spring : RoomType::closet;
        s.size = t < 3.2 ? 0.5f : 0.9f;
        s.duck = t < 3.8 ? 0.0f : 1.0f;
        m.seed = t < 4.4 ? 1u : 2u;
        m.damage = t < 5.0 ? 0.8f : 0.3f;
    });
    const auto crest = std::max (analysis::clickCrestDb (out.left, (size_t) (0.1 * fs)), analysis::clickCrestDb (out.right, (size_t) (0.1 * fs)));
    INFO ("crest " << crest << " dB");
    CHECK (crest <= -60.0);
}

TEST_CASE ("Room: a step in Size does not click on the tank spaces", "[room]")
{
    // A preset load, a typed value or stepped automation: every line's length moves at once, so
    // its speed must not step (a pitch step on each line). Down and up, on Room and Hall.
    const auto sine = analysis::sine (330.0, fs, 3.0, 0.5);
    const Stereo in { sine, sine };
    for (const auto type : { RoomType::room, RoomType::hall })
        for (const auto to : { 0.05f, 1.0f })
        {
            RoomModule module;
            module.prepare ({ fs, 256, 2 });
            const auto out = renderModule<RoomModule, RoomSettings> (module, settings ((int) type), { 0.8f, 0.0f }, in, 256, fs, {},
                                                                     [to] (int64_t start, RoomSettings& s, Macros&)
            {
                s.size = (double) start / fs < 1.5 ? 0.5f : to;
            });
            const auto crest = std::max (analysis::clickCrestDb (out.left, (size_t) (1.0 * fs)), analysis::clickCrestDb (out.right, (size_t) (1.0 * fs)));
            INFO ((type == RoomType::room ? "Room" : "Hall") << ", Size 50 -> " << to * 100.0f << " %: crest " << crest << " dB");
            CHECK (crest <= -60.0);
        }
}

TEST_CASE ("Room: Duck lowers the room while the input plays", "[room]")
{
    const auto in = burstThenSilence (1.0, 2.0, 13);
    const auto plain = wetOf (render (settings (1, 0.5f, 0.0f), { 1.0f, 0.0f }, in).left, in.left);
    const auto ducked = wetOf (render (settings (1, 0.5f, 1.0f), { 1.0f, 0.0f }, in).left, in.left);
    const auto during = analysis::rms (ducked, (size_t) (0.5 * fs), (size_t) fs) / analysis::rms (plain, (size_t) (0.5 * fs), (size_t) fs);
    const auto after = analysis::rms (ducked, (size_t) (1.3 * fs), (size_t) (1.5 * fs)) / analysis::rms (plain, (size_t) (1.3 * fs), (size_t) (1.5 * fs));
    INFO ("during " << analysis::toDb (during) << " dB, after " << analysis::toDb (after) << " dB");
    CHECK (during < 0.25);
    CHECK (after > 0.5);
}

TEST_CASE ("Room: stereo spaces, mono in and out, every sample rate", "[room]")
{
    for (int type = 0; type < 4; ++type)
    {
        // A mono source (noise) comes out decorrelated, and sums back to mono without a loss
        // beyond the 3 dB of two unrelated channels; the spring a little narrower, as a tank.
        const auto x = analysis::noise ((size_t) (3.0 * fs), 9, 0.3);
        const auto out = render (settings (type), { 1.0f, 0.0f }, Stereo { x, x });
        const auto l = wetOf (out.left, x), r = wetOf (out.right, x);
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (size_t k = (size_t) (0.5 * fs); k < l.size(); ++k)
        {
            lr += (double) l[k] * r[k];
            ll += (double) l[k] * l[k];
            rr += (double) r[k] * r[k];
        }
        const auto correlation = lr / std::sqrt (ll * rr);
        INFO (names[(size_t) type] << ": wet correlation " << correlation);
        CHECK (correlation > -0.2);
        CHECK (correlation < (type == 2 ? 0.6 : 0.2));
        if (type == 2)
            CHECK (correlation > 0.1);
    }

    for (const auto rate : { 44100.0, 96000.0, 192000.0 })
        for (int type = 0; type < 4; ++type)
        {
            RoomModule module;
            module.prepare ({ rate, 256, 2 });
            const Stereo mono { analysis::programme (rate, 0.5, 2).left, {} };
            const auto out = renderModule (module, settings (type), { 1.0f, 0.5f }, mono, 256, rate);
            bool finite = true;
            for (const auto v : out.left)
                finite = finite && std::isfinite (v);
            INFO (names[(size_t) type] << " at " << rate);
            CHECK (finite);
            CHECK (analysis::peak (out.left) < 4.0);
            CHECK (analysis::identical (render (settings (type), { 0.0f, 0.5f }, mono).left, mono.left));
        }
}

TEST_CASE ("Room: Size reads in metres (Spring: seconds) and reads back", "[room]")
{
    CHECK (room::sizeText (0, 50.0f) == "1.41 m");
    CHECK (room::sizeText (1, 50.0f) == "5.0 m");
    CHECK (room::sizeText (1, 100.0f) == "10.0 m");
    CHECK (room::sizeText (2, 0.0f) == "0.80 s");
    CHECK (room::sizeText (3, 50.0f) == "20.0 m");
    CHECK (room::sizeText (3, 100.0f) == "40 m");

    CHECK (std::abs (room::sizeFromText (1, "5 m") - 50.0f) < 0.01f);
    CHECK (std::abs (room::sizeFromText (3, "40") - 100.0f) < 0.01f);
    CHECK (std::abs (room::sizeFromText (3, "400 m") - 100.0f) < 0.01f);       // its nearest end
    CHECK (std::abs (room::sizeFromText (2, "1500 ms") - (float) (100.0 * room::positionOf (2, 1.5))) < 0.01f);
    CHECK (room::sizeFromText (1, "loud") < 0.0f);
    CHECK (room::sizeFromText (1, "3 s") < 0.0f);

    for (int type = 0; type < 4; ++type)
        for (const auto p : { 0.0f, 13.0f, 50.0f, 77.0f, 100.0f })
            CHECK (std::abs (room::sizeFromText (type, room::sizeText (type, p)) - p) < 1.5f);
}
