#include "dsp/common/MatchedSection.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

namespace viridian::dsp
{

using Complex = std::complex<double>;

std::complex<double> AnalogSection::response (double hz) const noexcept
{
    const Complex sigma { 0.0, 2.0 * std::numbers::pi * hz / omegaScale };
    const auto n = num[0] + sigma * (num[1] + sigma * num[2]);
    const auto d = den[0] + sigma * (den[1] + sigma * den[2]);
    return n / d;
}

namespace
{
    /** s' = j tan (pi f / fs) / g: the TPT integrators' frequency variable. */
    Complex tptVariable (double hz, double fs, double g) noexcept
    {
        const auto x = std::clamp (hz / fs, 0.0, 0.5 - 1.0e-12);
        return { 0.0, std::tan (std::numbers::pi * x) / g };
    }

    /** The analog roots of a real quadratic, finite ones first, as exactly real where real. */
    std::array<poly::ProjectiveRoot, 2> analogRoots (const std::array<double, 3>& c, double omegaScale) noexcept
    {
        std::array<poly::ProjectiveRoot, 2> r;
        poly::quadraticRoots (c[0], c[1], c[2], r[0], r[1]);
        for (auto& x : r)
        {
            x.alpha *= omegaScale;
            if (std::abs (x.alpha.imag()) <= 1.0e-12 * std::abs (x.alpha))
                x.alpha = { x.alpha.real(), 0.0 };
            if (std::abs (x.beta.imag()) <= 1.0e-12 * std::abs (x.beta))
                x.beta = { x.beta.real(), 0.0 };
        }
        if (r[0].isInfinite() && ! r[1].isInfinite())
            std::swap (r[0], r[1]);
        return r;
    }

    bool isConjugatePair (const poly::ProjectiveRoot& a, const poly::ProjectiveRoot& b) noexcept
    {
        if (a.isInfinite() || b.isInfinite())
            return false;
        const auto x = a.value(), y = b.value();
        return std::abs (x.imag()) > 1.0e-12 * std::abs (x) && std::abs (y - std::conj (x)) <= 1.0e-9 * std::abs (x);
    }

    /** prod (1 - r_k z^-1), ascending in z^-1, real parts (the roots are closed under conjugation). */
    template <size_t N>
    void expand (const Complex* roots, int count, std::array<double, N>& out) noexcept
    {
        std::array<Complex, N> p {};
        p[0] = 1.0;
        for (int k = 0; k < count; ++k)
            for (int j = k + 1; j >= 1; --j)
                p[(size_t) j] -= roots[k] * p[(size_t) j - 1];
        for (size_t j = 0; j < N; ++j)
            out[j] = j <= (size_t) count ? p[j].real() : 0.0;
    }

    /** Per-sample coefficients of the main stage and the correction. */
    struct Coefficients
    {
        int order = 2, correctionOrder = 0;
        double c1 = 0.0, c2 = 0.0, c3 = 0.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
        const double* b = nullptr;
        const double* a = nullptr;
    };

    Coefficients coefficientsOf (const MatchedSection::Stage& st, const MatchedSection::Correction& cr) noexcept
    {
        Coefficients c;
        c.order = st.order;
        c.m0 = st.m0;
        c.m1 = st.m1;
        c.m2 = st.m2;
        if (st.order == 1)
            c.c1 = st.g / (1.0 + st.g);
        else
        {
            c.c1 = 1.0 / (1.0 + st.g * (st.g + st.k));
            c.c2 = st.g * c.c1;
            c.c3 = st.g * c.c2;
        }
        c.correctionOrder = cr.order;
        c.b = cr.b.data();
        c.a = cr.a.data();
        return c;
    }

    template <typename State>
    double tick (State& s, double x, const Coefficients& c) noexcept
    {
        double y;
        if (c.order == 1)
        {
            const auto v = (x - s.s1) * c.c1;
            const auto lp = v + s.s1;
            s.s1 = lp + v;
            y = c.m0 * x + c.m1 * lp;
        }
        else
        {
            const auto v3 = x - s.s2;
            const auto v1 = c.c1 * s.s1 + c.c2 * v3;
            const auto v2 = s.s2 + c.c2 * s.s1 + c.c3 * v3;
            s.s1 = 2.0 * v1 - s.s1;
            s.s2 = 2.0 * v2 - s.s2;
            y = c.m0 * x + c.m1 * v1 + c.m2 * v2;
        }
        if (c.correctionOrder > 0)
        {
            // Transposed direct form II.
            const int n = c.correctionOrder;
            const auto out = c.b[0] * y + s.z[0];
            for (int k = 1; k < n; ++k)
                s.z[(size_t) k - 1] = c.b[k] * y - c.a[k] * out + s.z[(size_t) k];
            s.z[(size_t) n - 1] = c.b[n] * y - c.a[n] * out;
            y = out;
        }
        return y;
    }
} // namespace

std::complex<double> MatchedSection::Stage::response (double hz, double sampleRate) const noexcept
{
    const auto s = tptVariable (hz, sampleRate, g);
    if (order == 1)
        return m0 + m1 / (1.0 + s);
    return m0 + (m1 * s + m2) / (s * s + k * s + 1.0);
}

std::complex<double> MatchedSection::Correction::response (double hz, double sampleRate) const noexcept
{
    const auto zi = std::polar (1.0, -2.0 * std::numbers::pi * hz / sampleRate);
    Complex n {}, d {};
    for (int k = order; k >= 0; --k)
    {
        n = n * zi + b[(size_t) k];
        d = d * zi + a[(size_t) k];
    }
    return n / d;
}

void MatchedSection::design (const FrequencyMap& map, const AnalogSection& s, int rampSamples) noexcept
{
    const auto previous = target;
    const auto previousCorrection = targetCorrection;
    fs = map.sampleRate();
    section = s;
    reflected = false;
    const int D = std::clamp (map.degree(), 1, FrequencyMap::maxDegree);

    const auto poles = analogRoots (s.den, s.omegaScale);
    const auto zeros = analogRoots (s.num, s.omegaScale);
    const int n = std::clamp ((poles[0].isInfinite() ? 0 : 1) + (poles[1].isInfinite() ? 0 : 1), 1, 2);
    const bool structural = n != lastOrder || D != lastDegree;
    if (structural)
        poleTracks = zeroTracks = {};
    lastOrder = n;
    lastDegree = D;

    // Digital roots per analog root, primary first: [root][branch]. Each root continues the
    // track of the previous root nearest it.
    using RootSet = std::array<std::array<Complex, FrequencyMap::maxDegree>, 2>;
    RootSet dp {}, dz {};
    auto mapRoots = [&] (const std::array<poly::ProjectiveRoot, 2>& analog, bool areZeros, RootSet& out,
                         std::array<FrequencyMap::Track, 2>& tracks)
    {
        auto distance = [] (const poly::ProjectiveRoot& a, const FrequencyMap::Track& t)
        {
            if (! t.valid)
                return 0.0;
            if (a.isInfinite() || t.infinite)
                return a.isInfinite() && t.infinite ? 0.0 : 1.0e300;
            return std::abs (a.value() - t.analog);
        };
        if (n == 2 && distance (analog[0], tracks[1]) + distance (analog[1], tracks[0])
                          < distance (analog[0], tracks[0]) + distance (analog[1], tracks[1]))
            std::swap (tracks[0], tracks[1]);

        if (n == 2 && isConjugatePair (analog[0], analog[1]))
        {
            const int upper = analog[0].value().imag() > 0.0 ? 0 : 1;
            const bool rhp = analog[(size_t) upper].value().real() > 0.0;
            reflected = reflected || (rhp && ! areZeros);
            map.digitalRoots (analog[(size_t) upper], areZeros && rhp, out[(size_t) upper], tracks[(size_t) upper]);
            for (int k = 0; k < D; ++k)
                out[(size_t) (1 - upper)][(size_t) k] = std::conj (out[(size_t) upper][(size_t) k]);
            tracks[(size_t) (1 - upper)] = tracks[(size_t) upper];
            tracks[(size_t) (1 - upper)].z = std::conj (tracks[(size_t) upper].z);
            tracks[(size_t) (1 - upper)].analog = std::conj (tracks[(size_t) upper].analog);
            for (auto& x : tracks[(size_t) (1 - upper)].t)
                x = std::conj (x);
            return;
        }
        for (int r = 0; r < n; ++r)
        {
            const auto& root = analog[(size_t) r];
            const bool rhp = ! root.isInfinite() && root.value().real() > 0.0;
            reflected = reflected || (rhp && ! areZeros);
            map.digitalRoots (root, areZeros && rhp, out[(size_t) r], tracks[(size_t) r]);
        }
    };
    mapRoots (poles, false, dp, poleTracks);
    mapRoots (zeros, true, dz, zeroTracks);

    // Main stage from the primary roots: products of (1 -/+ z) keep low poles at high rates exact.
    target.order = n;
    if (n == 1)
    {
        const auto zp = dp[0][0].real(), zz = dz[0][0].real();
        target.g = std::clamp ((1.0 - zp) / (1.0 + zp), 1.0e-15, 1.0e15);
        target.k = 0.0;
        target.m0 = (1.0 + zz) / (1.0 + zp);
        target.m1 = (1.0 - zz) / (1.0 - zp) - target.m0;
        target.m2 = 0.0;
    }
    else
    {
        const auto p0 = dp[0][0], p1 = dp[1][0], z0 = dz[0][0], z1 = dz[1][0];
        const auto A0 = std::max (((1.0 - p0) * (1.0 - p1)).real(), 1.0e-300);
        const auto A1 = std::max (((1.0 + p0) * (1.0 + p1)).real(), 1.0e-300);
        const auto C = (1.0 - p0 * p1).real();
        const auto B0 = ((1.0 - z0) * (1.0 - z1)).real();
        const auto B1 = ((1.0 + z0) * (1.0 + z1)).real();
        const auto CN = (1.0 - z0 * z1).real();
        target.g = std::clamp (std::sqrt (A0 / A1), 1.0e-15, 1.0e15);
        target.k = 2.0 * C / (A1 * target.g);
        target.m0 = B1 / A1;
        target.m1 = target.g * 2.0 * CN / A0 - target.m0 * target.k;
        target.m2 = B0 / A0 - target.m0;
    }

    // The correcting roots as one filter with unity gain at DC (they stay away from z = 1).
    std::array<Complex, maxRoots> cp {}, cz {};
    int count = 0;
    for (size_t r = 0; r < dp.size() && (int) r < n; ++r)
        for (size_t k = 1; k < dp[r].size() && (int) k < D && count < maxRoots; ++k, ++count)
        {
            cp[(size_t) count] = dp[r][k];
            cz[(size_t) count] = dz[r][k];
        }
    targetCorrection.order = count;
    expand (cp.data(), count, targetCorrection.a);
    expand (cz.data(), count, targetCorrection.b);
    double sa = 0.0, sb = 0.0;
    for (int k = 0; k <= count; ++k)
    {
        sa += targetCorrection.a[(size_t) k];
        sb += targetCorrection.b[(size_t) k];
    }
    if (std::abs (sb) > 0.0)
        for (auto& v : targetCorrection.b)
            v *= sa / sb;

    // The gain: exact at a reference frequency (|H_d| = |H_a (Omega (f))| holds at every f); the
    // lowest candidate where the section is not near a zero, so the sign (the phase there agrees
    // to well within 90 degrees) is unambiguous.
    const std::array<double, 4> candidates { 0.0005 * fs, 0.005 * fs, 0.05 * fs, 0.2 * fs };
    std::array<Complex, 4> analogAt {};
    double largest = 0.0;
    for (size_t c = 0; c < candidates.size(); ++c)
    {
        analogAt[c] = section.response (map.analogOmega (candidates[c]) / (2.0 * std::numbers::pi));
        largest = std::max (largest, std::abs (analogAt[c]));
    }
    double gain = 0.0;
    for (size_t c = 0; c < candidates.size(); ++c)
    {
        if (std::abs (analogAt[c]) < 1.0e-3 * largest)
            continue;
        const auto digital = target.response (candidates[c], fs) * targetCorrection.response (candidates[c], fs);
        if (! (std::abs (digital) > 0.0))
            continue;
        gain = std::abs (analogAt[c]) / std::abs (digital);
        if ((analogAt[c] * std::conj (digital)).real() < 0.0)
            gain = -gain;
        break;
    }
    target.m0 *= gain;
    target.m1 *= gain;
    target.m2 *= gain;

    if (! designed || structural || rampSamples <= 0)
        restAtTarget();
    else
    {
        shiftPast (previous, previousCorrection);
        rampLength = rampSamples;
        rampPosition = 0;
        settle = spline::settleSegments;
    }
    designed = true;
}

void MatchedSection::blend (double t) noexcept
{
    std::array<double, spline::points> w;
    spline::weights (t, w);
    auto mix = [&] (double latest, auto field)
    {
        double v = w.back() * latest;
        for (size_t q = 0; q < past.size(); ++q)
            v += w[q] * field (q);
        return v;
    };
    now.order = target.order;
    now.g = mix (target.g, [&] (size_t q) { return past[q].g; });
    now.k = mix (target.k, [&] (size_t q) { return past[q].k; });
    now.m0 = mix (target.m0, [&] (size_t q) { return past[q].m0; });
    now.m1 = mix (target.m1, [&] (size_t q) { return past[q].m1; });
    now.m2 = mix (target.m2, [&] (size_t q) { return past[q].m2; });
    nowCorrection.order = targetCorrection.order;
    for (size_t k = 0; k < nowCorrection.b.size(); ++k)
    {
        nowCorrection.b[k] = mix (targetCorrection.b[k], [&] (size_t q) { return pastCorrection[q].b[k]; });
        nowCorrection.a[k] = mix (targetCorrection.a[k], [&] (size_t q) { return pastCorrection[q].a[k]; });
    }
}

void MatchedSection::restAtTarget() noexcept
{
    now = target;
    nowCorrection = targetCorrection;
    past.fill (target);
    pastCorrection.fill (targetCorrection);
    rampLength = rampPosition = 0;
    settle = 0;
}

void MatchedSection::shiftPast (const Stage& latest, const Correction& latestCorrection) noexcept
{
    for (size_t k = 0; k + 1 < past.size(); ++k)
    {
        past[k] = past[k + 1];
        pastCorrection[k] = pastCorrection[k + 1];
    }
    past.back() = latest;
    pastCorrection.back() = latestCorrection;
}

void MatchedSection::restart() noexcept
{
    poleTracks = zeroTracks = {};
    lastOrder = lastDegree = 0;
    designed = false;
}

void MatchedSection::reset() noexcept
{
    state = {};
    restAtTarget();
}

void MatchedSection::flushDenormals() noexcept
{
    auto flush = [] (double& v)
    {
        if (std::abs (v) < 1.0e-30)
            v = 0.0;
    };
    for (auto& s : state)
    {
        flush (s.s1);
        flush (s.s2);
        for (auto& z : s.z)
            flush (z);
    }
}

void MatchedSection::process (double* const* channels, int numChannels, int n) noexcept
{
    numChannels = std::min (numChannels, maxChannels);
    int i = 0;
    while (i < n && (rampPosition < rampLength || settle > 0))
    {
        if (rampPosition == rampLength)
        {
            // No new design: the next segments settle on the last.
            shiftPast (target, targetCorrection);
            rampPosition = 0;
            --settle;
        }
        ++rampPosition;
        // At rest the section runs the design itself, not a weighted sum of its copies.
        if (rampPosition == rampLength && settle == 0)
        {
            now = target;
            nowCorrection = targetCorrection;
        }
        else
            blend ((double) rampPosition / (double) rampLength);
        const auto c = coefficientsOf (now, nowCorrection);
        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] = tick (state[(size_t) ch], channels[ch][i], c);
        ++i;
    }
    if (i >= n)
        return;

    const auto c = coefficientsOf (now, nowCorrection);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& s = state[(size_t) ch];
        auto* x = channels[ch];
        for (int t = i; t < n; ++t)
            x[t] = tick (s, x[t], c);
    }
}

std::complex<double> MatchedSection::response (double hz) const noexcept
{
    return target.response (hz, fs) * targetCorrection.response (hz, fs);
}

} // namespace viridian::dsp
