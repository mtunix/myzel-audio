#include "dsp/algorithms/billow/BillowEngine.h"
#include "support/EngineGates.h"

#include <iostream>

namespace
{
    constexpr double fs = 48000.0;

    cerulean::gates::Options billowOptions()
    {
        cerulean::gates::Options o;
        o.cpuBudgetPercent = 5.0;   // PLAN.md budget for Nimbus/Billow/Echo

        // Billow's tail grows for up to one bloom length (4 s at Bloom 100 %, plus a 0.5 s
        // release) before it starts to decay, so every window that expects a decaying tail starts
        // that much later. The decay after the bloom is exactly the Decay setting (see the
        // "decay after the bloom" test below, which measures it from the bloom peak on).
        o.extraTailSeconds = 4.5;
        return o;
    }

    /** Energy envelope (L + R) in frames of frameMs, in dB. */
    std::vector<double> envelopeDb (const cerulean::analysis::Stereo& x, double frameMs = 20.0)
    {
        const auto frame = (size_t) (frameMs * 0.001 * x.sampleRate);
        std::vector<double> db;
        for (size_t pos = 0; pos + frame <= x.size(); pos += frame)
        {
            const auto l = cerulean::analysis::rms (x.left, pos, pos + frame);
            const auto r = cerulean::analysis::rms (x.right, pos, pos + frame);
            db.push_back (10.0 * std::log10 (std::max (l * l + r * r, 1.0e-30)));
        }
        return db;
    }

    struct Bloom
    {
        double peakSeconds = 0.0;   // time of the loudest 20 ms frame
        double riseDb = 0.0;        // loudest frame vs the first 100 ms
    };

    Bloom measureBloom (const cerulean::analysis::Stereo& ir)
    {
        const auto env = envelopeDb (ir);
        const auto peak = (size_t) (std::max_element (env.begin(), env.end()) - env.begin());
        double onset = -300.0;
        for (size_t i = 0; i < 5 && i < env.size(); ++i)
            onset = std::max (onset, env[i]);
        return { (double) peak * 0.020, env[peak] - onset };
    }

    /** Mid-band RT60 of the part of the IR after `startSeconds` (the decay after the bloom). */
    double rt60After (const cerulean::analysis::Stereo& ir, double startSeconds)
    {
        cerulean::analysis::Stereo tail;
        tail.sampleRate = ir.sampleRate;
        const auto start = (long) (startSeconds * ir.sampleRate);
        tail.left.assign (ir.left.begin() + start, ir.left.end());
        tail.right.assign (ir.right.begin() + start, ir.right.end());
        return cerulean::analysis::midBandRt60 (tail);
    }
}

CERULEAN_ENGINE_GATES ("[billow]", cerulean::dsp::BillowEngine, billowOptions())

namespace
{
    cerulean::analysis::Stereo billowIr (cerulean::dsp::EngineParams p, double seconds)
    {
        auto e = cerulean::gates::makeEngine<cerulean::dsp::BillowEngine> (fs);
        return cerulean::analysis::impulseResponse (*e, p, fs, seconds);
    }

    double bloomSeconds (float a) { return cerulean::dsp::maps::bloomLengthMs (a) * 0.001; }
}

TEST_CASE ("Billow: Bloom sets how long the tail keeps growing", "[billow]")
{
    using namespace cerulean;
    double previous = 0.0;
    for (const float a : { 0.0f, 0.5f, 1.0f })
    {
        auto p = dsp::EngineParams {};
        p.decaySeconds = 6.0f;
        p.paramA = a;
        const auto bloom = measureBloom (billowIr (p, bloomSeconds (a) + 3.0));
        INFO ("Bloom " << a << " (" << bloomSeconds (a) << " s): energy peaks at " << bloom.peakSeconds
              << " s, " << bloom.riseDb << " dB above the onset");
        CHECK (bloom.peakSeconds >= 0.8 * bloomSeconds (a));
        CHECK (bloom.peakSeconds <= bloomSeconds (a) + 0.25);
        CHECK (bloom.riseDb > 3.0);   // a 150 ms bloom has little time to build; the longer ones rise 10+ dB
        CHECK (bloom.peakSeconds > previous);
        previous = bloom.peakSeconds;
    }
}

TEST_CASE ("Billow: Feedback goes from a subtle swell to a huge build", "[billow]")
{
    using namespace cerulean;
    std::array<double, 3> rise {};
    int k = 0;
    for (const float b : { 0.0f, 0.5f, 1.0f })
    {
        auto p = dsp::EngineParams {};
        p.decaySeconds = 3.0f;
        p.paramB = b;
        rise[(size_t) k++] = measureBloom (billowIr (p, 3.0)).riseDb;
    }
    INFO ("bloom rise above the onset: Feedback 0 " << rise[0] << " dB, 50 % " << rise[1] << " dB, 100 % " << rise[2] << " dB");
    CHECK (rise[0] > 1.5);              // even the subtle end still grows after the note
    CHECK (rise[0] < 6.0);
    CHECK (rise[1] > rise[0] + 4.0);
    CHECK (rise[2] > rise[1] + 4.0);
    CHECK (rise[2] > 12.0);
}

TEST_CASE ("Billow: after the bloom, the tail decays at exactly Decay", "[billow]")
{
    using namespace cerulean;

    // The standard RT60 gate fits the whole IR (bloom included, see extraTailSeconds); this one
    // measures only the part after the bloom has closed (bloom length + release), across Bloom,
    // Feedback and Decay, including a Decay much shorter than the bloom.
    struct Case { float a, b, decay; };
    for (const auto c : { Case { 1.0f, 1.0f, 1.0f }, Case { 1.0f, 0.5f, 3.0f }, Case { 0.5f, 1.0f, 2.0f },
                          Case { 0.0f, 0.0f, 6.0f }, Case { 0.5f, 0.5f, 10.0f }, Case { 1.0f, 1.0f, 20.0f } })
    {
        auto p = dsp::EngineParams {};
        p.paramA = c.a;
        p.paramB = c.b;
        p.decaySeconds = c.decay;
        const auto closed = bloomSeconds (c.a) + std::clamp (0.25 * bloomSeconds (c.a), 0.08, 0.5) + 0.1;
        const auto ir = billowIr (p, closed + 0.65 * c.decay + 0.5);
        const auto rt = rt60After (ir, closed);
        INFO ("Bloom " << c.a << ", Feedback " << c.b << ", Decay " << c.decay << " s: RT60 after the bloom " << rt << " s");
        CHECK (std::abs (rt - c.decay) <= 0.1 * c.decay);
    }
}

TEST_CASE ("Billow: the bloom stays bounded, whatever feeds it", "[billow]")
{
    using namespace cerulean;
    auto e = gates::makeEngine<dsp::BillowEngine> (fs);

    // Worst case: Feedback 100 %, Bloom 4 s, Decay 60 s, biggest Size, full Mod, 0 dBFS square
    // waves. Once held (one onset: blooms, then settles), once as 100 ms bursts every 0.5 s, which
    // re-open the bloom forever.
    auto p = dsp::EngineParams {};
    p.decaySeconds = 60.0f;
    p.paramA = 1.0f;
    p.paramB = 1.0f;
    p.sizeScale = 2.0f;
    p.mod = 1.0f;

    analysis::Stereo held, bursts;
    held.left.resize ((size_t) (fs * 60.0));
    bursts.left.resize (held.left.size());
    for (size_t i = 0; i < held.left.size(); ++i)
    {
        held.left[i] = ((i / 240) % 2) ? 1.0f : -1.0f;
        bursts.left[i] = (i % 24000) < 4800 ? held.left[i] : 0.0f;
    }
    held.right = held.left;
    bursts.right = bursts.left;

    for (const auto* in : { &held, &bursts })
    {
        e->reset();
        const auto out = analysis::renderEngine (*e, p, *in, in->size());
        REQUIRE (analysis::allFinite (out));
        const auto middle = analysis::toDb (analysis::rms (out.left, (size_t) (20 * fs), (size_t) (40 * fs)));
        const auto late = analysis::toDb (analysis::rms (out.left, (size_t) (40 * fs)));
        INFO ((in == &held ? "held square" : "square bursts") << ": peak " << analysis::peak (out)
              << ", rms 20-40 s " << middle << " dB, 40-60 s " << late << " dB");
        CHECK (analysis::peak (out) < 32.0);
        CHECK (late < middle + 3.0);   // settled, not running away
    }
}

TEST_CASE ("Billow: freezing in the middle of a bloom holds the tail, it never grows", "[billow]")
{
    using namespace cerulean;
    auto e = gates::makeEngine<dsp::BillowEngine> (fs);

    auto p = dsp::EngineParams {};
    p.decaySeconds = 6.0f;
    p.paramA = 1.0f;     // 4 s bloom
    p.paramB = 1.0f;     // the strongest build
    auto frozen = p;
    frozen.freeze = 1.0f;

    // Freeze 1 s into a 4 s bloom: the bloom is open and pouring energy into the tank.
    gates::SmoothedAutomation automation (p, fs);
    const auto out = analysis::renderEngine (*e, p, gates::noise (fs, 0.3), (size_t) (fs * 30.0), [&] (size_t pos)
    {
        return automation.step (pos < (size_t) fs ? p : frozen);
    });
    REQUIRE (analysis::allFinite (out));

    // 1 s windows like the standard freeze gate (a static, lossless network beats slowly: shorter
    // windows wander by +-0.7 dB). Reference: the 2 s right after freezing.
    auto windowDb = [&] (double t0, double length = 1.0)
    {
        const auto a = (size_t) (t0 * fs), b = (size_t) ((t0 + length) * fs);
        return analysis::toDb (analysis::rms (out.left, a, b) + analysis::rms (out.right, a, b));
    };
    const auto reference = windowDb (1.5, 2.0);
    double loudest = -300.0, quietest = 300.0;
    for (double t = 1.5; t <= 29.0; t += 0.5)
    {
        loudest = std::max (loudest, windowDb (t));
        quietest = std::min (quietest, windowDb (t));
    }
    INFO ("frozen mid-bloom: " << reference << " dB (1.5-3.5 s), 1 s windows range " << quietest << " .. " << loudest << " dB");
    CHECK (loudest - reference < 1.0);
    CHECK (reference - quietest < 1.0);
}

TEST_CASE ("Billow: every new note blooms again", "[billow]")
{
    using namespace cerulean;
    auto e = gates::makeEngine<dsp::BillowEngine> (fs);

    auto p = dsp::EngineParams {};
    p.decaySeconds = 2.0f;
    analysis::Stereo in;
    in.left.assign ((size_t) (fs * 3.0) + 1, 0.0f);
    in.left.front() = 1.0f;
    in.left.back() = 1.0f;   // a second note 3 s later, into the decaying tail of the first
    in.right = in.left;
    const auto out = analysis::renderEngine (*e, p, in, (size_t) (fs * 6.0));

    // Second note: compare its first 100 ms with its bloom one bloom length later.
    const auto second = (size_t) (fs * 3.0);
    const auto t = (size_t) (bloomSeconds (p.paramA) * fs);
    const auto onset = analysis::toDb (analysis::rms (out.left, second, second + 4800));
    const auto bloomed = analysis::toDb (analysis::rms (out.left, second + t - 2400, second + t + 2400));
    INFO ("second note: " << onset << " dB at its onset, " << bloomed << " dB one bloom length later");
    CHECK (bloomed > onset + 6.0);
}

// Not a test: prints the numbers NOTES.md quotes (run with "[.measure]").
TEST_CASE ("Billow: measurement dump", "[.measure][billow]")
{
    using namespace cerulean;
    auto e = gates::makeEngine<dsp::BillowEngine> (fs);

    // Per-band RT60 after the bloom (defaults), echo density and stereo.
    for (const float decay : { 1.0f, 3.0f, 10.0f })
    {
        auto p = dsp::EngineParams {};
        p.decaySeconds = decay;
        const auto ir = analysis::impulseResponse (*e, p, fs, 1.3 * decay + 2.0);
        const auto start = bloomSeconds (p.paramA) + 0.35;
        analysis::Stereo tail;
        tail.sampleRate = fs;
        tail.left.assign (ir.left.begin() + (long) (start * fs), ir.left.end());
        tail.right.assign (ir.right.begin() + (long) (start * fs), ir.right.end());
        std::cout << "decay " << decay << " (after the bloom): RT125 " << analysis::bandRt60 (tail, 125.0)
                  << " RT250 " << analysis::bandRt60 (tail, 250.0) << " mid " << analysis::midBandRt60 (tail)
                  << " RT4k " << analysis::bandRt60 (tail, 4000.0) << " RT8k " << analysis::bandRt60 (tail, 8000.0)
                  << " | ED0.9 " << analysis::echoDensityTime (ir.left, fs)
                  << " rho " << analysis::correlation (ir.left, ir.right, (size_t) (0.2 * fs), (size_t) (2.0 * fs)) << "\n";
    }

    // Bloom grid: when the energy peaks, how far it rises, the decay after it.
    for (const float decay : { 1.0f, 3.0f, 10.0f })
        for (const float a : { 0.0f, 0.5f, 1.0f })
            for (const float b : { 0.0f, 0.5f, 1.0f })
            {
                auto p = dsp::EngineParams {};
                p.decaySeconds = decay;
                p.paramA = a;
                p.paramB = b;
                const auto ir = analysis::impulseResponse (*e, p, fs, bloomSeconds (a) + 1.0 + 0.8 * decay);
                const auto bloom = measureBloom (ir);
                std::cout << "decay " << decay << " A " << a << " (" << bloomSeconds (a) << " s) B " << b
                          << ": peak at " << bloom.peakSeconds << " s, rise " << bloom.riseDb
                          << " dB, RT after bloom " << rt60After (ir, bloomSeconds (a) + 0.6) << "\n";
            }
}
