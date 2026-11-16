#pragma once

#include "common/dsp/FloatCompare.h"
#include "dsp/shapes/split/SplitDesign.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace halite::dsp
{

#if JUCE_USE_SIMD
using SplitLanes = juce::dsp::SIMDRegister<double>;
#else
/** Where JUCE has no SIMD register: SplitFilter's two lanes as two doubles, with the operations it uses. */
struct SplitLanes
{
    static constexpr size_t SIMDRegisterSize = 2 * sizeof (double);
    static constexpr size_t size() noexcept { return 2; }

    static SplitLanes expand (double s) noexcept { return { { s, s } }; }
    static SplitLanes fromRawArray (const double* a) noexcept { return { { a[0], a[1] } }; }
    void copyToRawArray (double* a) const noexcept { a[0] = v[0]; a[1] = v[1]; }

    SplitLanes operator+ (SplitLanes o) const noexcept { return { { v[0] + o.v[0], v[1] + o.v[1] } }; }
    SplitLanes operator- (SplitLanes o) const noexcept { return { { v[0] - o.v[0], v[1] - o.v[1] } }; }
    SplitLanes operator* (SplitLanes o) const noexcept { return { { v[0] * o.v[0], v[1] * o.v[1] } }; }

    std::array<double, 2> v {};
};
#endif

/**
    What one Split band removes (PLAN.md, Reduction): a steep bell, the full depth over [low, high]
    and next to none a third of an octave past either edge, or a steep high shelf from `low`, cut to
    the linear gain 1 − depth. Subtractive: process() returns x − filtered (x), exactly 0 wherever the
    depth is 0, whatever the filter's state.

    A cascade of TPT SVFs (and one-poles) that keeps running at depth 0, so a cut starts settled
    (split/NOTES.md). The design runs once per segment of `interval` samples, for the segment's
    last sample and the one before it; the coefficients follow a cubic through the designs whose
    slope is continuous (the design's change per sample), so a fast depth does not click.
    Segments sit on the detector's hop grid (`phase`), where the chain already knows every depth
    of the segment. A new band glides there in log frequency over glideMs. One filter carries one
    or two signals: with one depth for both (one design, Linked stereo) or a depth each (Unlinked),
    side by side in the lanes of one SIMD register (one signal beside silence).
*/
class SplitFilter
{
public:
    using Kind = SplitKind;

    static constexpr int interval = splitDesign::interval;
    static constexpr int maxChannels = 2;
    static constexpr int noRestart = 1 << 30;     // process()'s `limit` when the grid does not restart
    static constexpr double glideMs = 5.0;

    explicit SplitFilter (const splitDesign::Prototype& p) noexcept : prototype (&p) {}

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        glideSamples = std::max (1, (int) std::lround (glideMs * 0.001 * sampleRate));
        reset();
    }

    /** State to zero, the band set last without a glide, the design at depth 0. */
    void reset() noexcept
    {
        state = {};
        kind = wanted.kind;
        from = to = edgesOf (wanted);
        glideAt = glideSamples;
        placeEdges (to);
        Path rest;
        designAt (lowBase, upperBase, rest.designedGain, rest.next);
        rest.now = rest.next;
        paths.fill (rest);
        left = 0;
    }

    void setBand (Kind newKind, double lowHz, double highHz) noexcept
    {
        const auto top = 0.49 * sampleRate;
        lowHz = std::clamp (lowHz, 10.0, top);
        highHz = newKind == Kind::highShelf ? lowHz : std::clamp (std::max (highHz, lowHz * 1.01), 10.0, top);
        wanted = { newKind, lowHz, highHz };
    }

    Kind getKind() const noexcept { return wanted.kind; }

    /** Split's rule (PLAN.md, Reduction): a high shelf once the upper Detect handle reaches 0.4 fs, a bell below. */
    static Kind kindFor (double upperHandleHz, double rate) noexcept
    {
        return upperHandleHz >= 0.4 * rate ? Kind::highShelf : Kind::bell;
    }

    /** How many depths past the n-th sample process() reads: to the end of the segment that holds it, and never from `limit` on. */
    static constexpr int lookahead (int phase, int n, int limit = noRestart) noexcept
    {
        return std::min ((interval - 1) - ((phase + n - 1) & (interval - 1)), limit - n);
    }

    /** The edges of the segment's design, in Hz: the low edge and, for a bell, the boost shelf's corner (high × guard). */
    double lowEdgeHz() const noexcept { return std::exp2 (designedEdges.low); }
    double upperCornerHz() const noexcept { return std::exp2 (designedEdges.upper); }

    /** The coefficients a path runs at the current sample: per section a1, a2, a3, r0, r1, then the gain. */
    static constexpr int numCoefficients = 5 * splitDesign::Design::maxSections + 1;
    using Coefficients = std::array<double, numCoefficients>;
    const Coefficients& current (int path = 0) const noexcept { return paths[(size_t) path].now; }

    /** A design in the order current() holds it. */
    static Coefficients flatten (const splitDesign::Design& d) noexcept
    {
        Coefficients c {};
        for (size_t k = 0; k < d.sections.size(); ++k)
        {
            const auto& s = d.sections[k];
            c[5 * k] = s.a1;
            c[5 * k + 1] = s.a2;
            c[5 * k + 2] = s.a3;
            c[5 * k + 3] = s.r0;
            c[5 * k + 4] = s.r1;
        }
        c[numCoefficients - 1] = d.gain;
        return c;
    }

    /**
        Writes what the band removes from n samples of `channels` signals (1 or 2). `depths` holds
        `numPaths` depths (1 − the linear gain): one for every signal, or one per signal; each has a
        value for every sample and for the lookahead (phase, n) samples after them. Within a segment
        a depth moves as the chain's does, continuous in value and slope (its quadratic B-spline of
        hop values): the slope a segment ends with is the depth's change over its last sample. A
        change larger than that spline ever makes (steepestStep) is a step, not a slope, and the
        segment ends level, so any depth on any sample stays stable (split/NOTES.md, Modulation).
        `phase` is x[0]'s place on the design grid (its input time on the hop grid). `limit`: where
        a transport restart moves the grid, in samples from x[0]; a segment ends there at the
        latest, and no depth from it on is read.
    */
    void process (const double* const* x, double* const* removed, int channels, const float* const* depths, int numPaths, int n, int phase,
                  int limit = noRestart) noexcept
    {
        if (wanted.kind != kind)
            switchKind();

        if (channels == 1)
            dispatch<1, 1> (x, removed, depths, n, phase, limit);
        else if (numPaths == 1)
            dispatch<2, 1> (x, removed, depths, n, phase, limit);
        else
            dispatch<2, 2> (x, removed, depths, n, phase, limit);
    }

    /** One signal. */
    void process (const double* x, const float* depth, double* removed, int n, int phase, int limit = noRestart) noexcept
    {
        process (&x, &removed, 1, &depth, 1, n, phase, limit);
    }

private:
    struct Band
    {
        Kind kind = Kind::bell;
        double low = 4500.0, high = 12000.0;
    };

    /** The two corners in log2 Hz: the cut shelf's and the boost shelf's (unused by a shelf). */
    struct Edges
    {
        double low = 0.0, upper = 0.0;
        bool operator== (const Edges&) const = default;
    };

    /** One depth's coefficients on their way through a segment: from `now` (with the slope `start`) to `next` (with `slope`). */
    struct Path
    {
        Coefficients now {}, next {}, start {}, slope {}, first {}, second {}, third {};
        double designedGain = 1.0;
        bool moving = false, sloped = false;
    };

    static Edges edgesOf (const Band& b) noexcept { return { std::log2 (b.low), std::log2 (b.high * splitDesign::guard) }; }

    static double gainOf (float depth) noexcept { return depth > 0.0f ? 1.0 - (double) depth : 1.0; }

    /**
        The largest change of depth over one sample a segment's slope takes: the deepest depth over a
        segment's length. The chain's spline of hop values changes by at most 63/64 of it.
    */
    double steepestStep() const noexcept { return (1.0 - prototype->deepestGain) / interval; }

    void placeEdges (const Edges& e) noexcept
    {
        designedEdges = e;
        lowBase = splitDesign::baseFor (*prototype, std::exp2 (e.low), sampleRate);
        upperBase = splitDesign::baseFor (*prototype, std::exp2 (e.upper), sampleRate);
    }

    void designAt (const splitDesign::Base& low, const splitDesign::Base& upper, double gain, Coefficients& out) noexcept
    {
        splitDesign::Design d;
        splitDesign::design (*prototype, kind, low, upper, gain, d);
        out = flatten (d);
    }

    /** A bell turns into a shelf (or back) at once and from rest: the chain does that only on a lane it has not let be heard yet. */
    void switchKind() noexcept
    {
        const auto keep = wanted;
        reset();
        wanted = keep;
    }

    Edges edgesAt (int at) const noexcept
    {
        if (at >= glideSamples)
            return to;
        const auto f = (double) std::max (at, 0) / glideSamples;
        return { from.low + f * (to.low - from.low), from.upper + f * (to.upper - from.upper) };
    }

    /**
        At a segment's first sample (i): the band at its last sample and the one before (the glide),
        then each path's design for its last sample, its change over that sample (the slope), and
        the cubic there from where the coefficients are.
    */
    void plan (const float* const* depths, int numPaths, int i, int phase, int limit) noexcept
    {
        const auto length = std::min (interval - ((phase + i) & (interval - 1)), limit - i);

        const auto target = edgesOf (wanted);
        if (! (target == to))
        {
            from = edgesAt (glideAt);
            to = target;
            glideAt = 0;
        }
        const auto edges = edgesAt (glideAt + length - 1);
        const auto before = edgesAt (glideAt + length - 2);
        glideAt = std::min (glideAt + length, glideSamples);
        left = length;

        const auto edgesMoved = ! (edges == designedEdges);
        if (edgesMoved)
            placeEdges (edges);
        const auto gliding = ! (before == edges);
        splitDesign::Base lowBefore = lowBase, upperBefore = upperBase;
        if (gliding)
        {
            lowBefore = splitDesign::baseFor (*prototype, std::exp2 (before.low), sampleRate);
            upperBefore = splitDesign::baseFor (*prototype, std::exp2 (before.upper), sampleRate);
        }

        for (int p = 0; p < numPaths; ++p)
        {
            auto& path = paths[(size_t) p];
            const auto* depth = depths[p];
            const auto last = i + length - 1;
            const auto gain = gainOf (depth[last]);
            const auto changed = edgesMoved || ! myzel::dsp::isExactly (gain, path.designedGain);
            if (changed)
            {
                path.designedGain = gain;
                designAt (lowBase, upperBase, gain, path.next);
            }

            // A one-sample segment has no sample before its last here, and a step is not a slope: either ends level in depth.
            const auto wasSloped = path.sloped;
            auto gainBefore = length > 1 ? gainOf (depth[last - 1]) : gain;
            if (std::abs (std::max (gainBefore, prototype->deepestGain) - std::max (gain, prototype->deepestGain)) > steepestStep())
                gainBefore = gain;
            path.sloped = gliding || ! myzel::dsp::isExactly (gainBefore, gain);
            path.start = path.slope;
            if (path.sloped)
            {
                designAt (lowBefore, upperBefore, gainBefore, path.slope);
                for (size_t j = 0; j < path.slope.size(); ++j)
                    path.slope[j] = path.next[j] - path.slope[j];
            }
            else
            {
                path.slope = {};
            }

            path.moving = changed || wasSloped || path.sloped;
            if (! path.moving)
            {
                // A still path steps by nothing beside one that moves: the lanes step together.
                path.first = path.second = path.third = {};
                continue;
            }

            // p (τ) = c0 + M0 τ + (3Δ − 2 M0 − M1) τ² + (M0 + M1 − 2Δ) τ³ over τ = j / length, M = slope × length,
            // as forward differences: three additions per coefficient and sample.
            const auto h = 1.0 / length, h2 = h * h, h3 = h2 * h, scale = (double) length;
            for (size_t j = 0; j < path.now.size(); ++j)
            {
                const auto m0 = path.start[j] * scale, m1 = path.slope[j] * scale, delta = path.next[j] - path.now[j];
                const auto c = 3.0 * delta - 2.0 * m0 - m1, d = m0 + m1 - 2.0 * delta;
                path.first[j] = m0 * h + c * h2 + d * h3;
                path.second[j] = 2.0 * c * h2 + 6.0 * d * h3;
                path.third[j] = 6.0 * d * h3;
            }
        }
    }

    /** The two prototypes' layouts: Ess, three pairs per shelf; Harsh, two pairs and a real root. */
    template <int channels, int numPaths>
    void dispatch (const double* const* x, double* const* removed, const float* const* depths, int n, int phase, int limit) noexcept
    {
        const auto bell = kind == Kind::bell;
        if (prototype->reals == 0)
            bell ? run<3, 0, 2, channels, numPaths> (x, removed, depths, n, phase, limit)
                 : run<3, 0, 1, channels, numPaths> (x, removed, depths, n, phase, limit);
        else
            bell ? run<2, 1, 2, channels, numPaths> (x, removed, depths, n, phase, limit)
                 : run<2, 1, 1, channels, numPaths> (x, removed, depths, n, phase, limit);
    }

    using Lanes = SplitLanes;

    /** A segment's samples, the signals side by side in each row (a lone signal's neighbour silent). */
    using Block = std::array<std::array<double, Lanes::size()>, (size_t) interval>;

    /**
        The cascade over the n samples, segment by segment and, within a segment's part, section by
        section over it, so each section's coefficients and state stay in registers and the signals'
        recurrences run side by side in one register's lanes. One signal runs the same arithmetic in
        its lane as two do, so the result is the sample-by-sample cascade's, bit for bit, however
        many signals share the filter.
    */
    template <int pairs, int reals, int shelves, int channels, int numPaths>
    void run (const double* const* x, double* const* removed, const float* const* depths, int n, int phase, int limit) noexcept
    {
        constexpr int perShelf = pairs + reals, count = perShelf * shelves;
        constexpr auto gainIndex = (size_t) numCoefficients - 1;
        alignas (Lanes::SIMDRegisterSize) Block v {};

        for (int i = 0; i < n;)
        {
            if (left == 0)
                plan (depths, numPaths, i, phase, limit);
            const auto length = std::min (left, n - i);
            const auto ends = length == left;

            for (int j = 0; j < length; ++j)
                for (size_t ch = 0; ch < (size_t) channels; ++ch)
                    v[(size_t) j][ch] = x[ch][i + j];

            for (int k = 0; k < count; ++k)
            {
                if (k % perShelf < pairs)
                    runSection<channels, numPaths, false> ((size_t) k, v, length, ends);
                else
                    runSection<channels, numPaths, true> ((size_t) k, v, length, ends);
            }

            // The gain: a shelf's level, which a bell keeps at 1. A shared path serves every channel.
            for (size_t p = 0; p < (size_t) numPaths; ++p)
            {
                auto& path = paths[p];
                auto gain = path.now[gainIndex], d1 = path.first[gainIndex], d2 = path.second[gainIndex];
                const auto d3 = path.third[gainIndex];
                const auto* depth = depths[p] + i;
                const auto lastChannel = numPaths == 1 ? (size_t) channels : p + 1;
                for (int j = 0; j < length; ++j)
                {
                    if (ends && j == length - 1)
                        gain = path.next[gainIndex];
                    else if (path.moving)
                    {
                        gain += d1;
                        d1 += d2;
                        d2 += d3;
                    }
                    const auto cut = depth[j] > 0.0f;
                    for (auto ch = p; ch < lastChannel; ++ch)
                        removed[ch][i + j] = cut ? x[ch][i + j] - gain * v[(size_t) j][ch] : 0.0;
                }
                path.now[gainIndex] = gain;
                path.first[gainIndex] = d1;
                path.second[gainIndex] = d2;
            }

            left -= length;
            i += length;
        }
    }

    /** One section (an SVF, or a one-pole) over `length` samples of the segment in place, each path's coefficients along its cubic. */
    template <int channels, int numPaths, bool onePole>
    void runSection (size_t k, Block& v, int length, bool ends) noexcept
    {
        // A one-pole uses a1 and r0 only. Each coefficient holds a value per lane: its path's, or the one path's in every lane.
        constexpr int used = onePole ? 2 : 5;
        const auto field = [] (size_t f) { return onePole && f == 1 ? (size_t) 3 : f; };
        const auto lanes = [] (const auto& values)
        {
            alignas (Lanes::SIMDRegisterSize) std::array<double, Lanes::size()> a {};
            for (size_t lane = 0; lane < a.size(); ++lane)
                a[lane] = values[std::min (lane, (size_t) numPaths - 1)];
            return Lanes::fromRawArray (a.data());
        };

        std::array<Lanes, (size_t) used> c, d1, d2, d3, end;
        auto moving = false;
        for (size_t f = 0; f < (size_t) used; ++f)
        {
            const auto j = 5 * k + field (f);
            std::array<double, (size_t) numPaths> now, first, second, third, next;
            for (size_t p = 0; p < (size_t) numPaths; ++p)
            {
                const auto& path = paths[p];
                moving = moving || path.moving;
                now[p] = path.now[j];
                first[p] = path.first[j];
                second[p] = path.second[j];
                third[p] = path.third[j];
                next[p] = path.next[j];
            }
            c[f] = lanes (now);
            d1[f] = lanes (first);
            d2[f] = lanes (second);
            d3[f] = lanes (third);
            end[f] = lanes (next);
        }

        // A lone signal's neighbour starts and stays silent.
        alignas (Lanes::SIMDRegisterSize) std::array<double, Lanes::size()> z0 {}, z1 {};
        for (size_t ch = 0; ch < (size_t) channels; ++ch)
        {
            z0[ch] = state[ch][k][0];
            z1[ch] = state[ch][k][1];
        }
        auto s0 = Lanes::fromRawArray (z0.data()), s1 = Lanes::fromRawArray (z1.data());
        const auto two = Lanes::expand (2.0);

        for (int j = 0; j < length; ++j)
        {
            if (ends && j == length - 1)
            {
                // The segment's last sample runs its own design exactly.
                c = end;
            }
            else if (moving)
            {
                for (size_t f = 0; f < (size_t) used; ++f)
                {
                    c[f] = c[f] + d1[f];
                    d1[f] = d1[f] + d2[f];
                    d2[f] = d2[f] + d3[f];
                }
            }

            auto* io = v[(size_t) j].data();
            const auto in = Lanes::fromRawArray (io);
            if constexpr (onePole)
            {
                const auto u = (in - s0) * c[0];
                const auto lp = u + s0;
                s0 = lp + u;
                (in - c[1] * (in - lp)).copyToRawArray (io);
            }
            else
            {
                const auto v3 = in - s1;
                const auto v1 = c[0] * s0 + c[1] * v3;
                const auto v2 = s1 + c[1] * s0 + c[2] * v3;
                s0 = two * v1 - s0;
                s1 = two * v2 - s1;
                (in - (c[3] * (in - v2) + c[4] * v1)).copyToRawArray (io);
            }
        }

        alignas (Lanes::SIMDRegisterSize) std::array<double, Lanes::size()> out {};
        for (size_t f = 0; f < (size_t) used; ++f)
        {
            const auto j = 5 * k + field (f);
            c[f].copyToRawArray (out.data());
            for (size_t p = 0; p < (size_t) numPaths; ++p)
                paths[p].now[j] = out[p];
            d1[f].copyToRawArray (out.data());
            for (size_t p = 0; p < (size_t) numPaths; ++p)
                paths[p].first[j] = out[p];
            d2[f].copyToRawArray (out.data());
            for (size_t p = 0; p < (size_t) numPaths; ++p)
                paths[p].second[j] = out[p];
        }
        s0.copyToRawArray (z0.data());
        s1.copyToRawArray (z1.data());
        for (size_t ch = 0; ch < (size_t) channels; ++ch)
            state[ch][k] = { z0[ch], z1[ch] };
    }

    const splitDesign::Prototype* prototype;
    double sampleRate = 48000.0;
    int glideSamples = 240, glideAt = 240;
    Band wanted;
    Kind kind = Kind::bell;
    Edges from, to, designedEdges;
    splitDesign::Base lowBase, upperBase;
    std::array<Path, maxChannels> paths {};
    int left = 0;
    std::array<std::array<std::array<double, 2>, splitDesign::Design::maxSections>, maxChannels> state {};
};

} // namespace halite::dsp
