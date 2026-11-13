#pragma once

#include "dsp/modules/crush/CrushVoicing.h"

#include "common/dsp/FloatCompare.h"
#include "common/dsp/TapeCore.h"
#include "common/dsp/TapeLosses.h"
#include "common/dsp/TriodeStage.h"

#include <array>
#include <cmath>
#include <complex>
#include <numbers>

namespace inkcap::dsp::crush
{

inline double flushed (double v) noexcept { return std::abs (v) < 1.0e-30 ? 0.0 : v; }

/** 2-pole TPT low-pass (Simper's SVF), double precision. */
class Lowpass2
{
public:
    void setup (double hz, double q, double sampleRate) noexcept
    {
        g = std::tan (std::numbers::pi * std::min (hz, 0.49 * sampleRate) / sampleRate);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0; }
    void flush() noexcept { ic1 = flushed (ic1); ic2 = flushed (ic2); }

    double process (double x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return v2;
    }

private:
    double g = 0.0, k = 2.0, a1 = 1.0, a2 = 0.0, a3 = 0.0, ic1 = 0.0, ic2 = 0.0;
};

/** 1-pole TPT low-pass; the high-pass is the input minus it. */
class Lowpass1
{
public:
    void setup (double hz, double sampleRate) noexcept
    {
        const auto g = std::tan (std::numbers::pi * std::min (hz, 0.49 * sampleRate) / sampleRate);
        G = g / (1.0 + g);
    }

    void reset() noexcept { s = 0.0; }
    void flush() noexcept { s = flushed (s); }

    double process (double x) noexcept
    {
        const auto v = (x - s) * G;
        const auto lp = v + s;
        s = lp + v;
        return lp;
    }

private:
    double G = 0.0, s = 0.0;
};

/**
    The band split every type shares (NOTES.md, Aliasing): the stage saturates the band below the
    type's split (a 2-pole low-pass, Q 0.5, so the two bands sum to the input with a gentle +1 dB
    at the split), the band above passes beside it. What reaches the nonlinearity at 7 kHz stays
    small enough at 2x that its folded harmonics sit below -80 dBc.
*/
struct Band
{
    double low = 0.0, high = 0.0;
};

class Splitter
{
public:
    void setup (double hz, double sampleRate) noexcept { lowpass.setup (hz, 0.5, sampleRate); }
    void reset() noexcept { lowpass.reset(); }
    void flush() noexcept { lowpass.flush(); }

    Band process (double x) noexcept
    {
        const auto low = lowpass.process (x);
        return { low, x - low };
    }

private:
    Lowpass2 lowpass;
};

/**
    What a stage reports for the display: how far its output is from its own input in the band
    it saturates (the distortion's energy against the band's), summed over a block.
*/
struct Wear
{
    double change = 0.0, band = 0.0;

    void add (double in, double out) noexcept
    {
        change += (out - in) * (out - in);
        band += in * in;
    }
};

/** Per-sample values every stage reads: the drive (linear) and the intensity (0 .. 1). */
struct StageFrame
{
    double drive = 1.0;
    double intensity = 0.0;
};

/**
    A tape head's playback losses and bump: TapeLosses' fitted prototypes (gap, spacing,
    thickness, the contour bump, the normalisation; common/dsp/TapeLosses.h), realised as TPT
    sections (each corner prewarped, see gain()), so the head's lengths can move with Damage
    every few samples without a tick. TapeLosses' own direct-form sections click when
    redesigned under a moving intensity (NOTES.md, The head). response() is the exact digital
    response, for the tests.
*/
class HeadLosses
{
public:
    using Losses = myzel::dsp::TapeLosses;

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        scale = Losses::bilinearScale (fs);
    }

    void reset() noexcept
    {
        for (auto& g : gap)
            g.ic1 = g.ic2 = 0.0;
        for (auto& f : first)
            f.s = 0.0;
        bump.ic1 = bump.ic2 = 0.0;
    }

    void flush() noexcept
    {
        for (auto& g : gap)
        {
            g.ic1 = flushed (g.ic1);
            g.ic2 = flushed (g.ic2);
        }
        for (auto& f : first)
            f.s = flushed (f.s);
        bump.ic1 = flushed (bump.ic1);
        bump.ic2 = flushed (bump.ic2);
    }

    void design (const myzel::dsp::TapeGeometry& t, double speed) noexcept
    {
        const auto v = std::max (speed, 1.0e-4);
        for (size_t i = 0; i < gap.size(); ++i)
            gap[i].set (gain (Losses::gapFit[i][0] * v / t.gap), 1.0 / Losses::gapFit[i][1]);

        size_t j = 0;
        for (const auto& shelf : Losses::spacingShelves)
            first[j++].set (gain (shelf[0] * v / t.spacing), gain (shelf[1] * v / t.spacing));
        for (const auto& shelf : Losses::thicknessShelves)
            first[j++].set (gain (shelf[0] * v / t.thickness), gain (shelf[1] * v / t.thickness));
        first[j++].set (gain (Losses::spacingPole * v / t.spacing), 0.0);
        first[j++].set (gain (Losses::thicknessPole * v / t.thickness), 0.0);

        bump.set (std::tan (std::numbers::pi * std::min (v / t.bumpWavelength, 0.45 * fs) / fs), 1.0 / Losses::bumpQ);
        norm = 1.0 / Losses::bumpPeak();
    }

    double process (double x) noexcept
    {
        for (auto& g : gap)
            x = g.process (x).lp;
        for (auto& f : first)
            x = f.process (x);
        const auto b = bump.process (x);
        return norm * (x - bump.k * b.bp - b.lp);
    }

    std::complex<double> response (double hz) const noexcept
    {
        const auto t = std::tan (std::numbers::pi * std::min (hz, 0.4999 * fs) / fs);
        const std::complex<double> sb { 0.0, t / bump.g };
        return norm * lossResponse (hz) * sb * sb / (sb * sb + bump.k * sb + 1.0);
    }

    /** The group delay at hz (samples of this rate): the delay Mix's dry takes to sit in phase with the head. */
    double groupDelay (double hz) const noexcept
    {
        const auto step = 0.01 * hz;
        const auto turn = std::arg (response (hz + step) / response (hz - step));
        return -turn / (2.0 * std::numbers::pi * 2.0 * step / fs);
    }

    /** The response without the bump and the normalisation: what the fits approximate. */
    std::complex<double> lossResponse (double hz) const noexcept
    {
        const auto t = std::tan (std::numbers::pi * std::min (hz, 0.4999 * fs) / fs);
        std::complex<double> h { 1.0, 0.0 };
        for (const auto& g : gap)
        {
            const std::complex<double> x { 0.0, t / g.g };
            h /= x * x + g.k * x + 1.0;
        }
        for (const auto& f : first)
        {
            const std::complex<double> x { 0.0, t / f.g };
            h *= (1.0 + f.ratio * x) / (1.0 + x);
        }
        return h;
    }

private:
    /**
        A corner's TPT gain: prewarped to the corner itself (exact there) up to 0.2 fs, then
        blended (TapeLosses' own blend) into its bilinear scale matched at 16 kHz, so corners far
        above the band never fold and the gain never jumps as a corner moves. Accurate at 2x
        (fs >= 88.2 kHz); at 44.1 kHz the top octave would be off by dB, as TapeLosses' own.
    */
    double gain (double hz) const noexcept
    {
        const auto w = Losses::blendWeight (hz, fs);
        const auto linear = 2.0 * std::numbers::pi * hz / scale;
        if (w >= 1.0)
            return linear;
        const auto warped = std::tan (std::numbers::pi * hz / fs);
        return warped + w * (linear - warped);
    }

    struct Svf
    {
        double g = 0.0, k = 1.0, a1 = 1.0, a2 = 0.0, a3 = 0.0, ic1 = 0.0, ic2 = 0.0;

        void set (double gain, double damping) noexcept
        {
            g = gain;
            k = damping;
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        struct Out { double lp, bp; };

        Out process (double x) noexcept
        {
            const auto v3 = x - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            return { v2, v1 };
        }
    };

    /** (1 + s / wz) / (1 + s / wp): a shelf, or a plain pole (zeroGain 0: no zero). */
    struct Shelf
    {
        double g = 0.0, G = 0.0, ratio = 0.0, s = 0.0;

        void set (double poleGain, double zeroGain) noexcept
        {
            g = poleGain;
            G = g / (1.0 + g);
            ratio = zeroGain > 0.0 ? g / zeroGain : 0.0;
        }

        double process (double x) noexcept
        {
            const auto v = (x - s) * G;
            const auto lp = v + s;
            s = lp + v;
            return lp + ratio * (x - lp);
        }
    };

    double fs = 96000.0, scale = 1.0, norm = 1.0;
    std::array<Svf, 2> gap {};
    std::array<Shelf, Losses::numFirstOrder> first {};
    Svf bump {};
};

/**
    Tape and Cassette: the Jiles-Atherton coating (common/dsp/TapeCore, Mulberry's solver of the
    shared equations), then the head's playback losses and bump (HeadLosses, from
    common/dsp/TapeLosses), both at 2x, where the head's sections hold the closed forms. Damage
    wears the tape in: the head's lengths grow with the intensity (no loss at 0, the voicing's at
    1) and the bump's wavelength shrinks with it (at 0 it sits at 0 Hz), so a lighter touch is
    simply a better tape, never a different filter sweeping through the band.
*/
class TapeStage
{
public:
    explicit TapeStage (TapeVoicing v, const Voicing& s) noexcept : voicing (v), shape (s) {}

    void prepare (double sampleRate) noexcept
    {
        split.setup (shape.splitHz, sampleRate);
        core.setParameters (voicing.core);
        susceptibility = core.initialSusceptibility();
        head.prepare (sampleRate);
        designed = -1.0;
        update (0.0);
        reset();
    }

    void reset() noexcept
    {
        split.reset();
        core.reset();
        head.reset();
    }

    void flush() noexcept
    {
        split.flush();
        head.flush();
    }

    /** The head at an intensity: its lengths times the intensity, the bump's wavelength over it. */
    static myzel::dsp::TapeGeometry wornHead (const myzel::dsp::TapeGeometry& full, double intensity) noexcept
    {
        const auto i = std::clamp (intensity, 1.0e-3, 1.0);
        auto g = full;
        g.gap *= i;
        g.spacing *= i;
        g.thickness *= i;
        g.bumpWavelength /= i;
        return g;
    }

    void update (double intensity) noexcept
    {
        if (myzel::dsp::isExactly (intensity, designed))
            return;
        designed = intensity;
        head.design (wornHead (voicing.geometry, intensity), voicing.speed);
    }

    const HeadLosses& getHead() const noexcept { return head; }

    double process (double x, const StageFrame& f, Wear& wear) noexcept
    {
        const auto [low, high] = split.process (x);
        const auto in = shape.level * f.drive;
        const auto m = core.process (low * in);
        const auto saturated = m / (in * susceptibility);
        wear.add (low, saturated);
        const auto highShare = 1.0 + f.intensity * (shape.highShare - 1.0);
        return head.process (saturated + highShare * high);
    }

private:
    TapeVoicing voicing;
    Voicing shape;
    double susceptibility = 0.3, designed = -1.0;
    Splitter split;
    myzel::dsp::TapeCore core;
    HeadLosses head;
};

/**
    Valve: one 12AX7 stage solved at circuit level (common/dsp/TriodeStage, validated in Umber's
    valve/NOTES.md). Its small-signal gain at 1 kHz is divided out (the stage inverts).
*/
class ValveStage
{
public:
    explicit ValveStage (const Voicing& s) noexcept : shape (s) {}

    void prepare (double sampleRate)
    {
        split.setup (shape.splitHz, sampleRate);
        stage.prepare (valveComponents(), sampleRate);
        const auto h = stage.smallSignalGain (1000.0);
        gain = (h.real() < 0.0 ? -1.0 : 1.0) * std::abs (h);
        reset();
    }

    void reset() noexcept
    {
        split.reset();
        stage.reset();
    }

    void flush() noexcept
    {
        split.flush();
        stage.flushTinyStates();
    }

    void update (double) noexcept {}

    double process (double x, const StageFrame& f, Wear& wear) noexcept
    {
        const auto [low, high] = split.process (x);
        const auto in = shape.level * f.drive;
        const auto saturated = stage.process (low * in) / (in * gain);
        wear.add (low, saturated);
        const auto highShare = 1.0 + f.intensity * (shape.highShare - 1.0);
        return saturated + highShare * high;
    }

private:
    Voicing shape;
    Splitter split;
    myzel::dsp::tubes::TriodeStage stage;
    double gain = 1.0;
};

/**
    Fuzz: our own transistor clipper, f (x) = tanh (x + b) - tanh (b), antialiased to first order
    (ADAA: the mean of f over the straight line between two samples, from its antiderivative
    log cosh (x + b) - tanh (b) x). Then the collector's bandwidth and the coupling cap.
*/
class FuzzStage
{
public:
    explicit FuzzStage (const Voicing& s) noexcept : shape (s) {}

    void prepare (double sampleRate) noexcept
    {
        split.setup (shape.splitHz, sampleRate);
        collector.setup (FuzzVoicing::collectorHz, sampleRate);
        coupling.setup (FuzzVoicing::couplingHz, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        split.reset();
        collector.reset();
        coupling.reset();
        previous = 0.0;
        previousIntegral = antiderivative (0.0);
    }

    void flush() noexcept
    {
        split.flush();
        collector.flush();
        coupling.flush();
    }

    void update (double) noexcept {}

    static double curve (double x) noexcept { return std::tanh (x + FuzzVoicing::bias) - std::tanh (FuzzVoicing::bias); }

    static double antiderivative (double x) noexcept
    {
        // log cosh y = |y| + log1p (exp (-2 |y|)) - log 2, without overflow.
        const auto y = std::abs (x + FuzzVoicing::bias);
        return y + std::log1p (std::exp (-2.0 * y)) - std::numbers::ln2 - std::tanh (FuzzVoicing::bias) * x;
    }

    static double slopeAtRest() noexcept
    {
        const auto t = std::tanh (FuzzVoicing::bias);
        return 1.0 - t * t;
    }

    double process (double x, const StageFrame& f, Wear& wear) noexcept
    {
        const auto [low, high] = split.process (x);
        const auto in = shape.level * f.drive;
        const auto u = low * in;

        const auto integral = antiderivative (u);
        const auto du = u - previous;
        // Below this step the difference quotient loses more digits than the midpoint's error.
        const auto clipped = std::abs (du) > 1.0e-6 ? (integral - previousIntegral) / du : curve (0.5 * (u + previous));
        previous = u;
        previousIntegral = integral;

        const auto saturated = clipped / (in * slopeAtRest());
        wear.add (low, saturated);
        const auto shaped = collector.process (saturated);
        const auto settled = shaped - coupling.process (shaped);
        const auto highShare = 1.0 + f.intensity * (shape.highShare - 1.0);
        return settled + highShare * high;
    }

private:
    Voicing shape;
    Splitter split;
    Lowpass1 collector, coupling;
    double previous = 0.0, previousIntegral = 0.0;
};

} // namespace inkcap::dsp::crush
