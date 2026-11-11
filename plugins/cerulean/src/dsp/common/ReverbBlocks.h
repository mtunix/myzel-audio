#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

/**
    Float building blocks shared by every algorithm.

    All of them are allocation-free after prepare()/setMaxDelay(), deterministic and cheap.
    Conventions:
      - Delay lengths are in samples (float). Convert from ms with msToSamples().
      - DelayLine::read (d) returns the sample written d samples ago (d = 1: the most recent one).
        In a feedback loop, read first, then write.
*/
namespace cerulean::dsp
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
/** Coefficients of a Simper TPT state-variable filter at Q = 1/sqrt 2 (shareable by many filters). */
struct ShelfCoefficients
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;

    static constexpr float sqrt2 = 1.41421356f;

    static ShelfCoefficients at (float hz, double sampleRate) noexcept
    {
        const auto fc = std::clamp (hz, 1.0f, 0.49f * (float) sampleRate);
        const auto g = std::tan (std::numbers::pi_v<float> * fc / (float) sampleRate);
        const auto a1 = 1.0f / (1.0f + g * (g + sqrt2));
        return { a1, g * a1, g * g * a1 };
    }
};

/**
    Three-band decay ("absorption") filter for one delay line, after Jot: a broadband gain for the
    mids and two *second-order* (Butterworth-type) shelves built from TPT SVFs at Q = 1/sqrt 2:

        high shelf  H(s) = (1 + sqrt2 sqrt(G) s + G s^2) / (1 + sqrt2 s + s^2)
                    |H|^2 = (1 + G^2 w^4) / (1 + w^4)          (low shelf: the mirror image)

    Each shelf is monotonic between 1 and G, so the filter never exceeds max(gLow, gMid, gHigh) < 1
    and the loop stays stable for any setting. Second order because the skirt matters: a
    first-order shelf at 4.5 kHz shortened the 1-2 kHz octaves' RT60 by 7-10 % (found by the
    Nimbus/Billow work); below the crossover this one leaks with w^4 instead of w^2.

    Written as  x + (G - 1) hp + sqrt2 (sqrt G - 1) bp  (and the low-shelf mirror), so at G = 1
    (freeze) it is exactly transparent. Every gain ramps per sample.
*/
class DecayFilter
{
public:
    void setCrossovers (float lowHz, float highHz, double sampleRate) noexcept
    {
        lo = ShelfCoefficients::at (lowHz, sampleRate);
        hi = ShelfCoefficients::at (highHz, sampleRate);
    }

    void setCrossovers (const ShelfCoefficients& low, const ShelfCoefficients& high) noexcept
    {
        lo = low;
        hi = high;
    }

    /**
        Per-pass gains of the three bands (each in (0, 1]). They ramp to the new values over the
        next numSamples samples (the first call after reset() jumps).
    */
    void setGains (float gLow, float gMid, float gHigh, int numSamples = 1) noexcept
    {
        const auto m = std::max (gMid, 1.0e-6f);
        const auto gl = gLow / m, gh = gHigh / m;
        mid.set (gMid, numSamples);
        loA.set (gl - 1.0f, numSamples);
        loB.set (ShelfCoefficients::sqrt2 * (std::sqrt (gl) - 1.0f), numSamples);
        hiA.set (gh - 1.0f, numSamples);
        hiB.set (ShelfCoefficients::sqrt2 * (std::sqrt (gh) - 1.0f), numSamples);
    }

    void reset() noexcept
    {
        lo1 = lo2 = hi1 = hi2 = 0.0f;
        mid.reset();
        loA.reset();
        loB.reset();
        hiA.reset();
        hiB.reset();
    }

    float process (float x) noexcept { return process (x, lo, hi); }

    /** With crossover coefficients shared by many filters (e.g. ramped once per sample by an FDN). */
    float process (float x, const ShelfCoefficients& low, const ShelfCoefficients& high) noexcept
    {
        {
            const auto v3 = x - hi2;
            const auto v1 = high.a1 * hi1 + high.a2 * v3;
            const auto v2 = hi2 + high.a2 * hi1 + high.a3 * v3;
            hi1 = flushTiny (2.0f * v1 - hi1);
            hi2 = flushTiny (2.0f * v2 - hi2);
            const auto hp = x - ShelfCoefficients::sqrt2 * v1 - v2;
            x += hiA.next() * hp + hiB.next() * v1;
        }
        {
            const auto v3 = x - lo2;
            const auto v1 = low.a1 * lo1 + low.a2 * v3;
            const auto v2 = lo2 + low.a2 * lo1 + low.a3 * v3;
            lo1 = flushTiny (2.0f * v1 - lo1);
            lo2 = flushTiny (2.0f * v2 - lo2);
            x += loA.next() * v2 + loB.next() * v1;
        }
        return mid.next() * x;
    }

private:
    ShelfCoefficients lo, hi;
    float lo1 = 0.0f, lo2 = 0.0f, hi1 = 0.0f, hi2 = 0.0f;
    LinearRamp mid, loA, loB, hiA, hiB;
};

/**
    Per-pass gains for a line of delaySamples from the shared global controls.
    freeze (0..1) blends every gain towards exactly 1.
*/
struct BandGains
{
    float low, mid, high;
};

inline BandGains computeBandGains (float delaySamples, float rt60Mid, float lowEndRatio,
                                   float highRatio, float freeze, float sampleRate) noexcept
{
    auto g = [&] (float rt)
    {
        return std::pow (10.0f, -3.0f * delaySamples / (std::max (rt, 1.0e-3f) * sampleRate));
    };

    BandGains b { g (rt60Mid * lowEndRatio), g (rt60Mid), g (rt60Mid * highRatio) };

    if (freeze > 0.0f)
    {
        const auto f = std::clamp (freeze, 0.0f, 1.0f);
        b.low  += (1.0f - b.low) * f;
        b.mid  += (1.0f - b.mid) * f;
        b.high += (1.0f - b.high) * f;
    }
    return b;
}

//==============================================================================
/** In-place fast Walsh-Hadamard transform, scaled to be orthonormal. N must be a power of 2. */
template <int N>
inline void hadamardInPlace (float* x) noexcept
{
    static_assert ((N & (N - 1)) == 0, "N must be a power of two");

    for (int h = 1; h < N; h <<= 1)
        for (int i = 0; i < N; i += h << 1)
            for (int j = i; j < i + h; ++j)
            {
                const auto a = x[j], b = x[j + h];
                x[j] = a + b;
                x[j + h] = a - b;
            }

    const auto scale = 1.0f / std::sqrt ((float) N);
    for (int i = 0; i < N; ++i)
        x[i] *= scale;
}

/** In-place Householder reflection I - (2/N) 1 1^T (orthogonal, cheap: O(N)). */
template <int N>
inline void householderInPlace (float* x) noexcept
{
    float sum = 0.0f;
    for (int i = 0; i < N; ++i)
        sum += x[i];

    const auto k = sum * (2.0f / (float) N);
    for (int i = 0; i < N; ++i)
        x[i] -= k;
}

/** +-1 sign of Hadamard row r, column c (Sylvester construction): orthogonal tap patterns. */
inline float hadamardSign (int r, int c) noexcept
{
    return (__builtin_popcount ((unsigned) (r & c)) & 1) ? -1.0f : 1.0f;
}

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

} // namespace cerulean::dsp
