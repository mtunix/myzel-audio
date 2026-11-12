#pragma once

#include "common/dsp/GrainVoice.h"
#include "dsp/common/ReverbBlocks.h"

namespace cerulean::dsp::halo
{

using myzel::dsp::GrainVoice;

/**
    In-loop shifter for one tank line: a history buffer shared by two grain voices (the second
    only runs for the +12/+19 interval), summed, then band-limited (high cut against whistling
    build-up, low cut against sub-octave rumble).
*/
class GrainShifter
{
public:
    void prepare (double sampleRate, float maxRatio, uint32_t seed)
    {
        history.setMaxDelay ((int) std::ceil (GrainVoice::historyFor (sampleRate, maxRatio)));
        voices[0].prepare (sampleRate, seed * 2654435761u + 11u);
        voices[1].prepare (sampleRate, seed * 2246822519u + 29u);
        highCut.setCoefficients (5200.0f, 0.6f, sampleRate);
        lowCut.setCoefficients (80.0f, 0.7071f, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        history.reset();
        voices[0].reset();
        voices[1].reset();
        highCut.reset();
        lowCut.reset();
    }

    /**
        The shifted path's high cut. Dark Tone settings pull it down (a darker shimmer that also
        keeps the high band's decay dark); flat/bright stay at 5.2 kHz, the anti-whistle limit.
    */
    void setHighCut (float hz, double sampleRate) noexcept { highCut.setCoefficients (hz, 0.6f, sampleRate); }

    void setRatios (float r1, float r2) noexcept
    {
        voices[0].setRatio (r1);
        voices[1].setRatio (r2);
    }

    /** gain1/gain2: per-voice gains (>= 0; 0 = that voice is not rendered). */
    float process (float x, float gain1, float gain2, bool render) noexcept
    {
        history.write (x);
        const auto a = voices[0].process (history, render && gain1 > 0.0f);
        const auto b = voices[1].process (history, render && gain2 > 0.0f);
        if (! render)
            return 0.0f;
        return highCut.lowpass (lowCut.highpass (gain1 * a + gain2 * b));
    }

private:
    DelayLine history;
    std::array<GrainVoice, 2> voices;
    Svf highCut;
    Svf lowCut;
};

} // namespace cerulean::dsp::halo
