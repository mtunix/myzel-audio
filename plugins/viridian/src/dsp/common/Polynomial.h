#pragma once

#include "common/dsp/FloatCompare.h"

#include <array>
#include <complex>
#include <span>

namespace viridian::dsp::poly
{

using Complex = std::complex<double>;

/** Exact comparisons where one is meant (an exact zero, an exact conjugate, an exactly real root). */
inline bool isExactly (Complex a, Complex b) noexcept
{
    return myzel::dsp::isExactly (a.real(), b.real()) && myzel::dsp::isExactly (a.imag(), b.imag());
}

inline bool isReal (Complex a) noexcept { return myzel::dsp::isExactly (a.imag(), 0.0); }

/** A root as alpha / beta: beta = 0 is a root at infinity, which a degree drop leaves behind. */
struct ProjectiveRoot
{
    Complex alpha { 0.0, 0.0 }, beta { 1.0, 0.0 };

    bool isInfinite() const noexcept { return isExactly (beta, {}); }
    Complex value() const noexcept { return alpha / beta; }
};

/**
    Both roots of c0 + c1 x + c2 x^2 without cancellation, as projective roots: a vanishing c2
    sends one root to infinity continuously instead of through a division by zero. With c2 = c1 = 0
    both roots are at infinity.
*/
void quadraticRoots (Complex c0, Complex c1, Complex c2, ProjectiveRoot& r1, ProjectiveRoot& r2) noexcept;

/**
    Roots of c[0] + ... + c[n] x^n (n <= maxDegree) by Aberth-Ehrlich iteration, polished by
    Newton steps. Vanishing leading coefficients are dropped first (their roots are at infinity):
    with `infinityRadius` 0, those below 1e-14 of the largest coefficient; above 0, each whose
    polynomial has a root beyond that radius by a lower bound on its largest root, however small
    the coefficient is against the others. Exact zero roots are split off. With `warm`, `roots`
    holds starting guesses on entry (the previous roots: a moving control converges in a few
    iterations; a real guess starts slightly off the axis, so two real roots that have met can
    turn complex). Returns the number of finite roots written; a degree above maxDegree finds none.
*/
inline constexpr int maxDegree = 24;
int roots (std::span<const Complex> c, std::span<Complex> roots, bool warm, double infinityRadius = 0.0) noexcept;

/**
    roots() in bounded steps, for a cold design spread over several callbacks: begin(), then
    step() until it returns true, then result(). The same iterations as roots(), so the same roots
    to the bit, however the sweeps are split.
*/
class RootFinder
{
public:
    /** As roots(): with `warm`, `start` holds the starting guesses. */
    void begin (std::span<const Complex> c, std::span<const Complex> start, bool warm, double infinityRadius = 0.0) noexcept;

    /** Up to `sweeps` Aberth sweeps over every root; true once the roots are found. */
    bool step (int sweeps) noexcept;

    /** The roots as roots() writes them (after step() returned true); their count. */
    int result (std::span<Complex> out) const noexcept;

    bool finished() const noexcept { return done; }

    /** The degree the sweeps run at (a sweep's cost grows with its square). */
    int degree() const noexcept { return m; }

private:
    void coldStart() noexcept;
    void polish() noexcept;

    std::array<Complex, maxDegree + 1> a {};
    std::array<double, maxDegree + 1> magnitudes {};     // |a_k|, for the rounding bound
    std::array<Complex, maxDegree> z {};
    int count = 0, m = 0, attempt = 0, iteration = 0;
    bool done = true;
};

/**
    Makes the roots of a real polynomial exactly conjugate-symmetric: imaginary parts below 1e-9 of
    the magnitude become 0, and each root above the axis gets its partner below set to its exact
    conjugate.
*/
void symmetrise (std::span<Complex> roots) noexcept;

} // namespace viridian::dsp::poly
