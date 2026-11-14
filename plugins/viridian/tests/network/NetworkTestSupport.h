#pragma once

#include "dsp/common/MatchedSection.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <vector>

namespace viridian::testing
{

inline constexpr double twoPi = 2.0 * std::numbers::pi;
inline constexpr double omegaScale = twoPi * 1000.0;
inline constexpr double rates[] { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

inline double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-300)); }

/** A named analog section in sigma = s / omegaScale. */
struct Case
{
    std::string name;
    dsp::AnalogSection section;
};

inline dsp::AnalogSection fromS (double n0, double n1, double n2, double d0, double d1, double d2)
{
    dsp::AnalogSection a;
    a.omegaScale = omegaScale;
    a.num = { n0, n1 * omegaScale, n2 * omegaScale * omegaScale };
    a.den = { d0, d1 * omegaScale, d2 * omegaScale * omegaScale };
    return a;
}

/** Peaking bell, boost and cut reciprocal, Q at the poles' geometric mean. */
inline dsp::AnalogSection bell (double f0, double q, double gainDb)
{
    const auto w = twoPi * f0, a = std::pow (10.0, gainDb / 40.0);
    return fromS (w * w, a * w / q, 1.0, w * w, w / (a * q), 1.0);
}

/** Second-order shelves (RBJ analog prototypes): `gainDb` below (low) or above (high) `f0`. */
inline dsp::AnalogSection lowShelf (double f0, double q, double gainDb)
{
    const auto w = twoPi * f0, a = std::pow (10.0, gainDb / 40.0), r = std::sqrt (a);
    return fromS (a * a * w * w, a * r / q * w, a, w * w, r / q * w, a);
}

inline dsp::AnalogSection highShelf (double f0, double q, double gainDb)
{
    const auto w = twoPi * f0, a = std::pow (10.0, gainDb / 40.0), r = std::sqrt (a);
    return fromS (a * w * w, a * r / q * w, a * a, a * w * w, r / q * w, 1.0);
}

inline dsp::AnalogSection lowpass2 (double f0, double q)
{
    const auto w = twoPi * f0;
    return fromS (w * w, 0.0, 0.0, w * w, w / q, 1.0);
}

inline dsp::AnalogSection highpass2 (double f0, double q)
{
    const auto w = twoPi * f0;
    return fromS (0.0, 0.0, 1.0, w * w, w / q, 1.0);
}

/** First-order shelf: gainDb at DC (low) or at high frequencies (high), corner f0. */
inline dsp::AnalogSection shelf1 (double f0, double gainDb, bool high)
{
    const auto w = twoPi * f0, g = std::pow (10.0, gainDb / 20.0), r = std::sqrt (g);
    return high ? fromS (w, r, 0.0, w, 1.0 / r, 0.0) : fromS (w * r, 1.0, 0.0, w / r, 1.0, 0.0);
}

inline dsp::AnalogSection lowpass1 (double f0)
{
    const auto w = twoPi * f0;
    return fromS (w, 0.0, 0.0, w, 1.0, 0.0);
}

inline dsp::AnalogSection highpass1 (double f0)
{
    const auto w = twoPi * f0;
    return fromS (0.0, 1.0, 0.0, w, 1.0, 0.0);
}

/** The case grid of the magnitude gate: every shape over frequency, Q and gain. */
inline std::vector<Case> sectionGrid()
{
    std::vector<Case> out;
    auto add = [&] (const std::string& n, const dsp::AnalogSection& s) { out.push_back ({ n, s }); };
    for (double f : { 20.0, 100.0, 1000.0, 3000.0, 5000.0, 8000.0, 10000.0, 12000.0, 16000.0, 20000.0, 26000.0, 40000.0 })
    {
        const auto fs = std::to_string ((int) f) + " Hz";
        for (double q : { 0.4, 0.8, 1.3, 2.5, 4.0 })
            for (double g : { -18.0, -6.0, 6.0, 18.0 })
                add ("bell " + fs + " Q " + std::to_string (q) + " " + std::to_string ((int) g) + " dB", bell (f, q, g));
        for (double g : { -18.0, -6.0, 6.0, 18.0 })
        {
            for (double q : { 0.5, 0.707, 1.3 })
            {
                add ("low shelf " + fs + " Q " + std::to_string (q) + " " + std::to_string ((int) g) + " dB", lowShelf (f, q, g));
                add ("high shelf " + fs + " Q " + std::to_string (q) + " " + std::to_string ((int) g) + " dB", highShelf (f, q, g));
            }
            add ("1st-order low shelf " + fs + " " + std::to_string ((int) g) + " dB", shelf1 (f, g, false));
            add ("1st-order high shelf " + fs + " " + std::to_string ((int) g) + " dB", shelf1 (f, g, true));
        }
        for (double q : { 0.5, 0.707, 1.3, 4.0 })
        {
            add ("low-pass " + fs + " Q " + std::to_string (q), lowpass2 (f, q));
            add ("high-pass " + fs + " Q " + std::to_string (q), highpass2 (f, q));
        }
        add ("1st-order low-pass " + fs, lowpass1 (f));
        add ("1st-order high-pass " + fs, highpass1 (f));
    }
    return out;
}

/** Log-spaced test frequencies from 10 Hz to `top`. */
inline std::vector<double> logFrequencies (double top, int perOctave = 48)
{
    std::vector<double> f;
    for (double x = 10.0; x <= top * (1.0 + 1.0e-12); x *= std::pow (2.0, 1.0 / perOctave))
        f.push_back (x);
    if (f.back() < top)
        f.push_back (top);
    return f;
}

/** The DFT of `x` at `hz` (an impulse response that has decayed: its frequency response). */
inline std::complex<double> dft (const std::vector<double>& x, double hz, double fs)
{
    std::complex<double> acc {};
    const auto step = std::polar (1.0, -twoPi * hz / fs);
    std::complex<double> phasor { 1.0, 0.0 };
    for (size_t n = 0; n < x.size(); ++n)
    {
        acc += x[n] * phasor;
        phasor *= step;
        if ((n & 1023) == 1023)
            phasor = std::polar (1.0, -twoPi * hz / fs * (double) (n + 1));
    }
    return acc;
}

/** Peak of the 3rd difference over RMS, in dB (the siblings' click detector). */
inline double clickCrestDb (const std::vector<double>& x, size_t start)
{
    double peak = 0.0, sum = 0.0;
    for (size_t i = std::max<size_t> (start, 3); i < x.size(); ++i)
        peak = std::max (peak, std::abs (x[i] - 3.0 * x[i - 1] + 3.0 * x[i - 2] - x[i - 3]));
    for (size_t i = start; i < x.size(); ++i)
        sum += x[i] * x[i];
    const auto rms = std::sqrt (sum / (double) std::max<size_t> (x.size() - start, 1));
    return toDb (peak / std::max (rms, 1.0e-300));
}

} // namespace viridian::testing
