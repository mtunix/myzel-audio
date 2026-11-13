#pragma once

#include "support/Analysis.h"

#include <cassert>
#include <complex>
#include <numbers>
#include <vector>

/**
    Spectra for the aliasing gates (Crush, Bits): a radix-2 FFT and the family's alias measure
    (Umber's AliasingTests, Mulberry's measure): a tone on an exact bin, a 7-term Blackman-Harris
    window (leakage below -150 dB), every bin that is not a harmonic of the tone is aliasing or
    noise. JUCE-free, test code only.
*/
namespace inkcap::analysis
{

inline void fft (std::vector<std::complex<double>>& a)
{
    const auto n = a.size();
    assert ((n & (n - 1)) == 0);
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto w = std::polar (1.0, -2.0 * std::numbers::pi / (double) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> t { 1.0, 0.0 };
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * t;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                t *= w;
            }
        }
    }
}

/** A frequency rounded to the nearest bin of an n-point transform. */
inline double onBin (double hz, double sampleRate, size_t n)
{
    const auto bin = sampleRate / (double) n;
    return std::round (hz / bin) * bin;
}

/** Magnitudes (dB, a full-scale sine reads 0) of x[from, from + n) under a 7-term Blackman-Harris window. */
inline std::vector<double> spectrumDb (const std::vector<float>& x, size_t from, size_t n)
{
    static constexpr double a[] = { 0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803,
                                    0.01081174209837, 0.00077658482522, 0.00001388721735 };
    std::vector<std::complex<double>> buffer (n);
    double windowSum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        double w = 0.0;
        for (int k = 0; k < 7; ++k)
            w += ((k % 2 == 0) ? 1.0 : -1.0) * a[k] * std::cos (2.0 * std::numbers::pi * k * (double) i / (double) n);
        buffer[i] = (double) x[from + i] * w;
        windowSum += w;
    }
    fft (buffer);
    std::vector<double> db (n / 2);
    for (size_t b = 0; b < n / 2; ++b)
        db[b] = toDb (2.0 * std::abs (buffer[b]) / windowSum + 1.0e-20);
    return db;
}

struct AliasMeasure
{
    double worstDbc = -300.0;       // the strongest bin that is not a harmonic of the tone, against the tone
    double worstHz = 0.0;
    double toneDb = -300.0;
};

/** The family's alias measure on x[from, from + n): f0 must sit on a bin (onBin). */
inline AliasMeasure aliasing (const std::vector<float>& x, size_t from, size_t n, double f0, double sampleRate)
{
    constexpr int guard = 8;        // the window's main lobe, in bins
    const auto spectrum = spectrumDb (x, from, n);
    const auto bin = sampleRate / (double) n;
    std::vector<bool> harmonic (spectrum.size(), false);
    for (int h = 1; h * f0 < sampleRate / 2.0; ++h)
    {
        const auto centre = (int) std::lround (h * f0 / bin);
        for (int b = centre - guard; b <= centre + guard; ++b)
            if (b >= 0 && b < (int) harmonic.size())
                harmonic[(size_t) b] = true;
    }

    AliasMeasure m;
    m.toneDb = spectrum[(size_t) std::lround (f0 / bin)];
    for (size_t b = guard + 1; b + guard < spectrum.size(); ++b)
        if (! harmonic[b] && spectrum[b] - m.toneDb > m.worstDbc)
        {
            m.worstDbc = spectrum[b] - m.toneDb;
            m.worstHz = (double) b * bin;
        }
    return m;
}

} // namespace inkcap::analysis
