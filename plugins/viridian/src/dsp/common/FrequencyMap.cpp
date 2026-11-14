#include "dsp/common/FrequencyMap.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>
#include <vector>

namespace viridian::dsp
{

namespace
{
    double trueMap (double t) noexcept
    {
        const auto a = std::asin (std::sqrt (t));
        return a * a;
    }

    /** Solves the n x n system in place (partial pivoting); false when singular. */
    bool solve (std::array<std::array<double, 6>, 5>& m, int n) noexcept
    {
        for (int col = 0; col < n; ++col)
        {
            int pivot = col;
            for (int r = col + 1; r < n; ++r)
                if (std::abs (m[(size_t) r][(size_t) col]) > std::abs (m[(size_t) pivot][(size_t) col]))
                    pivot = r;
            if (myzel::dsp::isExactly (m[(size_t) pivot][(size_t) col], 0.0))
                return false;
            std::swap (m[(size_t) pivot], m[(size_t) col]);
            for (int r = 0; r < n; ++r)
            {
                if (r == col)
                    continue;
                const auto f = m[(size_t) r][(size_t) col] / m[(size_t) col][(size_t) col];
                for (int k = col; k <= n; ++k)
                    m[(size_t) r][(size_t) k] -= f * m[(size_t) col][(size_t) k];
            }
        }
        for (int r = 0; r < n; ++r)
            m[(size_t) r][(size_t) n] /= m[(size_t) r][(size_t) r];
        return true;
    }
} // namespace

double FrequencyMap::fit (int degree, std::array<double, maxDegree>& a, std::array<double, maxDegree + 1>& b) const
{
    const int na = degree - 1, nb = degree, unknowns = na + nb;
    const auto top = std::sin (std::numbers::pi * topHz / fs);
    const auto T = top * top;
    constexpr int points = 1024;

    std::vector<double> t (points), w (points), lambda (points, 1.0 / points);
    for (int k = 0; k < points; ++k)
    {
        // Denser towards the band top, where the exact axis bends towards its branch point at t = 1.
        t[(size_t) k] = T * std::sin (0.5 * std::numbers::pi * (k + 0.5) / points);
        w[(size_t) k] = trueMap (t[(size_t) k]);
    }

    auto numerator = [&] (const std::array<double, maxDegree>& x, double tt)
    {
        double v = 0.0;
        for (int i = na; i >= 1; --i)
            v = (v + x[(size_t) i]) * tt;
        return tt * (1.0 + v);
    };
    auto denominator = [&] (const std::array<double, maxDegree + 1>& y, double tt)
    {
        double v = 0.0;
        for (int j = nb; j >= 1; --j)
            v = (v + y[(size_t) j]) * tt;
        return 1.0 + v;
    };
    auto valid = [&] (const std::array<double, maxDegree>& x, const std::array<double, maxDegree + 1>& y)
    {
        double previous = 0.0;
        for (int k = 1; k <= 1000; ++k)
        {
            const auto tt = k / 1000.0;
            const auto dv = denominator (y, tt);
            const auto nv = numerator (x, tt);
            if (dv <= 0.0 || nv <= 0.0 || nv / dv <= previous)
                return false;
            previous = nv / dv;
        }
        return true;
    };

    std::array<double, maxDegree> x {};
    std::array<double, maxDegree + 1> y {};
    x[0] = y[0] = 1.0;
    double best = 1.0e300;

    for (int iteration = 0; iteration < 150; ++iteration)
    {
        // Linearised relative error (t - w + sum a_i t^(i+1) - w sum b_j t^j) / (w D_prev (t)).
        std::array<std::array<double, 6>, 5> m {};
        for (int k = 0; k < points; ++k)
        {
            const auto tt = t[(size_t) k], ww = w[(size_t) k];
            const auto scale = 1.0 / (ww * denominator (y, tt));
            std::array<double, 5> row {};
            double p = tt * tt;
            for (int i = 0; i < na; ++i, p *= tt)
                row[(size_t) i] = p * scale;
            p = tt;
            for (int j = 0; j < nb; ++j, p *= tt)
                row[(size_t) (na + j)] = -ww * p * scale;
            const auto rhs = -(tt - ww) * scale;
            const auto l = lambda[(size_t) k];
            for (int r = 0; r < unknowns; ++r)
            {
                for (int c = 0; c < unknowns; ++c)
                    m[(size_t) r][(size_t) c] += l * row[(size_t) r] * row[(size_t) c];
                m[(size_t) r][(size_t) unknowns] += l * row[(size_t) r] * rhs;
            }
        }
        if (! solve (m, unknowns))
            break;

        std::array<double, maxDegree> nx {};
        std::array<double, maxDegree + 1> ny {};
        nx[0] = ny[0] = 1.0;
        for (int i = 0; i < na; ++i)
            nx[(size_t) i + 1] = m[(size_t) i][(size_t) unknowns];
        for (int j = 0; j < nb; ++j)
            ny[(size_t) j + 1] = m[(size_t) (na + j)][(size_t) unknowns];

        double worst = 0.0, sum = 0.0;
        for (int k = 0; k < points; ++k)
        {
            const auto e = std::abs (numerator (nx, t[(size_t) k]) / (w[(size_t) k] * denominator (ny, t[(size_t) k])) - 1.0);
            worst = std::max (worst, e);
            lambda[(size_t) k] *= e + 1.0e-18;
            sum += lambda[(size_t) k];
        }
        for (auto& l : lambda)
            l /= sum;

        x = nx;
        y = ny;
        if (worst < best && valid (nx, ny))
        {
            best = worst;
            a = nx;
            b = ny;
        }
    }
    return best;
}

void FrequencyMap::prepare (double sampleRate)
{
    assert (sampleRate > 0.0);
    fs = sampleRate;
    // The gate band; at high rates the band reaches past the top bands' centres (26 kHz).
    topHz = std::min (fs < 64000.0 ? 20000.0 : 32000.0, 0.46 * fs);

    d = 0;
    error = 1.0e300;
    for (int degree = 1; degree <= maxDegree; ++degree)
    {
        std::array<double, maxDegree> a {};
        std::array<double, maxDegree + 1> b {};
        const auto e = fit (degree, a, b);
        if (e < error)
        {
            d = degree;
            error = e;
            num = a;
            den = b;
        }
        if (e <= targetError)
            break;
    }
    assert (d > 0);

    // Branch points of the inverse map: the critical values of M, where M' = Mn' Md - Mn Md' = 0.
    std::array<double, maxDegree + 1> mn {}, md {};
    for (int k = 1; k <= d; ++k)
        mn[(size_t) k] = num[(size_t) k - 1];
    for (int k = 0; k <= d; ++k)
        md[(size_t) k] = den[(size_t) k];
    std::array<std::complex<double>, 2 * maxDegree> derivative {};
    for (int i = 0; i <= d; ++i)
        for (int j = 0; j <= d; ++j)
        {
            if (i >= 1)
                derivative[(size_t) (i - 1 + j)] += i * mn[(size_t) i] * md[(size_t) j];
            if (j >= 1)
                derivative[(size_t) (i + j - 1)] -= j * mn[(size_t) i] * md[(size_t) j];
        }
    std::array<std::complex<double>, poly::maxDegree> critical {};
    const int nc = poly::roots ({ derivative.data(), (size_t) (2 * d) }, critical, false);
    branch = 1.0e300;
    for (int k = 0; k < nc; ++k)
    {
        const auto t = critical[(size_t) k];
        std::complex<double> n {}, m {};
        for (int j = d; j >= 0; --j)
        {
            n = n * t + mn[(size_t) j];
            m = m * t + md[(size_t) j];
        }
        if (std::abs (m) > 0.0)
            branch = std::min (branch, 2.0 * fs * std::sqrt (std::abs (n / m)));
    }

    // The inverse map's power series about 0 by reversion: t (w) with M (t (w)) = w.
    constexpr int N = seriesTerms;
    std::array<double, N + 1> m {}, reciprocal {};
    reciprocal[0] = 1.0;
    for (int k = 1; k <= N; ++k)
    {
        double v = 0.0;
        for (int j = 1; j <= std::min (k, d); ++j)
            v -= md[(size_t) j] * reciprocal[(size_t) (k - j)];
        reciprocal[(size_t) k] = v;
    }
    for (int k = 1; k <= N; ++k)
        for (int j = 1; j <= std::min (k, d); ++j)
            m[(size_t) k] += mn[(size_t) j] * reciprocal[(size_t) (k - j)];
    inverse = {};
    inverse[1] = 1.0 / m[1];
    std::array<double, N + 1> power {}, composed {};
    for (int k = 2; k <= N; ++k)
    {
        // The w^k coefficient of M (t_(k-1) (w)), with t truncated after w^(k-1).
        composed = {};
        power = {};
        for (int i = 1; i < k; ++i)
            power[(size_t) i] = inverse[(size_t) i];
        for (int j = 1; j <= k; ++j)
        {
            for (int i = 0; i <= k; ++i)
                composed[(size_t) i] += m[(size_t) j] * power[(size_t) i];
            if (j == k)
                break;
            std::array<double, N + 1> next {};
            for (int i = 0; i <= k; ++i)
                for (int l = 1; l < k && i + l <= k; ++l)
                    next[(size_t) (i + l)] += power[(size_t) i] * inverse[(size_t) l];
            power = next;
        }
        inverse[(size_t) k] = -composed[(size_t) k] / m[1];
    }

    // A root at infinity continues the negative real axis (a real root moving up and away).
    infiniteReference = radialPrimary ({ -1.0e4 * branch, 0.0 });
}

double FrequencyMap::map (double t) const noexcept
{
    double n = 0.0, m = 0.0;
    for (int i = d - 1; i >= 0; --i)
        n = n * t + num[(size_t) i];
    for (int j = d; j >= 0; --j)
        m = m * t + den[(size_t) j];
    return t * n / m;
}

double FrequencyMap::analogOmega (double hz) const noexcept
{
    const auto s = std::sin (std::numbers::pi * std::clamp (hz / fs, 0.0, 0.5));
    return 2.0 * fs * std::sqrt (std::max (map (s * s), 0.0));
}

std::complex<double> FrequencyMap::zFromT (std::complex<double> t) noexcept
{
    const auto p = 2.0 - 4.0 * t;
    const auto root = std::sqrt (p * p - 4.0);
    const auto big = std::norm (p + root) >= std::norm (p - root) ? 0.5 * (p + root) : 0.5 * (p - root);
    const auto size = std::norm (big);
    if (! (std::isfinite (size) && size > 0.0))
        return {};
    return std::conj (big) / size;      // |big| >= 1: no library division needed
}

int FrequencyMap::rootsOf (poly::ProjectiveRoot root, std::array<std::complex<double>, maxDegree>& t,
                           std::array<std::complex<double>, maxDegree>& z, bool warm) const noexcept
{
    // M (t) = -(alpha / (2 fs beta))^2  <=>  (2 fs beta)^2 Mn (t) + alpha^2 Md (t) = 0.
    const auto twoFsBeta = 2.0 * fs * root.beta;
    auto A = twoFsBeta * twoFsBeta;
    auto B = root.alpha * root.alpha;
    const auto scale = std::max (std::abs (A), std::abs (B));
    A /= scale;
    B /= scale;
    std::array<std::complex<double>, maxDegree + 1> c {};
    for (int k = 0; k <= d; ++k)
        c[(size_t) k] = B * den[(size_t) k] + (k >= 1 ? A * num[(size_t) k - 1] : 0.0);

    const int n = poly::roots ({ c.data(), (size_t) d + 1 }, t, warm);
    if (poly::isReal (root.alpha) && poly::isReal (root.beta))
        poly::symmetrise ({ t.data(), (size_t) n });
    // Roots a degree drop removed are at t = infinity, z = 0.
    for (int k = 0; k < d; ++k)
        z[(size_t) k] = k < n ? zFromT (t[(size_t) k]) : std::complex<double> {};
    return n;
}

std::complex<double> FrequencyMap::seriesPrimary (std::complex<double> r) const noexcept
{
    const auto w = -r * r / (4.0 * fs * fs);
    std::complex<double> tw {};
    for (int k = seriesTerms; k >= 1; --k)
        tw = (tw + inverse[(size_t) k]) * w;
    return zFromT (tw);
}

std::complex<double> FrequencyMap::radialPrimary (std::complex<double> r) const noexcept
{
    const auto start = 0.7 * branch;
    auto reference = seriesPrimary (r / std::abs (r) * start);
    std::array<std::complex<double>, maxDegree> t {}, z {};
    constexpr int steps = 48;
    for (int k = 1; k <= steps; ++k)
    {
        const auto rk = r / std::abs (r) * start * std::pow (std::abs (r) / start, (double) k / steps);
        rootsOf ({ rk, 1.0 }, t, z, k > 1);
        int best = 0;
        for (int j = 1; j < d; ++j)
            if (std::abs (z[(size_t) j] - reference) < std::abs (z[(size_t) best] - reference))
                best = j;
        reference = z[(size_t) best];
    }
    return reference;
}

void FrequencyMap::digitalRoots (poly::ProjectiveRoot root, bool mirror, std::span<std::complex<double>> z, Track& track) const noexcept
{
    std::array<std::complex<double>, maxDegree> t = track.t, zs {};
    const int n = rootsOf (root, t, zs, track.valid);
    const bool real = poly::isReal (root.alpha) && poly::isReal (root.beta);

    // The primary: on the inverse series' branch inside its radius; outside, the root nearest
    // the previous primary, or for a fresh design the radial continuation of the series' branch.
    std::complex<double> reference;
    if (root.isInfinite())
        reference = infiniteReference;
    else if (std::abs (root.value()) < 0.8 * branch)
        reference = seriesPrimary (root.value());
    else if (track.valid)
        reference = track.z;
    else
        reference = radialPrimary (root.value());

    int primary = -1;
    for (int k = 0; k < d; ++k)
    {
        if (real && ! poly::isReal (zs[(size_t) k]))
            continue;
        if (primary < 0 || std::norm (zs[(size_t) k] - reference) < std::norm (zs[(size_t) primary] - reference))
            primary = k;
    }
    primary = std::max (primary, 0);
    if (primary != 0)
    {
        std::swap (zs[0], zs[(size_t) primary]);
        if (primary < n)
            std::swap (t[0], t[(size_t) primary]);
    }
    track = { root.isInfinite() ? std::complex<double> {} : root.value(), zs[0], root.isInfinite(), t, true };

    for (int k = 0; k < d; ++k)
        z[(size_t) k] = zs[(size_t) k];
    if (mirror && ! poly::isExactly (z[0], {}))
        z[0] = 1.0 / z[0];
}

} // namespace viridian::dsp
