#pragma once

#include "dsp/modules/Module.h"
#include "support/Analysis.h"

#include <functional>

/**
    Runs one module on its own, as the chain would: a GridClock fed by a simulated host
    transport, the module's settings and the macros per block. For the per-module suites
    (tests/modules/<m>/), which build each module in isolation.
*/
namespace inkcap::testing
{

/** A simulated host: a transport playing from a position at a tempo (or stopped: the clock runs free). */
struct Transport
{
    bool playing = true;
    double startPpq = 0.0;
    double bpm = 120.0;
    int timeSigNumerator = 4, timeSigDenominator = 4;

    dsp::HostPosition at (int64_t sample, double sampleRate) const noexcept
    {
        dsp::HostPosition h;
        h.playing = playing;
        h.hasPpq = playing;
        h.ppq = startPpq + (double) sample * bpm / (60.0 * sampleRate);
        h.hasSeconds = playing;
        h.seconds = startPpq * 60.0 / bpm + (double) sample / sampleRate;
        h.bpm = bpm;
        h.timeSigNumerator = timeSigNumerator;
        h.timeSigDenominator = timeSigDenominator;
        return h;
    }
};

struct Macros
{
    float damage = 1.0f;
    float chaos = 0.0f;
    uint32_t seed = 0x4393'0A3Du;
};

/**
    Renders a stereo (or mono: leave `right` empty) signal through a prepared module in blocks of
    `block` samples. `each` (optional) may change the settings or macros before each block: it
    gets the block's first sample.
*/
template <typename Module, typename Settings>
analysis::Stereo renderModule (Module& module, Settings settings, Macros macros, const analysis::Stereo& in, int block,
                               double sampleRate, const Transport& transport = {},
                               const std::function<void (int64_t, Settings&, Macros&)>& each = {})
{
    auto out = in;
    const auto stereo = ! in.right.empty();
    dsp::GridClock clock;
    clock.prepare (sampleRate);

    const auto n = (int64_t) in.left.size();
    for (int64_t start = 0; start < n; start += block)
    {
        const auto len = (int) std::min<int64_t> (block, n - start);
        if (each)
            each (start, settings, macros);

        clock.advance (transport.at (start, sampleRate), 120.0, len);
        const dsp::BlockContext context { clock, macros.damage, macros.chaos, macros.seed };

        float* channels[] = { out.left.data() + start, stereo ? out.right.data() + start : nullptr };
        const dsp::AudioBlock audio { channels, stereo ? 2 : 1, len, clock.blockStart() };
        module.process (audio, settings, context);
    }
    return out;
}

} // namespace inkcap::testing
