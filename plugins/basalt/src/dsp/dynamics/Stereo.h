#pragma once

#include "common/dsp/FloatCompare.h"
#include "dsp/Modes.h"
#include "dsp/dynamics/Levels.h"

#include <algorithm>

// The stereo matrix and the link (NOTES.md, Stereo, Settled 32). Detector channels per mode:
// Linked and Unlinked L and R, Mid M, Side S, Mid/Side M and S; a mono bus has one, whatever
// the mode.

namespace basalt::dynamics
{

struct StereoLayout
{
    int detectorChannels = 2;
    bool linked = true;     // Link applies: Linked mode with two channels
};

inline StereoLayout layoutFor (StereoMode mode, int numAudioChannels) noexcept
{
    if (numAudioChannels < 2)
        return { 1, false };

    switch (mode)
    {
        case StereoMode::linked:   return { 2, true };
        case StereoMode::unlinked: return { 2, false };
        case StereoMode::mid:      return { 1, false };
        case StereoMode::side:     return { 1, false };
        case StereoMode::midSide:  return { 2, false };
    }

    return { 2, true };
}

/** One frame of a stereo key (after the key EQ) into the mode's detector channels. */
inline void encodeKey (StereoMode mode, double left, double right, double* detector) noexcept
{
    switch (mode)
    {
        case StereoMode::linked:
        case StereoMode::unlinked:
            detector[0] = left;
            detector[1] = right;
            return;
        case StereoMode::mid:
            detector[0] = 0.5 * (left + right);
            return;
        case StereoMode::side:
            detector[0] = 0.5 * (left - right);
            return;
        case StereoMode::midSide:
            detector[0] = 0.5 * (left + right);
            detector[1] = 0.5 * (left - right);
            return;
    }
}

/**
    r_c = (1 − λ)·r_c + λ·max(r_L, r_R), in dB after the gain computer. The gain computer is
    monotonic, so the max is the louder channel's reduction: at λ = 1 both channels carry it to
    the bit, at λ = 0 each keeps its own to the bit.
*/
inline void linkReductions (double* reductionDb, double link) noexcept
{
    const auto loudest = std::max (reductionDb[0], reductionDb[1]);
    reductionDb[0] = (1.0 - link) * reductionDb[0] + link * loudest;
    reductionDb[1] = (1.0 - link) * reductionDb[1] + link * loudest;
}

/**
    Applies the detector channels' linear gains to the audio in place, per mode. A frame whose
    gains are all exactly 1 is left untouched, the M/S matrix included: the float round trip
    M + S is not exact in general, and the idle output must be the input to the bit.
*/
inline void applyGains (StereoMode mode, float* const* audio, int numAudioChannels, const float* const* gains, int numSamples) noexcept
{
    using myzel::dsp::isExactly;

    if (numAudioChannels < 2)
    {
        for (int i = 0; i < numSamples; ++i)
            if (! isExactly (gains[0][i], 1.0))
                audio[0][i] *= gains[0][i];
        return;
    }

    auto* left = audio[0];
    auto* right = audio[1];

    switch (mode)
    {
        case StereoMode::linked:
        case StereoMode::unlinked:
            for (int i = 0; i < numSamples; ++i)
            {
                if (! isExactly (gains[0][i], 1.0))
                    left[i] *= gains[0][i];
                if (! isExactly (gains[1][i], 1.0))
                    right[i] *= gains[1][i];
            }
            return;

        case StereoMode::mid:
        case StereoMode::side:
        case StereoMode::midSide:
            for (int i = 0; i < numSamples; ++i)
            {
                const auto gm = mode == StereoMode::side ? 1.0f : gains[0][i];
                const auto gs = mode == StereoMode::side ? gains[0][i] : (mode == StereoMode::midSide ? gains[1][i] : 1.0f);

                if (isExactly (gm, 1.0) && isExactly (gs, 1.0))
                    continue;

                const auto m = 0.5f * (left[i] + right[i]) * gm;
                const auto s = 0.5f * (left[i] - right[i]) * gs;
                left[i] = m + s;
                right[i] = m - s;
            }
            return;
    }
}

} // namespace basalt::dynamics
