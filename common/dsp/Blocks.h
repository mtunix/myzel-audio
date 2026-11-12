#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

/**
    Float building blocks for delay-based effects (Cerulean's reverbs, Mulberry's echoes).

    All of them are allocation-free after prepare()/setMaxDelay(), deterministic and cheap.
    Conventions:
      - Delay lengths are in samples (float). Convert from ms with msToSamples().
      - DelayLine::read (d) returns the sample written d samples ago (d = 1: the most recent one).
        In a feedback loop, read first, then write.
*/
namespace myzel::dsp
{

inline constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;

inline float msToSamples (float ms, double sampleRate) noexcept
{
    return ms * 0.001f * (float) sampleRate;
}

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

/** Flushes tiny values to zero (cheap denormal insurance inside long feedback loops). */
inline float flushTiny (float x) noexcept
{
    return std::abs (x) < 1.0e-20f ? 0.0f : x;
}

//==============================================================================
/**
    A value that moves linearly to a new target over one sub-block, so gains and coefficients
    change per sample instead of in steps (steps every 32 samples are audible as ticks).
    The first set() after reset() jumps straight to the target.
*/
class LinearRamp
{
public:
    void reset() noexcept { primed = false; }

    void set (float target, int numSamples) noexcept
    {
        if (! primed)
        {
            value = target;
            step = 0.0f;
            primed = true;
            return;
        }
        step = (target - value) / (float) std::max (numSamples, 1);
    }

    float next() noexcept
    {
        value += step;
        return value;
    }

    float get() const noexcept { return value; }

private:
    float value = 0.0f, step = 0.0f;
    bool primed = false;
};

//==============================================================================
/** Small, fast, deterministic PRNG (xorshift32). Never seed with 0. */
class Rng
{
public:
    explicit Rng (uint32_t seed = 0x9E3779B9u) noexcept { setSeed (seed); }

    void setSeed (uint32_t seed) noexcept { state = seed != 0 ? seed : 0x9E3779B9u; }

    uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** Uniform in [0, 1). */
    float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }

    /** Uniform in [-1, 1). */
    float bipolar() noexcept { return 2.0f * uniform() - 1.0f; }

private:
    uint32_t state = 0x9E3779B9u;
};

//==============================================================================
/** Circular delay line with integer, linear and 3rd-order Lagrange reads. */
class DelayLine
{
public:
    /** Allocates. maxDelaySamples is the longest delay that will be read. */
    void setMaxDelay (int maxDelaySamples)
    {
        int size = 4;
        while (size < maxDelaySamples + 4)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writeIndex = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    int getMaxDelay() const noexcept { return mask - 3; }

    void write (float x) noexcept
    {
        buffer[(size_t) writeIndex] = x;
        writeIndex = (writeIndex + 1) & mask;
    }

    /** d >= 1. */
    float read (int d) const noexcept
    {
        return buffer[(size_t) ((writeIndex - d) & mask)];
    }

    /** d >= 1. */
    float readLinear (float d) const noexcept
    {
        const auto i = (int) d;
        const auto f = d - (float) i;
        const auto a = read (i);
        const auto b = read (i + 1);
        return a + f * (b - a);
    }

    /**
        3rd-order Lagrange interpolation, centred (reads d-1 .. d+2), so |H| <= 1 at every
        fractional delay: safe inside lossless loops. d is clamped to >= 2.
    */
    float readCubic (float d) const noexcept
    {
        d = std::max (d, 2.0f);
        const auto i = (int) d;
        const auto f = d - (float) i;

        const auto xm1 = read (i - 1);
        const auto x0  = read (i);
        const auto x1  = read (i + 1);
        const auto x2  = read (i + 2);

        // Lagrange basis for points at offsets -1, 0, 1, 2 evaluated at f.
        const auto fm1 = f - 1.0f, fm2 = f - 2.0f, fp1 = f + 1.0f;
        const auto c0 = -f * fm1 * fm2 * (1.0f / 6.0f);
        const auto c1 = fp1 * fm1 * fm2 * 0.5f;
        const auto c2 = -fp1 * f * fm2 * 0.5f;
        const auto c3 = fp1 * f * fm1 * (1.0f / 6.0f);
        return c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2;
    }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writeIndex = 0;
};

//==============================================================================
/**
    Schroeder allpass: w[n] = x[n] + g w[n-D], y[n] = w[n-D] - g w[n].
    The delay may be fractional and modulated (Lagrange read).
*/
class Allpass
{
public:
    void setMaxDelay (int maxDelaySamples) { line.setMaxDelay (maxDelaySamples); }
    void reset() noexcept { line.reset(); }

    float process (float x, float delaySamples, float g) noexcept
    {
        const auto delayed = line.readCubic (delaySamples);
        const auto w = x + g * delayed;
        line.write (flushTiny (w));
        return delayed - g * w;
    }

    /** Integer delay, cheaper (for static diffusers). */
    float processFixed (float x, int delaySamples, float g) noexcept
    {
        const auto delayed = line.read (delaySamples);
        const auto w = x + g * delayed;
        line.write (flushTiny (w));
        return delayed - g * w;
    }

    /** Reads inside the allpass's delay memory (for multi-tap outputs, Dattorro style). */
    float tap (int d) const noexcept { return line.read (d); }

private:
    DelayLine line;
};

//==============================================================================
/** 1-pole TPT filter with lowpass and highpass outputs (lp + hp == input). */
class OnePole
{
public:
    void setCutoff (float hz, double sampleRate) noexcept
    {
        const auto fc = std::clamp (hz, 1.0f, 0.49f * (float) sampleRate);
        const auto g = std::tan (std::numbers::pi_v<float> * fc / (float) sampleRate);
        G = g / (1.0f + g);
    }

    /** The TPT coefficient for a cutoff, so several filters can share one tan(). */
    static float coefficientFor (float hz, double sampleRate) noexcept
    {
        const auto fc = std::clamp (hz, 1.0f, 0.49f * (float) sampleRate);
        const auto g = std::tan (std::numbers::pi_v<float> * fc / (float) sampleRate);
        return g / (1.0f + g);
    }

    void setCoefficient (float coefficient) noexcept { G = coefficient; }

    void reset() noexcept { s = 0.0f; }

    float lowpass (float x) noexcept
    {
        const auto v = (x - s) * G;
        const auto lp = v + s;
        s = flushTiny (lp + v);
        return lp;
    }

    float highpass (float x) noexcept { return x - lowpass (x); }

private:
    float G = 0.0f, s = 0.0f;
};

//==============================================================================
/** 2-pole TPT state-variable filter (Simper), float. */
class Svf
{
public:
    void setCoefficients (float hz, float q, double sampleRate) noexcept
    {
        const auto fc = std::clamp (hz, 1.0f, 0.49f * (float) sampleRate);
        g = std::tan (std::numbers::pi_v<float> * fc / (float) sampleRate);
        k = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    struct Out { float lp, bp, hp; };

    Out process (float x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushTiny (2.0f * v1 - ic1);
        ic2 = flushTiny (2.0f * v2 - ic2);
        return { v2, v1, x - k * v1 - v2 };
    }

    float lowpass (float x) noexcept  { return process (x).lp; }
    float highpass (float x) noexcept { return process (x).hp; }
    float bandpass (float x) noexcept { return process (x).bp; }

private:
    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

//==============================================================================
/**
    Smooth random LFO: a random walk between seeded random targets, cosine-interpolated.
    Output in [-1, 1]. Rate is the number of new targets per second.
*/
class SmoothRandom
{
public:
    void setSeed (uint32_t seed) noexcept { rng.setSeed (seed); startSeed = seed; reset(); }

    void reset() noexcept
    {
        rng.setSeed (startSeed);
        from = rng.bipolar();
        to = rng.bipolar();
        phase = rng.uniform();
    }

    void setRate (float hz, double sampleRate) noexcept { increment = std::max (hz, 0.0f) / (float) sampleRate; }

    float next() noexcept
    {
        phase += increment;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            from = to;
            to = rng.bipolar();
        }
        const auto w = 0.5f - 0.5f * std::cos (std::numbers::pi_v<float> * phase);
        return from + (to - from) * w;
    }

    /** Advances by n samples at once (control-rate use). */
    float advance (int n) noexcept
    {
        phase += increment * (float) (n - 1);
        while (phase >= 1.0f)
        {
            phase -= 1.0f;
            from = to;
            to = rng.bipolar();
        }
        return next();
    }

private:
    Rng rng;
    uint32_t startSeed = 1;
    float from = 0.0f, to = 0.0f, phase = 0.0f, increment = 0.0f;
};

/** Sine LFO with a settable start phase. Output in [-1, 1]. */
class SineLfo
{
public:
    void setStartPhase (float phase01) noexcept { startPhase = phase01; phase = phase01; }
    void reset() noexcept { phase = startPhase; }
    void setRate (float hz, double sampleRate) noexcept { increment = hz / (float) sampleRate; }

    float next() noexcept
    {
        const auto y = std::sin (twoPi * phase);
        phase += increment;
        phase -= std::floor (phase);
        return y;
    }

    float advance (int n) noexcept
    {
        phase += increment * (float) (n - 1);
        phase -= std::floor (phase);
        return next();
    }

private:
    float phase = 0.0f, startPhase = 0.0f, increment = 0.0f;
};

//==============================================================================
/**
    A delay length that glides to its target: one-pole approach (time constant tau) with a slew
    limit, so Size changes sound like a tape-speed glide and never click.
*/
class GlideValue
{
public:
    void prepare (double sampleRate, float tauMs = 220.0f, float maxSlewPerSample = 0.12f) noexcept
    {
        coeff = 1.0 - std::exp (-1.0 / ((double) tauMs * 0.001 * sampleRate));
        maxSlew = maxSlewPerSample;
    }

    void snap (float v) noexcept { value = target = v; }
    void setTarget (float v) noexcept { target = v; }
    float getTarget() const noexcept { return (float) target; }
    float get() const noexcept { return (float) value; }

    float next() noexcept
    {
        value += std::clamp ((target - value) * coeff, -maxSlew, maxSlew);
        if (std::abs (target - value) < 1.0e-6)
            value = target;
        return (float) value;
    }

    /** n samples at once. */
    float advance (int n) noexcept
    {
        if (value >= target && value <= target)
            return (float) value;

        for (int i = 0; i < n; ++i)
            next();
        return (float) value;
    }

private:
    // Double precision: delay lengths are thousands of samples, where a float step of
    // (target - value) * coeff falls below the float spacing and the glide would stall short of
    // its target. A stalled fractional length makes a frozen loop lose treble on every pass.
    double value = 0.0, target = 0.0, coeff = 1.0, maxSlew = 1.0;
};

/** One-pole parameter smoother (per sample or per block). */
class Smoother
{
public:
    void prepare (double rate, float timeMs) noexcept
    {
        coeff = 1.0f - std::exp (-1.0f / (std::max (timeMs, 0.01f) * 0.001f * (float) rate));
    }

    void snap (float v) noexcept { value = v; }
    float get() const noexcept { return value; }

    float next (float target) noexcept
    {
        value += (target - value) * coeff;
        if (std::abs (target - value) < 1.0e-7f)
            value = target;
        return value;
    }

private:
    float value = 0.0f, coeff = 1.0f;
};

//==============================================================================
/** Peak envelope follower with separate attack and release (linear magnitude out). */
class EnvelopeFollower
{
public:
    void setTimes (float attackMs, float releaseMs, double sampleRate) noexcept
    {
        attack = 1.0f - std::exp (-1.0f / (std::max (attackMs, 0.01f) * 0.001f * (float) sampleRate));
        release = 1.0f - std::exp (-1.0f / (std::max (releaseMs, 0.01f) * 0.001f * (float) sampleRate));
    }

    void reset() noexcept { env = 0.0f; }

    float process (float x) noexcept
    {
        const auto a = std::abs (x);
        env += (a - env) * (a > env ? attack : release);
        env = flushTiny (env);
        return env;
    }

    float get() const noexcept { return env; }

private:
    float attack = 1.0f, release = 1.0f, env = 0.0f;
};

//==============================================================================
/**
    A chain of Schroeder allpasses for one channel: the classic input diffuser.
    Lengths are nominal ms, scaled by Size; the diffusion amount sets the coefficient.
*/
template <int Stages>
class AllpassChain
{
public:
    void prepare (double sampleRate, const std::array<float, (size_t) Stages>& nominalMs, float maxScale)
    {
        fs = sampleRate;
        nominal = nominalMs;
        for (int i = 0; i < Stages; ++i)
        {
            stages[(size_t) i].setMaxDelay ((int) std::ceil (msToSamples (nominalMs[(size_t) i] * maxScale, sampleRate)) + 4);
            lengths[(size_t) i].prepare (sampleRate);
            lengths[(size_t) i].snap (msToSamples (nominalMs[(size_t) i], sampleRate));
            current[(size_t) i] = lengths[(size_t) i].get();
        }
    }

    void reset() noexcept
    {
        for (int i = 0; i < Stages; ++i)
        {
            stages[(size_t) i].reset();
            lengths[(size_t) i].snap (lengths[(size_t) i].getTarget());
            current[(size_t) i] = lengths[(size_t) i].get();
            increment[(size_t) i] = 0.0f;
        }
        snapNext = true;
    }

    void setScale (float scale) noexcept
    {
        for (int i = 0; i < Stages; ++i)
        {
            const auto target = std::max (2.0f, msToSamples (nominal[(size_t) i] * scale, fs));
            if (snapNext)
                lengths[(size_t) i].snap (target);
            else
                lengths[(size_t) i].setTarget (target);
        }
        snapNext = false;
    }

    /** Call once per sub-block before processing it: lengths then glide per sample. */
    void advance (int numSamples) noexcept
    {
        for (int i = 0; i < Stages; ++i)
        {
            const auto start = lengths[(size_t) i].get();
            const auto end = lengths[(size_t) i].advance (numSamples);
            current[(size_t) i] = start;
            increment[(size_t) i] = (end - start) / (float) std::max (numSamples, 1);
        }
    }

    float process (float x, float g) noexcept
    {
        for (int i = 0; i < Stages; ++i)
        {
            current[(size_t) i] += increment[(size_t) i];
            x = stages[(size_t) i].process (x, current[(size_t) i], g);
        }
        return x;
    }

private:
    double fs = 48000.0;
    std::array<float, (size_t) Stages> nominal {};
    std::array<Allpass, (size_t) Stages> stages;
    std::array<GlideValue, (size_t) Stages> lengths;
    std::array<float, (size_t) Stages> current {}, increment {};
    bool snapNext = true;
};

} // namespace myzel::dsp
