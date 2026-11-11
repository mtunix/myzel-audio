#pragma once

#include "dsp/models/desk/JilesAtherton.h"

#include <cmath>
#include <complex>
#include <numbers>

namespace umber::dsp::desk
{

/**
    Component values of one audio transformer and its terminations, referred to the primary
    (both Desk transformers are 1:1, so no turns-ratio scaling is needed).

        Vs ──[ R1 ]──[ Cc ]──┬──[ Lleak ]──[ R2 ]──┬────────┬──── out
                             │                     │        │
                            Lm (JA core)           Cw       RL
                             │                     │        │
        ─────────────────────┴─────────────────────┴────────┴────

    R1 = source/build-out resistance + primary winding resistance. Cc is an optional series
    coupling capacitor (0 = none). Lleak is the total leakage inductance, R2 the secondary winding
    resistance, Cw the winding/stray capacitance and RL the load.

    The core is set by its material plus two derived geometry constants:
        K  = mu0 * N * A    (flux linkage per unit B/mu0), from the saturation flux linkage
        lN = l / N          (magnetising current per unit H), from the small-signal inductance
    so  lambda = K (H + M)  and  i_m = lN * H.
*/
struct TransformerParams
{
    JaMaterial core;
    double R1, Cc, Lleak, R2, Cw, RL;
    double saturationFlux;        // lambda_sat = mu0 N A Ms (V s)
    double smallSignalInductance; // Lm at low level, demagnetised (H)

    double fluxConstant() const noexcept { return saturationFlux / core.Ms; }

    double currentConstant() const noexcept
    {
        return fluxConstant() * (1.0 + core.initialSusceptibility()) / smallSignalInductance;
    }

    /** Analog small-signal transfer Vout / Vs at frequency f (linearised core). */
    std::complex<double> smallSignalResponse (double f) const noexcept
    {
        using C = std::complex<double>;
        const C s (0.0, 2.0 * std::numbers::pi * f);
        const C zm  = s * smallSignalInductance;
        const C zs  = C (R1) + (Cc > 0.0 ? 1.0 / (s * Cc) : C (0.0));
        const C zp2 = 1.0 / (1.0 / RL + s * Cw);
        const C zlb = s * Lleak + R2 + zp2;
        const C zsh = 1.0 / (1.0 / zm + 1.0 / zlb);
        return zsh / (zs + zsh) * zp2 / zlb;
    }
};

/**
    One transformer as a wave digital filter (bilinear transform, Fettweis / Werner style) with
    the nonlinear magnetising inductance as the tree's root:

        root: Lm (JA hysteresis)
          └─ parallel P
               ├─ series S  = [ resistive source (Vs, R1) , Cc ]
               └─ series Lb = [ Lleak , R2 , parallel P2 = [ Cw , RL ] ]

    Root solve. The root sees a resistive Thevenin port (incident wave a, port resistance R0), so
    v = a - R0 i. The flux linkage is discretised with the same trapezoidal rule as every other
    reactance:   lambda[n] = lambda[n-1] + T/2 (v[n] + v[n-1]),  lambda = K (H + M),  i = lN H.
    That gives one scalar equation in H:   K (H + M(H)) + Rk H = p,   Rk = T/2 R0 lN,
    where M(H) follows the JA ODE dM/dH = chi(H, M, delta) from the previous state.

    It is solved semi-implicitly: the direction delta is the sign of the flux increment (exact,
    since dB/dH > 0), a predictor uses chi at the start point, the corrector re-solves the
    (linear) circuit equation with chi at the predicted midpoint (explicit midpoint rule for the
    JA ODE). Finally H is recomputed from the exact flux balance, so the circuit always sees the
    correct flux even when M is clamped. Fixed cost: two chi evaluations, no iteration.
*/
class TransformerStage
{
public:
    void setParams (const TransformerParams& p) noexcept
    {
        params = p;
        core.setMaterial (p.core);
    }
    const TransformerParams& getParams() const noexcept { return params; }

    void prepare (double sampleRate) noexcept
    {
        const double T = 1.0 / sampleRate;
        const auto& p = params;

        K  = p.fluxConstant();
        lN = p.currentConstant();

        // one-port resistances (bilinear: C -> T/2C, L -> 2L/T)
        rCc  = p.Cc > 0.0 ? T / (2.0 * p.Cc) : 0.0;
        rS   = p.R1 + rCc;
        rLl  = 2.0 * p.Lleak / T;
        const double gCw = 2.0 * p.Cw / T;
        const double gRL = 1.0 / p.RL;
        gP2   = gCw + gRL;
        rP2   = 1.0 / gP2;
        cwToP2 = gCw / gP2;
        rLb   = rLl + p.R2 + rP2;

        const double gS = 1.0 / rS, gLb = 1.0 / rLb;
        r0     = 1.0 / (gS + gLb);
        sToP   = gS * r0;
        lbToP  = gLb * r0;
        ccInS  = rCc / rS;
        llInLb = rLl / rLb;
        p2InLb = rP2 / rLb;

        halfT    = 0.5 * T;
        rk       = halfT * r0 * lN;
        invKRk   = 1.0 / (K + rk);
        reset();
    }

    void reset() noexcept
    {
        H = M = vPrev = 0.0;
        zCc = zLl = zCw = 0.0;
    }

    /** Processes one sample of source voltage, returns the load voltage. */
    double process (double vs) noexcept
    {
        auto s = predict (vs);
        correct (s);
        return scatter (s);
    }

    /**
        Processes one sample on each of two independent stages, phase by phase. Each stage's
        sample is one long serial dependency chain (chi -> divide -> chi -> divide); issuing the
        two chains' phases side by side lets the CPU overlap them. Results are bit-identical to
        calling process() on each.
    */
    static void processPair (TransformerStage& x, double& vx, TransformerStage& y, double& vy) noexcept
    {
        auto sx = x.predict (vx);
        auto sy = y.predict (vy);
        x.correct (sx);
        y.correct (sy);
        vx = x.scatter (sx);
        vy = y.scatter (sy);
    }

    /** Flushes decaying states that would otherwise reach the denormal range in long silence. */
    void flushDenormals() noexcept
    {
        constexpr double tiny = 1.0e-30;
        for (double* s : { &H, &vPrev, &zCc, &zLl, &zCw })
            if (std::abs (*s) < tiny)
                *s = 0.0;
    }

    double fieldH() const noexcept { return H; }
    double magnetisation() const noexcept { return M; }
    double fluxDensity() const noexcept { return 4.0e-7 * std::numbers::pi * (H + M); }

private:
    /** One sample in flight: the reflected waves, the root's incident wave and the core step. */
    struct Step
    {
        double vs, bCc, bLl, bP2, bS, bLb, a, p, r, delta, dH0, dM0;
    };

    /** Reflected waves up the tree, then the core's predictor (chi at the start point). */
    Step predict (double vs) const noexcept
    {
        Step s;
        s.vs  = vs;
        s.bCc = zCc;
        s.bLl = -zLl;
        s.bP2 = cwToP2 * zCw;
        s.bS  = vs - s.bCc;                     // series: -(b_source + b_Cc), b_source = -Vs
        s.bLb = -(s.bLl + s.bP2);               // series (R2 reflects 0)
        s.a   = sToP * s.bS + lbToP * s.bLb;

        // nonlinear root: magnetising inductance with JA hysteresis.
        // flux balance K (H + M) + rk H = p
        s.p = K * (H + M) + halfT * (s.a + vPrev);
        s.r = halfT * (s.a + vPrev) - rk * H;   // = p - K (H0 + M0) - rk H0
        s.delta = s.r >= 0.0 ? 1.0 : -1.0;

        // predictor: chi at the start point. With chi = n/d the circuit equation
        // dH (K (1 + chi) + rk) = r gives dH = r d / (K (d + n) + rk d), chi dH = r n / (...)
        const auto chi0 = core.susceptibility (H, M, s.delta);
        const double q0 = s.r / (K * (chi0.den + chi0.num) + rk * chi0.den);
        s.dH0 = q0 * chi0.den;
        s.dM0 = q0 * chi0.num;
        return s;
    }

    /** Corrector: chi at the predicted midpoint, re-solve, keep the magnetisation step; then H
        from the exact flux balance. */
    void correct (const Step& s) noexcept
    {
        const auto chiMid = core.susceptibility (H + 0.5 * s.dH0, M + 0.5 * s.dM0, s.delta);
        const double dM = s.r * chiMid.num / (K * (chiMid.den + chiMid.num) + rk * chiMid.den);

        M = std::clamp (M + dM, -params.core.Ms, params.core.Ms);
        H = (s.p - K * M) * invKRk;
    }

    /** Root reflection and the waves back down the tree; returns the load voltage. */
    double scatter (const Step& s) noexcept
    {
        const double i = lN * H;
        vPrev = s.a - r0 * i;

        const double v    = s.a - r0 * i;
        const double bRoot = 2.0 * v - s.a;

        const double toS  = bRoot + s.a - s.bS;
        const double toLb = bRoot + s.a - s.bLb;

        zCc = s.bCc - ccInS * (toS + (-s.vs) + s.bCc);

        const double sumLb = toLb + s.bLl + s.bP2;
        zLl = s.bLl - llInLb * sumLb;
        const double toP2 = s.bP2 - p2InLb * sumLb;
        const double vP2  = 0.5 * (toP2 + s.bP2);
        zCw = toP2 + s.bP2 - zCw;

        return -vP2; // series adaptor orientation: physical load voltage is -v(P2)
    }

    TransformerParams params {};
    JaCore core;

    // derived
    double K = 1.0, lN = 1.0, halfT = 0.0, rk = 0.0, invKRk = 1.0;
    double rCc = 0.0, rS = 1.0, rLl = 1.0, gP2 = 1.0, rP2 = 1.0, cwToP2 = 0.0, rLb = 1.0;
    double r0 = 1.0, sToP = 0.5, lbToP = 0.5, ccInS = 0.0, llInLb = 0.0, p2InLb = 0.0;

    // state
    double H = 0.0, M = 0.0, vPrev = 0.0;
    double zCc = 0.0, zLl = 0.0, zCw = 0.0;
};

} // namespace umber::dsp::desk
