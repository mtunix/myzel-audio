#include "analysis/ReverbAnalysis.h"
#include "dsp/chain/ReverbChain.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace cerulean;

namespace
{
    constexpr double fs = 48000.0;

    struct Render
    {
        std::vector<float> left, right;
    };

    /** Runs the chain over `seconds`, with the input and settings chosen per block. */
    Render run (dsp::ReverbChain& chain, double seconds, int blockSize,
                const std::function<float (size_t, int)>& input,
                const std::function<void (size_t, dsp::ChainSettings&)>& settingsAt,
                std::vector<dsp::ChainMeters>* metersOut = nullptr)
    {
        const auto total = (size_t) (seconds * fs);
        Render r;
        r.left.resize (total);
        r.right.resize (total);
        for (size_t i = 0; i < total; ++i)
        {
            r.left[i] = input (i, 0);
            r.right[i] = input (i, 1);
        }

        dsp::ChainSettings s;
        for (size_t pos = 0; pos < total; pos += (size_t) blockSize)
        {
            const auto n = (int) std::min<size_t> ((size_t) blockSize, total - pos);
            settingsAt (pos, s);
            dsp::ChainMeters m;
            chain.process (r.left.data() + pos, r.right.data() + pos, n, s, m);
            if (metersOut != nullptr)
                metersOut->push_back (m);
        }
        return r;
    }

    std::unique_ptr<dsp::ReverbChain> makeChain (double sampleRate = fs)
    {
        auto c = std::make_unique<dsp::ReverbChain>();
        c->prepare (sampleRate, 512);
        return c;
    }

    const auto pink = analysis::pinkNoise ((size_t) (fs * 12.0), 5);
    const auto pinkR = analysis::pinkNoise ((size_t) (fs * 12.0), 6);

    float noiseFor (size_t i, int ch, double untilSeconds)
    {
        if ((double) i >= untilSeconds * fs)
            return 0.0f;
        return ch == 0 ? pink[i] : pinkR[i];
    }

    double windowDb (const Render& r, double t0, double t1)
    {
        const auto a = (size_t) (t0 * fs), b = (size_t) (t1 * fs);
        return analysis::toDb (std::sqrt (0.5 * (std::pow (analysis::rms (r.left, a, b), 2.0)
                                               + std::pow (analysis::rms (r.right, a, b), 2.0))));
    }
}

TEST_CASE ("Chain: Mix 0 passes the dry signal untouched", "[chain]")
{
    auto chain = makeChain();
    const auto out = run (*chain, 1.0, 256, [] (size_t i, int ch) { return noiseFor (i, ch, 1.0); },
                          [] (size_t, dsp::ChainSettings& s) { s.mix = 0.0f; });

    for (size_t i = 0; i < out.left.size(); ++i)
    {
        REQUIRE (std::abs (out.left[i] - pink[i]) <= 0.0f);
        REQUIRE (std::abs (out.right[i] - pinkR[i]) <= 0.0f);
    }
}

TEST_CASE ("Chain: pedal Mix law keeps the dry at full level up to 50 %", "[chain]")
{
    auto input = [] (size_t i, int ch) { return noiseFor (i, ch, 2.0); };

    // Wet alone (wet only = dry 0, wet 1) from an identical, fresh chain.
    auto wetChain = makeChain();
    const auto wet = run (*wetChain, 2.0, 256, input, [] (size_t, dsp::ChainSettings& s) { s.wetOnly = true; });

    for (const auto& [mix, dry, wetGain] : { std::tuple { 0.25f, 1.0f, 0.5f },
                                            std::tuple { 0.5f, 1.0f, 1.0f },
                                            std::tuple { 0.75f, 0.5f, 1.0f },
                                            std::tuple { 1.0f, 0.0f, 1.0f } })
    {
        auto chain = makeChain();
        const auto out = run (*chain, 2.0, 256, input, [mix = mix] (size_t, dsp::ChainSettings& s) { s.mix = mix; });

        double maxError = 0.0;
        for (size_t i = 0; i < out.left.size(); ++i)
            maxError = std::max (maxError, (double) std::abs (out.left[i] - (dry * pink[i] + wetGain * wet.left[i])));

        INFO ("mix " << mix << ": max deviation from dry " << dry << " + wet " << wetGain << " = " << maxError);
        CHECK (maxError < 1.0e-5);
    }
}

TEST_CASE ("Chain: trails let the old algorithm ring out; without trails it fades in 30 ms", "[chain]")
{
    // White noise: pink noise's sub-5 Hz content would ring out of the input DC filter into the
    // new engine and mask what this test looks at.
    static dsp::Rng rng (77);
    static std::vector<float> white = []
    {
        std::vector<float> w ((size_t) fs * 2);
        for (auto& x : w)
            x = 0.3f * rng.bipolar();
        return w;
    }();

    auto render = [] (bool trails)
    {
        auto chain = makeChain();
        return run (*chain, 2.5, 256, [] (size_t i, int) { return i < (size_t) fs ? white[i] : 0.0f; },
                    [trails] (size_t pos, dsp::ChainSettings& s)
                    {
                        s.wetOnly = true;
                        s.trails = trails;
                        s.preDelayMs = 0.0f;   // else the pre-delay buffer (rightly) feeds the new engine
                        s.engine.decaySeconds = 4.0f;
                        s.algorithm = pos < (size_t) fs ? Algorithm::hall : Algorithm::plate;
                    });
    };

    // Without trails, all that is left is the new engine hearing the input filters ring out.
    const auto withTrails = windowDb (render (true), 1.2, 1.6);
    const auto without = windowDb (render (false), 1.2, 1.6);
    INFO ("200-600 ms after the switch: trails " << withTrails << " dB, no trails " << without << " dB");
    CHECK (withTrails > -40.0);
    CHECK (without < withTrails - 40.0);
}

TEST_CASE ("Chain: loading a preset starts a fresh instance of the same algorithm", "[chain]")
{
    auto chain = makeChain();
    uint32_t generation = 0;

    const auto out = run (*chain, 2.5, 256, [] (size_t i, int ch) { return noiseFor (i, ch, 2.5); },
                          [&] (size_t pos, dsp::ChainSettings& s)
                          {
                              s.wetOnly = true;
                              s.engine.decaySeconds = pos < (size_t) fs ? 6.0f : 0.3f;
                              if (pos >= (size_t) fs && generation == 0)
                                  generation = 1;
                              s.presetGeneration = generation;
                          });

    // The old 6 s tail keeps ringing next to the new 0.3 s one: much more energy than the new
    // preset alone would have half a second later.
    auto alone = makeChain();
    const auto reference = run (*alone, 2.5, 256, [] (size_t i, int ch) { return noiseFor (i, ch, 2.5); },
                                [] (size_t, dsp::ChainSettings& s) { s.wetOnly = true; s.engine.decaySeconds = 0.3f; });

    INFO ("1.5-2.5 s: with old tail " << windowDb (out, 1.5, 2.5) << " dB, new preset alone "
          << windowDb (reference, 1.5, 2.5) << " dB");
    CHECK (windowDb (out, 1.5, 2.5) > windowDb (reference, 1.5, 2.5) + 3.0);
}

TEST_CASE ("Chain: never more than two old tails ring at once", "[chain]")
{
    auto chain = makeChain();
    std::vector<dsp::ChainMeters> meters;
    run (*chain, 3.0, 256, [] (size_t i, int ch) { return noiseFor (i, ch, 3.0); },
         [] (size_t pos, dsp::ChainSettings& s)
         {
             s.engine.decaySeconds = 20.0f;
             s.algorithm = static_cast<Algorithm> ((pos / 4800) % numAlgorithms);   // a new one every 100 ms
         },
         &meters);

    int most = 0;
    for (size_t i = 10; i < meters.size(); ++i)
        most = std::max (most, meters[i].busyEngines);

    INFO ("most engines busy at once: " << most);
    CHECK (most <= 1 + dsp::ReverbChain::maxRinging + 2);   // active + ringing + (briefly) fading
}

TEST_CASE ("Chain: bypass passes the dry signal and keeps (or cuts) the tail", "[chain]")
{
    for (const auto trails : { true, false })
    {
        auto chain = makeChain();
        const auto out = run (*chain, 2.0, 256, [] (size_t i, int ch) { return noiseFor (i, ch, 1.0); },
                              [trails] (size_t pos, dsp::ChainSettings& s)
                              {
                                  s.mix = 0.8f;   // dry at 0.4 while active
                                  s.trails = trails;
                                  s.engine.decaySeconds = 4.0f;
                                  s.bypass = pos >= (size_t) (0.9 * fs);
                              });

        // Bypassed, before the input stops: the dry is back at unity (after the 30 ms ramp).
        for (size_t i = (size_t) (0.95 * fs); i < (size_t) (0.96 * fs) && ! trails; ++i)
            REQUIRE (std::abs (out.left[i] - pink[i]) < 1.0e-6f);

        const auto tail = windowDb (out, 1.1, 1.5);
        INFO ("trails " << trails << ": after bypass and input stop " << tail << " dB");
        if (trails)
            CHECK (tail > -40.0);
        else
            CHECK (tail < -120.0);
    }
}

TEST_CASE ("Chain: freeze holds the tail and ignores new input", "[chain]")
{
    auto chain = makeChain();
    const auto out = run (*chain, 8.0, 256, [] (size_t i, int ch)
                          {
                              // Quiet before the freeze, 12 dB louder after it.
                              const auto g = (double) i < 1.5 * fs ? 1.0f : 4.0f;
                              return g * noiseFor (i, ch, 8.0);
                          },
                          [] (size_t pos, dsp::ChainSettings& s)
                          {
                              s.wetOnly = true;
                              s.engine.decaySeconds = 2.0f;
                              s.freeze = pos >= (size_t) (1.5 * fs);
                          });

    const auto a = windowDb (out, 2.5, 3.5), b = windowDb (out, 6.5, 7.5);
    INFO ("frozen level 2.5-3.5 s: " << a << " dB, 6.5-7.5 s: " << b << " dB");
    CHECK (std::abs (a - b) < 1.0);
}

TEST_CASE ("Chain: freeze layering adds the playing, stays bounded and then holds", "[chain]")
{
    // Quiet playing, Freeze at 1 s, then 12 dB louder playing into the frozen pad until 6 s.
    auto input = [] (size_t i, int ch)
    {
        const auto g = (double) i < 1.0 * fs ? 0.25f : 1.0f;
        return g * noiseFor (i, ch, 6.0);
    };

    for (int a = 0; a < numAlgorithms; ++a)
    {
        const auto algorithm = static_cast<Algorithm> (a);
        auto render = [&] (bool layer)
        {
            auto chain = makeChain();
            return run (*chain, layer ? 38.0 : 8.0, 256, input, [&] (size_t pos, dsp::ChainSettings& s)
            {
                s.algorithm = algorithm;
                s.wetOnly = true;
                s.engine.decaySeconds = 2.0f;
                s.freeze = pos >= (size_t) (1.0 * fs);
                s.freezeLayer = layer;
            });
        };

        const auto plain = render (false), layered = render (true);
        const auto playing = windowDb (Render { std::vector<float> (pink.begin(), pink.end()),
                                                std::vector<float> (pinkR.begin(), pinkR.end()) }, 2.0, 6.0);
        const auto added = windowDb (layered, 6.5, 7.5) - windowDb (plain, 6.5, 7.5);
        const auto loudest = std::max ({ windowDb (layered, 2.0, 3.0), windowDb (layered, 3.0, 4.0),
                                         windowDb (layered, 4.0, 5.0), windowDb (layered, 5.0, 6.0) });
        const auto held = windowDb (layered, 36.5, 37.5) - windowDb (layered, 6.5, 7.5);

        INFO ("algorithm " << a << ": layers add " << added << " dB, loudest second "
              << loudest - playing << " dB vs the playing, after playing stops "
              << held << " dB over 30 s");
        // The louder playing went into the pad. Billow's bloom stage stands still while frozen
        // (so a held pad can't swell), which leaves only its direct path for new layers.
        CHECK (added > (algorithm == Algorithm::billow ? 5.0 : 6.0));
        CHECK (loudest - playing < 1.0);        // ... but it can't pile up
        CHECK (std::abs (held) < 1.0);          // and once you stop, it holds like a freeze
    }
}

TEST_CASE ("Chain: Duck lowers the wet while playing and lets it bloom back", "[chain]")
{
    auto render = [] (bool duck)
    {
        auto chain = makeChain();
        return run (*chain, 3.0, 256, [] (size_t i, int ch) { return noiseFor (i, ch, 1.5); },
                    [duck] (size_t, dsp::ChainSettings& s)
                    {
                        s.wetOnly = true;
                        s.duck = duck;
                        s.engine.decaySeconds = 3.0f;
                    });
    };

    const auto plain = render (false), ducked = render (true);
    const auto whilePlaying = windowDb (ducked, 1.0, 1.4) - windowDb (plain, 1.0, 1.4);
    const auto afterwards = windowDb (ducked, 2.4, 2.9) - windowDb (plain, 2.4, 2.9);
    INFO ("duck: " << whilePlaying << " dB while playing, " << afterwards << " dB 1 s after");
    CHECK (whilePlaying < -6.0);
    CHECK (afterwards > -1.0);
}

TEST_CASE ("Chain: any block size works and renders are deterministic", "[chain]")
{
    auto input = [] (size_t i, int ch) { return noiseFor (i, ch, 1.0); };
    auto settings = [] (size_t, dsp::ChainSettings& s) { s.mix = 0.5f; s.algorithm = Algorithm::hall; };

    auto a = makeChain();
    const auto reference = run (*a, 2.0, 512, input, settings);
    auto b = makeChain();
    const auto again = run (*b, 2.0, 512, input, settings);
    REQUIRE (std::memcmp (reference.left.data(), again.left.data(), reference.left.size() * sizeof (float)) == 0);

    for (const auto blockSize : { 1, 7, 64, 4096 })
    {
        auto c = makeChain();
        const auto out = run (*c, 2.0, blockSize, input, settings);
        REQUIRE (analysis::allFinite ({ out.left, out.right, fs }));

        double errorSquares = 0.0;
        for (size_t i = 0; i < out.left.size(); ++i)
            errorSquares += std::pow ((double) out.left[i] - reference.left[i], 2.0);
        const auto errorDb = analysis::toDb (std::sqrt (errorSquares / (double) out.left.size()))
                           - analysis::toDb (analysis::rms (reference.left));
        INFO ("block size " << blockSize << ": difference to 512 " << errorDb << " dB");
        CHECK (errorDb < -40.0);
    }
}

TEST_CASE ("Chain: sample rates 44.1 to 192 kHz", "[chain]")
{
    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        dsp::ReverbChain chain;
        chain.prepare (rate, 1024);
        std::vector<float> l ((size_t) rate), r ((size_t) rate);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = (i / 100) % 2 ? 0.9f : -0.9f;

        dsp::ChainSettings s;
        dsp::ChainMeters m;
        for (size_t pos = 0; pos < l.size(); pos += 1024)
            chain.process (l.data() + pos, r.data() + pos, (int) std::min<size_t> (1024, l.size() - pos), s, m);

        INFO (rate << " Hz");
        REQUIRE (analysis::allFinite ({ l, r, rate }));
    }
}

TEST_CASE ("Chain: Size changes the space, not the level", "[chain]")
{
    for (const auto algorithm : { Algorithm::hall, Algorithm::plate, Algorithm::nimbus, Algorithm::billow, Algorithm::room })
    {
        double lowest = 1.0e9, highest = -1.0e9;
        for (const auto scale : { 0.3f, 0.55f, 1.0f, 1.41f, 2.0f })
        {
            auto chain = makeChain();
            const auto out = run (*chain, 6.0, 512, [] (size_t i, int ch) { return noiseFor (i, ch, 6.0); },
                                  [algorithm, scale] (size_t, dsp::ChainSettings& s)
                                  {
                                      s.wetOnly = true;
                                      s.algorithm = algorithm;
                                      s.engine.decaySeconds = 2.0f;
                                      s.engine.sizeScale = scale;
                                  });
            const auto level = windowDb (out, 3.0, 6.0);
            lowest = std::min (lowest, level);
            highest = std::max (highest, level);
        }
        INFO ("algorithm " << (int) algorithm << ": wet level spans " << highest - lowest << " dB over Size 0.3x-2x");
        CHECK (highest - lowest < 2.5);
    }
}
