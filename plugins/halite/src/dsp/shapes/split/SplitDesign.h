#pragma once

#include <array>

namespace halite::dsp
{

enum class SplitKind
{
    bell,       // the full depth over [low, high]; the analog cut within 0.1 dB of none a third of an octave past either edge
    highShelf   // the full depth from low up; the analog cut within 0.1 dB of none a third of an octave under it
};

/**
    The steep Split cut (split/NOTES.md, *The design*). A shelf is |H|² = (B − (1 − G) A) / (B − (1 − 1/G) A)
    in t = Ω², with A (t) and B (t) the squared numerator and denominator of an elliptic analog
    low-pass (passband edge Ω = 1, stopband edge a third of an octave on) and G the linear gain. A
    bell is a cut shelf at its low edge times a boost shelf (1/G) a third of an octave past its high
    edge. Digitally t = α / γ with a first-order base per edge, α = (1 − x) + a1 x and γ = c1 x in
    x = sin² (π f / fs), and each root t_j of the numerator and denominator gives one section.
*/
namespace splitDesign
{
    /** A third of an octave: the guard band past each edge, where the cut's transition lies. */
    inline constexpr double guard = 1.2599210498948732;

    /** The design runs once every this many samples, on a grid that never crosses a detector hop (split/NOTES.md, Modulation). */
    inline constexpr int interval = 32;

    struct Root
    {
        double re = 0.0, im = 0.0;
    };

    /** One shelf's prototype and what is derived from it once: the polynomials, its roots per dB of depth, its base table. */
    struct Prototype
    {
        static constexpr int maxOrder = 6, maxRoots = 3, maxRows = 49, baseNodes = 69;
        using BaseTable = std::array<double, baseNodes>;

        int order = 0, pairs = 0, reals = 0;   // pairs + reals roots: one per conjugate pair, one real (odd orders)
        double deepestDb = 0.0, deepestGain = 1.0;

        /** The elliptic low-pass: poles (the upper one of each conjugate pair and the real one, in scipy's order), zeros at ±jΩ, gain. */
        std::array<Root, maxRoots> poles {};
        std::array<double, maxRoots> zeros {};
        double lowPassGain = 0.0;

        /** A (t) and B (t), ascending powers of t. */
        std::array<double, maxOrder + 1> a {}, b {};

        /** The roots of B − (1 − G) A at −deepest … +deepest dB in 1 dB rows (a negative depth: the denominator's), pairs first. */
        std::array<std::array<Root, maxRoots>, maxRows> roots {};
        int rows = 0;

        /** The base's minimax table over u = fc / fs, u = i / 128: a1 = p (u), c1 = q (u) / (π u)². */
        const BaseTable* baseP = nullptr;
        const BaseTable* baseQ = nullptr;
    };

    /** The Ess band's: order 6, cuts to 24 dB. Built once, on first use (not on the audio thread: SplitFilter keeps a reference). */
    const Prototype& ess() noexcept;

    /** The Harsh band's: order 5, cuts to 12 dB. */
    const Prototype& harsh() noexcept;

    /** The analog power gain |H|² at `hz` for the linear gain G: a shelf from `low`, or the bell over [low, high]. */
    double analogPower (const Prototype&, SplitKind, double lowHz, double highHz, double hz, double gain) noexcept;

    /** The analog shelf alone, from the low-pass's poles and zeros (not its polynomials): |H|² = (1 + G ρ) / (1 + ρ / G). */
    double analogShelfFromPoles (const Prototype&, double cornerHz, double hz, double gain) noexcept;

    /** The roots at a depth in dB (negative: the denominator's), interpolated linearly between the 1 dB rows. */
    std::array<Root, Prototype::maxRoots> rootsAt (const Prototype&, double depthDb) noexcept;

    /** One edge's first-order base. */
    struct Base
    {
        double a1 = 1.0, c1 = 1.0;
    };

    /** The base for a shelf whose corner is `cornerHz`: Catmull-Rom on the table, clamped to its ends. */
    Base baseFor (const Prototype&, double cornerHz, double sampleRate) noexcept;

    /**
        One section, as the cascade runs it: a TPT SVF (a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2;
        removed = r0 (x − lp) + r1 bp) or a TPT one-pole (a1 = g / (1 + g); removed = r0 · hp).
    */
    struct Section
    {
        double a1 = 0.0, a2 = 0.0, a3 = 0.0, r0 = 0.0, r1 = 0.0;
    };

    /** The SVF and the one-pole for a section's numerator P and denominator Q (P1, P2 and Q1, Q2 in M2's basis). */
    Section svf (double p1, double p2, double q1, double q2) noexcept;
    Section onePole (double p1, double q1) noexcept;

    /** A cut: its sections in cascade order (each shelf's pairs, then its real root; the cut shelf first) and its overall gain. */
    struct Design
    {
        static constexpr int maxSections = 2 * Prototype::maxRoots;
        std::array<Section, maxSections> sections {};
        double gain = 1.0;
    };

    /**
        The cut at the linear gain G (1 down to the deepest; deeper is held there): the cut shelf on
        `low`, then for a bell the boost shelf on `upper` (its high edge times guard). A shelf alone
        keeps the prototype's level at DC, √(lead (G) / lead (1/G)); a bell is exactly 1 there. Two
        root interpolations, a logarithm and five square roots per pair; no allocation.
    */
    void design (const Prototype&, SplitKind, const Base& low, const Base& upper, double gain, Design&) noexcept;
}

} // namespace halite::dsp
