/*
    Bits on its own (InkcapTests_bits): the clean path at Damage 0 and Off, the clocked domain
    against a brute-force sampler at 64x (it aliases as the modelled sampler does, and only so),
    what Damage, Rate and Depth do, the phone band and mu-law, the clock tied to the timeline,
    determinism, silence, no allocation, the read-outs and the golden renders. Measured values:
    NOTES.md.
*/

#include "dsp/modules/bits/BitsControls.h"
#include "dsp/modules/bits/BitsModule.h"

#include "common/dsp/FloatCompare.h"
#include "support/AllocationCount.h"
#include "support/ModuleHarness.h"
#include "support/Spectrum.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::testing;

namespace
{
    constexpr std::array<const char*, 3> typeNames { "sampler12", "sampler8", "phone" };

    BitsSettings full (int type, float rate = 0.5f, float depth = 0.5f, float jitter = 0.0f)
    {
        return { true, 1.0f, (BitsType) type, rate, depth, jitter };
    }

    analysis::Stereo monoSine (double hz, double fs, double seconds, double amplitude)
    {
        return { analysis::sine (hz, fs, seconds, amplitude), {} };
    }

    analysis::Stereo run (const BitsSettings& s, const analysis::Stereo& in, double fs, float damage = 1.0f, int block = 512,
                          const Transport& transport = {}, const std::function<void (int64_t, BitsSettings&, Macros&)>& each = {})
    {
        BitsModule module;
        module.prepare ({ fs, block, in.right.empty() ? 1 : 2 });
        return renderModule (module, s, { damage, 0.0f }, in, block, fs, transport, each);
    }

    double levelDb (const std::vector<float>& y, double hz, double fs, size_t from = 12000, size_t n = 16384)
    {
        return analysis::toDb (analysis::toneAmplitude (y, hz, fs, from, n));
    }
}

TEST_CASE ("Bits: Damage 0 and Off are the input, to the bit", "[bits]")
{
    const auto in = analysis::programme (48000.0, 1.0);
    for (int type = 0; type < 3; ++type)
        for (const auto block : { 1, 64, 512 })
        {
            INFO (typeNames[(size_t) type] << ", block " << block);
            const auto atZero = run (full (type, 0.0f, 0.0f, 1.0f), in, 48000.0, 0.0f, block);
            CHECK (analysis::identical (atZero.left, in.left));
            CHECK (analysis::identical (atZero.right, in.right));

            auto off = full (type);
            off.on = false;
            const auto offOut = run (off, in, 48000.0, 1.0f, block);
            CHECK (analysis::identical (offOut.left, in.left));
        }
}

TEST_CASE ("Bits: on, then Damage to 0, lands on the input to the bit", "[bits]")
{
    const auto in = analysis::programme (48000.0, 1.0);
    for (int type = 0; type < 3; ++type)
    {
        const auto out = run (full (type), in, 48000.0, 1.0f, 256, {}, [] (int64_t start, BitsSettings&, Macros& m) {
            m.damage = start < 24000 ? 1.0f : 0.0f;
        });
        INFO (typeNames[(size_t) type]);
        CHECK (! analysis::identical (out.left, in.left, 0, 24000));
        CHECK (analysis::identical (out.left, in.left, 24000 + 1024));
    }
}

TEST_CASE ("Bits: aliases and images match a brute-force sampler at 64x", "[bits][aliasing]")
{
    // Sampler 8 (filters at its Nyquist, so it aliases) at a low clock, 14 bits so the
    // converter's noise sits below what is compared. The same converter simulated directly at
    // 64x the host rate: the filters as exact modal steps, sampling at the nearest 64x instant,
    // the same quantiser, the staircase held at 64x, the host reading every 64th sample.
    const auto fs = 48000.0;
    const auto type = (int) BitsType::sampler8;
    const auto rate = 0.25f;            // 16 kHz down 1.5 octaves: 5.66 kHz
    const auto depth = 1.0f;            // 14 bits
    const auto clock = bits::clockHz (BitsType::sampler8, rate);
    const auto cutoff = bits::voicingOf (BitsType::sampler8).filterRatio * clock;
    const auto tone = 2000.0;
    const auto in = monoSine (tone, fs, 0.75, 0.5);

    BitsModule module;
    module.prepare ({ fs, 512, 1 });
    const auto fast = renderModule (module, full (type, rate, depth), { 1.0f, 0.0f }, in, 512, fs);
    REQUIRE (std::abs (module.clockNow() - clock) < 1.0e-6 * clock);

    constexpr int over = 64;
    const auto f = myzel::dsp::ModalFilter<BitsModule::modes>::butterworth (cutoff);
    const auto dt = 1.0 / (fs * over);
    std::array<std::complex<double>, BitsModule::modes> xin {}, yout {}, stepIn {};
    for (size_t i = 0; i < stepIn.size(); ++i)
        stepIn[i] = std::exp (f.poles[i] * dt);
    const auto step = std::exp2 (1.0 - bits::depthBits (BitsType::sampler8, depth));
    std::vector<float> slow (in.size());
    double phase = 0.0, held = 0.0;
    for (size_t n = 0; n < in.size(); ++n)
        for (int j = 1; j <= over; ++j)
        {
            for (size_t i = 0; i < xin.size(); ++i)
                xin[i] *= stepIn[i];
            phase += clock * dt;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                double v = 0.0;
                for (size_t i = 0; i < xin.size(); ++i)
                    v += 2.0 * (f.residues[i] * xin[i]).real();
                held = BitsModule::quantise (v, step, false);
            }
            double y = 0.0;
            for (size_t i = 0; i < yout.size(); ++i)
            {
                const auto target = -f.residues[i] / f.poles[i] * held;
                yout[i] = target + (yout[i] - target) * stepIn[i];
                y += 2.0 * yout[i].real();
            }
            if (j == over)
            {
                slow[n] = (float) y;
                for (size_t i = 0; i < xin.size(); ++i)
                    xin[i] += (double) in.left[n] / fs;
            }
        }

    int compared = 0;
    for (const auto hz : { tone, clock - tone, clock + tone, 2.0 * clock - tone })
    {
        const auto a = levelDb (fast.left, hz, fs), b = levelDb (slow, hz, fs);
        INFO (hz << " Hz: Bits " << a << " dB, 64x reference " << b << " dB");
        if (b > -70.0)
        {
            CHECK (std::abs (a - b) < 1.0);
            ++compared;
        }
    }
    CHECK (compared >= 3);
    CHECK (levelDb (fast.left, clock - tone, fs) > -40.0);        // it does alias, as this sampler does
}

TEST_CASE ("Bits: Damage takes the converter from transparent to its setting", "[bits]")
{
    const auto fs = 48000.0;
    const auto in = monoSine (1000.0, fs, 0.3, 0.5);
    for (int type = 0; type < 3; ++type)
    {
        double lastClock = 1.0e9, lastBits = 100.0;
        for (const auto damage : { 0.1f, 0.4f, 0.7f, 1.0f })
        {
            BitsModule module;
            module.prepare ({ fs, 512, 1 });
            renderModule (module, full (type, 0.3f, 0.3f), { damage, 0.0f }, in, 512, fs);
            INFO (typeNames[(size_t) type] << " at Damage " << damage << ": " << module.clockNow() << " Hz, " << module.bitsNow() << " bits");
            CHECK (module.clockNow() < lastClock);
            CHECK (module.bitsNow() < lastBits);
            lastClock = module.clockNow();
            lastBits = module.bitsNow();
        }
        CHECK (std::abs (lastClock - bits::clockHz ((BitsType) type, (double) 0.3f)) < 1.0e-9 * lastClock);
        CHECK (std::abs (lastBits - bits::depthBits ((BitsType) type, (double) 0.3f)) < 1.0e-9);
    }

    // Nearly transparent at a light touch: Sampler 12, intensity 0.05.
    BitsModule light;
    light.prepare ({ fs, 512, 1 });
    auto s = full (0);
    s.amount = 0.05f;
    const auto out = renderModule (light, s, { 1.0f, 0.0f }, in, 512, fs);
    CHECK (std::abs (levelDb (out.left, 1000.0, fs) - levelDb (in.left, 1000.0, fs)) < 0.1);
}

TEST_CASE ("Bits: Depth sets the converter's noise, 6 dB a bit", "[bits]")
{
    // Sampler 12 at its own clock; the error against the 1 kHz tone, measured beside it.
    const auto fs = 48000.0;
    const auto in = monoSine (997.0, fs, 0.6, 0.5);
    auto noiseDb = [&] (float depth)
    {
        const auto out = run (full (0, 0.5f, depth), in, fs);
        const auto reference = run (full (0, 0.5f, 1.0f), in, fs);        // 18 bits: the filters alone
        std::vector<float> e (out.size());
        for (size_t i = 0; i < e.size(); ++i)
            e[i] = out.left[i] - reference.left[i];
        return analysis::toDb (analysis::rms (e, 9600));
    };
    const auto at12 = noiseDb (0.5f), at8 = noiseDb (0.5f - 4.0f / 12.0f);
    INFO ("12 bits: " << at12 << " dB, 8 bits: " << at8 << " dB");
    CHECK (std::abs ((at8 - at12) - 24.1) < 3.0);
    CHECK (at12 < -65.0);
}

TEST_CASE ("Bits: mu-law keeps quiet signals finer than a linear converter of the same depth", "[bits]")
{
    const auto step = std::exp2 (1.0 - 8.0);
    for (const auto v : { 0.002, 0.01, 0.05 })
    {
        const auto mu = std::abs (BitsModule::quantise (v, step, true) - v);
        const auto linear = std::abs (BitsModule::quantise (v, step, false) - v);
        INFO (v << ": mu-law error " << mu << ", linear " << linear);
        CHECK (mu < linear);
    }
    CHECK (myzel::dsp::isExactly (BitsModule::quantise (0.0, step, true), 0.0));
    CHECK (std::abs (BitsModule::quantise (1.0, step, true) - 1.0) < 1.0e-12);
    CHECK (BitsModule::quantise (-0.3, step, true) < 0.0);
    double last = -2.0;
    for (double v = -1.0; v <= 1.0; v += 0.001)
    {
        const auto q = BitsModule::quantise (v, step, true);
        CHECK (q >= last);
        last = q;
    }
}

TEST_CASE ("Bits: Phone is a 300 - 3400 Hz band", "[bits]")
{
    const auto fs = 48000.0;
    auto at = [&] (double hz)
    {
        const auto in = monoSine (hz, fs, 0.6, 0.25);
        return levelDb (run (full (2, 0.5f, 1.0f), in, fs).left, hz, fs, 12000, 16384) - analysis::toDb (0.25);
    };
    const auto mid = at (1000.0), low = at (120.0), high = at (3900.0);
    INFO ("120 Hz " << low << " dB, 1 kHz " << mid << " dB, 3.9 kHz " << high << " dB");
    CHECK (std::abs (mid) < 1.5);
    CHECK (low < mid - 12.0);
    CHECK (high < mid - 6.0);
}

TEST_CASE ("Bits: renders are the same to the bit for any block size, jitter included", "[bits]")
{
    const auto fs = 48000.0;
    const auto in = analysis::programme (fs, 1.0, 5);
    for (int type = 0; type < 3; ++type)
    {
        const auto s = full (type, 0.4f, 0.4f, 0.8f);
        const auto reference = run (s, in, fs, 0.9f, 4096);
        for (const auto block : { 1, 7, 64, 333, 1024 })
        {
            const auto out = run (s, in, fs, 0.9f, block);
            INFO (typeNames[(size_t) type] << ", block " << block);
            CHECK (analysis::identical (out.left, reference.left));
            CHECK (analysis::identical (out.right, reference.right));
        }
    }
}

TEST_CASE ("Bits: a render from bar 17 samples at the instants a render from bar 1 did", "[bits]")
{
    // The clock's phase and its jitter are functions of the song time.
    const auto fs = 48000.0;
    const auto bar17 = 64.0;                                     // beats, 4/4
    const auto start = (size_t) std::lround (bar17 * 60.0 / 120.0 * fs);
    const auto all = analysis::programme (fs, (double) start / fs + 1.0, 9);
    analysis::Stereo late { std::vector<float> (all.left.begin() + (long) start, all.left.end()),
                            std::vector<float> (all.right.begin() + (long) start, all.right.end()) };

    for (int type = 0; type < 3; ++type)
    {
        const auto s = full (type, 0.3f, 0.5f, 1.0f);
        const auto fromOne = run (s, all, fs, 1.0f, 512);
        Transport t;
        t.startPpq = bar17;
        const auto fromSeventeen = run (s, late, fs, 1.0f, 512, t);
        const std::vector<float> tail (fromOne.left.begin() + (long) start, fromOne.left.end());
        const auto db = analysis::nullDb (fromSeventeen.left, tail, (size_t) (0.5 * fs));
        INFO (typeNames[(size_t) type] << ": " << db << " dB after 500 ms");
        CHECK (db < -80.0);
    }
}

TEST_CASE ("Bits: Jitter moves the instants, not the level", "[bits]")
{
    const auto fs = 48000.0;
    const auto in = monoSine (1500.0, fs, 0.6, 0.5);
    const auto steady = run (full (0, 0.5f, 1.0f, 0.0f), in, fs);
    const auto shaky = run (full (0, 0.5f, 1.0f, 1.0f), in, fs);
    const auto difference = analysis::nullDb (shaky.left, steady.left, 9600);
    INFO ("jitter vs none: " << difference << " dB; levels " << levelDb (shaky.left, 1500.0, fs) << " / " << levelDb (steady.left, 1500.0, fs));
    CHECK (difference > -50.0);
    CHECK (std::abs (levelDb (shaky.left, 1500.0, fs) - levelDb (steady.left, 1500.0, fs)) < 0.5);

    // A different seed, a different wander.
    BitsModule module;
    module.prepare ({ fs, 512, 1 });
    Macros other { 1.0f, 0.0f, 0x1234'5678u };
    const auto reseeded = renderModule (module, full (0, 0.5f, 1.0f, 1.0f), other, in, 512, fs);
    CHECK (! analysis::identical (reseeded.left, shaky.left));
}

TEST_CASE ("Bits: a type change ducks, then the new converter plays", "[bits]")
{
    const auto fs = 48000.0;
    const auto in = analysis::programme (fs, 0.5, 4);
    BitsModule module;
    module.prepare ({ fs, 256, 2 });
    const auto out = renderModule<BitsModule, BitsSettings> (module, full (0), { 1.0f, 0.0f }, in, 256, fs, {}, [] (int64_t t, BitsSettings& s, Macros&) {
        s.type = t < 12000 ? BitsType::sampler12 : BitsType::phone;
    });
    bool finite = true;
    for (const auto v : out.left)
        finite = finite && std::isfinite (v);
    CHECK (finite);
    CHECK (std::abs (module.clockNow() - bits::clockHz (BitsType::phone, 0.5)) < 1.0e-6);
    CHECK (analysis::peak (out.left, 12000) < 2.0 * analysis::peak (in.left));
}

TEST_CASE ("Bits: silence after sound settles to exact zeros, never to denormals", "[bits]")
{
    const auto fs = 48000.0;
    auto in = analysis::programme (fs, 4.0, 3);
    for (size_t i = 24000; i < in.size(); ++i)
        in.left[i] = in.right[i] = 0.0f;
    for (int type = 0; type < 3; ++type)
    {
        const auto out = run (full (type, 0.3f, 0.3f, 0.5f), in, fs);
        bool normal = true;
        size_t lastSound = 0;
        for (size_t i = 0; i < out.size(); ++i)
        {
            for (const auto v : { out.left[i], out.right[i] })
            {
                normal = normal && (myzel::dsp::isExactly (v, 0.0f) || std::isnormal (v));
                if (! myzel::dsp::isExactly (v, 0.0f))
                    lastSound = i;
            }
        }
        INFO (typeNames[(size_t) type] << ": exact silence " << (double) (lastSound - 24000) / fs << " s after the input stops");
        CHECK (normal);
        CHECK (lastSound < 24000 + (size_t) fs);
    }
}

TEST_CASE ("Bits: process() never allocates", "[bits]")
{
    const auto fs = 48000.0;
    BitsModule module;
    module.prepare ({ fs, 512, 2 });
    GridClock clock;
    clock.prepare (fs);
    std::vector<float> left (512), right (512);
    float* channels[] = { left.data(), right.data() };

    const auto before = allocations();
    int64_t sample = 0;
    for (int b = 0; b < 200; ++b)
    {
        for (size_t i = 0; i < 512; ++i)
            left[i] = right[i] = (float) (0.5 * std::sin (0.05 * (double) (sample + (int64_t) i)));
        clock.advance (Transport {}.at (sample, fs), 120.0, 512);
        auto s = full (b / 20 % 3, (float) (b % 7) / 6.0f, (float) (b % 5) / 4.0f, (float) (b % 3) / 2.0f);
        s.on = b % 23 != 0;
        module.process ({ channels, 2, 512, clock.blockStart() }, s, { clock, (float) (b % 11) / 10.0f, 0.0f, (uint32_t) b / 50u });
        sample += 512;
    }
    CHECK (allocations() == before);
}

TEST_CASE ("Bits: Rate and Depth read out per type, and typed text comes back", "[bits]")
{
    using namespace inkcap::dsp::bits;
    CHECK (rateText (0, 50.0f) == "26.0 kHz");
    CHECK (rateText (2, 50.0f) == "8.00 kHz");
    CHECK (rateText (1, 100.0f) == "32.0 kHz");
    CHECK (depthText (0, 50.0f) == "12 bits");
    CHECK (depthText (1, 50.0f) == "8 bits");
    CHECK (depthText (0, 0.0f) == "6 bits");
    for (int type = 0; type < 3; ++type)
        for (const auto percent : { 0.0f, 20.0f, 50.0f, 80.0f, 100.0f })
        {
            INFO (typeNames[(size_t) type] << " " << percent << " %: " << rateText (type, percent) << ", " << depthText (type, percent));
            CHECK (std::abs (rateFromText (type, rateText (type, percent)) - percent) < 1.0f);
            CHECK (std::abs (depthFromText (type, depthText (type, percent)) - percent) < 1.0f);
        }
    CHECK (std::abs (rateFromText (0, "26.04k") - 50.0f) < 0.01f);
    CHECK (std::abs (rateFromText (0, "26040 Hz") - 50.0f) < 0.01f);
    CHECK (std::abs (rateFromText (2, "8") - 50.0f) < 0.01f);
    CHECK (std::abs (depthFromText (1, "8 bit") - 50.0f) < 0.01f);
    CHECK (rateFromText (0, "fast") < 0.0f);
    CHECK (depthFromText (0, "12 kHz") < 0.0f);
}

TEST_CASE ("Bits: golden renders per type", "[bits][golden]")
{
    // Fixed input, settings and seed per type; float32 interleaved stereo. Re-record only for an
    // intended sound change: INKCAP_UPDATE_GOLDEN=1 InkcapTests_bits "[golden]".
    const auto fs = 48000.0;
    const auto in = analysis::programme (fs, 1.5, 11);
    const auto update = std::getenv ("INKCAP_UPDATE_GOLDEN") != nullptr;

    for (int type = 0; type < 3; ++type)
    {
        // Chaos 0.4 widens the jitter: part of the sound it locks.
        const auto out = run (full (type, 0.4f, 0.4f, 0.5f), in, fs, 0.8f, 256, {}, [] (int64_t, BitsSettings&, Macros& m) { m.chaos = 0.4f; });
        std::vector<float> interleaved (2 * out.size());
        for (size_t i = 0; i < out.size(); ++i)
        {
            interleaved[2 * i] = out.left[i];
            interleaved[2 * i + 1] = out.right[i];
        }

        const auto path = std::string (INKCAP_TEST_DATA_DIR) + "/modules/bits/golden/" + typeNames[(size_t) type] + ".f32";
        if (update)
        {
            std::ofstream file (path, std::ios::binary);
            file.write (reinterpret_cast<const char*> (interleaved.data()), (std::streamsize) (interleaved.size() * sizeof (float)));
            continue;
        }

        std::ifstream file (path, std::ios::binary);
        INFO (path);
        REQUIRE (file.good());
        std::vector<float> golden (interleaved.size());
        file.read (reinterpret_cast<char*> (golden.data()), (std::streamsize) (golden.size() * sizeof (float)));
        REQUIRE ((size_t) file.gcount() == golden.size() * sizeof (float));

        // Error energy: a quantiser turns a last-bit difference of another libm into a whole step
        // now and then; any change of the sound lands far above the threshold.
        const auto error = analysis::nullDb (interleaved, golden);
        INFO (typeNames[(size_t) type] << ": " << error << " dB from the golden render");
        CHECK (error < -60.0);
    }
}

TEST_CASE ("Bits: CPU per type at 48 kHz stereo", "[bits][.perf]")
{
    const auto fs = 48000.0;
    const auto in = analysis::programme (fs, 10.0, 2);
    for (int type = 0; type < 3; ++type)
        for (const auto jitter : { 0.0f, 1.0f })
        {
            BitsModule module;
            module.prepare ({ fs, 512, 2 });
            const auto start = std::chrono::steady_clock::now();
            const auto out = renderModule (module, full (type, 0.5f, 0.5f, jitter), { 1.0f, 0.0f }, in, 512, fs);
            const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            WARN (typeNames[(size_t) type] << ", jitter " << jitter << ": " << 100.0 * seconds / 10.0 << " % of one core");
            CHECK (std::isfinite (out.left.back()));
        }
}

TEST_CASE ("Bits: Chaos widens the jitter", "[bits]")
{
    // Jitter 0 at Chaos 1 jitters as Jitter 50 % does; Chaos 0 leaves Jitter alone.
    CHECK (std::abs (bits::jitterWithChaos (0.0f, 0.0f)) < 1.0e-12);
    CHECK (std::abs (bits::jitterWithChaos (0.3f, 0.0f) - 0.3) < 1.0e-7);
    CHECK (std::abs (bits::jitterWithChaos (0.0f, 1.0f) - 0.5) < 1.0e-12);
    CHECK (std::abs (bits::jitterWithChaos (1.0f, 1.0f) - 1.0) < 1.0e-12);

    const auto fs = 48000.0;
    const auto in = monoSine (1500.0, fs, 0.6, 0.5);
    auto chaos = [] (float c) { return [c] (int64_t, BitsSettings&, Macros& m) { m.chaos = c; }; };
    const auto steady = run (full (0, 0.5f, 0.9f, 0.0f), in, fs, 1.0f, 512, {}, chaos (0.0f));
    const auto wandering = run (full (0, 0.5f, 0.9f, 0.0f), in, fs, 1.0f, 512, {}, chaos (1.0f));
    const auto half = run (full (0, 0.5f, 0.9f, 0.5f), in, fs, 1.0f, 512, {}, chaos (0.0f));
    CHECK (analysis::nullDb (wandering.left, steady.left, 12000) > -60.0);
    CHECK (analysis::identical (wandering.left, half.left, 24000));
}
