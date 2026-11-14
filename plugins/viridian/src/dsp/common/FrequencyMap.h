#pragma once

#include "dsp/common/Polynomial.h"

#include <array>
#include <complex>
#include <span>

namespace viridian::dsp
{

/**
    A rational M (t) = t (1 + a1 t + ...) / (1 + b1 t + ... + bD t^D), t = sin^2 (w / 2), fitted to
    the exact axis (Omega / 2 fs)^2 = asin^2 (sqrt t) over the band: to 20 kHz below 64 kHz, to
    32 kHz above. An analog root r becomes the D roots of M (t) = -(r / 2 fs)^2, each mapped to z
    by z + 1/z = 2 - 4 t; the magnitude is then exact up to M's frequency error
    (tools/netlist/README.md). prepare() is not real-time safe; the rest is.
*/
class FrequencyMap
{
public:
    static constexpr int maxDegree = 3;
    static constexpr double targetError = 2.0e-5;

    void prepare (double sampleRate);

    double sampleRate() const noexcept { return fs; }
    int degree() const noexcept { return d; }
    double bandTopHz() const noexcept { return topHz; }
    double maxRelativeError() const noexcept { return error; }   // of Omega^2 over the band

    /** M (t), normalised: (Omega / 2 fs)^2. */
    double map (double t) const noexcept;

    /** The analog angular frequency (rad/s) whose magnitude the digital filter shows at `hz`. */
    double analogOmega (double hz) const noexcept;

    /**
        Continuity of one analog root's digital roots between designs. Inside 0.8 branchRadius()
        the primary is the one on the inverse map's own branch through r = 0 (its power series
        converges there); outside, a moving root keeps as primary the digital root nearest its
        previous primary, so the main stage never jumps.
    */
    struct Track
    {
        std::complex<double> analog {}, z {};
        bool infinite = false;
        std::array<std::complex<double>, maxDegree> t {};
        bool valid = false;
    };

    /**
        The degree() digital roots of one analog root alpha / beta (beta = 0: at infinity), inside
        the unit circle, the primary root first: the one on the branch of the inverse map that
        carries the root's own response; the others correct the map's error and stay near the
        map's fixed roots. `mirror` puts the primary outside the unit circle (a right half-plane
        zero keeps its phase).
    */
    void digitalRoots (poly::ProjectiveRoot root, bool mirror, std::span<std::complex<double>> z, Track& track) const noexcept;

    /** |r| (rad/s) of the nearest branch point of the inverse map: below it, primaries are unambiguous. */
    double branchRadius() const noexcept { return branch; }

    /** z from t: the root of z + 1/z = 2 - 4 t inside the unit circle (t = infinity gives 0). */
    static std::complex<double> zFromT (std::complex<double> t) noexcept;

private:
    double fit (int degree, std::array<double, maxDegree>& a, std::array<double, maxDegree + 1>& b) const;

    static constexpr int seriesTerms = 48;

    /** All t roots and their z for an analog root (r, or infinity), warm from `t`. */
    int rootsOf (poly::ProjectiveRoot root, std::array<std::complex<double>, maxDegree>& t,
                 std::array<std::complex<double>, maxDegree>& z, bool warm) const noexcept;

    /** z of the primary on the inverse series' branch (|r| < branch radius). */
    std::complex<double> seriesPrimary (std::complex<double> r) const noexcept;

    /** z of the primary continued radially outwards from inside the series' radius. */
    std::complex<double> radialPrimary (std::complex<double> r) const noexcept;

    double fs = 0.0, topHz = 0.0, error = 0.0, branch = 0.0;
    std::array<double, seriesTerms + 1> inverse {};      // t (w) = sum inverse[k] w^k, M (t (w)) = w
    std::complex<double> infiniteReference {};          // the primary's z at r = infinity
    int d = 0;
    std::array<double, maxDegree> num {};        // 1, a1, ... : M = t * (num[0] + num[1] t + ...)
    std::array<double, maxDegree + 1> den {};    // 1, b1, ..., bD
};

} // namespace viridian::dsp
