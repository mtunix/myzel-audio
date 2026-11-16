#include "dsp/shapes/ShapeLane.h"

#include <algorithm>
#include <cmath>

namespace halite::dsp
{

namespace
{
    // A NaN or Inf would stay in the filters' and the frames' state; the dry path passes it on untouched.
    float sane (float x) noexcept
    {
        return std::isfinite (x) ? std::clamp (x, -inputLimit, inputLimit) : 0.0f;
    }

    bool isComponent (StereoMode m) noexcept { return m == StereoMode::mid || m == StereoMode::side; }
}

void ShapeLane::prepare (double sampleRate, int frameSize)
{
    shape.prepare (sampleRate);
    spectral.prepare (sampleRate, frameSize);
    frame = heard = frameSize;
}

void ShapeLane::start (const LaneConfig& c, const ShapeBands& bands, const ShapeLane* learned) noexcept
{
    laneConfig = c;
    heard = frame;
    shape.setBands ({ c.essKind, bands.essLow, bands.essHigh, bands.harshLow, bands.harshHigh });
    shape.reset();

    if (c.shape == Shape::spectral)
    {
        const auto mode = c.numChannels == 2 ? c.stereo : StereoMode::linked;
        const auto signals = c.numChannels == 2 && ! isComponent (mode) ? 2 : 1;
        spectral.start (signals, mode == StereoMode::linked);
        if (learned != nullptr && learned != this && learned->laneConfig.shape == Shape::spectral)
            spectral.takeTargetFrom (learned->spectral);
    }
}

std::array<int, 2> ShapeLane::reductionsFor() const noexcept
{
    const auto unlinked = laneConfig.numChannels == 2 && laneConfig.stereo == StereoMode::unlinked;
    return { 0, unlinked ? 1 : 0 };
}

int ShapeLane::components (const float* const* in, int n, std::array<const float*, 2>& out) noexcept
{
    const auto mode = laneConfig.numChannels == 2 ? laneConfig.stereo : StereoMode::linked;
    if (isComponent (mode))
    {
        const auto sign = mode == StereoMode::mid ? 1.0f : -1.0f;
        auto* m = component[0].data();
        for (int i = 0; i < n; ++i)
            m[i] = 0.5f * (sane (in[0][i]) + sign * sane (in[1][i]));
        out[0] = m;
        return 1;
    }

    for (int c = 0; c < laneConfig.numChannels; ++c)
    {
        auto* x = component[(size_t) c].data();
        for (int i = 0; i < n; ++i)
            x[i] = sane (in[c][i]);
        out[(size_t) c] = x;
    }
    return laneConfig.numChannels;
}

void ShapeLane::fill (const float* const* history) noexcept
{
    if (laneConfig.shape != Shape::spectral)
        return;

    for (int done = 0; done < frame;)
    {
        const auto n = std::min (maxSamples, frame - done);
        std::array<const float*, 2> at { history[0] + done, history[laneConfig.numChannels - 1] + done };
        std::array<const float*, 2> signals {};
        components (at.data(), n, signals);
        spectral.fill (signals.data(), n);
        done += n;
    }
    heard = 0;
}

void ShapeLane::restart (std::int64_t shift, std::int64_t boundary) noexcept
{
    if (laneConfig.shape == Shape::spectral)
        spectral.restart (shift, boundary);
}

void ShapeLane::process (const LaneInput& in, float* const* essRemoved, float* const* harshRemoved, int n) noexcept
{
    const auto wide = laneConfig.shape == Shape::wide;
    const auto stereo = laneConfig.numChannels == 2;
    const auto mode = stereo ? laneConfig.stereo : StereoMode::linked;
    const ShapeBands own { laneConfig.essKind, in.bands.essLow, in.bands.essHigh, in.bands.harshLow, in.bands.harshHigh };
    const auto midOrSide = isComponent (mode);
    std::array<const float*, 2> signals {};
    heard = std::min (heard + n, frame);

    if (laneConfig.shape == Shape::spectral)
    {
        components (in.input, n, signals);
        std::array<float*, 2> ess { midOrSide ? componentEss[0].data() : essRemoved[0], essRemoved[laneConfig.numChannels - 1] };
        std::array<float*, 2> harsh { midOrSide ? componentHarsh[0].data() : harshRemoved[0], harshRemoved[laneConfig.numChannels - 1] };
        auto settings = in.spectral;
        settings.harsh = in.harsh;
        spectral.process (signals.data(), ess.data(), harsh.data(), n, in.position, in.bands, settings, *in.reductions, reductionsFor());
    }
    else
    {
        // The channels (or the component) through one shape: Unlinked with a reduction each, else one for all.
        const auto channels = components (in.dry, n, signals);
        const auto paths = mode == StereoMode::unlinked && stereo ? 2 : 1;
        const std::array<const float*, 2> essDepth { in.essDepth[0], in.essDepth[1] }, harshDepth { in.harshDepth[0], in.harshDepth[1] };
        std::array<float*, 2> ess { essRemoved[0], essRemoved[laneConfig.numChannels - 1] };
        std::array<float*, 2> harsh { harshRemoved[0], harshRemoved[laneConfig.numChannels - 1] };
        if (midOrSide)
        {
            ess[0] = componentEss[0].data();
            harsh[0] = componentHarsh[0].data();
        }
        shape.setBands (own);
        shape.process (signals.data(), essDepth.data(), harshDepth.data(), ess.data(), harsh.data(), channels, paths, n, in.grid, wide, in.harsh);
    }

    if (midOrSide)
    {
        // L = M + S, R = M − S: what the mid loses both channels lose; what the side loses, R gains.
        const auto sign = mode == StereoMode::mid ? 1.0f : -1.0f;
        for (int i = 0; i < n; ++i)
        {
            essRemoved[0][i] = componentEss[0][(size_t) i];
            harshRemoved[0][i] = componentHarsh[0][(size_t) i];
            essRemoved[1][i] = sign * componentEss[0][(size_t) i];
            harshRemoved[1][i] = sign * componentHarsh[0][(size_t) i];
        }
    }
}

} // namespace halite::dsp
