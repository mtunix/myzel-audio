#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

namespace mulberry::dsp
{

/** A playback head and tape: the lengths that set the losses (metres). */
struct TapeGeometry
{
    double gap = 2.5e-6;            // playback head gap
    double spacing = 0.2e-6;        // head-to-tape spacing (Wallace)
    double thickness = 1.0e-6;      // effective recording depth
    double bumpWavelength = 5.4e-3; // the head bump: the contour effect peaks at f = v / this
};

/**
    Playback losses of a tape head (H. N. Bertram, Theory of Magnetic Recording, 1994), as
    low-order filters whose corners follow the instantaneous tape speed.

        gap        |sin(pi x) / (pi x)|           x = g f / v
        spacing    exp(-2 pi x)                   x = d f / v
        thickness  (1 - exp(-2 pi x)) / (2 pi x)  x = delta f / v

    Each loss depends on one dimensionless x (a length over the recorded wavelength), so one
    analog prototype per loss serves every speed: its corners sit at fixed x, at v x / length in
    Hz. The prototypes were fitted to the closed forms (tests/machines/loop/validation/
    tape_losses.py, fit_shelves.py): gap, two 2-pole low-passes, within 0.10 dB down to -12 dB;
    spacing (within 0.23 dB to x = 0.5, -27 dB) and thickness (0.06 dB to x = 0.7), first-order
    shelves and a closing pole: their dB falls linearly in f at first, which no all-pole response
    can follow.

    The contour effect (head bump) is a resonant 2nd-order high-pass at v / bumpWavelength (Q 0.8,
    a +0.2 dB peak). The whole playback is scaled by the inverse of that peak, so the head never
    gains at any frequency (a loop element with |H| <= 1).

    The losses depend only on the recorded wavelength, so they act the same whether applied when
    the tape is written (with the speed then) or read (with the speed now): the machines apply
    them in the record path (at 2x), once per line. First-order sections are matched at DC and Nyquist;
    the 2-pole sections at DC, Nyquist and their corner (Vicanek).
*/
class TapeLosses
{
public:
    // Fitted prototypes, in x (see the header comment). Shelves: { pole, zero }.
    static constexpr std::array<std::array<double, 2>, 2> gapFit { { { 0.691938, 1.368542 }, { 0.391067, 0.603753 } } };
    static constexpr std::array<std::array<double, 2>, 4> spacingShelves { { { 0.026467, 0.038300 }, { 0.460655, 1.718594 },
                                                                               { 0.328343, 5.459435 }, { 0.334702, 13.277708 } } };
    static constexpr double spacingPole = 0.154937;
    static constexpr std::array<std::array<double, 2>, 2> thicknessShelves { { { 0.012423, 0.013449 }, { 0.091830, 0.122437 } } };
    static constexpr double thicknessPole = 0.240056;
    static constexpr double bumpQ = 0.8;

    static constexpr int numShelves = (int) spacingShelves.size() + (int) thicknessShelves.size();

    /** A first-order section, b0 + b1 z^-1 over 1 + a1 z^-1. */
    struct FirstOrder
    {
        double b0 = 1.0, b1 = 0.0, a1 = 0.0;
    };

    static constexpr int numFirstOrder = numShelves + 2;

    /** A biquad, b0 + b1 z^-1 + b2 z^-2 over 1 + a1 z^-1 + a2 z^-2. */
    struct SecondOrder
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    struct Coefficients
    {
        std::array<SecondOrder, 2> gap {};
        std::array<FirstOrder, numFirstOrder> first {};
        double bumpG = 0.0, bumpK = 1.0;
        double norm = 1.0;
    };

    struct State
    {
        std::array<double, 2> gap1 {}, gap2 {};     // biquad (transposed direct form II) states
        std::array<double, numFirstOrder> first {};
        double bump1 = 0.0, bump2 = 0.0;
    };

    /** The analog magnitude of the closed forms (gap, spacing, thickness) at a speed. */
    static double closedForm (const TapeGeometry& t, double speed, double hz) noexcept
    {
        const auto gx = t.gap * hz / speed, dx = t.spacing * hz / speed, tx = t.thickness * hz / speed;
        const auto gap = gx < 1.0e-9 ? 1.0 : std::abs (std::sin (std::numbers::pi * gx) / (std::numbers::pi * gx));
        const auto spacing = std::exp (-2.0 * std::numbers::pi * dx);
        const auto k = 2.0 * std::numbers::pi * tx;
        const auto thickness = k < 1.0e-9 ? 1.0 : (1.0 - std::exp (-k)) / k;
        return gap * spacing * thickness;
    }

    static Coefficients design (const TapeGeometry& t, double speed, double fs) noexcept
    {
        Coefficients c;
        const auto v = std::max (speed, 1.0e-4);
        for (size_t i = 0; i < 2; ++i)
            c.gap[i] = matchedLowpass (gapFit[i][0] * v / t.gap, gapFit[i][1], fs);

        // Shelves and poles: matched first-order sections (see matchedFirstOrder()).
        size_t j = 0;
        for (const auto& s : spacingShelves)
            c.first[j++] = matchedFirstOrder (s[0] * v / t.spacing, s[1] * v / t.spacing, fs);
        for (const auto& s : thicknessShelves)
            c.first[j++] = matchedFirstOrder (s[0] * v / t.thickness, s[1] * v / t.thickness, fs);
        c.first[j++] = matchedFirstOrder (spacingPole * v / t.spacing, 0.0, fs);
        c.first[j++] = matchedFirstOrder (thicknessPole * v / t.thickness, 0.0, fs);

        c.bumpG = std::tan (std::numbers::pi * std::min (v / t.bumpWavelength, 0.45 * fs) / fs);
        c.bumpK = 1.0 / bumpQ;
        c.norm = 1.0 / bumpPeak();
        return c;
    }

    static float process (float in, const Coefficients& c, State& s) noexcept
    {
        const auto y = processDouble ((double) in, c, s);
        return std::abs (y) < 1.0e-30 ? 0.0f : (float) y;
    }

    static double processDouble (double x, const Coefficients& c, State& s) noexcept
    {

        for (size_t i = 0; i < 2; ++i)
        {
            const auto& q = c.gap[i];
            const auto y = q.b0 * x + s.gap1[i];
            s.gap1[i] = q.b1 * x - q.a1 * y + s.gap2[i];
            s.gap2[i] = q.b2 * x - q.a2 * y;
            x = y;
        }

        for (size_t i = 0; i < (size_t) numFirstOrder; ++i)
        {
            const auto& f = c.first[i];
            const auto y = f.b0 * x + s.first[i];
            s.first[i] = f.b1 * x - f.a1 * y;
            x = y;
        }

        {
            const auto g = c.bumpG, k = c.bumpK;
            const auto a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const auto v3 = x - s.bump2;
            const auto v1 = a1 * s.bump1 + a2 * v3;
            const auto v2 = s.bump2 + a2 * s.bump1 + a3 * v3;
            s.bump1 = 2.0 * v1 - s.bump1;
            s.bump2 = 2.0 * v2 - s.bump2;
            x = x - k * v1 - v2;
        }

        return x * c.norm;
    }

    static void flush (State& s) noexcept
    {
        auto f = [] (double& v) { if (std::abs (v) < 1.0e-30) v = 0.0; };
        for (auto& v : s.gap1) f (v);
        for (auto& v : s.gap2) f (v);
        for (auto& v : s.first) f (v);
        f (s.bump1);
        f (s.bump2);
    }

    /** The exact digital response of the filters at a frequency. */
    static std::complex<double> response (const Coefficients& c, double hz, double fs) noexcept
    {
        const auto t = std::tan (std::numbers::pi * std::min (hz, 0.4999 * fs) / fs);
        std::complex<double> h { c.norm, 0.0 };
        const auto z1 = std::polar (1.0, -2.0 * std::numbers::pi * std::min (hz, 0.4999 * fs) / fs);
        const auto z2 = z1 * z1;
        for (const auto& q : c.gap)
            h *= (q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2);
        for (const auto& f : c.first)
            h *= (f.b0 + f.b1 * z1) / (1.0 + f.a1 * z1);
        const std::complex<double> s { 0.0, t / c.bumpG };
        h *= s * s / (s * s + c.bumpK * s + 1.0);
        return h;
    }

    /** The response without the head bump and its normalisation: what the fits approximate. */
    static std::complex<double> lossResponse (const Coefficients& c, double hz, double fs) noexcept
    {
        const auto t = std::tan (std::numbers::pi * std::min (hz, 0.4999 * fs) / fs);
        const std::complex<double> s { 0.0, t / c.bumpG };
        return response (c, hz, fs) / (c.norm * s * s / (s * s + c.bumpK * s + 1.0));
    }

    /** Peak gain of the resonant high-pass (the bump): Q / sqrt (1 - 1 / (4 Q^2)). */
    static double bumpPeak() noexcept
    {
        return bumpQ / std::sqrt (1.0 - 1.0 / (4.0 * bumpQ * bumpQ));
    }

    /**
        A first-order section (1 + s/wz) / (1 + s/wp) (zeroHz 0: a plain one-pole low-pass) with
        its pole by the matched z-transform and its numerator set so the magnitude is exact at DC
        and at Nyquist (after M. Vicanek, Matched One-Pole Digital Shelving Filters, 2019): no
        bilinear squeeze of corners near or above Nyquist.
    */
    static FirstOrder matchedFirstOrder (double poleHz, double zeroHz, double fs) noexcept
    {
        // Above 0.2 fs the bilinear design takes over, blended in up to 0.3 fs: a gliding tape
        // speed moves the corners through this range, and switching designs at once would tick.
        const auto w = blendWeight (poleHz, fs);
        if (w >= 1.0)
            return bilinearFirstOrder (poleHz, zeroHz, fs);

        auto f = poleMatchedFirstOrder (poleHz, zeroHz, fs);
        if (w > 0.0)
        {
            const auto b = bilinearFirstOrder (poleHz, zeroHz, fs);
            f.b0 += w * (b.b0 - f.b0);
            f.b1 += w * (b.b1 - f.b1);
            f.a1 += w * (b.a1 - f.a1);
        }
        return f;
    }

    static double blendWeight (double hz, double fs) noexcept
    {
        const auto x = std::clamp ((hz / fs - 0.2) / 0.1, 0.0, 1.0);
        return x * x * (3.0 - 2.0 * x);
    }

    static FirstOrder poleMatchedFirstOrder (double poleHz, double zeroHz, double fs) noexcept
    {
        const auto p = std::exp (-2.0 * std::numbers::pi * poleHz / fs);
        const auto nyquist = 0.5 * fs;
        const auto analog = [&] (double hz)
        {
            const auto num = zeroHz > 0.0 ? 1.0 + (hz / zeroHz) * (hz / zeroHz) : 1.0;
            return std::sqrt (num / (1.0 + (hz / poleHz) * (hz / poleHz)));
        };
        const auto atNyquist = analog (nyquist);
        FirstOrder f;
        f.a1 = -p;
        f.b0 = 0.5 * ((1.0 - p) + (1.0 + p) * atNyquist);
        f.b1 = 0.5 * ((1.0 - p) - (1.0 + p) * atNyquist);
        return f;
    }

    /**
        A 2-pole low-pass 1 / (s^2/w0^2 + s/(Q w0) + 1): poles by the matched z-transform, the
        numerator chosen so the magnitude is exact at DC, at Nyquist and at min (f0, fs/4) (after
        M. Vicanek, Matched Second Order Digital Filters, 2016).
    */
    static SecondOrder matchedLowpass (double f0, double q, double fs) noexcept
    {
        // Blended into the bilinear design between 0.2 and 0.3 fs, as for the first-order sections
        // (a convex blend of two stable biquads is stable).
        const auto w = blendWeight (f0, fs);
        if (w >= 1.0)
            return bilinearLowpass (f0, q, fs);

        auto c = poleMatchedLowpass (f0, q, fs);
        if (w > 0.0)
        {
            const auto b = bilinearLowpass (f0, q, fs);
            c.b0 += w * (b.b0 - c.b0);
            c.b1 += w * (b.b1 - c.b1);
            c.b2 += w * (b.b2 - c.b2);
            c.a1 += w * (b.a1 - c.a1);
            c.a2 += w * (b.a2 - c.a2);
        }
        return c;
    }

    static SecondOrder poleMatchedLowpass (double f0, double q, double fs) noexcept
    {
        SecondOrder c;
        const auto w0 = 2.0 * std::numbers::pi * f0 / fs;
        const auto zeta = 0.5 / q;
        if (zeta < 1.0)
        {
            const auto r = std::exp (-zeta * w0);
            c.a1 = -2.0 * r * std::cos (w0 * std::sqrt (1.0 - zeta * zeta));
            c.a2 = r * r;
        }
        else
        {
            const auto root = std::sqrt (zeta * zeta - 1.0);
            c.a1 = -(std::exp (-(zeta - root) * w0) + std::exp (-(zeta + root) * w0));
            c.a2 = std::exp (-2.0 * zeta * w0);
        }

        const auto analog = [f0, q] (double hz)
        {
            const auto x = hz / f0;
            return 1.0 / ((1.0 - x * x) * (1.0 - x * x) + x * x / (q * q));
        };
        const auto A0 = (1.0 + c.a1 + c.a2) * (1.0 + c.a1 + c.a2);
        const auto A1 = (1.0 - c.a1 + c.a2) * (1.0 - c.a1 + c.a2);
        const auto A2 = -4.0 * c.a2;
        const auto B0 = A0;
        const auto B1 = A1 * analog (0.5 * fs);
        const auto fm = std::min (f0, 0.25 * fs);
        const auto s2 = std::sin (std::numbers::pi * fm / fs) * std::sin (std::numbers::pi * fm / fs);
        const auto phi0 = 1.0 - s2, phi1 = s2, phi2 = 4.0 * phi0 * phi1;
        const auto B2 = (analog (fm) * (A0 * phi0 + A1 * phi1 + A2 * phi2) - B0 * phi0 - B1 * phi1) / phi2;

        const auto W = std::sqrt (B0), V = std::sqrt (B1);
        c.b1 = 0.5 * (W - V);
        const auto S = 0.5 * (W + V);
        const auto disc = std::sqrt (std::max (S * S + B2, 0.0));
        c.b0 = 0.5 * (S + disc);
        c.b2 = S - c.b0;
        return c;
    }
    /**
        Corners far above the band (fast tape): the bilinear transform with its frequency axis
        matched at min (16 kHz, 0.4 fs), so the in-band slope of such a section is exact at DC and
        at the top of the audio band (pole-matching would fold them; a Nyquist match would bend
        the band in between).
    */
    static double bilinearScale (double fs) noexcept
    {
        const auto fm = std::min (16000.0, 0.4 * fs);
        return 2.0 * std::numbers::pi * fm / std::tan (std::numbers::pi * fm / fs);
    }

    static FirstOrder bilinearFirstOrder (double poleHz, double zeroHz, double fs) noexcept
    {
        // (1 + s/wz) / (1 + s/wp), s = K (1 - z^-1) / (1 + z^-1)
        const auto K = bilinearScale (fs);
        const auto rz = zeroHz > 0.0 ? K / (2.0 * std::numbers::pi * zeroHz) : 0.0;
        const auto rp = K / (2.0 * std::numbers::pi * poleHz);
        const auto norm = 1.0 / (1.0 + rp);
        FirstOrder f;
        f.b0 = (1.0 + rz) * norm;
        f.b1 = (1.0 - rz) * norm;
        f.a1 = (1.0 - rp) * norm;
        return f;
    }

    static SecondOrder bilinearLowpass (double f0, double q, double fs) noexcept
    {
        const auto K = bilinearScale (fs) / (2.0 * std::numbers::pi * f0);
        const auto k2 = K * K, kq = K / q;
        const auto norm = 1.0 / (k2 + kq + 1.0);
        SecondOrder c;
        c.b0 = norm;
        c.b1 = 2.0 * norm;
        c.b2 = norm;
        c.a1 = 2.0 * (1.0 - k2) * norm;
        c.a2 = (k2 - kq + 1.0) * norm;
        return c;
    }
};

} // namespace mulberry::dsp
