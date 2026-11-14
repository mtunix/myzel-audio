#pragma once

#include "dsp/common/BSpline.h"
#include "dsp/common/MatchedSection.h"
#include "dsp/common/NetworkTypes.h"
#include "dsp/common/Polynomial.h"
#include "dsp/common/RootCascade.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>

namespace viridian::dsp
{

inline constexpr int maxControlBlock = 128;

/**
    A network's control block at a sample rate: 64 samples up to 72 kHz, 128 above (689 Hz at
    44.1 kHz, 750 Hz at 96, 1.5 kHz at 192), so a model's networks can take turns within a block
    (Network::align()). Designing more often is not smoother: where roots of different cascade
    stages meet, the spline carries each design's corner (tools/netlist/README.md, "Network").
*/
inline int networkControlBlock (double sampleRate) noexcept
{
    return sampleRate > 72000.0 ? maxControlBlock : 64;
}

/**
    A model's linear network from its generated topology (tools/netlist). Every control block
    (networkControlBlock(): 64 samples at 44.1 and 48 kHz, 128 above 72 kHz) the moving pots
    glide one step and only the blocks that depend on them are re-designed. The glide is two
    one-pole stages (no corner in a pot's path when it starts to move) in the log ratio of the
    pot's two segments (a wiper near an end, where one segment is a few ohms, moves the circuit by
    a bounded factor per step, as it does mid-travel); a jump is
    within 10 % of its way at 80 ms and 1 % at 0.13 s (tools/netlist/README.md, "Network"). A
    stepped control's detent step glides alike in the log of its component value, within 1 % of
    the step in 10 ms, where the hardware's rotary switch clicks. A block of degree 2 or less is
    one MatchedSection; a higher one (interacting controls) is a RootCascade, its roots tracked
    from design to design. Each block is designed from its
    polynomials normalised, its scale in the output gain; the gain follows the same quartic
    B-spline through the designs as every block's coefficients (BSpline.h), so a block's scale
    never moves ahead of its roots. Real-time safe after prepare(); not thread safe.
*/
template <typename Topology>
class Network
{
public:
    static constexpr int numControls = Topology::numControls;
    static constexpr int maxControlBlock = viridian::dsp::maxControlBlock;
    static constexpr int maxSections = Topology::maxSections;
    static constexpr int maxDegree = Topology::maxDegree;
    static_assert (maxDegree <= poly::maxDegree, "a block of this degree is beyond the root finder");
    using Controls = std::array<double, (size_t) numControls>;

    Network() noexcept
    {
        for (int i = 0; i < numControls; ++i)
        {
            const auto& c = Topology::controls[(size_t) i];
            if (c.kind == NetworkControl::Kind::stepped)
            {
                stepLow[(size_t) i] = *std::min_element (c.steps, c.steps + c.positions);
                stepSpan[(size_t) i] = *std::max_element (c.steps, c.steps + c.positions) - stepLow[(size_t) i];
            }
            target[(size_t) i] = current[(size_t) i] = coordinate (i, c.defaultValue);
        }
    }

    /** Fits the frequency map and designs at the targets. Not real-time safe. */
    void prepare (double sampleRate)
    {
        controlBlock = networkControlBlock (sampleRate);
        map.prepare (sampleRate);
        setSmoothingTime (smoothingSeconds);
        setStepTime (stepSeconds);
        reset();
    }

    /**
        Which part of a shared clock's control block this network designs on: part of parts, so the
        networks of one model spread their designs over the callbacks of a block (align()).
    */
    void setStagger (int part, int parts) noexcept
    {
        staggerPart = part;
        staggerParts = std::max (1, parts);
    }

    /**
        Places the control-block boundaries on a shared clock (the index of the next sample this
        network processes): at clock positions of part / parts of a block. For a network at rest
        after a fresh design, or after clear(): a glide in progress keeps its steps, shifted.
    */
    void align (std::int64_t clock) noexcept
    {
        const auto offset = (std::int64_t) (controlBlock * staggerPart / staggerParts);
        phase = (int) (((clock - offset) % controlBlock + controlBlock) % controlBlock);
    }

    /** Time constant of each of a pot glide's two stages (a jump settles within about six of them). */
    void setSmoothingTime (double seconds) noexcept
    {
        smoothingSeconds = std::max (seconds, 0.0);
        glide = coefficient (smoothingSeconds);
    }

    /** The same for a stepped control's detent steps. */
    void setStepTime (double seconds) noexcept
    {
        stepSeconds = std::max (seconds, 0.0);
        stepGlide = coefficient (stepSeconds);
    }

    /** Pots are clamped to 0 ... 1, switches and stepped controls rounded to a position. */
    void setTargets (const Controls& values) noexcept
    {
        for (int i = 0; i < numControls; ++i)
            target[(size_t) i] = coordinate (i, values[(size_t) i]);
    }

    /** Jumps to the targets, designs from scratch and clears the states. */
    void reset() noexcept
    {
        beginReset();
        while (! resetStep())
        {
        }
    }

    /**
        reset() in bounded steps, for a caller that spreads a fresh design over callbacks: the
        evaluation, then the sections, then each cascade's cold roots and maps a few at a time.
        beginReset(), then resetStep() until it returns true; process() waits until then. The same
        design as reset(), to the bit.
    */
    void beginReset() noexcept
    {
        current = target;
        for (int i = 0; i < numControls; ++i)
            between[(size_t) i] = warp (i, target[(size_t) i]);
        phase = 0;
        resetBlock = -1;
        cascadeStarted = false;
        resetting = true;
    }

    bool resetStep() noexcept
    {
        if (! resetting)
            return true;
        if (resetBlock < 0)
        {
            Topology::evaluate (current.data(), polys);
            arrange();
            ramp = 0;
            freshGain = polys.gain;
            for (int b = 0; b < polys.numBlocks; ++b)
            {
                normalise (b);
                freshGain *= normalised[(size_t) b].scale;
            }
            resetBlock = 0;
            return false;
        }
        if (resetBlock < polys.numBlocks && layouts[(size_t) resetBlock].cascade)
        {
            if constexpr (numCascades > 0)
            {
                const auto& pb = polys.blocks[(size_t) resetBlock];
                const auto& nb = normalised[(size_t) resetBlock];
                auto& c = cascades[(size_t) layouts[(size_t) resetBlock].index];
                if (! cascadeStarted)
                    c.beginFresh (map, nb.num.data(), pb.numDegree, nb.den.data(), pb.denDegree, Topology::omegaRef);
                cascadeStarted = ! c.freshStep();
                if (cascadeStarted)
                    return false;
            }
            ++resetBlock;
        }
        else
            for (; resetBlock < polys.numBlocks && ! layouts[(size_t) resetBlock].cascade; ++resetBlock)
                design (resetBlock, true);
        if (resetBlock < polys.numBlocks)
            return false;

        gain = freshGain;
        gainAtRest();
        for (auto& s : sections)
            s.reset();
        for (auto& c : cascades)
            c.reset();
        resetting = false;
        return true;
    }

    /** Clears the states; the designs and the pots' glides stay. */
    void clear() noexcept
    {
        for (auto& s : sections)
            s.reset();
        for (auto& c : cascades)
            c.reset();
        gainAtRest();
    }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        assert (! resetting);
        std::array<std::array<double, maxControlBlock>, MatchedSection::maxChannels> buffer;
        run (numChannels, numSamples, [&] (int offset, int length, double* const* work)
             {
                 for (int c = 0; c < numChannels; ++c)
                     for (int i = 0; i < length; ++i)
                         work[c][i] = channels[c][offset + i];
                 runSections (work, numChannels, length);
                 for (int c = 0; c < numChannels; ++c)
                     for (int i = 0; i < length; ++i)
                         channels[c][offset + i] = (float) work[c][i];
             }, buffer);
    }

    void process (double* const* channels, int numChannels, int numSamples) noexcept
    {
        assert (! resetting);
        std::array<std::array<double, maxControlBlock>, MatchedSection::maxChannels> buffer;
        run (numChannels, numSamples, [&] (int offset, int length, double* const*)
             {
                 std::array<double*, MatchedSection::maxChannels> at {};
                 for (int c = 0; c < numChannels; ++c)
                     at[(size_t) c] = channels[c] + offset;
                 runSections (at.data(), numChannels, length);
             }, buffer);
    }

    /**
        One channel of this network and one of `other` (the same topology with controls of its
        own: a stereo pair with a spread) in one pass: the same output as processing each, with
        the two channels' cascades in one loop (their recursions overlap). Their control blocks
        may be staggered: the pass runs from either's boundary to the next.
    */
    void processPair (Network& other, double* a, double* b, int numSamples) noexcept
    {
        assert (! resetting && ! other.resetting);
        for (int done = 0; done < numSamples;)
        {
            if (phase == 0)
                beginControlBlock();
            if (other.phase == 0)
                other.beginControlBlock();
            const int length = std::min ({ controlBlock - phase, other.controlBlock - other.phase, numSamples - done });
            double* chA[] { a + done };
            double* chB[] { b + done };
            for (int i = 0; i < activeSections; ++i)
                sections[(size_t) i].process (chA, 1, length);
            for (int i = 0; i < other.activeSections; ++i)
                other.sections[(size_t) i].process (chB, 1, length);
            const bool paired = activeCascades == other.activeCascades;
            for (int i = 0; i < std::max (activeCascades, other.activeCascades); ++i)
            {
                if (paired && RootCascade::processPair (cascades[(size_t) i], chA[0], other.cascades[(size_t) i], chB[0], length))
                    continue;
                if (i < activeCascades)
                    cascades[(size_t) i].process (chA, 1, length);
                if (i < other.activeCascades)
                    other.cascades[(size_t) i].process (chB, 1, length);
            }
            applyGain (chA, 1, length);
            other.applyGain (chB, 1, length);
            phase = (phase + length) % controlBlock;
            other.phase = (other.phase + length) % other.controlBlock;
            done += length;
        }
    }

    /** The realised digital response now: what process() does at rest. */
    std::complex<double> response (double hz) const noexcept
    {
        std::complex<double> h { gain, 0.0 };
        for (int i = 0; i < activeSections; ++i)
            h *= sections[(size_t) i].response (hz);
        for (int i = 0; i < activeCascades; ++i)
            h *= cascades[(size_t) i].response (hz);
        return h;
    }

    /** The analog H (j 2 pi hz) at the current control values. */
    std::complex<double> analogResponse (double hz) const noexcept
    {
        const std::complex<double> sigma { 0.0, 2.0 * std::numbers::pi * hz / Topology::omegaRef };
        std::complex<double> h { polys.gain, 0.0 };
        for (int b = 0; b < polys.numBlocks; ++b)
        {
            const auto& pb = polys.blocks[(size_t) b];
            h *= horner (pb.num, pb.numDegree, sigma) / horner (pb.den, pb.denDegree, sigma);
        }
        return h;
    }

    const Controls& currentControls() const noexcept { return current; }
    bool isSmoothing() const noexcept { return current != target; }
    const FrequencyMap& frequencyMap() const noexcept { return map; }
    int numSections() const noexcept { return activeSections; }
    const MatchedSection& section (int i) const noexcept { return sections[(size_t) i]; }
    int numCascadeBlocks() const noexcept { return activeCascades; }
    const RootCascade& cascade (int i) const noexcept { return cascades[(size_t) i]; }
    double outputGain() const noexcept { return gain; }

private:
    static constexpr int numCascades = maxDegree > 2 ? Topology::maxBlocks : 0;

    /** Where a block runs: one MatchedSection (degree <= 2) or a RootCascade. */
    struct Layout
    {
        int index = -1, numDegree = -1, denDegree = -1;
        bool cascade = false;
    };

    template <size_t N>
    static std::complex<double> horner (const std::array<double, N>& c, int degree, std::complex<double> x) noexcept
    {
        std::complex<double> v {};
        for (int k = degree; k >= 0; --k)
            v = v * x + c[(size_t) k];
        return v;
    }

    template <typename Body, typename Buffer>
    void run (int numChannels, int numSamples, const Body& body, Buffer& buffer) noexcept
    {
        assert (numChannels >= 0 && numChannels <= MatchedSection::maxChannels);
        numChannels = std::min (numChannels, MatchedSection::maxChannels);
        std::array<double*, MatchedSection::maxChannels> work {};
        for (int c = 0; c < MatchedSection::maxChannels; ++c)
            work[(size_t) c] = buffer[(size_t) c].data();
        int done = 0;
        while (done < numSamples)
        {
            if (phase == 0)
                beginControlBlock();
            const int length = std::min (controlBlock - phase, numSamples - done);
            body (done, length, work.data());
            phase = (phase + length) % controlBlock;
            done += length;
        }
    }

    /** Every control block: the glides' next step, and denormals out of the states. */
    void beginControlBlock() noexcept
    {
        update();
        for (int i = 0; i < activeSections; ++i)
            sections[(size_t) i].flushDenormals();
        for (int i = 0; i < activeCascades; ++i)
            cascades[(size_t) i].flushDenormals();
    }

    void runSections (double* const* work, int numChannels, int length) noexcept
    {
        for (int i = 0; i < activeSections; ++i)
            sections[(size_t) i].process (work, numChannels, length);
        for (int i = 0; i < activeCascades; ++i)
            cascades[(size_t) i].process (work, numChannels, length);
        applyGain (work, numChannels, length);
    }

    void applyGain (double* const* work, int numChannels, int length) noexcept
    {
        // The gain follows the blocks' spline: a block's scale and its roots move together.
        for (int i = 0; i < length; ++i)
        {
            if (gainPosition == gainRamp && gainSettle > 0)
            {
                shiftGain();
                gainPosition = 0;
                --gainSettle;
            }
            if (gainPosition < gainRamp)
            {
                ++gainPosition;
                if (gainPosition == gainRamp && gainSettle == 0)
                    gainNow = gain;
                else
                {
                    std::array<double, spline::points> w;
                    spline::weights ((double) gainPosition / (double) gainRamp, w);
                    gainNow = w.back() * gain;
                    for (size_t q = 0; q < gainPast.size(); ++q)
                        gainNow += w[q] * gainPast[q];
                }
            }
            for (int c = 0; c < numChannels; ++c)
                work[c][i] *= gainNow;
        }
    }

    void update() noexcept
    {
        std::uint32_t changed = 0;
        bool switched = false;
        for (int i = 0; i < numControls; ++i)
        {
            auto& c = current[(size_t) i];
            const auto t = target[(size_t) i];
            if (myzel::dsp::isExactly (c, t))
                continue;
            changed |= 1u << i;
            if (Topology::controls[(size_t) i].kind == NetworkControl::Kind::selector)
            {
                c = t;
                switched = true;
            }
            else
            {
                const auto g = Topology::controls[(size_t) i].kind == NetworkControl::Kind::stepped ? stepGlide : glide;
                const auto ut = warp (i, t);
                auto& m = between[(size_t) i];
                m += (ut - m) * g;
                if (std::abs (ut - m) < 1.0e-9)
                    m = ut;
                const auto uc = warp (i, c);
                c = unwarp (i, uc + (m - uc) * g);
                if (myzel::dsp::isExactly (m, ut) && std::abs (t - c) < 1.0e-7)
                    c = t;
            }
        }
        if (changed == 0)
            return;

        Topology::evaluate (current.data(), polys);
        const bool relaid = arrange();
        const bool fresh = switched || relaid;
        ramp = fresh ? 0 : controlBlock;
        auto total = polys.gain;
        for (int b = 0; b < polys.numBlocks; ++b)
        {
            normalise (b);
            total *= normalised[(size_t) b].scale;
            if (fresh || (polys.blocks[(size_t) b].controls & changed) != 0)
                design (b, fresh);
        }

        if (ramp == 0)
        {
            gain = total;
            gainAtRest();
            return;
        }
        shiftGain();
        gain = total;
        gainRamp = ramp;
        gainPosition = 0;
        gainSettle = spline::settleSegments;
    }

    void gainAtRest() noexcept
    {
        gainNow = gain;
        gainPast.fill (gain);
        gainRamp = gainPosition = gainSettle = 0;
    }

    void shiftGain() noexcept
    {
        for (size_t k = 0; k + 1 < gainPast.size(); ++k)
            gainPast[k] = gainPast[k + 1];
        gainPast.back() = gain;
    }

    /** Places each block; true when the layout changed (designs start from scratch). */
    bool arrange() noexcept
    {
        bool changed = polys.numBlocks != activeBlocks;
        int nextSection = 0, nextCascade = 0;
        for (int b = 0; b < polys.numBlocks; ++b)
        {
            const auto& pb = polys.blocks[(size_t) b];
            auto& l = layouts[(size_t) b];
            const bool cascade = pb.denDegree > 2;
            const int index = cascade ? nextCascade++ : nextSection++;
            if (l.index != index || l.cascade != cascade || l.denDegree != pb.denDegree || l.numDegree != pb.numDegree)
                changed = true;
            l = { index, pb.numDegree, pb.denDegree, cascade };
        }
        assert (nextSection <= maxSections && nextCascade <= numCascades);
        activeBlocks = polys.numBlocks;
        activeSections = nextSection;
        activeCascades = nextCascade;
        return changed;
    }

    /**
        A block's polynomials over their Euclidean norms, the ratio moved into the output gain, so
        evaluate()'s arbitrary scaling never moves a block against the gain (tools/netlist/README.md,
        "Network").
    */
    void normalise (int b) noexcept
    {
        const auto& pb = polys.blocks[(size_t) b];
        auto& n = normalised[(size_t) b];
        double numNorm = 0.0, denNorm = 0.0;
        for (int k = 0; k <= pb.numDegree; ++k)
            numNorm += pb.num[(size_t) k] * pb.num[(size_t) k];
        for (int k = 0; k <= pb.denDegree; ++k)
            denNorm += pb.den[(size_t) k] * pb.den[(size_t) k];
        numNorm = numNorm > 0.0 ? std::sqrt (numNorm) : 1.0;
        denNorm = denNorm > 0.0 ? std::sqrt (denNorm) : 1.0;
        for (int k = 0; k <= maxDegree; ++k)
        {
            n.num[(size_t) k] = k <= pb.numDegree ? pb.num[(size_t) k] / numNorm : 0.0;
            n.den[(size_t) k] = k <= pb.denDegree ? pb.den[(size_t) k] / denNorm : 0.0;
        }
        n.scale = numNorm / denNorm;
    }

    void design (int b, bool fresh) noexcept
    {
        const auto& pb = polys.blocks[(size_t) b];
        const auto& nb = normalised[(size_t) b];
        const auto& l = layouts[(size_t) b];
        if (l.cascade)
        {
            if constexpr (numCascades > 0)
                cascades[(size_t) l.index].design (map, nb.num.data(), pb.numDegree, nb.den.data(), pb.denDegree,
                                                   Topology::omegaRef, ramp, fresh);
            return;
        }
        AnalogSection s;
        s.omegaScale = Topology::omegaRef;
        s.num = { 0.0, 0.0, 0.0 };
        s.den = { 0.0, 0.0, 0.0 };
        for (int k = 0; k <= std::min (pb.numDegree, 2); ++k)
            s.num[(size_t) k] = nb.num[(size_t) k];
        for (int k = 0; k <= pb.denDegree; ++k)
            s.den[(size_t) k] = nb.den[(size_t) k];
        // From scratch means without the history of earlier designs: a reset renders the same
        // whatever ran before it.
        if (fresh)
            sections[(size_t) l.index].restart();
        sections[(size_t) l.index].design (map, s, ramp);
    }

    double coefficient (double seconds) const noexcept
    {
        const auto fs = map.sampleRate() > 0.0 ? map.sampleRate() : 48000.0;
        return seconds > 0.0 ? 1.0 - std::exp (-controlBlock / (seconds * fs)) : 1.0;
    }

    /** What evaluate() takes for a control's value: a pot's position, a switch's, a stepped control's coordinate. */
    double coordinate (int i, double v) const noexcept
    {
        const auto& c = Topology::controls[(size_t) i];
        if (c.kind == NetworkControl::Kind::pot)
            return std::clamp (v, 0.0, 1.0);
        const auto position = std::clamp (std::round (v), 0.0, (double) (c.positions - 1));
        if (c.kind == NetworkControl::Kind::selector)
            return position;
        return (c.steps[(size_t) position] - stepLow[(size_t) i]) / stepSpan[(size_t) i];
    }

    /**
        The glide's coordinate: for a pot, ln of its segments' ratio, each kept above 2 % of the pot;
        for a stepped control, ln of its component value.
    */
    static constexpr double segmentFloor = 0.02;
    double warp (int i, double x) const noexcept
    {
        if (Topology::controls[(size_t) i].kind == NetworkControl::Kind::stepped)
            return std::log (stepLow[(size_t) i] + x * stepSpan[(size_t) i]);
        return std::log ((x + segmentFloor) / (1.0 - x + segmentFloor));
    }

    double unwarp (int i, double u) const noexcept
    {
        const auto e = std::exp (u);
        if (Topology::controls[(size_t) i].kind == NetworkControl::Kind::stepped)
            return std::clamp ((e - stepLow[(size_t) i]) / stepSpan[(size_t) i], 0.0, 1.0);
        return std::clamp ((e * (1.0 + segmentFloor) - segmentFloor) / (1.0 + e), 0.0, 1.0);
    }

    FrequencyMap map;
    int controlBlock = 64, staggerPart = 0, staggerParts = 1;
    Controls target {}, current {};
    Controls between {};        // the glide's first stage, warped
    double smoothingSeconds = 0.02, glide = 1.0;
    double stepSeconds = 0.00125, stepGlide = 1.0;      // two stages of 1.25 ms: a detent step within 1 % in 10 ms
    Controls stepLow {}, stepSpan {};
    double gain = 1.0, gainNow = 1.0;
    std::array<double, spline::points - 1> gainPast { 1.0, 1.0, 1.0, 1.0 };     // the gain's spline, as a block's
    int gainRamp = 0, gainPosition = 0, gainSettle = 0;
    double freshGain = 1.0;
    int resetBlock = -1;
    bool resetting = false, cascadeStarted = false;
    int phase = 0, activeBlocks = 0, activeSections = 0, activeCascades = 0, ramp = 0;
    struct Normalised
    {
        std::array<double, (size_t) maxDegree + 1> num {}, den {};
        double scale = 1.0;
    };

    typename Topology::Polynomials polys;
    std::array<Normalised, (size_t) Topology::maxBlocks> normalised {};
    std::array<Layout, (size_t) Topology::maxBlocks> layouts {};
    std::array<MatchedSection, (size_t) maxSections> sections {};
    std::array<RootCascade, (size_t) numCascades> cascades {};
};

} // namespace viridian::dsp
