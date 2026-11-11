#pragma once

#include "dsp/common/tubes/KorenTriode.h"

#include <cmath>
#include <numbers>

namespace umber::dsp::tubes
{

/**
    Koren's pentode model (same 1996 paper; netlist form in his tube library):

        E1  = (Vg2k / Kp) * ln (1 + exp (Kp * (1/mu + Vg1k / Vg2k)))
        Ik  = 2 * E1^Ex / Kg1                               (space-charge limited current)
        Ip  = Ik * atan (Vpk / Kvb)
        Ig2 = (Vg2k / mu + Vg1k)^Ex / Kg2

    Since E1 -> Vg2k/mu + Vg1k whenever the tube conducts (Kp large), Ig2 is evaluated as
    E1^Ex / Kg2 = Ik * Kg1 / (2 Kg2): identical in the conducting region, smooth (instead of a
    hard max (0, .)) at cutoff, and free.

    Extension (see fuzz/NOTES.md): Koren's screen current ignores current division, i.e. that
    electrons which can no longer reach a bottomed-out plate land on the screen instead. Real
    power pentodes show a sharp screen-current rise below the knee, which is what makes the screen
    supply sag when the stage is slammed. We add

        Ig2 += kDiv * Ik * (pi/2 - atan (Vpk / Kvb))

    which reuses Ik and the atan and vanishes above the knee, so Koren's fit of the normal
    operating region is untouched.

    Defaults: EL84 / 6BQ5, fitted to the Philips datasheet class-A point (Va = Vg2 = 250 V,
    Vg1 = -7.3 V: Ia = 48 mA, Ig2 = 5.5 mA, gm = 11.3 mA/V, mu_g1g2 = 19); see fuzz/NOTES.md.
*/
struct KorenPentode
{
    double mu   = 19.0;
    double ex   = 1.35;
    double kg1  = 659.0;
    double kg2  = 2774.0;
    double kp   = 200.0;
    double kvb  = 30.0;
    double kDiv = 0.4;

    GridDiode grid { 1000.0, 11.6, 0.2 };

    // Shared tables for the exponent above (ex is fixed once the tube is constructed).
    const SoftplusPowerTable* softplus = &SoftplusPowerTable::forExponent (1.0);
    const SoftplusPowerTable* plateCurve = &SoftplusPowerTable::forExponent (ex);
    const PowerTable* power = &PowerTable::forExponent (ex);
    const AtanTable* atanTable = &AtanTable::instance();

    struct Result
    {
        double ig1, ig2, ip;
        // partials with respect to (Vg1k, Vg2k, Vpk)
        double dIg1[3], dIg2[3], dIp[3];
    };

    Result evaluate (double vg1k, double vg2k, double vpk) const noexcept
    {
        constexpr double leak = 1.0e-9;

        const auto g = grid.evaluate (vg1k, *softplus);
        Result r {};
        r.ig1     = g.current;
        r.dIg1[0] = g.conductance;
        r.ip      = leak * vpk;
        r.dIp[2]  = leak;
        r.ig2     = leak * vg2k;
        r.dIg2[1] = leak;

        // screen below ~1 V: the tube is starved and conducts nothing (keeps Vg1k / Vg2k finite)
        if (vg2k <= 1.0)
            return r;

        // space-charge current Ik = 2/Kg1 (Vg2k/Kp)^Ex P(a), P = softplus^Ex (two table lookups)
        const double invG2 = 1.0 / vg2k;
        const double a     = kp * (1.0 / mu + vg1k * invG2);
        const auto   p     = (*plateCurve) (a);
        const double k     = 2.0 / kg1 * (*power) (vg2k / kp); // dIk/dP
        const double ik    = k * p.value;

        const double dIkDa   = k * p.slope;
        const double dIkDvg1 = dIkDa * kp * invG2;
        const double dIkDvg2 = ex * ik * invG2 - dIkDa * kp * vg1k * invG2 * invG2;

        // plate knee
        const double q   = vpk / kvb;
        const double at  = (*atanTable) (q);
        const double dAt = 1.0 / (kvb * (1.0 + q * q));

        r.ip     += ik * at;
        r.dIp[0]  = dIkDvg1 * at;
        r.dIp[1]  = dIkDvg2 * at;
        r.dIp[2] += ik * dAt;

        // screen: Koren's (Vg2k/mu + Vg1k)^Ex / Kg2, evaluated through the softplus-smoothed E1
        // (E1 -> Vg2k/mu + Vg1k above cutoff), so it costs nothing extra; plus current division
        const double screenShare = kg1 / (2.0 * kg2) + kDiv * (0.5 * std::numbers::pi - at);

        r.ig2     += ik * screenShare;
        r.dIg2[0]  = dIkDvg1 * screenShare;
        r.dIg2[1] += dIkDvg2 * screenShare;
        r.dIg2[2]  = -ik * kDiv * dAt;
        return r;
    }

    /**
        Newton iterate limiter for the plate-cathode voltage. Ik * atan (Vpk / Kvb) is flat on
        both sides of the knee, so from a saturated point an unlimited step can jump across
        0 V into the other flat region and back (a 2-cycle, typical with the inductive plate
        load in flyback). Same remedy as the triode: Vpk may fall by at most 4x per iteration.
    */
    static double limitPlate (double previous, double next) noexcept
    {
        return KorenTriode::limitPlate (previous, next);
    }
};

} // namespace umber::dsp::tubes
