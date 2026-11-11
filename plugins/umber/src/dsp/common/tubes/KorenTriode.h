#pragma once

#include "dsp/common/tubes/TubeMath.h"

#include <algorithm>
#include <cmath>

namespace umber::dsp::tubes
{

/**
    Thermionic grid-to-cathode conduction (the "grid diode").

    Koren's SPICE models use an ideal diode (IS = 1 nA) behind a resistor RGI. We use the smooth
    equivalent

        Ig = softplus (c * (Vgk - v0)) / (c * RGI)

    which is an exponential below v0 (slope c = q / kT_cathode, ~1/86 mV for an oxide cathode at
    ~1000 K, cf. Dempwolf & Zoelzer, DAFx 2011) and becomes the RGI resistor above it. Being smooth
    it keeps Newton-Raphson well conditioned and adds no needless high-order curvature (aliasing).
*/
struct GridDiode
{
    double rgi   = 2000.0; // ohms, Koren table 1
    double slope = 11.6;   // 1/V
    double v0    = 0.2;    // V, knee

    struct Result
    {
        double current, conductance;
    };

    Result evaluate (double vgk, const SoftplusPowerTable& softplus) const noexcept
    {
        const double a = slope * (vgk - v0);
        if (a < -40.0)
            return { 0.0, 0.0 }; // < 1e-19 A: exactly off

        const auto sp = softplus (a); // n = 1: softplus and the sigmoid
        return { sp.value / (slope * rgi), sp.slope / rgi };
    }
};

/**
    Norman Koren's triode plate-current model ("Improved vacuum tube models for SPICE
    simulations", Glass Audio 8(5), 1996), with the grid diode above:

        E1 = (Vpk / Kp) * ln (1 + exp (Kp * (1/mu + Vgk / sqrt (Kvb + Vpk^2))))
        Ip = 2 * E1^Ex / Kg1          (E1 > 0, else 0)

    evaluated as Ip = 2/Kg1 * (Vpk/Kp)^Ex * softplus (a)^Ex with both powers from shared tables
    (PowerTable, SoftplusPowerTable: no exp / log on the solver's critical path), plus a
    1 GOhm plate-cathode leak (as in Koren's netlists) so the Jacobian is never singular.
    Defaults are Koren's published 12AX7 parameters.
*/
struct KorenTriode
{
    double mu  = 100.0;
    double ex  = 1.4;
    double kg1 = 1060.0;
    double kp  = 600.0;
    double kvb = 300.0;

    GridDiode grid;

    // Shared tables for the exponent above (ex is fixed once the tube is constructed).
    const SoftplusPowerTable* softplus = &SoftplusPowerTable::forExponent (1.0);
    const SoftplusPowerTable* plateCurve = &SoftplusPowerTable::forExponent (ex);
    const PowerTable* power = &PowerTable::forExponent (ex);

    struct Result
    {
        double ig, ip;                       // grid and plate current, A (flowing into the tube)
        double dIgDvgk;                      // dIg/dVgk
        double dIpDvgk, dIpDvpk;             // plate current partials
    };

    Result evaluate (double vgk, double vpk) const noexcept
    {
        constexpr double leak = 1.0e-9;

        const auto g = grid.evaluate (vgk, *softplus);
        Result r { g.current, leak * vpk, g.conductance, 0.0, leak };

        if (vpk <= 1.0e-6)
            return r;

        // Ip = 2/Kg1 (Vpk/Kp)^Ex P(a), P = softplus^Ex: two table lookups in parallel
        const double s2   = kvb + vpk * vpk;
        const double invS = 1.0 / std::sqrt (s2);
        const double a    = kp * (1.0 / mu + vgk * invS);
        const auto   p    = (*plateCurve) (a);
        const double k    = 2.0 / kg1 * (*power) (vpk / kp); // dIp/dP

        const double ip       = k * p.value;
        const double dIpDa    = k * p.slope;
        const double dAdVgk   = kp * invS;
        const double dAdVpk   = -kp * vgk * vpk * invS / s2;

        r.ip      += ip;
        r.dIpDvgk  = dIpDa * dAdVgk;
        r.dIpDvpk += ex * ip / vpk + dIpDa * dAdVpk;
        return r;
    }

    /**
        Newton iterate limiter for the plate-cathode voltage. Ip ~ Vpk^Ex is flat at Vpk -> 0 and
        zero below, so an unlimited Newton step from a saturated operating point can land at
        Vpk < 0 and bounce back (a 2-cycle). Letting Vpk fall by at most 4x per iteration (as
        SPICE limits junction voltages) makes the iteration approach the knee monotonically.
    */
    static double limitPlate (double previous, double next) noexcept
    {
        return previous > 0.0 ? std::max (next, 0.25 * previous) : next;
    }
};

} // namespace umber::dsp::tubes
