#include "dsp/chain/EqChain.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <random>
#include <vector>

using namespace viridian;

namespace
{
    std::vector<float> noise (size_t samples, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        std::vector<float> v (samples);
        for (auto& x : v)
            x = dist (rng);
        return v;
    }
}

TEST_CASE ("Chain: latency is reported through the generic path", "[chain]")
{
    dsp::EqChain chain;
    chain.prepare (48000.0, 512, 2);
    CHECK (chain.getLatencySamples() == 0);
}

TEST_CASE ("Chain: passes mono and stereo through at every block size", "[chain]")
{
    for (const auto channels : { 1, 2 })
        for (const auto block : { 1, 2, 5, 31, 64, 256, 1000, 4096 })
        {
            INFO (channels << " ch, block " << block);
            dsp::EqChain chain;
            chain.prepare (44100.0, block, channels);

            const auto l = noise (9000, 1u), r = noise (9000, 2u);
            auto outL = l, outR = r;
            float* ptrs[2] {};
            dsp::ChainSettings s;

            for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
            {
                const auto n = (int) std::min ((size_t) block, l.size() - pos);
                ptrs[0] = outL.data() + pos;
                ptrs[1] = outR.data() + pos;
                chain.process (ptrs, channels, n, s);
            }

            CHECK (std::memcmp (outL.data(), l.data(), l.size() * sizeof (float)) == 0);
            if (channels == 2)
                CHECK (std::memcmp (outR.data(), r.data(), r.size() * sizeof (float)) == 0);
        }
}

TEST_CASE ("Chain: Output glides over 50 ms after the first block", "[chain]")
{
    constexpr double rate = 48000.0;
    dsp::EqChain chain;
    chain.prepare (rate, 64, 1);

    std::vector<float> x (64, 1.0f);
    float* ptr = x.data();
    dsp::ChainSettings s;
    chain.process (&ptr, 1, 64, s);

    s.outputDb = -12.0f;
    std::vector<float> y ((size_t) (rate * 0.1), 1.0f);
    ptr = y.data();
    chain.process (&ptr, 1, (int) y.size(), s);

    const auto target = juce::Decibels::decibelsToGain (-12.0f);
    const auto glide = (size_t) (rate * dsp::EqChain::outputSeconds);
    CHECK (y.front() < 1.0f);
    CHECK (y[glide / 2] > target);
    CHECK (y[glide / 2] < 1.0f);
    CHECK (juce::exactlyEqual (y.back(), target));

    for (size_t i = 1; i < y.size(); ++i)
        REQUIRE (y[i] <= y[i - 1]);
}

TEST_CASE ("Chain: reset makes the next block take its settings at once", "[chain]")
{
    dsp::EqChain chain;
    chain.prepare (48000.0, 64, 1);

    dsp::ChainSettings s;
    s.bypass = true;
    std::vector<float> x (64, 0.25f);
    float* ptr = x.data();
    chain.process (&ptr, 1, 64, s);
    CHECK (juce::exactlyEqual (x.back(), 0.25f));

    chain.reset();
    s.bypass = false;
    s.outputDb = 6.0f;
    std::fill (x.begin(), x.end(), 0.25f);
    chain.process (&ptr, 1, 64, s);
    CHECK (juce::exactlyEqual (x.front(), 0.25f * juce::Decibels::decibelsToGain (6.0f)));
}
