#include "dsp/chain/EqChain.h"
#include "dsp/models/console/ConsoleCard.h"
#include "dsp/models/console/ConsoleModel.h"
#include "support/ChainGates.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <random>
#include <vector>

using namespace viridian;

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    std::vector<float> noise (size_t samples, unsigned seed, float level = 0.25f)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-level, level);
        std::vector<float> v (samples);
        for (auto& x : v)
            x = dist (rng);
        return v;
    }

    std::vector<float> sine (size_t samples, double hz, double fs, float level)
    {
        std::vector<float> v (samples);
        for (size_t i = 0; i < samples; ++i)
            v[i] = level * (float) std::sin (twoPi * hz * (double) i / fs);
        return v;
    }

    /** Console with every band and both filters at work. */
    dsp::ChainSettings consoleSettings (bool alt = false)
    {
        dsp::ChainSettings s;
        s.model = Model::console;
        s.alt = alt;
        s.positions = flatPositions (Model::console);
        s.positions[(size_t) Control::hpfFreq] = 0.3f;
        s.positions[(size_t) Control::lpfFreq] = 0.7f;
        s.positions[(size_t) Control::lowGain] = 0.7f;
        s.positions[(size_t) Control::lmGain] = 0.25f;
        s.positions[(size_t) Control::lmQ] = 0.8f;
        s.positions[(size_t) Control::hmGain] = 0.85f;
        s.positions[(size_t) Control::highGain] = 0.3f;
        s.positions[(size_t) Control::highShape] = 1.0f;
        return s;
    }

    /** Renders stereo through a fresh chain in blocks of the given sizes (cycled). */
    std::pair<std::vector<float>, std::vector<float>> render (const dsp::ChainSettings& s, std::vector<float> l, std::vector<float> r,
                                                             const std::vector<int>& sizes, double fs = 48000.0, int channels = 2)
    {
        dsp::EqChain chain;
        chain.prepare (fs, *std::max_element (sizes.begin(), sizes.end()), channels, s);
        size_t pos = 0, k = 0;
        while (pos < l.size())
        {
            const auto n = std::min ((size_t) sizes[k++ % sizes.size()], l.size() - pos);
            float* ptrs[] { l.data() + pos, r.data() + pos };
            chain.process (ptrs, channels, (int) n, s);
            pos += n;
        }
        return { l, r };
    }

    /** One model from each position on: the knob as a host automates it. */
    using ModelAt = std::vector<std::pair<size_t, Model>>;

    /** Settings from each position on. */
    using SettingsAt = std::vector<std::pair<size_t, dsp::ChainSettings>>;

    /**
        Renders stereo through a fresh chain with the settings changing at the given positions; a
        block that would cross a change ends there, as a host splits its blocks at automation points.
    */
    std::pair<std::vector<float>, std::vector<float>> renderChanges (const SettingsAt& changes, std::vector<float> l, std::vector<float> r,
                                                                     const std::vector<int>& sizes)
    {
        dsp::EqChain chain;
        chain.prepare (48000.0, *std::max_element (sizes.begin(), sizes.end()), 2, changes.front().second);
        auto s = changes.front().second;
        size_t pos = 0, k = 0;
        while (pos < l.size())
        {
            auto end = std::min (pos + (size_t) sizes[k++ % sizes.size()], l.size());
            for (const auto& [at, settings] : changes)
            {
                if (at <= pos)
                    s = settings;
                else
                    end = std::min (end, at);
            }
            float* ptrs[] { l.data() + pos, r.data() + pos };
            chain.process (ptrs, 2, (int) (end - pos), s);
            pos = end;
        }
        return { l, r };
    }

    /** renderChanges() with only the Model knob changing. */
    std::pair<std::vector<float>, std::vector<float>> renderModels (dsp::ChainSettings s, const ModelAt& models, std::vector<float> l,
                                                                    std::vector<float> r, const std::vector<int>& sizes)
    {
        SettingsAt changes;
        for (const auto& [at, model] : models)
        {
            s.model = model;
            changes.push_back ({ at, s });
        }
        return renderChanges (changes, std::move (l), std::move (r), sizes);
    }

    double rmsDb (const std::vector<float>& x, size_t from, size_t to)
    {
        double sum = 0.0;
        for (size_t i = from; i < to; ++i)
            sum += (double) x[i] * x[i];
        return 10.0 * std::log10 (std::max (sum / (double) (to - from), 1.0e-300));
    }

    /** Peak of the 3rd difference over RMS, in dB (the siblings' click detector). */
    double clickCrestDb (const std::vector<float>& x, size_t start)
    {
        double peak = 0.0, sum = 0.0;
        for (size_t i = std::max<size_t> (start, 3); i < x.size(); ++i)
            peak = std::max (peak, (double) std::abs (x[i] - 3.0f * x[i - 1] + 3.0f * x[i - 2] - x[i - 3]));
        for (size_t i = start; i < x.size(); ++i)
            sum += (double) x[i] * x[i];
        return 20.0 * std::log10 (peak / std::sqrt (sum / (double) (x.size() - start)));
    }

    bool same (const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
    }
} // namespace

TEST_CASE ("Chain: latency is the oversamplers' delay, constant across models and rates", "[chain]")
{
    for (const auto fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        dsp::ChainSettings s;
        s.model = Model::mastering;
        dsp::EqChain chain;
        chain.prepare (fs, 512, 2, s);
        // The Console's two 2x pairs (input amp, output amp) of 3.32 samples each, three band-limits
        // (0.43 each: in front of the first pair and behind each), and each amp's linear-phase path
        // to its 8x clip, 11 samples: 29.92.
        const dsp::ChainOversampler pair;
        const auto path = 22.0 + 2.0 * pair.latencySamples() + 3.0 * dsp::ChainBandLimit::delaySamples;
        CHECK (std::abs (path - 29.92) < 0.01);
        CHECK (chain.getLatencySamples() == 30);

        // A not-yet-built model runs through the same two pairs and three band-limits and the pad to
        // the same delay: a 2 kHz tone (well above the DC block's phase) comes out 29.9 samples
        // late, its level the pairs' and band-limits' own.
        auto tone = viridian::gates::sine (2000.0, fs, 0.5, 0.1);
        const auto in = tone.left;
        const auto out = viridian::gates::render (chain, s, std::move (tone));
        const auto from = (size_t) (0.25 * fs), n = (size_t) (0.2 * fs);
        const auto x = viridian::gates::dft (in, 2000.0, fs, from, n), y = viridian::gates::dft (out.left, 2000.0, fs, from, n);
        auto lag = -std::arg (y / x) / (2.0 * std::numbers::pi * 2000.0 / fs);
        while (lag < 20.0)
            lag += fs / 2000.0;
        const auto limit = dsp::ChainBandLimit::response (2000.0 / fs);
        const auto level = std::norm (pair.response (2000.0 / fs)) * std::norm (limit) * std::abs (limit);
        const auto bandLimitDelay = -std::arg (limit) / (2.0 * std::numbers::pi * 2000.0 / fs);
        const auto expected = 22.0 + 2.0 * pair.latencySamples() + 3.0 * bandLimitDelay;
        INFO (fs << " Hz: " << lag << " samples (expected " << expected << "), level " << std::abs (y / x) << " (expected " << level << ")");
        CHECK (std::abs (lag - expected) < 0.05);
        CHECK (std::abs (expected - path) < 0.05);
        CHECK (std::abs (std::abs (y / x) - level) < 1.0e-4);
    }
}

TEST_CASE ("Chain: the band-limit behind each 2x pair", "[chain]")
{
    // Flat to 0.4535 fs within 0.001 dB (20 kHz at 44.1 kHz), below -100 dB from 0.4702 fs, at
    // every rate the same: its response, and its impulse response's DFT, which must agree.
    double worstPass = 0.0, worstStop = -300.0, worstMatch = 0.0;
    for (int i = 0; i <= 20000; ++i)
    {
        const auto f = 0.5 * i / 20000.0;
        const auto db = 20.0 * std::log10 (std::abs (dsp::ChainBandLimit::response (f)));
        if (f <= 0.4535)
            worstPass = std::max (worstPass, std::abs (db));
        if (f >= 0.4702)
            worstStop = std::max (worstStop, db);
    }
    dsp::ChainBandLimit filter;
    std::vector<double> impulse (16384, 0.0);
    impulse[0] = 1.0;
    double* channels[] { impulse.data() };
    filter.process (channels, 1, (int) impulse.size());
    for (const auto f : { 0.01, 0.1, 0.3, 0.45, 0.4535, 0.46, 0.47 })
    {
        std::complex<double> h;
        for (size_t k = 0; k < impulse.size(); ++k)
            h += impulse[k] * std::polar (1.0, -2.0 * std::numbers::pi * f * (double) k);
        worstMatch = std::max (worstMatch, std::abs (h - dsp::ChainBandLimit::response (f)));
    }
    const auto near0 = std::arg (dsp::ChainBandLimit::response (1.0e-6)) / (-2.0 * std::numbers::pi * 1.0e-6);
    INFO ("pass band " << worstPass << " dB, stop band " << worstStop << " dB, impulse vs response " << worstMatch
                       << ", delay at DC " << near0);
    CHECK (worstPass < 0.0010001);
    CHECK (worstStop < -100.0);
    CHECK (worstMatch < 1.0e-12);
    CHECK (std::abs (near0 - dsp::ChainBandLimit::delaySamples) < 1.0e-6);
}

TEST_CASE ("Chain: models not built yet are placeholders that pass the signal flat", "[chain]")
{
    for (int m = 0; m < numModels; ++m)
    {
        dsp::EqChain chain;
        CHECK (chain.model (static_cast<Model> (m)).isPlaceholder() == (m != (int) Model::console));
    }
}

TEST_CASE ("Chain: the output does not depend on the host's block sizes", "[chain]")
{
    auto s = consoleSettings();
    s.driveDb = 12.0f;
    s.noise = 0.5f;
    s.tolerance = 0.5f;
    const auto l = noise (24000, 1u), r = noise (24000, 2u);
    const auto reference = render (s, l, r, { 32 });
    for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 7 }, { 4096 }, { 7, 129, 1, 32, 500 } })
    {
        INFO ("first block " << sizes.front());
        const auto out = render (s, l, r, sizes);
        CHECK (same (out.first, reference.first));
        CHECK (same (out.second, reference.second));
    }
}

TEST_CASE ("Chain: a model change does not depend on the host's block sizes", "[chain]")
{
    // Each step of a change (the incoming model's readiness, its warm-up, the fade) falls on the
    // chain's own sample count, not on the host's blocks.
    auto s = consoleSettings (true);
    s.noise = 0.5f;
    s.tolerance = 0.5f;
    const auto l = noise (33600, 7u), r = noise (33600, 8u);
    // Away and back, a change dropped in its warm-up, a change waiting for a running fade.
    const ModelAt models { { 0, Model::console },       { 4800, Model::passive }, { 14400, Model::console },
                           { 14700, Model::passive },   { 19200, Model::console }, { 20400, Model::mastering } };
    const auto reference = renderModels (s, models, l, r, { 32 });
    for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 7 }, { 64 }, { 333 }, { 4096 }, { 7, 129, 1, 32, 500 } })
    {
        INFO ("first block " << sizes.front());
        const auto out = renderModels (s, models, l, r, sizes);
        CHECK (same (out.first, reference.first));
        CHECK (same (out.second, reference.second));
    }
}

TEST_CASE ("Chain: switch changes do not depend on the host's block sizes", "[chain]")
{
    // A switch change builds its networks a bounded step per 32 samples on the model's own grid.
    auto s = consoleSettings (true);
    s.noise = 0.5f;
    const auto l = noise (28800, 11u), r = noise (28800, 12u);
    auto with = [&] (auto&& edit)
    {
        auto x = s;
        edit (x);
        return x;
    };
    const SettingsAt changes { { 0, s },
                               { 2400, with ([] (auto& x) { x.positions[(size_t) Control::highShape] = 0.0f; }) },
                               { 2500, with ([] (auto& x) { x.positions[(size_t) Control::lpfFreq] = 1.0f; }) },
                               { 9600, with ([] (auto& x) { x.tolerance = 0.5f; }) },
                               { 14400, with ([] (auto& x) { x.alt = false; x.tolerance = 0.5f; }) },
                               { 19200, with ([] (auto& x) { x.alt = false; x.stereo = StereoMode::side; x.eqIn = false; }) },
                               { 21600, with ([] (auto& x) { x.alt = false; x.stereo = StereoMode::side; }) } };
    const auto reference = renderChanges (changes, l, r, { 32 });
    for (const auto& sizes : std::vector<std::vector<int>> { { 1 }, { 7 }, { 64 }, { 333 }, { 4096 }, { 7, 129, 1, 32, 500 } })
    {
        INFO ("first block " << sizes.front());
        const auto out = renderChanges (changes, l, r, sizes);
        CHECK (same (out.first, reference.first));
        CHECK (same (out.second, reference.second));
    }
}

TEST_CASE ("Chain: prepare() designs the active model for the settings it is given (Settled 19)", "[chain]")
{
    // The networks match their H(s) at those settings before any callback.
    for (const bool alt : { false, true })
    {
        const auto s = consoleSettings (alt);
        dsp::EqChain chain;
        chain.prepare (48000.0, 512, 2, s);
        const auto& model = dynamic_cast<const dsp::ConsoleModel&> (chain.model (Model::console));
        REQUIRE (model.ready());
        const auto pots = gates::consolePots (s);
        auto card = alt ? dsp::console::makeLaterCard() : dsp::console::makeEarlyCard();
        card->prepare (48000.0);
        card->reset (pots);
        for (const auto hz : { 30.0, 200.0, 1000.0, 5000.0, 15000.0 })
        {
            INFO ((alt ? "later" : "early") << " card, " << hz << " Hz");
            CHECK (std::abs (model.networkResponse (hz) - card->response (hz)) <= 1.0e-12 * std::abs (card->response (hz)));
        }
    }
}

TEST_CASE ("Chain: reset() clears the states and keeps the designs (Settled 19)", "[chain]")
{
    // Halfway through a glide, reset() leaves the networks where they are: the glide goes on
    // from there instead of jumping to the knobs (which would need a design).
    auto s = consoleSettings (true);
    dsp::EqChain chain;
    chain.prepare (48000.0, 256, 2, s);
    s.positions[(size_t) Control::highFreq] = 0.9f;
    s.positions[(size_t) Control::hmGain] = 0.1f;
    auto l = noise (2048, 13u), r = noise (2048, 14u);
    float* ptrs[] { l.data(), r.data() };
    chain.process (ptrs, 2, 256, s);
    const auto& model = dynamic_cast<const dsp::ConsoleModel&> (chain.model (Model::console));
    const auto before = model.networkResponse (3000.0);
    chain.reset();
    CHECK (model.networkResponse (3000.0) == before);

    // A reset in the middle of a switch change or a model change: the change starts again and
    // completes, and the chain ends where a fresh one starts.
    for (const auto& edit : std::vector<void (*) (dsp::ChainSettings&)> {
             [] (dsp::ChainSettings& x) { x.positions[(size_t) Control::lpfFreq] = 1.0f; },
             [] (dsp::ChainSettings& x) { x.alt = false; },
             [] (dsp::ChainSettings& x) { x.model = Model::mastering; } })
    {
        auto a = consoleSettings (true), b = a;
        edit (b);
        dsp::EqChain changing, fresh;
        changing.prepare (48000.0, 64, 2, a);
        fresh.prepare (48000.0, 64, 2, b);
        const auto input = noise (64 * 2003, 15u);
        std::vector<float> x (64), y (64);
        float* p[] { x.data(), y.data() };
        for (int k = 0; k < 2003; ++k)
        {
            if (k == 3)
                changing.reset();
            std::copy (input.begin() + 64 * k, input.begin() + 64 * (k + 1), x.begin());
            std::copy (x.begin(), x.end(), y.begin());
            changing.process (p, 2, 64, b);
            for (const auto v : x)
                REQUIRE (std::isfinite (v));
        }
        CHECK_FALSE (changing.isChangingModel());
        if (b.model == Model::console)
        {
            const auto& m = dynamic_cast<const dsp::ConsoleModel&> (changing.model (Model::console));
            const auto& f = dynamic_cast<const dsp::ConsoleModel&> (fresh.model (Model::console));
            CHECK_FALSE (m.isSwitching());
            CHECK (std::abs (m.networkResponse (1000.0) - f.networkResponse (1000.0)) <= 1.0e-9);
        }
    }
}

TEST_CASE ("Chain: a model change taken back before its fade is never heard", "[chain]")
{
    const auto l = noise (24000, 9u), r = noise (24000, 10u);
    for (const auto active : { Model::inductor, Model::console })
    {
        // The incoming model runs unheard until its warm-up ends (20 ms after it is ready).
        auto s = consoleSettings();
        s.model = active;
        const auto other = active == Model::console ? Model::passive : Model::console;
        const auto steady = renderModels (s, { { 0, active } }, l, r, { 256 });
        const auto back = renderModels (s, { { 0, active }, { 4800, other }, { 5040, active } }, l, r, { 256 });
        INFO ("active model " << (int) active);
        CHECK (same (back.first, steady.first));
        CHECK (same (back.second, steady.second));
    }

    // A third model in the warm-up takes the change over, as if it had been chosen at once.
    auto s = consoleSettings();
    const auto direct = renderModels (s, { { 0, Model::inductor }, { 5040, Model::console } }, l, r, { 256 });
    const auto retaken = renderModels (s, { { 0, Model::inductor }, { 4800, Model::passive }, { 5040, Model::console } }, l, r, { 256 });
    CHECK (same (retaken.first, direct.first));
    CHECK (same (retaken.second, direct.second));
}

TEST_CASE ("Chain: renders are deterministic, reset and prepare included", "[chain]")
{
    auto s = consoleSettings (true);
    s.noise = 1.0f;
    s.tolerance = 1.0f;
    const auto l = noise (9600, 3u), r = noise (9600, 4u);
    dsp::EqChain chain;
    auto pass = [&]
    {
        auto a = l, b = r;
        float* ptrs[] { a.data(), b.data() };
        chain.process (ptrs, 2, (int) a.size(), s);
        return std::make_pair (a, b);
    };
    chain.prepare (48000.0, 9600, 2, s);
    const auto first = pass();
    chain.reset();
    const auto second = pass();
    chain.prepare (48000.0, 9600, 2, s);
    const auto third = pass();
    CHECK (same (first.first, second.first));
    CHECK (same (first.second, second.second));
    CHECK (same (first.first, third.first));
}

TEST_CASE ("Chain: Tolerance 0 keeps L and R bit-identical; above 0 they differ", "[chain]")
{
    for (const bool alt : { false, true })
    {
        auto s = consoleSettings (alt);
        s.driveDb = 18.0f;
        const auto x = noise (19200, 5u);
        const auto matched = render (s, x, x, { 512 });
        CHECK (same (matched.first, matched.second));

        s.tolerance = 0.5f;
        const auto spread = render (s, x, x, { 512 });
        CHECK_FALSE (same (spread.first, spread.second));
        // A pot spread (+-10 % here), not a different EQ: with four bands boosted or cut by up to
        // 10 dB the channels stay within a few dB of each other.
        CHECK (std::abs (rmsDb (spread.first, 4800, 19200) - rmsDb (spread.second, 4800, 19200)) < 3.0);
    }
}

TEST_CASE ("Chain: Noise 0 gives exact silence for silence; Noise adds the model's floor", "[chain]")
{
    const std::vector<float> silence (48000, 0.0f);
    for (int m = 0; m < numModels; ++m)
    {
        auto s = consoleSettings();
        s.model = static_cast<Model> (m);
        s.driveDb = 24.0f;
        const auto out = render (s, silence, silence, { 256 });
        INFO ("model " << m);
        CHECK (std::all_of (out.first.begin(), out.first.end(), [] (float v) { return juce::exactlyEqual (v, 0.0f); }));
        CHECK (std::all_of (out.second.begin(), out.second.end(), [] (float v) { return juce::exactlyEqual (v, 0.0f); }));
    }

    auto s = consoleSettings();
    s.positions = flatPositions (Model::console);
    s.noise = 1.0f;
    const auto out = render (s, silence, silence, { 256 });
    const auto level = rmsDb (out.first, 4800, 48000);
    INFO ("Console noise floor at Noise 100 %: " << level << " dBFS");
    CHECK (level > -112.0);
    CHECK (level < -104.0);
    CHECK_FALSE (same (out.first, out.second));     // independent per channel
}

TEST_CASE ("Chain: Mid or Side runs only that half through the network", "[chain]")
{
    // -40 dBFS: the amps are linear to far below these checks' tolerance.
    auto s = consoleSettings();
    const auto x = noise (19200, 6u, 0.01f);
    std::vector<float> minus (x.size());
    std::transform (x.begin(), x.end(), minus.begin(), [] (float v) { return -v; });

    auto amps = s;
    amps.eqIn = false;

    // Pure side through Mid mode: the network sees silence, the side passes as with EQ In off.
    s.stereo = StereoMode::mid;
    const auto mid = render (s, x, minus, { 512 });
    const auto ampsOnly = render (amps, x, minus, { 512 });
    CHECK (same (mid.first, ampsOnly.first));
    CHECK (same (mid.second, ampsOnly.second));

    // Pure mid through Side mode, likewise; and Stereo does change it.
    s.stereo = StereoMode::side;
    const auto side = render (s, x, x, { 512 });
    const auto ampsMid = render (amps, x, x, { 512 });
    CHECK (same (side.first, ampsMid.first));
    s.stereo = StereoMode::stereo;
    CHECK_FALSE (same (render (s, x, x, { 512 }).first, ampsMid.first));

    // Mid mode on a stereo signal: L + R carries the EQ, L - R is the amps' alone.
    s.stereo = StereoMode::mid;
    const auto y = noise (19200, 7u, 0.01f);
    const auto both = render (s, x, y, { 512 });
    const auto plain = render (amps, x, y, { 512 });
    double worst = 0.0;
    for (size_t i = 2400; i < x.size(); ++i)
        worst = std::max (worst, (double) std::abs ((both.first[i] - both.second[i]) - (plain.first[i] - plain.second[i])));
    CHECK (worst < 1.0e-7);
}

TEST_CASE ("Chain: Drive changes the colour, not the level (small signal)", "[chain][drive]")
{
    for (const auto fs : { 44100.0, 96000.0 })
        for (const bool alt : { false, true })
        {
            auto s = consoleSettings (alt);
            const auto x = sine ((size_t) (0.5 * fs), 1000.0, fs, 0.001f);   // -60 dBFS
            s.driveDb = 0.0f;
            const auto reference = rmsDb (render (s, x, x, { 512 }, fs).first, (size_t) (0.25 * fs), x.size());
            for (const auto drive : { -12.0f, -6.0f, 6.0f, 12.0f, 18.0f, 24.0f })
            {
                s.driveDb = drive;
                const auto level = rmsDb (render (s, x, x, { 512 }, fs).first, (size_t) (0.25 * fs), x.size());
                INFO (fs << " Hz" << (alt ? ", Alt" : "") << ", Drive " << drive << " dB: " << level - reference << " dB");
                CHECK (std::abs (level - reference) <= 0.1);
            }
        }
}

TEST_CASE ("Chain: a model change crossfades over 20 ms without a click", "[chain]")
{
    const double fs = 48000.0;
    for (const auto to : { Model::mastering, Model::console })
    {
        dsp::EqChain chain;
        auto s = consoleSettings();
        s.model = to == Model::console ? Model::inductor : Model::console;
        chain.prepare (fs, 256, 2, s);
        auto l = sine ((size_t) fs, 330.0, fs, 0.25f), r = l;
        const auto change = (size_t) (0.5 * fs);
        for (size_t pos = 0; pos < l.size(); pos += 256)
        {
            if (pos >= change - change % 256)
                s.model = to;
            float* ptrs[] { l.data() + pos, r.data() + pos };
            chain.process (ptrs, 2, (int) std::min<size_t> (256, l.size() - pos), s);
        }
        const auto crest = clickCrestDb (l, (size_t) (0.1 * fs));
        INFO ("to model " << (int) to << ": " << crest << " dB");
        CHECK (crest <= -60.0);
    }
}

TEST_CASE ("Chain: a model crossfade holds the level of correlated models (Settled 17)", "[chain]")
{
    // Both models carry the same signal, so the fade is amplitude-complementary: a tone's level
    // stays between the two models' levels through the fade (equal power would swell by 3 dB).
    // Every model's path runs through the same 2x pairs, so the two stay in phase to the top of
    // the band (a placeholder padded by a plain delay combed 17 dB at 15 kHz, 44.1 kHz).
    for (const auto fs : { 44100.0, 48000.0 })
        for (const auto hz : { 1000.0, 10000.0, 15000.0, 18000.0 })
            for (const bool alt : { false, true })
                for (const auto& [from, to] : { std::pair { Model::console, Model::passive }, std::pair { Model::passive, Model::console } })
                {
                    dsp::ChainSettings s;
                    s.model = from;
                    s.alt = alt;
                    s.positions = flatPositions (Model::console);
                    dsp::EqChain chain;
                    chain.prepare (fs, 256, 2, s);
                    auto x = viridian::gates::sine (hz, fs, 1.0, 0.1);
                    const auto change = (size_t) (0.5 * fs);
                    for (size_t pos = 0; pos < x.size(); pos += 256)
                    {
                        s.model = pos >= change ? to : from;
                        float* ptrs[] { x.left.data() + pos, x.right.data() + pos };
                        chain.process (ptrs, 2, (int) std::min<size_t> (256, x.size() - pos), s);
                    }
                    // The tone's level in windows of eight periods, a quarter apart, from the change to well after the fade.
                    auto level = [&] (size_t at, size_t n) { return viridian::gates::toneAmplitude (x.left, at, n, hz, fs); };
                    const auto before = level (change - 4800, 4096), after = level (x.size() - 4800, 4096);
                    const auto window = (size_t) (8.0 * fs / hz);
                    double lowest = 1.0e9, highest = 0.0;
                    for (size_t i = change; i + window <= change + (size_t) (0.1 * fs); i += window / 4)
                    {
                        lowest = std::min (lowest, level (i, window));
                        highest = std::max (highest, level (i, window));
                    }
                    const auto over = 20.0 * std::log10 (highest / std::max (before, after));
                    const auto under = 20.0 * std::log10 (std::min (before, after) / lowest);
                    INFO (fs << " Hz, " << hz << " Hz tone, " << (alt ? "later" : "early") << " card, model " << (int) from << " -> " << (int) to
                             << ": " << lowest << " ... " << highest << " against " << before << " before and " << after << " after: "
                             << over << " dB over, " << under << " dB under");
                    CHECK (over <= 0.1);
                    CHECK (under <= 0.1);
                }
}

TEST_CASE ("Chain: switch changes (shape, filter in or out, card, EQ In, Stereo) crossfade without a click", "[chain]")
{
    const double fs = 48000.0;
    using Change = void (*) (dsp::ChainSettings&);
    const Change changes[] {
        [] (dsp::ChainSettings& s) { s.positions[(size_t) Control::highShape] = 0.0f; },
        [] (dsp::ChainSettings& s) { s.positions[(size_t) Control::lowShape] = 1.0f; },
        [] (dsp::ChainSettings& s) { s.positions[(size_t) Control::hpfFreq] = 0.0f; },
        [] (dsp::ChainSettings& s) { s.positions[(size_t) Control::lpfFreq] = 1.0f; },
        [] (dsp::ChainSettings& s) { s.alt = ! s.alt; },
        [] (dsp::ChainSettings& s) { s.eqIn = false; },
        [] (dsp::ChainSettings& s) { s.stereo = StereoMode::side; },
    };
    int index = 0;
    for (const auto change : changes)
    {
        dsp::EqChain chain;
        auto s = consoleSettings();
        chain.prepare (fs, 256, 2, s);
        auto l = sine ((size_t) fs, 330.0, fs, 0.25f), r = sine ((size_t) fs, 330.0, fs, 0.2f);
        for (size_t pos = 0; pos < l.size(); pos += 256)
        {
            if (pos == 256 * 100)
                change (s);
            float* ptrs[] { l.data() + pos, r.data() + pos };
            chain.process (ptrs, 2, (int) std::min<size_t> (256, l.size() - pos), s);
        }
        const auto crest = std::max (clickCrestDb (l, (size_t) (0.1 * fs)), clickCrestDb (r, (size_t) (0.1 * fs)));
        INFO ("change " << index++ << ": " << crest << " dB");
        CHECK (crest <= -60.0);
    }
}

TEST_CASE ("Flat is flat: every model at flat settings, EQ In on, within 0.1 dB of its amps-only response or its circuit's own flat", "[chain][flat]")
{
    const double fs = 48000.0;
    std::vector<double> freqs;
    for (double f = 20.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 12.0))
        freqs.push_back (f);

    for (int m = 0; m < numModels; ++m)
        for (const bool alt : { false, true })
        {
            dsp::ChainSettings flat;
            flat.model = static_cast<Model> (m);
            flat.alt = alt;
            flat.positions = flatPositions (flat.model);
            auto amps = flat;
            amps.eqIn = false;

            dsp::EqChain chain;
            chain.prepare (fs, 512, 2, flat);
            auto measure = [&] (const dsp::ChainSettings& s)
            {
                chain.rebuild (s);
                // 1.4 s: the coupling capacitors' slow tails have decayed far enough at 20 Hz. An
                // impulse at -60 dBFS: the amps are linear far below this test's resolution.
                std::vector<float> l (65536, 0.0f), r (65536, 0.0f);
                l[0] = r[0] = 1.0e-3f;
                for (size_t pos = 0; pos < l.size(); pos += 512)
                {
                    float* ptrs[] { l.data() + pos, r.data() + pos };
                    chain.process (ptrs, 2, 512, s);
                }
                std::vector<std::complex<double>> h;
                for (const auto f : freqs)
                {
                    std::complex<double> acc {}, phasor { 1.0, 0.0 };
                    const auto step = std::polar (1.0, -twoPi * f / fs);
                    for (size_t n = 0; n < l.size(); ++n)
                    {
                        acc += (double) l[n] * phasor;
                        phasor = (n & 1023) == 1023 ? std::polar (1.0, -twoPi * f / fs * (double) (n + 1)) : phasor * step;
                    }
                    h.push_back (acc);
                }
                return h;
            };
            const auto a = measure (flat);
            // The console's own network at these settings: the chain's flat must be the circuit's.
            std::vector<double> circuit (freqs.size(), 0.0);
            if (const auto* console = dynamic_cast<dsp::ConsoleModel*> (&chain.model (flat.model)))
                for (size_t k = 0; k < freqs.size(); ++k)
                    circuit[k] = 20.0 * std::log10 (std::abs (console->networkAnalogResponse (freqs[k])));
            const auto b = measure (amps);

            double departure = 0.0, fromCircuit = 0.0;
            for (size_t k = 0; k < freqs.size(); ++k)
            {
                const auto ratio = 20.0 * std::log10 (std::abs (a[k]) / std::abs (b[k]));
                departure = std::max (departure, std::abs (ratio));
                fromCircuit = std::max (fromCircuit, std::abs (ratio - circuit[k]));
            }
            INFO ("model " << m << (alt ? " Alt" : "") << ": " << departure << " dB from the amps-only response, "
                           << fromCircuit << " dB from the network's own response");
            // Settled 12: a drawn circuit that lifts by itself (Console, NOTES.md "Flat") is gated
            // against its own flat; flat circuits keep the 0.1 dB.
            if (flat.model != Model::console)
                CHECK (departure <= 0.1);
            CHECK (fromCircuit <= 0.01);
        }
}

TEST_CASE ("Chain: CPU per model, 48 kHz stereo, at rest and during a model crossfade", "[chain][.perf]")
{
    const double fs = 48000.0;
    const int block = 512, seconds = 10;
    for (int m = 0; m < numModels; ++m)
        for (const bool alt : { false, true })
        {
            auto s = consoleSettings (alt);
            s.model = static_cast<Model> (m);
            dsp::EqChain chain;
            chain.prepare (fs, block, 2, s);
            auto l = noise ((size_t) block, 9u), r = noise ((size_t) block, 10u);
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < (int) (seconds * fs) / block; ++i)
            {
                auto a = l, b = r;
                float* ptrs[] { a.data(), b.data() };
                chain.process (ptrs, 2, block, s);
            }
            const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            std::printf ("%-12s%s at rest: %.2f %% of one core (budget %.1f %%)\n", info (s.model).name, alt ? " Alt" : "    ",
                         100.0 * elapsed / seconds, info (s.model).cpuBudgetPercent);
            if (s.model != Model::console)
                continue;

            // The four band frequencies swept continuously (every control block re-designs).
            const auto moving = std::chrono::steady_clock::now();
            for (int i = 0; i < (int) (seconds * fs) / block; ++i)
            {
                const auto v = 0.5f + 0.4f * std::sin ((float) i * 0.02f);
                for (const auto c : { Control::lowFreq, Control::lmFreq, Control::hmFreq, Control::highFreq })
                    s.positions[(size_t) c] = v;
                auto a = l, b = r;
                float* ptrs[] { a.data(), b.data() };
                chain.process (ptrs, 2, block, s);
            }
            const auto busy = std::chrono::steady_clock::now() - moving;
            std::printf ("%-12s%s four frequencies moving: %.2f %% of one core\n", info (s.model).name, alt ? " Alt" : "    ",
                         100.0 * std::chrono::duration<double> (busy).count() / seconds);
        }

    // A model crossfade runs both models: the budget is their sum (PLAN.md, Performance budget).
    for (const bool alt : { false, true })
        for (const auto other : { Model::passive, Model::mastering })
        {
            auto s = gates::consoleWorking (alt);
            const auto changing = gates::medianOf (7, [&] { return gates::modelChangeCpuPercent (s, other, 3.0); });
            const auto sum = info (Model::console).cpuBudgetPercent + info (other).cpuBudgetPercent;
            std::printf ("Console%s <-> %-12s while a change runs both: %.2f %% of one core (budget %.1f %%)\n", alt ? " Alt" : "    ",
                         info (other).name, changing, sum);
            CHECK (changing <= sum);
        }
}
