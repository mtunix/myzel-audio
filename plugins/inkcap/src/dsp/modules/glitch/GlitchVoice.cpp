#include "dsp/modules/glitch/GlitchVoice.h"

#include "dsp/common/Crossfade.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace inkcap::dsp::glitch
{

namespace
{
    /** Smoothstep and its integral: the speed curves of Stall and Bend (closed form, so the head's lag is exact). */
    double smoothstep (double u) noexcept { return u * u * (3.0 - 2.0 * u); }
    double smoothstepIntegral (double u) noexcept { return u * u * u * (1.0 - 0.5 * u); }

    /** 0 -> 1 -> 0 along the fade shape: the crinkle's slur. */
    double bump (double u) noexcept { return u < 0.5 ? fadeShape (2.0 * u) : fadeShape (2.0 - 2.0 * u); }

    float dbToGain (double db) noexcept { return (float) std::pow (10.0, db / 20.0); }

    // A stalling tape's level falls with its speed: from full at 35 % of the speed down to nothing.
    constexpr double stallLevelSpeed = 0.35;
}

void Voice::start (const Event& e) noexcept
{
    current = e;
    playing = true;
    tau = 0;
    env = 0.0f;
    total = std::max<int64_t> (1, (int64_t) std::ceil (durationSeconds (e) * fs));

    switch (e.type)
    {
        case GlitchType::skip:
            piece = std::max (2, (int) std::lround (e.lengthSeconds * fs));
            skipFade = std::clamp ((int) std::lround (skipFadeSeconds * fs), 1, std::max (1, piece / 4));
            returnFade = std::max (1, (int) std::lround (skipReturnSeconds * fs));
            total = (int64_t) piece * e.repeats + returnFade;
            break;

        case GlitchType::chew:
        {
            const auto length = std::max (8.0, e.lengthSeconds * fs);
            attack = std::min (0.004 * fs, 0.25 * length);
            release = 0.45 * length;
            releaseStart = length - release;
            slurLength = std::min (length, 0.12 * fs);
            slurDepth = (0.0003 + 0.0006 * e.depth / 30.0) * fs;
            dipDb = 2.0f + 6.0f * (e.depth - 6.0f) / 24.0f;
            // A 2-pole low-pass falls 40 dB a decade: this cutoff loses `depth` dB at the reference.
            const auto cutoff = std::min (lossReferenceHz * std::pow (10.0, -(double) e.depth / 40.0), 0.45 * fs);
            for (auto& f : lift)
            {
                f.setCoefficients (cutoff, std::numbers::sqrt2 / 2.0, fs);
                f.reset();
            }
            total = (int64_t) std::ceil (length);
            break;
        }

        case GlitchType::stall:
            fall = 1.0;
            down = std::max (1.0, e.lengthSeconds * fs);
            hold = e.holdSeconds * fs;
            up = std::max (1.0, e.restartSeconds * fs);
            break;

        case GlitchType::bend:
            fall = 1.0 - std::exp2 (-(double) e.depth / 12.0);
            down = std::max (1.0, 0.2 * e.lengthSeconds * fs);
            up = std::max (1.0, 0.35 * e.lengthSeconds * fs);
            hold = std::max (0.0, e.lengthSeconds * fs - down - up);
            break;

        case GlitchType::any:
            playing = false;
            break;
    }

    if (e.type == GlitchType::stall || e.type == GlitchType::bend)
    {
        endLag = fall * (0.5 * down + hold + 0.5 * up);
        catchUp = std::max (1, (int) std::lround (catchUpSeconds * fs));
        total = (int64_t) std::ceil (down + hold + up) + catchUp;
    }
}

void Voice::render (const CaptureBuffer& capture, const float* input, float* out, int numChannels) noexcept
{
    if (playing)
    {
        switch (current.type)
        {
            case GlitchType::skip:  renderSkip (capture, input, out, numChannels); break;
            case GlitchType::chew:  renderChew (capture, input, out, numChannels); break;
            case GlitchType::stall:
            case GlitchType::bend:  renderBehind (capture, input, out, numChannels); break;
            case GlitchType::any:   playing = false; break;
        }
    }

    if (! playing)
    {
        env = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            out[c] = input[c];
        return;
    }

    ++tau;
}

void Voice::renderSkip (const CaptureBuffer& capture, const float* input, float* out, int numChannels) noexcept
{
    const auto k = (int) (tau / piece);
    const auto q = (int) (tau - (int64_t) k * piece);

    if (k < current.repeats)
    {
        // Repeat k plays the piece from `piece` samples before the start: (k + 1) pieces back now.
        const auto now = (k + 1) * piece;
        env = 1.0f;

        if (q >= skipFade)
        {
            for (int c = 0; c < numChannels; ++c)
                out[c] = capture.at (c, now);
            return;
        }

        // The jump: the previous repeat (or the input) runs on under the fade.
        const auto g = equalPower ((double) q / (double) skipFade);
        for (int c = 0; c < numChannels; ++c)
            out[c] = g.out * capture.at (c, k * piece) + g.in * capture.at (c, now);
        return;
    }

    const auto r = (int) (tau - (int64_t) current.repeats * piece);
    if (r >= returnFade)
    {
        playing = false;
        return;
    }

    const auto g = equalPower ((double) r / (double) returnFade);
    env = g.out;
    for (int c = 0; c < numChannels; ++c)
        out[c] = g.out * capture.at (c, current.repeats * piece) + g.in * input[c];
}

void Voice::renderChew (const CaptureBuffer& capture, const float* input, float* out, int numChannels) noexcept
{
    const auto t = (double) tau;
    if (tau >= total)
    {
        playing = false;
        return;
    }

    double e = 1.0;
    if (t < attack)
        e = fadeShape (t / attack);
    else if (t >= releaseStart)
        e = 1.0 - fadeShape ((t - releaseStart) / release);
    env = (float) e;

    const auto delay = t < slurLength ? slurDepth * bump (t / slurLength) : 0.0;
    const auto level = dbToGain (-dipDb * e);

    for (int c = 0; c < numChannels; ++c)
    {
        const auto x = (double) (delay > 0.0 ? capture.read (c, delay) : input[c]);
        const auto lp = lift[(size_t) c].processLowpass (x);
        out[c] = level * (float) (x + e * (lp - x));
    }
}

void Voice::headAt (double t, double& lag, double& speed) const noexcept
{
    if (t < down)
    {
        const auto u = t / down;
        lag = fall * down * smoothstepIntegral (u);
        speed = 1.0 - fall * smoothstep (u);
    }
    else if (t < down + hold)
    {
        lag = fall * (0.5 * down + (t - down));
        speed = 1.0 - fall;
    }
    else if (t < down + hold + up)
    {
        const auto u = (t - down - hold) / up;
        lag = fall * (0.5 * down + hold + up * (u - smoothstepIntegral (u)));
        speed = 1.0 - fall * (1.0 - smoothstep (u));
    }
    else
    {
        lag = endLag;
        speed = 1.0;
    }
}

void Voice::renderBehind (const CaptureBuffer& capture, const float* input, float* out, int numChannels) noexcept
{
    const auto t = (double) tau;
    const auto catchUpStart = total - catchUp;

    if (tau >= catchUpStart)
    {
        const auto r = tau - catchUpStart;
        if (r >= catchUp)
        {
            playing = false;
            return;
        }
        const auto g = equalPower ((double) r / (double) catchUp);
        env = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            out[c] = g.out * capture.read (c, endLag) + g.in * input[c];
        return;
    }

    double lag = 0.0, speed = 1.0;
    headAt (t, lag, speed);
    env = (float) std::clamp ((1.0 - speed) / fall, 0.0, 1.0);

    const auto gain = current.type == GlitchType::stall ? (float) fadeShape (speed / stallLevelSpeed) : 1.0f;
    for (int c = 0; c < numChannels; ++c)
        out[c] = gain * capture.read (c, lag);
}

} // namespace inkcap::dsp::glitch
