#pragma once

#include "dsp/chain/InkcapChain.h"
#include "support/ModuleHarness.h"

#include <chrono>
#include <ctime>
#include <cstddef>
#include <functional>

/**
    The chain's quality gates (PLAN.md, Measurement and quality gates), shared by the chain
    suite (tests/chain/) and tools/measure. JUCE-free.
*/
namespace inkcap::gates
{

using analysis::Stereo;
using testing::Transport;

/** Every module on at the given Amount, the given types (or each module's first) and macros. */
inline dsp::ChainSettings everyModule (float damage, float chaos, float amount = 1.0f, int type = 0)
{
    dsp::ChainSettings s;
    s.damage = damage;
    s.chaos = chaos;
    s.glitch = { true, amount, (GlitchType) std::clamp (type, 0, 4), defaultGlitchRate, 0.5f, 0.5f };
    s.warble = { true, amount, (WarbleType) std::clamp (type, 0, 2), 0.5f, false, 0.5f };
    s.crush = { true, amount, (CrushType) std::clamp (type, 0, 3), 0.5f, 0.0f };
    s.bits = { true, amount, (BitsType) std::clamp (type, 0, 2), 0.5f, 0.5f, 0.5f };
    s.hiss = { true, amount, (HissType) std::clamp (type, 0, 2), 0.0f, 0.0f };
    s.room = { true, amount, (RoomType) std::clamp (type, 0, 3), 0.5f, 0.0f };
    return s;
}

/**
    Every module at full Damage at its costliest setting (PLAN.md, Performance budget): Glitch Any,
    Warble Drift with Spread, Crush Tape, Bits Sampler 12 with Jitter, Hiss Hum, Room Hall at its
    largest. `only`: that module alone (numModules: all six).
*/
inline dsp::ChainSettings heaviest (int only = numModules)
{
    auto s = everyModule (1.0f, 0.5f, 1.0f, 0);
    const auto on = [only] (Module m) { return only == numModules || only == (int) m; };
    s.glitch = { on (Module::glitch), 1.0f, GlitchType::any, defaultGlitchRate, 0.5f, 0.5f };
    s.warble = { on (Module::warble), 1.0f, WarbleType::drift, 0.5f, false, 1.0f };
    s.crush = { on (Module::crush), 1.0f, CrushType::tape, 0.5f, 0.0f };
    s.bits = { on (Module::bits), 1.0f, BitsType::sampler12, 0.5f, 0.5f, 1.0f };
    s.hiss = { on (Module::hiss), 1.0f, HissType::hum, 0.0f, 0.0f };
    s.room = { on (Module::room), 1.0f, RoomType::hall, 1.0f, 0.0f };
    return s;
}

/** Renders through a fresh chain in blocks; `each` may change the settings before a block (it gets the block's first sample). */
inline Stereo render (const dsp::ChainSettings& settings, const Stereo& in, int block, double sampleRate, const Transport& transport = {},
                      const std::function<void (int64_t, dsp::ChainSettings&)>& each = {}, dsp::InkcapChain* use = nullptr)
{
    dsp::InkcapChain local;
    auto& chain = use != nullptr ? *use : local;
    if (use == nullptr)
        chain.prepare (sampleRate, block, 2);

    auto out = in;
    auto s = settings;
    const auto n = (int64_t) in.size();
    for (int64_t start = 0; start < n; start += block)
    {
        const auto len = (int) std::min<int64_t> (block, n - start);
        if (each)
            each (start, s);
        float* channels[] = { out.left.data() + start, out.right.data() + start };
        chain.process (channels, 2, len, s, transport.at (start, sampleRate));
    }
    return out;
}

/** The input delayed by the chain's latency (the aligned dry). */
inline std::vector<float> delayed (const std::vector<float>& x, int latency)
{
    std::vector<float> d (x.size(), 0.0f);
    for (size_t i = (size_t) latency; i < x.size(); ++i)
        d[i] = x[i - (size_t) latency];
    return d;
}

/** Damage 0 against the aligned dry (dB; -300 is identical). The gate is -100 dB. */
inline double damageZeroNullDb (const dsp::ChainSettings& settings, double sampleRate, int block = 512)
{
    auto s = settings;
    s.damage = 0.0f;
    const auto in = analysis::programme (sampleRate, 2.0, 21);
    const auto out = render (s, in, block, sampleRate);
    const auto latency = dsp::InkcapChain::fixedLatencySamples();
    return std::max (analysis::nullDb (out.left, delayed (in.left, latency)), analysis::nullDb (out.right, delayed (in.right, latency)));
}

/** True when every block size gives the reference render (4096) to the bit. */
inline bool sameForEveryBlockSize (const dsp::ChainSettings& s, double sampleRate, double seconds = 2.0)
{
    const auto in = analysis::programme (sampleRate, seconds, 22);
    const auto reference = render (s, in, 4096, sampleRate);
    for (const auto block : { 1, 7, 64, 333, 1024 })
    {
        const auto r = render (s, in, block, sampleRate);
        if (! analysis::identical (r.left, reference.left) || ! analysis::identical (r.right, reference.right))
            return false;
    }
    return true;
}

/**
    A render started at bar 17 against the same stretch of a render from bar 1, at 120 BPM: the
    difference (dB) after `settle` seconds. The plan's gate: -80 dB after 500 ms.
*/
inline double barSeventeenDb (const dsp::ChainSettings& s, double sampleRate, double settle = 0.5, double after = 6.0)
{
    const auto bar17 = (size_t) std::lround (32.0 * sampleRate);
    const auto in = analysis::programme (sampleRate, 32.0 + after, 23);
    Stereo late;
    late.left.assign (in.left.begin() + (std::ptrdiff_t) bar17, in.left.end());
    late.right.assign (in.right.begin() + (std::ptrdiff_t) bar17, in.right.end());

    const auto whole = render (s, in, 512, sampleRate, { true, 0.0, 120.0 });
    const auto part = render (s, late, 480, sampleRate, { true, 64.0, 120.0 });
    const std::vector<float> wholeLeft (whole.left.begin() + (std::ptrdiff_t) bar17, whole.left.end());
    const std::vector<float> wholeRight (whole.right.begin() + (std::ptrdiff_t) bar17, whole.right.end());
    const auto from = (size_t) (settle * sampleRate);
    return std::max (analysis::nullDb (part.left, wholeLeft, from), analysis::nullDb (part.right, wholeRight, from));
}

/**
    Mix at 50 % against the in-phase sum of its wet and its dry (Mix 100 and Mix 0), on -40 dBFS
    sines from fromHz to 0.4 fs, `steps` + 1 of them (dB; 0: no comb, negative: the two cancel).
    The worst over the band. The gate: -1 dB.
*/
inline double mixCombDb (dsp::ChainSettings s, double sampleRate, double fromHz = 100.0, int steps = 40)
{
    s.match = false;
    constexpr double amplitude = 0.01;
    const auto from = (size_t) (0.25 * sampleRate), length = (size_t) (0.15 * sampleRate);
    double worst = 0.0;
    for (double hz = fromHz; hz <= 0.4 * sampleRate * 1.0001; hz *= std::pow (0.4 * sampleRate / fromHz, 1.0 / (double) steps))
    {
        const auto tone = analysis::sine (hz, sampleRate, 0.4, amplitude);
        const Stereo in { tone, tone };
        s.mix = 1.0f;
        const auto wet = analysis::toneAmplitude (render (s, in, 512, sampleRate).left, hz, sampleRate, from, length) / amplitude;
        s.mix = 0.0f;
        const auto dry = analysis::toneAmplitude (render (s, in, 512, sampleRate).left, hz, sampleRate, from, length) / amplitude;
        s.mix = 0.5f;
        const auto half = analysis::toneAmplitude (render (s, in, 512, sampleRate).left, hz, sampleRate, from, length) / amplitude;
        worst = std::min (worst, analysis::toDb (half / (0.5 * (wet + dry))));
    }
    return worst;
}

/** Seconds of this thread's CPU time where the platform counts it (a loaded machine waits, it does not work), else wall time. */
inline double threadSeconds()
{
#if defined(__unix__) || defined(__APPLE__)
    timespec t {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t);
    return (double) t.tv_sec + 1.0e-9 * (double) t.tv_nsec;
#else
    return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

/** CPU load in percent of real time for a stereo render (median of `runs`). */
inline double cpuPercent (const dsp::ChainSettings& s, double sampleRate, double seconds = 10.0, int block = 512, int runs = 3)
{
    const auto in = analysis::programme (sampleRate, seconds, 24);
    std::vector<double> loads;
    for (int r = 0; r < runs; ++r)
    {
        dsp::InkcapChain chain;
        chain.prepare (sampleRate, block, 2);
        const auto start = threadSeconds();
        render (s, in, block, sampleRate, {}, {}, &chain);
        loads.push_back (100.0 * (threadSeconds() - start) / seconds);
    }
    std::sort (loads.begin(), loads.end());
    return loads[loads.size() / 2];
}

} // namespace inkcap::gates
