#include "dsp/TapPlan.h"

#include <algorithm>
#include <cmath>

namespace mulberry::dsp
{

namespace
{
    std::array<float, numBuses> panned (float gain, float pan) noexcept
    {
        const auto p = std::clamp (pan, -1.0f, 1.0f);
        return { 0.0f, 0.0f, gain * 0.5f * (1.0f - p), gain * 0.5f * (1.0f + p) };
    }

    Read output (float fraction, float offsetMs, float gain, float pan) noexcept
    {
        return { fraction, offsetMs, panned (gain, pan) };
    }

    Read tap (float fraction, float into0, float into1) noexcept
    {
        return { fraction, 0.0f, { into0, into1, 0.0f, 0.0f } };
    }

    /**
        The heads of a line: the playing heads (outputs) and the feedback taps with their shares,
        which sum to 1. Without Swell the playing heads feed back equally (Loop's head sum); Swell
        blends that towards every head of the machine equally (Drum).
    */
    struct Heads
    {
        std::array<float, maxHeads> fractions {};
        int count = 0;
        float longest = 1.0f;
        std::array<float, maxHeads> tapFractions {};
        std::array<float, maxHeads> tapShares {};
        int taps = 0;
        float longestTap = 1.0f;
        bool multi = false;         // anything but one head at Time feeding back alone
    };

    Heads headsOf (const TapPlanInput& in) noexcept
    {
        Heads h;
        for (int i = 0; i < maxHeads; ++i)
            if (in.headMask & (1 << i))
            {
                h.fractions[(size_t) h.count++] = (float) (i + 1) * in.headUnit;
                h.longest = (float) (i + 1) * in.headUnit;
            }
        if (h.count == 0)
        {
            h.fractions[0] = 1.0f;
            h.count = 1;
        }

        const auto swell = in.swellHeads > 0 ? std::clamp (in.swell, 0.0f, 1.0f) : 0.0f;
        if (swell <= 0.0f)
        {
            for (int i = 0; i < h.count; ++i)
            {
                h.tapFractions[(size_t) i] = h.fractions[(size_t) i];
                h.tapShares[(size_t) i] = 1.0f / (float) h.count;
            }
            h.taps = h.count;
            h.longestTap = h.longest;
        }
        else
        {
            const auto heads = std::min (in.swellHeads, maxHeads);
            for (int i = 0; i < heads; ++i)
            {
                const auto fraction = (float) (i + 1) * in.headUnit;
                bool playing = false;
                for (int k = 0; k < h.count; ++k)
                    playing = playing || same (h.fractions[(size_t) k], fraction);
                h.tapFractions[(size_t) i] = fraction;
                h.tapShares[(size_t) i] = (playing ? (1.0f - swell) / (float) h.count : 0.0f) + swell / (float) heads;
                h.longestTap = fraction;
            }
            h.taps = heads;
        }

        h.multi = h.count > 1 || ! same (h.fractions[0], 1.0f) || h.taps > 1;
        return h;
    }
}

bool TapPlan::samePositions (const TapPlan& other) const noexcept
{
    if (twoTransports != other.twoTransports)
        return false;

    for (size_t l = 0; l < 2; ++l)
    {
        const auto& a = lines[l];
        const auto& b = other.lines[l];
        if (a.numReads != b.numReads)
            return false;
        for (int i = 0; i < a.numReads; ++i)
            if (! same (a.reads[(size_t) i].fraction, b.reads[(size_t) i].fraction)
                || ! same (a.reads[(size_t) i].offsetMs, b.reads[(size_t) i].offsetMs))
                return false;
    }
    return true;
}

float patternPan (PanRule rule, int k, int n) noexcept
{
    switch (rule)
    {
        case PanRule::centre:    return 0.0f;
        case PanRule::alternate: return (k % 2 == 0) ? -1.0f : 1.0f;
        case PanRule::sweep:     return n <= 1 ? 0.0f : -1.0f + 2.0f * (float) k / (float) (n - 1);
        case PanRule::scatter:
        {
            // A fixed scatter (golden-ratio sequence): the same pattern every time it is chosen.
            const auto x = std::fmod (0.5f + 0.6180339887f * (float) (k + 1) * 3.0f, 1.0f);
            return 2.0f * x - 1.0f;
        }
    }
    return 0.0f;
}

TapPlan compileTapPlan (const TapPlanInput& in) noexcept
{
    TapPlan plan;
    const auto w = std::clamp (in.width, 0.0f, 1.0f);
    const auto g = std::clamp (in.groove, 0.0f, 0.5f);
    const auto feel = in.feelMs;
    // Pattern mode replaces the playing heads: its steps play, and it feeds back from Time.
    auto headInput = in;
    if (in.mode == Mode::pattern)
        headInput.headMask = 0;
    const auto heads = headsOf (headInput);
    const auto multiHead = heads.multi;

    auto& L = plan.lines[0];
    auto& R = plan.lines[1];

    switch (in.mode)
    {
        case Mode::single:
        {
            L.inputFromLeft = 1.0f;
            R.inputFromRight = 1.0f;

            for (size_t line = 0; line < 2; ++line)
            {
                auto& lp = plan.lines[line];
                const auto pan = line == 0 ? -w : w;
                const auto into0 = line == 0 ? 1.0f : 0.0f;

                if (multiHead || g <= 0.0f)
                {
                    for (int h = 0; h < heads.count; ++h)
                        lp.add (output (heads.fractions[(size_t) h], feel, 1.0f, pan));
                    for (int t = 0; t < heads.taps; ++t)
                        lp.add (tap (heads.tapFractions[(size_t) t], into0 * heads.tapShares[(size_t) t], (1.0f - into0) * heads.tapShares[(size_t) t]));
                }
                else
                {
                    // Groove: the loop is two echoes long and the odd echo swings.
                    lp.add (output (1.0f + g, feel, 1.0f, pan));
                    lp.add (output (2.0f, feel, 1.0f, pan));
                    lp.add (tap (2.0f, into0, 1.0f - into0));
                }
            }

            const auto cycle = (multiHead || g <= 0.0f) ? heads.longestTap : 2.0f;
            plan.holdFraction = { cycle, cycle };
            break;
        }

        case Mode::dual:
        {
            plan.twoTransports = true;
            L.inputFromLeft = 1.0f;
            R.inputFromRight = 1.0f;
            const auto c = std::clamp (in.crossFeed, 0.0f, 1.0f);

            for (size_t line = 0; line < 2; ++line)
            {
                auto& lp = plan.lines[line];
                const auto pan = line == 0 ? -w : w;

                for (int h = 0; h < heads.count; ++h)
                    lp.add (output (heads.fractions[(size_t) h], feel, 1.0f, pan));
                for (int t = 0; t < heads.taps; ++t)
                {
                    const auto own = (1.0f - c) * heads.tapShares[(size_t) t], cross = c * heads.tapShares[(size_t) t];
                    lp.add (tap (heads.tapFractions[(size_t) t], line == 0 ? own : cross, line == 0 ? cross : own));
                }
            }

            plan.holdLine = { 0, 1 };
            plan.holdFraction = { heads.longestTap, heads.longestTap };
            break;
        }

        case Mode::pingPong:
        {
            // The input (summed) goes into the left line; the lines feed each other: L -> R -> L.
            L.inputFromLeft = in.monoInput ? 1.0f : 0.5f;
            L.inputFromRight = in.monoInput ? 0.0f : 0.5f;
            const auto swing = multiHead ? 0.0f : g;

            for (size_t line = 0; line < 2; ++line)
            {
                auto& lp = plan.lines[line];
                const auto pan = line == 0 ? -w : w;
                const auto scale = line == 0 ? 1.0f + swing : 1.0f - swing;

                for (int h = 0; h < heads.count; ++h)
                    lp.add (output (heads.fractions[(size_t) h] * scale, feel, 1.0f, pan));
                for (int t = 0; t < heads.taps; ++t)
                {
                    const auto share = heads.tapShares[(size_t) t];
                    lp.add (tap (heads.tapFractions[(size_t) t] * scale, line == 0 ? 0.0f : share, line == 0 ? share : 0.0f));
                }
            }

            plan.holdFraction = { 2.0f * heads.longestTap, 2.0f * heads.longestTap };
            break;
        }

        case Mode::pattern:
        {
            L.inputFromLeft = 1.0f;
            R.inputFromRight = 1.0f;
            const auto n = std::clamp (in.steps, 1, maxPatternSteps);
            const auto stepLength = 1.0f / (float) n;

            for (int k = 0; k < n; ++k)
            {
                const auto level = std::clamp (in.stepLevels[(size_t) k], 0.0f, 1.0f);
                // Even steps (2nd, 4th, ...) swing by up to half a step.
                const auto swingOffset = (k % 2 == 1) ? g * stepLength : 0.0f;
                const auto fraction = (float) (k + 1) * stepLength + swingOffset;
                const auto p = patternPan (in.panRule, k, n) * w;
                const auto spread = 1.0f - std::abs (p);   // the stereo image narrows as the step pans out

                L.add (output (fraction, feel, level, p - spread));
                R.add (output (fraction, feel, level, p + spread));
            }

            // The pattern feeds back from its end (the Drum's last head); Swell spreads it as above.
            for (int t = 0; t < heads.taps; ++t)
            {
                L.add (tap (heads.tapFractions[(size_t) t], heads.tapShares[(size_t) t], 0.0f));
                R.add (tap (heads.tapFractions[(size_t) t], 0.0f, heads.tapShares[(size_t) t]));
            }
            plan.holdFraction = { heads.longestTap, heads.longestTap };
            break;
        }
    }

    return plan;
}

} // namespace mulberry::dsp
