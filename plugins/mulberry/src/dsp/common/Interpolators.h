#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace mulberry::dsp
{

/**
    12-tap Kaiser-windowed sinc (beta 8) for fractional reads, tabulated at 512 fractions and
    interpolated linearly between them.

    Every fraction's kernel is normalised to the same DC gain, so a moving read never modulates
    the level, and the whole table is scaled by the one factor that keeps every kernel's gain at
    or below 1 at all frequencies (the raw kernels overshoot by up to 0.0013 dB near Nyquist): safe
    inside a loop at 100 % feedback, for a uniform 0.0013 dB per pass. At 48 kHz the worst
    per-pass loss is 0.003 dB to 10 kHz and 0.14 dB at 16 kHz at any fraction (cubic Lagrange:
    3.3 dB at 16 kHz at half a sample).
*/
class WindowedSinc
{
public:
    static constexpr int taps = 12;
    static_assert (taps % 4 == 0);
    static constexpr int half = taps / 2;
    static constexpr int phases = 512;
    static constexpr double beta = 8.0;

    static const WindowedSinc& instance()
    {
        static const WindowedSinc table;
        return table;
    }

    /**
        Interpolates 12 consecutive samples, oldest first: window[j] is the sample (half - j + i)
        samples older than the target position i + fraction counted back from the newest.
    */
    float interpolate (const float* window, float fraction) const noexcept
    {
        const auto p = std::clamp (fraction, 0.0f, 1.0f) * (float) phases;
        const auto index = std::min ((int) p, phases - 1);
        const auto t = p - (float) index;
        const auto* a = kernels[(size_t) index].data();
        const auto* b = kernels[(size_t) index + 1].data();

        // Four running sums (taps is a multiple of 4): independent chains the compiler can pack.
        std::array<float, 4> acc {};
        for (int j = 0; j < taps; j += 4)
            for (int q = 0; q < 4; ++q)
                acc[(size_t) q] += window[j + q] * (a[j + q] + t * (b[j + q] - a[j + q]));
        return (acc[0] + acc[1]) + (acc[2] + acc[3]);
    }

    /** Nearest tabulated fraction (1/1024 sample at worst): half the work, for output-only reads. */
    float interpolateNearest (const float* window, float fraction) const noexcept
    {
        const auto index = std::clamp ((int) (fraction * (float) phases + 0.5f), 0, phases);
        const auto* a = kernels[(size_t) index].data();
        std::array<float, 4> acc {};
        for (int j = 0; j < taps; j += 4)
            for (int q = 0; q < 4; ++q)
                acc[(size_t) q] += window[j + q] * a[j + q];
        return (acc[0] + acc[1]) + (acc[2] + acc[3]);
    }

    /** The gain of every kernel at DC (the table's one scale factor). */
    float dcGain() const noexcept
    {
        float sum = 0.0f;
        for (const auto v : kernels.front())
            sum += v;
        return sum;
    }

    /** The kernel at a fraction (tests). */
    const std::array<float, taps>& kernel (int phase) const noexcept { return kernels[(size_t) std::clamp (phase, 0, phases)]; }

private:
    WindowedSinc()
    {
        kernels.resize (phases + 1);
        std::vector<std::array<double, taps>> raw ((size_t) phases + 1);
        const auto i0Beta = besselI0 (beta);

        for (int p = 0; p <= phases; ++p)
        {
            const auto f = (double) p / phases;
            std::array<double, taps> h {};

            for (int j = 0; j < taps; ++j)
            {
                // window[j] sits (half - j) samples older than the integer part, so its distance
                // from the target is x = j - half + f.
                const auto x = (double) j - half + f;
                const auto r = x / half;
                const auto w = std::abs (r) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / i0Beta;
                const auto s = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (std::numbers::pi * x) / (std::numbers::pi * x);
                h[(size_t) j] = s * w;
            }

            double sum = 0.0;
            for (const auto v : h)
                sum += v;
            for (int j = 0; j < taps; ++j)
                raw[(size_t) p][(size_t) j] = h[(size_t) j] / sum;
        }

        // One scale for the whole table: the largest gain of any kernel at any frequency.
        constexpr int grid = 1024;
        std::vector<std::array<std::complex<double>, taps>> basis ((size_t) grid + 1);
        for (int k = 0; k <= grid; ++k)
            for (int j = 0; j < taps; ++j)
                basis[(size_t) k][(size_t) j] = std::polar (1.0, -std::numbers::pi * k / grid * j);

        double peak = 1.0;
        for (const auto& h : raw)
            for (const auto& e : basis)
            {
                std::complex<double> H {};
                for (int j = 0; j < taps; ++j)
                    H += h[(size_t) j] * e[(size_t) j];
                peak = std::max (peak, std::abs (H));
            }

        // A little margin, so float rounding of the coefficients never lifts a band above 1.
        const auto scale = (1.0 - 1.0e-6) / peak;
        for (int p = 0; p <= phases; ++p)
            for (int j = 0; j < taps; ++j)
                kernels[(size_t) p][(size_t) j] = (float) (raw[(size_t) p][(size_t) j] * scale);
    }

    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= (x * 0.5 / k) * (x * 0.5 / k);
            sum += term;
            if (term < 1.0e-17 * sum)
                break;
        }
        return sum;
    }

    std::vector<std::array<float, taps>> kernels;
};

/**
    A ring buffer of audio over borrowed memory, with a mirrored guard at the end, so every
    12-sample interpolation window is contiguous.
*/
class MirroredRing
{
public:
    static constexpr int guard = WindowedSinc::taps;

    /** memory must hold capacity + guard floats. */
    void attach (float* memory, int capacity) noexcept
    {
        data = memory;
        size = capacity;
        writeIndex = 0;
        written = 0;
    }

    /** Forgets the contents without touching the memory: reads before the first writes give 0. */
    void restart() noexcept
    {
        writeIndex = 0;
        written = 0;
    }

    int capacity() const noexcept { return size; }
    int getWriteIndex() const noexcept { return writeIndex; }
    long long samplesWritten() const noexcept { return written; }

    void write (float x) noexcept
    {
        data[writeIndex] = x;
        if (writeIndex < guard)
            data[size + writeIndex] = x;
        if (++writeIndex == size)
            writeIndex = 0;
        ++written;
    }

    /** Adds to the sample written d samples ago (1 <= d < capacity), then bounds it to +-ceiling. */
    void addAt (int d, float value, float ceiling) noexcept
    {
        if (d < 1 || d > written || d >= size)
            return;
        auto i = writeIndex - d;
        if (i < 0)
            i += size;
        data[i] = std::clamp (data[i] + value, -ceiling, ceiling);
        if (i < guard)
            data[size + i] = data[i];
    }

    /** The sample written d samples ago (d >= 1); 0 before anything was written there. */
    float at (int d) const noexcept
    {
        if (d < 1 || d > written || d > size)
            return 0.0f;
        auto i = writeIndex - d;
        if (i < 0)
            i += size;
        return data[i];
    }

    /** Fractional read d samples ago (d >= half), windowed sinc (fast: nearest tabulated fraction). */
    float read (double d, bool fast = false) const noexcept
    {
        constexpr auto half = WindowedSinc::half;
        d = std::clamp (d, (double) half, (double) (size - guard - half));

        const auto i = (int) d;
        const auto f = (float) (d - i);
        auto start = writeIndex - i - half;
        if (start < 0)
            start += size;

        // Samples older than the first write since restart() are stale memory: read them as 0.
        const auto oldest = (long long) i + half;
        if (oldest > written)
        {
            std::array<float, WindowedSinc::taps> window {};
            for (int j = 0; j < WindowedSinc::taps; ++j)
                if ((long long) (i + half - j) <= written)
                    window[(size_t) j] = data[start + j];
            return WindowedSinc::instance().interpolate (window.data(), f);
        }

        return fast ? WindowedSinc::instance().interpolateNearest (data + start, f)
                    : WindowedSinc::instance().interpolate (data + start, f);
    }

    float readAtIndexNearest (double index) const noexcept
    {
        auto i = (int) std::floor (index);
        const auto f = (float) (index - i);
        auto start = i - (WindowedSinc::half - 1);
        while (start < 0)
            start += size;
        while (start >= size)
            start -= size;
        return WindowedSinc::instance().interpolateNearest (data + start, 1.0f - f);
    }

    /** Read at ring index i plus a fraction in [0, 1] (tape heads: no floor, one wrap). */
    float readAt (int i, float fraction, bool fast) const noexcept
    {
        auto start = i - (WindowedSinc::half - 1);
        if (start < 0)
            start += size;
        const auto& sinc = WindowedSinc::instance();
        return fast ? sinc.interpolateNearest (data + start, 1.0f - fraction) : sinc.interpolate (data + start, 1.0f - fraction);
    }

    /** Fractional read at an absolute (possibly fractional) index into the ring, for tape heads. */
    float readAtIndex (double index) const noexcept
    {
        auto i = (int) std::floor (index);
        const auto f = (float) (index - i);
        auto start = i - (WindowedSinc::half - 1);
        while (start < 0)
            start += size;
        while (start >= size)
            start -= size;
        return WindowedSinc::instance().interpolate (data + start, 1.0f - f);
    }

private:
    float* data = nullptr;
    int size = 0;
    int writeIndex = 0;
    long long written = 0;
};

} // namespace mulberry::dsp
