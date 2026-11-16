#pragma once

#include "dsp/detector/Detector.h"

#include <vector>

/**
    The detector, offline, for tools/measure and the tests. It runs the plugin's own Detector (the
    class HaliteChain runs, same code, same settings struct) over a whole buffer, under the same
    flush-to-zero mode as processBlock, and returns everything it decided:

        auto r = halite::dsp::probe::run (channels, numChannels, numSamples, 48000.0, settings);
        r.hops[i]      // input samples [i * r.hopSize, (i + 1) * r.hopSize)
        r.events       // every ess, positions in input samples

    Unlike the plugin's live frames, every field of hops[i] belongs to that one span: the probe
    feeds the look-ahead's worth of silence after the input and moves the reductions (and
    eventActive) back to the hop they apply to. Per hop: B, V, B − V, N with the raw cue values,
    S, H, the Harsh body, H − body, the Harsh band's N, and the candidate, body-hold and rise
    flags (paths[p]); the targets and the Harsh hold (decisions[r]); the shaped Ess and Harsh
    reductions and S at the reduction's time (reductions[r]). numPaths and numReductions say how
    many entries are in use. settings.noiseCue = blend computes both cues, so one run reports
    both raw values (each NaN when its cue is off).
*/
namespace halite::dsp::probe
{

struct Result
{
    double sampleRate = 0.0;
    int hopSize = Detector::hopSize;
    int lookaheadSamples = 0;               // what the reductions were moved back by
    std::vector<DetectorFrame> hops;        // ceil (numSamples / hopSize) of them
    std::vector<EssEvent> events;           // ordered; one still open at the end is closed by the flush
};

/**
    `input`: numChannels (1 or 2) pointers of numSamples each; `key`: the side chain, used when
    settings.key is External (0 … 2 channels). Blocks of `blockSizes`, cycled: the output does not
    depend on them. `tuning`: the detector's constants, the plugin's by default.
*/
Result run (const float* const* input, int numChannels, int numSamples, double sampleRate,
            const DetectorSettings& settings, const float* const* key = nullptr, int numKeyChannels = 0,
            const std::vector<int>& blockSizes = { 512 }, const DetectorTuning& tuning = {});

} // namespace halite::dsp::probe
