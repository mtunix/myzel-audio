/*
    Crush on its own (InkcapTests_crush): the clean path at Damage 0 and Off, the aliasing gate,
    unity small-signal gain, what Damage and Drive do, clicks, determinism, silence, no
    allocation, the read-outs and the golden renders. Measured values: NOTES.md.
*/

#include "dsp/modules/crush/CrushControls.h"
#include "dsp/modules/crush/CrushModule.h"

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
    constexpr std::array<const char*, 4> typeNames { "tape", "cassette", "valve", "fuzz" };

    CrushSettings full (int type, float drive = 1.0f, float tone = 0.0f)
    {
        return { true, 1.0f, (CrushType) type, drive, tone };
    }

    analysis::Stereo monoSine (double hz, double fs, double seconds, double amplitude)
    {
        return { analysis::sine (hz, fs, seconds, amplitude), {} };
    }

    analysis::Stereo run (const CrushSettings& s, const analysis::Stereo& in, double fs, float damage = 1.0f, int block = 512,
                          const std::function<void (int64_t, CrushSettings&, Macros&)>& each = {})
    {
        CrushModule module;
        module.prepare ({ fs, block, in.right.empty() ? 1 : 2 });
        return renderModule (module, s, { damage, 0.0f }, in, block, fs, {}, each);
    }

    std::vector<float> delayed (const std::vector<float>& x, int latency)
    {
        std::vector<float> d (x.size(), 0.0f);
        for (size_t i = (size_t) latency; i < x.size(); ++i)
            d[i] = x[i - (size_t) latency];
        return d;
    }

    /** Level of the tone's harmonics 2 .. 9 against the tone (dB). */
    double harmonicsDb (const std::vector<float>& y, double f0, double fs, size_t from, size_t n)
    {
        const auto fundamental = analysis::toneAmplitude (y, f0, fs, from, n);
        double sum = 0.0;
        for (int h = 2; h <= 9; ++h)
        {
            const auto a = analysis::toneAmplitude (y, h * f0, fs, from, n);
            sum += a * a;
        }
        return analysis::toDb (std::sqrt (sum) / fundamental);
    }
}

TEST_CASE ("Crush: Damage 0 and Off are the input delayed by the pair's latency, to the bit", "[crush]")
{
    CHECK (CrushModule::pairLatencySamples() == 3);
    const auto in = analysis::programme (48000.0, 1.0);

    for (int type = 0; type < 4; ++type)
        for (const auto block : { 1, 64, 512 })
        {
            CrushModule module;
            module.prepare ({ 48000.0, block, 2 });
            REQUIRE (module.latencySamples() == 3);

            // Damage 0, on, every other control at its far end.
            const auto atZero = renderModule (module, full (type, 1.0f, 1.0f), { 0.0f, 1.0f }, in, block, 48000.0);
            INFO (typeNames[(size_t) type] << ", block " << block);
            CHECK (analysis::identical (atZero.left, delayed (in.left, 3)));
            CHECK (analysis::identical (atZero.right, delayed (in.right, 3)));

            // Off at full Damage.
            module.reset();
            auto off = full (type);
            off.on = false;
            const auto offOut = renderModule (module, off, { 1.0f, 0.0f }, in, block, 48000.0);
            CHECK (analysis::identical (offOut.left, delayed (in.left, 3)));
            CHECK (myzel::dsp::isExactly (module.intensity(), 0.0f));
        }
}

TEST_CASE ("Crush: on, then Damage to 0, lands on the clean path to the bit", "[crush]")
{
    // The module fades to the clean path over 20 ms once the intensity is 0; after that the
    // output is the delayed input exactly, whatever the stages did before.
    const auto fs = 48000.0;
    const auto in = analysis::programme (fs, 1.0);
    for (int type = 0; type < 4; ++type)
    {
        const auto out = run (full (type), in, fs, 1.0f, 256, [] (int64_t start, CrushSettings&, Macros& m) {
            m.damage = start < 24000 ? 1.0f : 0.0f;
        });
        INFO (typeNames[(size_t) type]);
        CHECK (! analysis::identical (out.left, delayed (in.left, 3), 0, 24000));
        CHECK (analysis::identical (out.left, delayed (in.left, 3), 24000 + 2048));
        CHECK (analysis::identical (out.right, delayed (in.right, 3), 24000 + 2048));
    }
}

TEST_CASE ("Crush: aliasing at max Drive, 44.1 kHz, stays below -80 dBc", "[crush][aliasing]")
{
    // The family's torture test (Mulberry, Umber): a 7 kHz tone at -1 dBFS, where every harmonic
    // from the 10th folds about the 2x rate into the audio band. Amount, Damage and Drive at max.
    const auto fs = 44100.0;
    constexpr size_t n = 1 << 16, settle = 16384;
    const auto f0 = analysis::onBin (7000.0, fs, n);
    const auto in = monoSine (f0, fs, (double) (n + settle) / fs + 0.01, std::pow (10.0, -1.0 / 20.0));

    for (int type = 0; type < 4; ++type)
    {
        const auto out = run (full (type), in, fs);
        const auto m = analysis::aliasing (out.left, settle, n, f0, fs);
        INFO (typeNames[(size_t) type] << ": worst alias " << m.worstDbc << " dBc at " << m.worstHz << " Hz, tone " << m.toneDb << " dBFS");
        CHECK (m.worstDbc < -80.0);
        WARN (typeNames[(size_t) type] << ": worst alias " << m.worstDbc << " dBc at " << m.worstHz << " Hz");
    }

    // A tape's reversal kinks fold back at every Drive, not least in the middle (the old coating:
    // -78.5 dBc at Tape's 61 %, -75.7 at Cassette's 43 %), so the gate holds over the whole range.
    for (int type = 0; type < 3; ++type)
    {
        double worst = -300.0, at = 0.0;
        for (int d = 0; d <= 100; d += 5)
        {
            const auto m = analysis::aliasing (run (full (type, (float) d / 100.0f), in, fs).left, settle, n, f0, fs);
            if (m.worstDbc > worst)
            {
                worst = m.worstDbc;
                at = d;
            }
        }
        INFO (typeNames[(size_t) type] << ": worst alias over the Drive range " << worst << " dBc at " << at << " %");
        CHECK (worst < -80.0);
    }

    // Chaos wanders the drive by up to 4 dB but never past the type's top (F1BBCDC8 took Tape to
    // -79.7 dBc with the old coating).
    for (int type = 0; type < 3; ++type)
        for (const auto seed : { 0xF1BBCDC8u, 1u, 7u, 0x4393'0A3Du, 0x8719'8E02u, 0xDEADBEEFu })
        {
            CrushModule module;
            module.prepare ({ fs, 512, 1 });
            const auto out = renderModule (module, full (type), { 1.0f, 1.0f, seed }, in, 512, fs);
            const auto m = analysis::aliasing (out.left, settle, n, f0, fs);
            INFO (typeNames[(size_t) type] << ", Chaos 100 %, seed " << std::hex << seed << std::dec << ": worst alias " << m.worstDbc << " dBc at " << m.worstHz << " Hz");
            CHECK (m.worstDbc < -80.0);
        }
}

TEST_CASE ("Crush: the head realises TapeLosses' response, at every wear", "[crush]")
{
    // HeadLosses is TapeLosses' fitted prototypes as TPT sections: its response follows
    // TapeLosses' own digital response in the band, and the running filter follows its response().
    // At 2x, where Crush runs it.
    const auto fs = 88200.0;
    for (const auto& voicing : { crush::tapeVoicing(), crush::cassetteVoicing() })
        for (const auto intensity : { 0.2, 0.6, 1.0 })
        {
            const auto head = crush::TapeStage::wornHead (voicing.geometry, intensity);
            crush::HeadLosses mine;
            mine.prepare (fs);
            mine.design (head, voicing.speed);
            const auto theirs = myzel::dsp::TapeLosses::design (head, voicing.speed, fs);

            // Against Bertram's closed forms, where the fits hold (down to -12 dB), as TapeLosses'
            // own sections are; up to a quarter of the rate (22 kHz), and to 16 kHz.
            double worst = 0.0, worstLow = 0.0, theirsWorst = 0.0;
            for (double hz = 20.0; hz <= 16000.0; hz *= 1.05)
            {
                const auto closed = analysis::toDb (myzel::dsp::TapeLosses::closedForm (head, voicing.speed, hz));
                if (closed < -12.0)
                    break;
                const auto error = std::abs (analysis::toDb (std::abs (mine.lossResponse (hz))) - closed);
                worst = std::max (worst, error);
                if (hz <= 0.25 * fs)
                    worstLow = std::max (worstLow, error);
                theirsWorst = std::max (theirsWorst, std::abs (analysis::toDb (std::abs (myzel::dsp::TapeLosses::lossResponse (theirs, hz, fs))) - closed));
            }
            INFO ("speed " << voicing.speed << ", intensity " << intensity << ": " << worstLow << " dB from the closed forms up to fs/4, "
                  << worst << " dB to 16 kHz (TapeLosses " << theirsWorst << " dB)");
            CHECK (worstLow < 0.6);
            CHECK (worst < 0.6);

            // The whole head, bump included, against TapeLosses' digital response in the band.
            double apart = 0.0;
            for (double hz = 20.0; hz <= 8000.0; hz *= 1.1)
                apart = std::max (apart, std::abs (analysis::toDb (std::abs (mine.response (hz)))
                                                   - analysis::toDb (std::abs (myzel::dsp::TapeLosses::response (theirs, hz, fs)))));
            INFO ("worst " << apart << " dB from TapeLosses up to 8 kHz");
            CHECK (apart < 0.5);

            // The running filter against its response(), by sines.
            for (const auto hz : { 100.0, 3000.0, 12000.0 })
            {
                mine.reset();
                const auto x = analysis::sine (hz, fs, 0.25, 0.5);
                std::vector<float> y (x.size());
                for (size_t n = 0; n < x.size(); ++n)
                    y[n] = (float) mine.process (x[n]);
                const auto measured = analysis::toneAmplitude (y, hz, fs, 8192, 8192) / 0.5;
                INFO (hz << " Hz: " << analysis::toDb (measured) << " dB, response " << analysis::toDb (std::abs (mine.response (hz))));
                CHECK (std::abs (analysis::toDb (measured / std::abs (mine.response (hz)))) < 0.05);
            }
        }
}

TEST_CASE ("Crush: small signals pass at unity below the split", "[crush]")
{
    // Every stage divides its small-signal gain out, so Damage changes colour, not level (Match
    // takes care of the loudness of the saturation in M3).
    const auto fs = 48000.0;
    const auto in = monoSine (300.0, fs, 1.0, 0.001);
    for (int type = 0; type < 4; ++type)
    {
        const auto out = run (full (type, 0.5f), in, fs);
        const auto db = analysis::toDb (analysis::toneAmplitude (out.left, 300.0, fs, 24000, 16384) / 0.001);
        INFO (typeNames[(size_t) type] << ": " << db << " dB at 300 Hz");
        CHECK (std::abs (db) < 1.0);
    }
}

TEST_CASE ("Crush: Damage and Drive raise the distortion, steadily", "[crush]")
{
    const auto fs = 48000.0;
    const auto in = monoSine (200.0, fs, 0.6, 0.5);
    for (int type = 0; type < 4; ++type)
    {
        double last = -300.0;
        for (const auto damage : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const auto out = run (full (type, 0.5f), in, fs, damage);
            const auto h = harmonicsDb (out.left, 200.0, fs, 9600, 16384);
            INFO (typeNames[(size_t) type] << " at Damage " << damage << ": harmonics " << h << " dB");
            CHECK (h > last);
            last = h;
        }
        CHECK (last > -40.0);

        last = -300.0;
        for (const auto drive : { 0.0f, 0.5f, 1.0f })
        {
            const auto out = run (full (type, drive), in, fs);
            const auto h = harmonicsDb (out.left, 200.0, fs, 9600, 16384);
            INFO (typeNames[(size_t) type] << " at Drive " << drive << ": harmonics " << h << " dB");
            CHECK (h > last);
            last = h;
        }
    }
}

TEST_CASE ("Crush: Tone tilts around 1 kHz, and only as far as Damage goes", "[crush]")
{
    const auto fs = 48000.0;
    const auto in = monoSine (8000.0, fs, 0.5, 0.001);
    for (int type = 0; type < 4; ++type)
    {
        auto levelAt = [&] (float tone, float damage)
        {
            const auto out = run (full (type, 0.0f, tone), in, fs, damage);
            return analysis::toDb (analysis::toneAmplitude (out.left, 8000.0, fs, 12000, 8192));
        };
        const auto bright = levelAt (1.0f, 1.0f) - levelAt (0.0f, 1.0f);
        const auto dark = levelAt (-1.0f, 1.0f) - levelAt (0.0f, 1.0f);
        const auto halfway = levelAt (1.0f, 0.4f) - levelAt (0.0f, 0.4f);
        INFO (typeNames[(size_t) type] << ": bright " << bright << " dB, dark " << dark << " dB, bright at Damage 0.4 " << halfway << " dB");
        CHECK (bright > 4.0);
        CHECK (dark < -4.0);
        CHECK (halfway > 0.5);
        CHECK (halfway < bright);
    }
}

TEST_CASE ("Crush: renders are the same to the bit for any block size", "[crush]")
{
    const auto fs = 48000.0;
    const auto in = analysis::programme (fs, 1.0, 5);
    for (int type = 0; type < 4; ++type)
    {
        const auto reference = run (full (type, 0.5f, 0.3f), in, fs, 0.8f, 4096);
        for (const auto block : { 1, 7, 64, 333, 1024 })
        {
            const auto out = run (full (type, 0.5f, 0.3f), in, fs, 0.8f, block);
            INFO (typeNames[(size_t) type] << ", block " << block);
            CHECK (analysis::identical (out.left, reference.left));
            CHECK (analysis::identical (out.right, reference.right));
        }

        // A type change and a Drive move on the way, at a sample that starts a block for every size.
        auto each = [type] (int64_t start, CrushSettings& s, Macros&)
        {
            if (start >= 16384)
            {
                s.type = (CrushType) (type ^ 1);
                s.drive = 0.9f;
            }
        };
        const auto changed = run (full (type, 0.5f, 0.3f), in, fs, 0.8f, 4096, each);
        for (const auto block : { 1, 64, 1024 })
        {
            const auto out = run (full (type, 0.5f, 0.3f), in, fs, 0.8f, block, each);
            INFO (typeNames[(size_t) type] << ", block " << block << ", with a type change");
            CHECK (analysis::identical (out.left, changed.left));
            CHECK (analysis::identical (out.right, changed.right));
        }
    }
}

TEST_CASE ("Crush: no clicks from On/Off, type changes, Drive and Tone moves", "[crush]")
{
    // The chain's click gate on a 330 Hz sine: the 3rd-difference crest stays at or below -60 dB
    // around every change (the steady sine reads about -70 dB at 48 kHz).
    const auto fs = 48000.0;
    const auto in = monoSine (330.0, fs, 1.6, 0.5);

    // Each case and the drives and tones it visits. Fuzz at full Drive clips the sine into edges
    // of its own (-50 dB steady at Damage 0.6): a change must be no sharper than its settings.
    struct Case { const char* name; std::function<void (int64_t, CrushSettings&, Macros&)> each; std::vector<std::pair<float, float>> visits; };
    const std::vector<Case> cases {
        { "on/off", [] (int64_t t, CrushSettings& s, Macros&) { s.on = ! (t >= 19200 && t < 48000); }, { { 0.5f, 0.0f } } },
        { "type", [] (int64_t t, CrushSettings& s, Macros&) { s.type = (CrushType) ((t / 12000) % 4); }, { { 0.5f, 0.0f } } },
        { "drive", [] (int64_t t, CrushSettings& s, Macros&) { s.drive = (t / 9600) % 2 == 0 ? 0.0f : 1.0f; }, { { 0.0f, 0.0f }, { 1.0f, 0.0f } } },
        { "tone", [] (int64_t t, CrushSettings& s, Macros&) { s.tone = (t / 9600) % 2 == 0 ? -1.0f : 1.0f; }, { { 0.5f, -1.0f }, { 0.5f, 1.0f } } },
        { "damage to 0", [] (int64_t t, CrushSettings&, Macros& m) { m.damage = (t / 19200) % 2 == 0 ? 0.6f : 0.0f; }, { { 0.5f, 0.0f } } },
    };

    for (int type = 0; type < 4; ++type)
        for (const auto& c : cases)
        {
            auto steady = -300.0;
            for (const auto& [drive, tone] : c.visits)
                steady = std::max (steady, analysis::clickCrestDb (run (full (type, drive, tone), in, fs, 0.6f, 480).left, 4800));
            const auto gate = std::max (-60.0, steady + 1.0);

            const auto out = run (full (type, 0.5f), in, fs, 0.6f, 480, c.each);
            const auto crest = analysis::clickCrestDb (out.left, 4800);
            const auto peak = analysis::thirdDifferencePeak (out.left, 4800);
            size_t worst = 0;
            for (size_t i = 4800; i < out.size(); ++i)
                if (std::abs ((double) out.left[i] - 3.0 * out.left[i - 1] + 3.0 * out.left[i - 2] - out.left[i - 3]) >= peak)
                    worst = i;
            INFO (typeNames[(size_t) type] << ", " << c.name << ": crest " << crest << " dB at sample " << worst << ", steady " << steady << " dB");
            CHECK (crest <= gate);
        }
}

TEST_CASE ("Crush: silence after sound settles to exact zeros, never to denormals", "[crush]")
{
    const auto fs = 48000.0;
    auto in = analysis::programme (fs, 8.0, 3);
    for (size_t i = 24000; i < in.size(); ++i)
        in.left[i] = in.right[i] = 0.0f;

    for (int type = 0; type < 4; ++type)
    {
        const auto out = run (full (type, 0.7f, 0.5f), in, fs);
        bool normal = true;
        for (size_t i = 24000; i < out.size(); ++i)
            normal = normal && (myzel::dsp::isExactly (out.left[i], 0.0f) || std::isnormal (out.left[i])) && (myzel::dsp::isExactly (out.right[i], 0.0f) || std::isnormal (out.right[i]));
        size_t lastSound = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (! myzel::dsp::isExactly (out.left[i], 0.0f) || ! myzel::dsp::isExactly (out.right[i], 0.0f))
                lastSound = i;
        INFO (typeNames[(size_t) type] << ": exact silence " << (double) (lastSound - 24000) / fs << " s after the input stops");
        CHECK (normal);
        CHECK (lastSound < 24000 + (size_t) (2.0 * fs));

        // And it wakes as a fresh render would: the second burst sounds like the first.
        auto twice = in;
        for (size_t i = 0; i < 24000; ++i)
        {
            twice.left[i + 6 * 48000] = in.left[i];
            twice.right[i + 6 * 48000] = in.right[i];
        }
        const auto again = run (full (type, 0.7f, 0.5f), twice, fs);
        CHECK (analysis::identical (std::vector<float> (again.left.begin() + 6 * 48000, again.left.begin() + 6 * 48000 + 24000),
                                    std::vector<float> (again.left.begin(), again.left.begin() + 24000)));
    }
}

TEST_CASE ("Crush: process() never allocates", "[crush]")
{
    const auto fs = 48000.0;
    CrushModule module;
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
        auto s = full (b / 20 % 4, (float) (b % 7) / 6.0f, (float) (b % 5) / 2.0f - 1.0f);
        s.on = b % 23 != 0;
        module.process ({ channels, 2, 512, clock.blockStart() }, s, { clock, (float) (b % 11) / 10.0f, 0.0f, 1u });
        sample += 512;
    }
    CHECK (allocations() == before);
}

TEST_CASE ("Crush: Drive reads out in dB per type, and typed text comes back", "[crush]")
{
    using namespace inkcap::dsp::crush;
    CHECK (driveText (0, 0.0f) == "+4 dB");
    CHECK (driveText (0, 100.0f) == "+22 dB");
    CHECK (driveText (3, 50.0f) == "+24 dB");
    for (int type = 0; type < 4; ++type)
        for (const auto percent : { 0.0f, 25.0f, 50.0f, 100.0f })
        {
            const auto back = driveFromText (type, driveText (type, percent));
            INFO (typeNames[(size_t) type] << " " << percent << " % -> " << driveText (type, percent) << " -> " << back);
            CHECK (std::abs (back - percent) < 3.0f);
        }
    CHECK (std::abs (driveFromText (1, "15") - 50.0f) < 0.01f);
    CHECK (std::abs (driveFromText (1, "15 dB") - 50.0f) < 0.01f);
    CHECK (std::abs (driveFromText (1, "40 %") - 40.0f) < 0.01f);
    CHECK (driveFromText (2, "loud") < 0.0f);
    CHECK (driveFromText (2, "12 Hz") < 0.0f);
}

TEST_CASE ("Crush: golden renders per type", "[crush][golden]")
{
    // Fixed input and settings per type; the files hold float32 interleaved stereo. Re-record
    // only for an intended sound change: INKCAP_UPDATE_GOLDEN=1 InkcapTests_crush "[golden]".
    const auto fs = 48000.0;
    const auto in = analysis::programme (fs, 1.5, 11);
    const auto update = std::getenv ("INKCAP_UPDATE_GOLDEN") != nullptr;

    for (int type = 0; type < 4; ++type)
    {
        // Chaos 0.5: the drive's wander (stream crushDrift) is part of the sound it locks.
        const auto out = run (full (type, 0.6f, -0.2f), in, fs, 0.8f, 256, [] (int64_t, CrushSettings&, Macros& m) { m.chaos = 0.5f; });
        std::vector<float> interleaved (2 * out.size());
        for (size_t i = 0; i < out.size(); ++i)
        {
            interleaved[2 * i] = out.left[i];
            interleaved[2 * i + 1] = out.right[i];
        }

        const auto path = std::string (INKCAP_TEST_DATA_DIR) + "/modules/crush/golden/" + typeNames[(size_t) type] + ".f32";
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

        // Error energy against the recording: other compilers and libms round differently, any
        // change of the sound lands far above the threshold.
        const auto error = analysis::nullDb (interleaved, golden);
        INFO (typeNames[(size_t) type] << ": " << error << " dB from the golden render");
        CHECK (error < -90.0);
    }
}

TEST_CASE ("Crush: aliasing report, max Drive, 44.1 kHz", "[crush][.quality]")
{
    // Beyond the gate's 7 kHz: lower tones reach the stages at full strength (NOTES.md, Aliasing).
    const auto fs = 44100.0;
    constexpr size_t n = 1 << 16, settle = 16384;
    for (const auto hz : { 1000.0, 3000.0, 7000.0 })
    {
        const auto f0 = analysis::onBin (hz, fs, n);
        const auto in = monoSine (f0, fs, (double) (n + settle) / fs + 0.01, std::pow (10.0, -1.0 / 20.0));
        for (int type = 0; type < 4; ++type)
        {
            const auto m = analysis::aliasing (run (full (type), in, fs).left, settle, n, f0, fs);
            WARN (typeNames[(size_t) type] << ", " << hz << " Hz: worst alias " << m.worstDbc << " dBc at " << m.worstHz << " Hz");
        }
    }
}

TEST_CASE ("Crush: CPU per type at 48 kHz stereo", "[crush][.perf]")
{
    const auto fs = 48000.0;
    const auto in = analysis::programme (fs, 10.0, 2);
    for (int type = 0; type < 4; ++type)
    {
        CrushModule module;
        module.prepare ({ fs, 512, 2 });
        const auto start = std::chrono::steady_clock::now();
        const auto out = renderModule (module, full (type, 0.5f), { 1.0f, 0.0f }, in, 512, fs);
        const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        WARN (typeNames[(size_t) type] << ": " << 100.0 * seconds / 10.0 << " % of one core (Drive 50 %, full Damage)");
        CHECK (std::isfinite (out.left.back()));
    }
}

TEST_CASE ("Crush: Chaos lets the drive wander with the song time, never clicking", "[crush]")
{
    // A 200 Hz sine at -6 dBFS through Tape at full intensity: how hard it saturates (harmonics
    // 2 to 9 against the fundamental) in quarter-second windows. Steady at Chaos 0; at Chaos 1 the
    // drive wanders by up to 4 dB either way, so the saturation breathes.
    const auto fs = 48000.0;
    const auto in = monoSine (200.0, fs, 8.0, 0.5);
    auto spread = [&] (float chaos)
    {
        const auto out = run (full (0, 0.5f), in, fs, 1.0f, 512, [chaos] (int64_t, CrushSettings&, Macros& m) { m.chaos = chaos; });
        double lo = 1.0e9, hi = -1.0e9;
        for (size_t from = (size_t) fs; from + 12000 <= out.size(); from += 12000)
        {
            const auto f = analysis::toneAmplitude (out.left, 200.0, fs, from, 12000);
            double h = 0.0;
            for (int k = 2; k <= 9; ++k)
                h += std::pow (analysis::toneAmplitude (out.left, 200.0 * k, fs, from, 12000), 2.0);
            const auto thd = analysis::toDb (std::sqrt (h) / f);
            lo = std::min (lo, thd);
            hi = std::max (hi, thd);
        }
        return hi - lo;
    };
    const auto steady = spread (0.0f), wandering = spread (1.0f);
    INFO ("THD spread over 7 s: " << steady << " dB at Chaos 0, " << wandering << " dB at Chaos 1");
    CHECK (steady < 0.2);
    CHECK (wandering > 2.0);

    // The same for any block size (the seed changes on a boundary every block size shares,
    // 49152 = 12 x 4096), and a seed change crossfades the wander (no click).
    const auto x = analysis::programme (fs, 1.5, 9);
    auto each = [] (int64_t t, CrushSettings&, Macros& m)
    {
        m.chaos = 0.8f;
        m.seed = t < 49152 ? 7u : 8u;
    };
    const auto reference = run (full (1, 0.6f), x, fs, 0.9f, 4096, each);
    for (const auto block : { 1, 64, 1024 })
        CHECK (analysis::identical (run (full (1, 0.6f), x, fs, 0.9f, block, each).left, reference.left));

    const auto sine = monoSine (330.0, fs, 2.0, 0.5);
    const auto seeded = run (full (0, 0.5f), sine, fs, 0.6f, 480, [] (int64_t t, CrushSettings&, Macros& m)
    {
        m.chaos = 1.0f;
        m.seed = t < 48000 ? 7u : 8u;
    });
    CHECK (analysis::clickCrestDb (seeded.left, 4800) <= -60.0);
}
