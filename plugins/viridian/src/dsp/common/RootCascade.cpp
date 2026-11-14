#include "dsp/common/RootCascade.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace viridian::dsp
{

namespace
{
    using Complex = std::complex<double>;
    constexpr double farAway = 1.0e12;      // sigma magnitude standing in for a root at infinity

    /** log-polar distance of roots in sigma (1e-3 floor for roots at DC). */
    double distance (Complex a, bool aInfinite, Complex b, bool bInfinite) noexcept
    {
        const auto ma = aInfinite ? farAway : std::abs (a);
        const auto mb = bInfinite ? farAway : std::abs (b);
        const auto pa = aInfinite ? 0.0 : std::arg (a);
        const auto pb = bInfinite ? 0.0 : std::arg (b);
        return std::abs (std::log ((ma + 1.0e-3) / (mb + 1.0e-3))) + std::abs (std::remainder (pa - pb, 2.0 * std::numbers::pi));
    }

    /** A cold design's roots, in the order found; the labels past them are at infinity. */
    void placeCold (const std::array<Complex, poly::maxDegree>& found, int count, int degree,
                    std::array<Complex, RootCascade::maxRoots>& labels, std::array<bool, RootCascade::maxRoots>& infinite) noexcept
    {
        for (int i = 0; i < degree; ++i)
        {
            infinite[(size_t) i] = i >= count;
            labels[(size_t) i] = i < count ? found[(size_t) i] : Complex {};
        }
    }

    /**
        The roots of c[0 ... degree] into `labels`, continuing each label's previous root (`warm`).
        Roots lost to a vanishing leading coefficient sit at infinity. Real roots come out exactly
        real and conjugate pairs exactly conjugate.
    */
    void trackRoots (const double* c, int degree, std::array<Complex, RootCascade::maxRoots>& labels,
                     std::array<bool, RootCascade::maxRoots>& infinite, bool warm, double infinityRadius) noexcept
    {
        std::array<Complex, poly::maxDegree + 1> coefficients {};
        for (int k = 0; k <= degree; ++k)
            coefficients[(size_t) k] = c[k];

        std::array<int, RootCascade::maxRoots> finiteLabels {};
        int previous = 0;
        std::array<Complex, poly::maxDegree> found {};
        if (warm)
            for (int i = 0; i < degree; ++i)
                if (! infinite[(size_t) i])
                {
                    finiteLabels[(size_t) previous] = i;
                    found[(size_t) previous++] = labels[(size_t) i];
                }

        const int count = degree > 0 ? poly::roots ({ coefficients.data(), (size_t) degree + 1 }, found, warm, infinityRadius) : 0;
        poly::symmetrise ({ found.data(), (size_t) count });
        if (! warm)
        {
            placeCold (found, count, degree, labels, infinite);
            return;
        }

        if (warm && count == previous)
        {
            // Warm-started roots come back in their labels' order; keep it when each is nearest
            // its own label, else match them below.
            bool ordered = true;
            for (int k = 0; k < count && ordered; ++k)
            {
                const auto own = std::abs (found[(size_t) k] - labels[(size_t) finiteLabels[(size_t) k]]);
                for (int j = 0; j < count && ordered; ++j)
                    if (j != k && std::abs (found[(size_t) k] - labels[(size_t) finiteLabels[(size_t) j]]) < own)
                        ordered = false;
            }
            if (ordered)
            {
                for (int k = 0; k < count; ++k)
                    labels[(size_t) finiteLabels[(size_t) k]] = found[(size_t) k];
                return;
            }
        }

        // Each new root continues the nearest label (the closest pairs first), from one table of
        // distances: the logarithms dominate at degree 21.
        std::array<std::array<double, RootCascade::maxRoots>, RootCascade::maxRoots> d {};
        for (int k = 0; k < count; ++k)
            for (int i = 0; i < degree; ++i)
                d[(size_t) k][(size_t) i] = distance (found[(size_t) k], false, labels[(size_t) i], infinite[(size_t) i]);
        std::array<bool, RootCascade::maxRoots> taken {};
        std::array<bool, RootCascade::maxRoots> placed {};
        for (int round = 0; round < count; ++round)
        {
            int bestRoot = -1, bestLabel = -1;
            double best = std::numeric_limits<double>::infinity();
            for (int k = 0; k < count; ++k)
            {
                if (placed[(size_t) k])
                    continue;
                for (int i = 0; i < degree; ++i)
                    if (! taken[(size_t) i] && d[(size_t) k][(size_t) i] < best)
                        best = d[(size_t) k][(size_t) i], bestRoot = k, bestLabel = i;
            }
            placed[(size_t) bestRoot] = taken[(size_t) bestLabel] = true;
            labels[(size_t) bestLabel] = found[(size_t) bestRoot];
            infinite[(size_t) bestLabel] = false;
        }
        for (int i = 0; i < degree; ++i)
            if (! taken[(size_t) i])
                infinite[(size_t) i] = true;
    }

    /** prod (1 - r_k z^-1), ascending in z^-1. */
    void expand (const Complex* roots, int count, std::array<double, RootCascade::maxOrder + 1>& re,
                 std::array<double, RootCascade::maxOrder + 1>& im) noexcept
    {
        std::array<Complex, RootCascade::maxOrder + 1> p {};
        p[0] = 1.0;
        for (int k = 0; k < count; ++k)
            for (int j = k + 1; j >= 1; --j)
                p[(size_t) j] -= roots[k] * p[(size_t) j - 1];
        for (size_t j = 0; j < p.size(); ++j)
        {
            re[j] = p[j].real();
            im[j] = p[j].imag();
        }
    }

    Complex horner (const double* c, int degree, Complex x) noexcept
    {
        Complex v {};
        for (int k = degree; k >= 0; --k)
            v = v * x + c[k];
        return v;
    }
} // namespace

void RootCascade::design (const FrequencyMap& map, const double* num, int numDegree, const double* den, int denDegree,
                          double omegaRef, int rampSamples, bool fresh) noexcept
{
    const int D = std::clamp (map.degree(), 1, maxMapDegree);
    fresh = fresh || ! designed || std::clamp (denDegree, 0, maxRoots) != poleCount
            || std::clamp (numDegree, 0, std::clamp (denDegree, 0, maxRoots)) != numZeros || D != mapDegree;
    if (fresh)
    {
        beginFresh (map, num, numDegree, den, denDegree, omegaRef);
        while (! freshStep())
        {
        }
        return;
    }

    take (map, num, numDegree, den, denDegree, omegaRef);
    shiftPast();
    trackRoots (analogDen.data(), analogDenDegree, poles, poleInfinite, true, infinityRadius());
    trackRoots (analogNum.data(), analogNumDegree, zeros, zeroInfinite, true, infinityRadius());
    for (int i = analogNumDegree; i < analogDenDegree; ++i)
        zeroInfinite[(size_t) i] = true;
    reflected = false;
    mapRoots (false, 0, poleCount);
    mapRoots (true, 0, poleCount);
    mirrorLowerRoots (false);
    mirrorLowerRoots (true);
    makeStages();
    if (rampSamples <= 0)
        restAtTarget();
    else
    {
        rampLength = rampSamples;
        rampPosition = 0;
        settle = spline::settleSegments;
    }
}

double RootCascade::infinityRadius() const noexcept
{
    // In the band such a root moves the magnitude by less than 1e-6 and the phase by less than
    // 1e-3 rad; on the Console's blocks any radius from 1e3 to 1e6 in sigma designs the same
    // (tools/netlist/README.md, "Interacting blocks").
    return 1000.0 * 2.0 * std::numbers::pi * fs / omegaScale;
}

void RootCascade::copyDesign (Design& to, const Design& from) const noexcept
{
    // Only the stages in use: the count changes only with a fresh design, which rests at once.
    std::copy_n (from.stage.begin(), stages, to.stage.begin());
    to.gain = from.gain;
}

void RootCascade::restAtTarget() noexcept
{
    copyDesign (now, target);
    for (auto& d : past)
        copyDesign (d, target);
    oldest = 0;
    rampLength = rampPosition = 0;
    settle = 0;
}

void RootCascade::shiftPast() noexcept
{
    copyDesign (past[(size_t) oldest], target);
    oldest = (oldest + 1) % (int) past.size();
}

void RootCascade::take (const FrequencyMap& map, const double* num, int numDegree, const double* den, int denDegree,
                        double omegaRef) noexcept
{
    jobMap = &map;
    fs = map.sampleRate();
    omegaScale = omegaRef;
    denDegree = std::clamp (denDegree, 0, maxRoots);
    numDegree = std::clamp (numDegree, 0, denDegree);
    analogNumDegree = numDegree;
    analogDenDegree = denDegree;
    for (int k = 0; k <= maxRoots; ++k)
    {
        analogNum[(size_t) k] = k <= numDegree ? num[k] : 0.0;
        analogDen[(size_t) k] = k <= denDegree ? den[k] : 0.0;
    }
}

void RootCascade::beginFresh (const FrequencyMap& map, const double* num, int numDegree, const double* den, int denDegree,
                              double omegaRef) noexcept
{
    take (map, num, numDegree, den, denDegree, omegaRef);
    poleInfinite = {};
    zeroInfinite = {};
    job = Job::poles;
    startRoots (analogDen.data(), analogDenDegree);
}

void RootCascade::startRoots (const double* c, int degree) noexcept
{
    std::array<Complex, poly::maxDegree + 1> coefficients {};
    for (int k = 0; k <= degree; ++k)
        coefficients[(size_t) k] = c[k];
    finder.begin ({ coefficients.data(), (size_t) std::max (degree, 0) + 1 }, {}, false, infinityRadius());
}

bool RootCascade::freshStep() noexcept
{
    // A step does about stepBudget of work, at least one item, so a caller that takes one step per
    // control block bounds its callbacks; the design is the same however it is split. The costs
    // are estimates in nanoseconds (measured on the development machine), never timings: the
    // split must not depend on the run.
    constexpr double stepBudget = 40000.0;
    double spent = 0.0;
    while (job != Job::idle)
    {
        const auto cost = nextCost();
        if (spent > 0.0 && spent + cost > stepBudget)
            return false;
        nextItem();
        spent += cost;
    }
    return true;
}

double RootCascade::nextCost() const noexcept
{
    switch (job)
    {
        case Job::poles:
        case Job::zeros:
        {
            // An Aberth sweep: every root against every other.
            const auto m = (double) finder.degree();
            return finder.finished() ? 1500.0 : 300.0 + 11.0 * m * m;
        }
        case Job::pairing:
            return 1300.0 * poleCount;
        case Job::mapPoles:
        case Job::mapZeros:
        {
            if (mapNext >= poleCount)
                return 300.0;                                      // the lower roots of conjugate pairs
            const auto& roots = job == Job::mapZeros ? zeros : poles;
            const auto& infinite = job == Job::mapZeros ? zeroInfinite : poleInfinite;
            const auto r = roots[(size_t) mapNext];
            if (! infinite[(size_t) mapNext] && r.imag() < 0.0)
                return 0.0;
            // A fresh root outside the inverse series' radius continues it radially (48 small root finds).
            const bool radial = ! infinite[(size_t) mapNext] && std::abs (r) * omegaScale >= 0.8 * jobMap->branchRadius();
            return radial ? 55000.0 : 3000.0;
        }
        case Job::stages:
            return 4000.0;
        case Job::idle:
            break;
    }
    return 0.0;
}

void RootCascade::nextItem() noexcept
{
    switch (job)
    {
        case Job::idle:
            return;

        case Job::poles:
        case Job::zeros:
        {
            if (! finder.finished())
            {
                finder.step (1);
                return;
            }
            const bool areZeros = job == Job::zeros;
            const int degree = areZeros ? analogNumDegree : analogDenDegree;
            std::array<Complex, poly::maxDegree> found {};
            int count = 0;
            if (degree > 0)
            {
                count = finder.result (found);
                poly::symmetrise ({ found.data(), (size_t) count });
            }
            placeCold (found, count, degree, areZeros ? zeros : poles, areZeros ? zeroInfinite : poleInfinite);
            if (! areZeros)
            {
                job = Job::zeros;
                startRoots (analogNum.data(), analogNumDegree);
                return;
            }
            for (int i = analogNumDegree; i < analogDenDegree; ++i)
                zeroInfinite[(size_t) i] = true;
            poleCount = analogDenDegree;
            numZeros = analogNumDegree;
            mapDegree = std::clamp (jobMap->degree(), 1, maxMapDegree);
            order = 2 * mapDegree;
            poleTracks = {};
            zeroTracks = {};
            job = Job::pairing;
            return;
        }

        case Job::pairing:
            pair();
            reflected = false;
            job = Job::mapPoles;
            mapNext = 0;
            return;

        case Job::mapPoles:
        case Job::mapZeros:
        {
            const bool areZeros = job == Job::mapZeros;
            if (mapNext < poleCount)
            {
                mapRoots (areZeros, mapNext, mapNext + 1);
                ++mapNext;
                return;
            }
            mirrorLowerRoots (areZeros);
            mapNext = 0;
            job = areZeros ? Job::stages : Job::mapZeros;
            return;
        }

        case Job::stages:
            makeStages();
            restAtTarget();
            designed = true;
            job = Job::idle;
            return;
    }
}

void RootCascade::mapRoots (bool areZeros, int from, int to) noexcept
{
    const auto& roots = areZeros ? zeros : poles;
    const auto& infinite = areZeros ? zeroInfinite : poleInfinite;
    auto& tracks = areZeros ? zeroTracks : poleTracks;
    auto& out = areZeros ? digitalZeros : digitalPoles;
    for (int i = from; i < to; ++i)
    {
        const auto r = roots[(size_t) i];
        if (! infinite[(size_t) i] && r.imag() < 0.0)
            continue;
        poly::ProjectiveRoot root;
        if (infinite[(size_t) i])
            root = { { 1.0, 0.0 }, { 0.0, 0.0 } };
        else
            root = { r * omegaScale, { 1.0, 0.0 } };
        // Minimum phase throughout: a zero that crosses into the right half-plane (through the
        // j-axis, or through infinity where a leading coefficient changes sign) would have its
        // mirrored root jump; reflected, it only touches the unit circle and comes back.
        reflected = reflected || (! areZeros && ! infinite[(size_t) i] && r.real() > 0.0);
        jobMap->digitalRoots (root, false, { out[(size_t) i].data(), (size_t) mapDegree }, tracks[(size_t) i]);
    }
}

void RootCascade::mirrorLowerRoots (bool areZeros) noexcept
{
    // The lower of a conjugate pair copies its partner's digital roots, so the set of roots stays
    // closed under conjugation (the product stays real).
    const auto& roots = areZeros ? zeros : poles;
    const auto& infinite = areZeros ? zeroInfinite : poleInfinite;
    auto& tracks = areZeros ? zeroTracks : poleTracks;
    auto& out = areZeros ? digitalZeros : digitalPoles;
    for (int i = 0; i < poleCount; ++i)
    {
        const auto r = roots[(size_t) i];
        if (infinite[(size_t) i] || r.imag() >= 0.0)
            continue;
        int upper = -1;
        for (int j = 0; j < poleCount && upper < 0; ++j)
            if (! infinite[(size_t) j] && poly::isExactly (roots[(size_t) j], std::conj (r)))
                upper = j;
        if (upper < 0)
        {
            jobMap->digitalRoots ({ r * omegaScale, { 1.0, 0.0 } }, false, { out[(size_t) i].data(), (size_t) mapDegree },
                                  tracks[(size_t) i]);
            continue;
        }
        for (int k = 0; k < mapDegree; ++k)
            out[(size_t) i][(size_t) k] = std::conj (out[(size_t) upper][(size_t) k]);
        auto t = tracks[(size_t) upper];
        t.analog = std::conj (t.analog);
        t.z = std::conj (t.z);
        for (auto& x : t.t)
            x = std::conj (x);
        tracks[(size_t) i] = t;
    }
}

void RootCascade::makeStages() noexcept
{
    // Each stage: its members' digital roots, normalised to unity at fs / 4 so no stage carries
    // the block's whole gain.
    const Complex quarter { 0.0, -1.0 };
    for (int i = 0; i < stages; ++i)
    {
        std::array<Complex, maxOrder> zp {}, zz {};
        int np = 0, nz = 0;
        for (const auto label : members[(size_t) i].poles)
            for (int k = 0; label >= 0 && k < mapDegree; ++k)
                zp[(size_t) np++] = digitalPoles[(size_t) label][(size_t) k];
        for (const auto label : members[(size_t) i].zeros)
            for (int k = 0; label >= 0 && k < mapDegree; ++k)
                zz[(size_t) nz++] = digitalZeros[(size_t) label][(size_t) k];
        auto& c = target.stage[(size_t) i];
        expand (zz.data(), nz, c.br, c.bi);
        expand (zp.data(), np, c.ar, c.ai);
        // Roots closed under conjugation (two real ones, or a pair) give real coefficients: make
        // them exactly real, so the stage runs in real arithmetic while its input is real.
        auto closed = [] (const std::array<Complex, maxRoots>& roots, const std::array<bool, maxRoots>& infinite,
                          const std::array<int, 2>& m)
        {
            auto real = [&] (int l) { return l < 0 || infinite[(size_t) l] || poly::isReal (roots[(size_t) l]); };
            if (real (m[0]) && real (m[1]))
                return true;
            return m[0] >= 0 && m[1] >= 0 && ! infinite[(size_t) m[0]] && ! infinite[(size_t) m[1]]
                   && poly::isExactly (roots[(size_t) m[1]], std::conj (roots[(size_t) m[0]]));
        };
        c.real = closed (poles, poleInfinite, members[(size_t) i].poles) && closed (zeros, zeroInfinite, members[(size_t) i].zeros);
        if (c.real)
        {
            c.bi = {};
            c.ai = {};
        }
        const auto h = std::abs (stageResponse (c, quarter));
        const auto scale = h > 0.0 && std::isfinite (h) ? 1.0 / h : 1.0;
        for (int k = 0; k <= order; ++k)
        {
            c.br[(size_t) k] *= scale;
            c.bi[(size_t) k] *= scale;
        }
    }

    // The gain: |H_d| = |H_a (Omega (f))| at the lowest reference where the block is not near a
    // zero, with the sign that keeps the phase there within 90 degrees.
    const std::array<double, 4> candidates { 0.0005 * fs, 0.005 * fs, 0.05 * fs, 0.2 * fs };
    std::array<Complex, 4> analogAt {};
    double largest = 0.0;
    for (size_t k = 0; k < candidates.size(); ++k)
    {
        analogAt[k] = analogResponse (jobMap->analogOmega (candidates[k]) / (2.0 * std::numbers::pi));
        largest = std::max (largest, std::abs (analogAt[k]));
    }
    target.gain = 0.0;
    for (size_t k = 0; k < candidates.size(); ++k)
    {
        if (std::abs (analogAt[k]) < 1.0e-3 * largest)
            continue;
        const auto zi = std::polar (1.0, -2.0 * std::numbers::pi * candidates[k] / fs);
        Complex digital { 1.0, 0.0 };
        for (int i = 0; i < stages; ++i)
            digital *= stageResponse (target.stage[(size_t) i], zi);
        if (! (std::abs (digital) > 0.0) || ! std::isfinite (std::abs (digital)))
            continue;
        target.gain = std::abs (analogAt[k]) / std::abs (digital);
        if ((analogAt[k] * std::conj (digital)).real() < 0.0)
            target.gain = -target.gain;
        break;
    }
}

namespace
{
    /**
        Pairs roots that may meet and turn complex: conjugates together, then the nearest within
        each range (sub-audio below 10 Hz, audio to 30 kHz, above), then what is left. A real root
        meets a neighbour of its own kind: a band's two poles, two coupling capacitors' roots.
    */
    int pairRoots (const std::array<std::complex<double>, RootCascade::maxRoots>& roots,
                   const std::array<bool, RootCascade::maxRoots>& infinite, int count, double omegaScale,
                   std::array<std::array<int, 2>, RootCascade::maxRoots>& units) noexcept
    {
        std::array<bool, RootCascade::maxRoots> used {};
        int n = 0;
        for (int i = 0; i < count; ++i)
        {
            if (used[(size_t) i] || infinite[(size_t) i] || roots[(size_t) i].imag() <= 0.0)
                continue;
            for (int j = 0; j < count; ++j)
                if (! used[(size_t) j] && j != i && ! infinite[(size_t) j]
                    && poly::isExactly (roots[(size_t) j], std::conj (roots[(size_t) i])))
                {
                    used[(size_t) i] = used[(size_t) j] = true;
                    units[(size_t) n++] = { i, j };
                    break;
                }
        }
        auto range = [&] (int i)
        {
            if (infinite[(size_t) i])
                return 3;
            const auto hz = std::abs (roots[(size_t) i]) * omegaScale / (2.0 * std::numbers::pi);
            return hz < 10.0 ? 0 : (hz < 30000.0 ? 1 : 2);
        };
        for (const bool sameRange : { true, false })
            for (;;)
            {
                int bi = -1, bj = -1;
                double best = std::numeric_limits<double>::infinity();
                for (int i = 0; i < count; ++i)
                    for (int j = i + 1; j < count; ++j)
                    {
                        if (used[(size_t) i] || used[(size_t) j] || (sameRange && range (i) != range (j)))
                            continue;
                        const auto d = distance (roots[(size_t) i], infinite[(size_t) i], roots[(size_t) j], infinite[(size_t) j]);
                        if (d < best)
                            best = d, bi = i, bj = j;
                    }
                if (bi < 0)
                    break;
                used[(size_t) bi] = used[(size_t) bj] = true;
                units[(size_t) n++] = { bi, bj };
            }
        for (int i = 0; i < count; ++i)
            if (! used[(size_t) i])
                units[(size_t) n++] = { i, -1 };
        return n;
    }

    /** A stable insertion sort of order[0 ... n) by size: std::stable_sort takes a buffer from the heap. */
    void sortBySize (std::array<int, RootCascade::maxRoots>& order, const std::array<double, RootCascade::maxRoots>& size,
                     int n) noexcept
    {
        for (int i = 1; i < n; ++i)
        {
            const auto x = order[(size_t) i];
            int j = i;
            for (; j > 0 && size[(size_t) x] < size[(size_t) order[(size_t) j - 1]]; --j)
                order[(size_t) j] = order[(size_t) j - 1];
            order[(size_t) j] = x;
        }
    }
} // namespace

void RootCascade::pair() noexcept
{
    // Poles two by two, one stage per pair (a single one last when the count is odd).
    std::array<std::array<int, 2>, maxRoots> poleUnits {}, zeroUnits {};
    stages = pairRoots (poles, poleInfinite, poleCount, omegaScale, poleUnits);
    for (int i = 0; i < stages; ++i)
        members[(size_t) i] = { poleUnits[(size_t) i], { -1, -1 } };

    // Zeros paired alike, so zeros that meet share a stage too; the pairs fill the stages in order
    // of size, so no stage amplifies what an earlier one attenuated (tools/netlist/README.md).
    const int units = pairRoots (zeros, zeroInfinite, poleCount, omegaScale, zeroUnits);
    // The mean ln |root| of a pair; DC and infinity at -700 and +700, beyond any finite root.
    auto logSize = [] (const std::array<Complex, maxRoots>& roots, const std::array<bool, maxRoots>& infinite,
                       const std::array<int, 2>& unit)
    {
        double sum = 0.0;
        int n = 0;
        for (const auto l : unit)
            if (l >= 0)
            {
                const auto m = std::abs (roots[(size_t) l]);
                sum += infinite[(size_t) l] ? 700.0 : (m > 0.0 ? std::clamp (std::log (m), -700.0, 700.0) : -700.0);
                ++n;
            }
        return n > 0 ? sum / n : 0.0;
    };
    std::array<int, maxRoots> stageOrder {}, unitOrder {};
    std::array<double, maxRoots> stageSize {}, unitSize {};
    for (int i = 0; i < stages; ++i)
    {
        stageOrder[(size_t) i] = i;
        stageSize[(size_t) i] = logSize (poles, poleInfinite, members[(size_t) i].poles);
    }
    for (int u = 0; u < units; ++u)
    {
        unitOrder[(size_t) u] = u;
        unitSize[(size_t) u] = logSize (zeros, zeroInfinite, zeroUnits[(size_t) u]);
    }
    sortBySize (stageOrder, stageSize, stages);
    sortBySize (unitOrder, unitSize, units);

    std::array<bool, maxRoots> placed {};
    auto freeSlots = [] (const Members& m)
    {
        return (m.poles[0] >= 0 ? 1 : 0) + (m.poles[1] >= 0 ? 1 : 0) - (m.zeros[0] >= 0 ? 1 : 0) - (m.zeros[1] >= 0 ? 1 : 0);
    };
    for (int k = 0; k < stages; ++k)
    {
        auto& m = members[(size_t) stageOrder[(size_t) k]];
        for (int j = 0; j < units && freeSlots (m) > 0; ++j)
        {
            const auto u = unitOrder[(size_t) j];
            const int size = zeroUnits[(size_t) u][1] >= 0 ? 2 : 1;
            if (placed[(size_t) u] || size > freeSlots (m))
                continue;
            placed[(size_t) u] = true;
            for (const auto z : zeroUnits[(size_t) u])
                if (z >= 0)
                    m.zeros[m.zeros[0] < 0 ? 0 : 1] = z;
        }
    }
    // A zero pair with no two-pole stage left: split it into singles where room remains.
    for (int u = 0; u < units; ++u)
        for (const auto z : zeroUnits[(size_t) u])
        {
            if (placed[(size_t) u] || z < 0)
                continue;
            for (int st = 0; st < stages; ++st)
                if (freeSlots (members[(size_t) st]) > 0)
                {
                    auto& m = members[(size_t) st];
                    m.zeros[m.zeros[0] < 0 ? 0 : 1] = z;
                    break;
                }
        }
}

void RootCascade::reset() noexcept
{
    state = {};
    restAtTarget();
}

void RootCascade::flushDenormals() noexcept
{
    for (auto& channel : state)
        for (int i = 0; i < stages; ++i)
            for (int k = 0; k < order; ++k)
            {
                auto& s = channel[(size_t) i];
                if (std::abs (s.re[(size_t) k]) < 1.0e-30)
                    s.re[(size_t) k] = 0.0;
                if (std::abs (s.im[(size_t) k]) < 1.0e-30)
                    s.im[(size_t) k] = 0.0;
            }
}

template <int N>
void RootCascade::blend (double t) noexcept
{
    // The order as a constant: the loops unroll (this runs every sample of a ramp).
    std::array<double, spline::points> w;
    spline::weights (t, w);
    std::array<const Design*, spline::points> p {};
    for (size_t q = 0; q < past.size(); ++q)
        p[q] = &past[((size_t) oldest + q) % past.size()];
    p.back() = &target;
    for (int i = 0; i < stages; ++i)
    {
        auto& n = now.stage[(size_t) i];
        for (int k = 0; k <= N; ++k)
        {
            double br = 0.0, bi = 0.0, ar = 0.0, ai = 0.0;
            for (size_t q = 0; q < p.size(); ++q)
            {
                const auto& c = p[q]->stage[(size_t) i];
                br += w[q] * c.br[(size_t) k];
                bi += w[q] * c.bi[(size_t) k];
                ar += w[q] * c.ar[(size_t) k];
                ai += w[q] * c.ai[(size_t) k];
            }
            n.br[(size_t) k] = br;
            n.bi[(size_t) k] = bi;
            n.ar[(size_t) k] = ar;
            n.ai[(size_t) k] = ai;
        }
    }
    now.gain = 0.0;
    for (size_t q = 0; q < p.size(); ++q)
        now.gain += w[q] * p[q]->gain;
}

void RootCascade::process (double* const* channels, int numChannels, int numSamples) noexcept
{
    switch (order)
    {
        case 2:  run<2> (channels, numChannels, numSamples); break;
        case 4:  run<4> (channels, numChannels, numSamples); break;
        default: run<6> (channels, numChannels, numSamples); break;
    }
}

template <int N>
void RootCascade::run (double* const* channels, int numChannels, int numSamples) noexcept
{
    static_assert (N <= maxOrder);
    numChannels = std::min (numChannels, maxChannels);

    // A stage whose coefficients stay real over this run and whose states hold no imaginary part
    // runs in real arithmetic while its input is real: the same numbers, a quarter of the work.
    std::array<bool, maxRoots> realStage {};
    bool allReal = true;
    for (int i = 0; i < stages; ++i)
    {
        bool real = target.stage[(size_t) i].real;
        for (const auto& d : past)
            real = real && d.stage[(size_t) i].real;
        for (int ch = 0; ch < numChannels && real; ++ch)
            for (int k = 0; k < N && real; ++k)
                real = myzel::dsp::isExactly (state[(size_t) ch][(size_t) i].im[(size_t) k], 0.0);
        realStage[(size_t) i] = real;
        allReal = allReal && real;
    }

    if (allReal && atRest())
    {
        // At rest with every stage real (the common case): two channels as two lanes, whose
        // recursions (each bound by its own latency) overlap.
        if (numChannels == 2)
            runRealLanes<N> (*this, 0, channels[0], *this, 1, channels[1], numSamples);
        else if (numChannels == 1)
            runRealLanes<N> (*this, 0, channels[0], *this, -1, nullptr, numSamples);
        return;
    }

    for (int t = 0; t < numSamples; ++t)
    {
        if (rampPosition == rampLength && settle > 0)
        {
            // No new design: the next segments settle on the last.
            shiftPast();
            rampPosition = 0;
            --settle;
        }
        if (rampPosition < rampLength)
        {
            ++rampPosition;
            // At rest the stages run the design itself, not a weighted sum of its copies.
            if (rampPosition == rampLength && settle == 0)
                copyDesign (now, target);
            else
                blend<N> ((double) rampPosition / (double) rampLength);
        }
        for (int ch = 0; ch < numChannels; ++ch)
        {
            double xr = channels[ch][t], xi = 0.0;
            bool complexSignal = false;
            auto& states = state[(size_t) ch];
            for (int i = 0; i < stages; ++i)
            {
                const auto& c = now.stage[(size_t) i];
                auto& s = states[(size_t) i];
                if (! complexSignal && realStage[(size_t) i])
                {
                    const double y = c.br[0] * xr + s.re[0];
                    for (int k = 1; k <= N; ++k)
                        s.re[(size_t) k - 1] = (k < N ? s.re[(size_t) k] : 0.0) + c.br[(size_t) k] * xr - c.ar[(size_t) k] * y;
                    xr = y;
                    continue;
                }
                complexSignal = true;
                // Transposed direct form II in complex arithmetic, written out.
                const double yr = c.br[0] * xr - c.bi[0] * xi + s.re[0];
                const double yi = c.br[0] * xi + c.bi[0] * xr + s.im[0];
                for (int k = 1; k <= N; ++k)
                {
                    const double nextRe = k < N ? s.re[(size_t) k] : 0.0;
                    const double nextIm = k < N ? s.im[(size_t) k] : 0.0;
                    s.re[(size_t) k - 1] = nextRe + c.br[(size_t) k] * xr - c.bi[(size_t) k] * xi
                                           - (c.ar[(size_t) k] * yr - c.ai[(size_t) k] * yi);
                    s.im[(size_t) k - 1] = nextIm + c.br[(size_t) k] * xi + c.bi[(size_t) k] * xr
                                           - (c.ar[(size_t) k] * yi + c.ai[(size_t) k] * yr);
                }
                xr = yr;
                xi = yi;
            }
            channels[ch][t] = now.gain * xr;
        }
    }
}

bool RootCascade::atRest() const noexcept
{
    return rampPosition == rampLength && settle == 0;
}

bool RootCascade::allStagesReal (int channel) const noexcept
{
    for (int i = 0; i < stages; ++i)
    {
        if (! target.stage[(size_t) i].real)
            return false;
        for (const auto& d : past)
            if (! d.stage[(size_t) i].real)
                return false;
        for (int k = 0; k < order; ++k)
            if (! myzel::dsp::isExactly (state[(size_t) channel][(size_t) i].im[(size_t) k], 0.0))
                return false;
    }
    return true;
}

template <int N>
void RootCascade::runRealLanes (RootCascade& a, int channelA, double* xa, RootCascade& b, int channelB, double* xb, int numSamples) noexcept
{
    // Stage by stage over the block, the same operations per sample as run()'s real stages; a
    // second lane (channelB >= 0) runs in the same loop.
    for (int i = 0; i < a.stages; ++i)
    {
        const auto& ca = a.now.stage[(size_t) i];
        auto sa = a.state[(size_t) channelA][(size_t) i].re;
        if (channelB < 0)
        {
            for (int t = 0; t < numSamples; ++t)
            {
                const double x = xa[t];
                const double y = ca.br[0] * x + sa[0];
                for (int k = 1; k <= N; ++k)
                    sa[(size_t) k - 1] = (k < N ? sa[(size_t) k] : 0.0) + ca.br[(size_t) k] * x - ca.ar[(size_t) k] * y;
                xa[t] = y;
            }
            a.state[(size_t) channelA][(size_t) i].re = sa;
            continue;
        }
        // The two lanes side by side in pairs of doubles, which the compiler packs into one register.
        const auto& cb = b.now.stage[(size_t) i];
        auto& stateB = b.state[(size_t) channelB][(size_t) i].re;
        std::array<std::array<double, 2>, (size_t) N + 1> bb {}, ab {};
        std::array<std::array<double, 2>, (size_t) N> st {};
        for (int k = 0; k <= N; ++k)
        {
            bb[(size_t) k] = { ca.br[(size_t) k], cb.br[(size_t) k] };
            ab[(size_t) k] = { ca.ar[(size_t) k], cb.ar[(size_t) k] };
        }
        for (int k = 0; k < N; ++k)
            st[(size_t) k] = { sa[(size_t) k], stateB[(size_t) k] };
        for (int t = 0; t < numSamples; ++t)
        {
            const std::array<double, 2> x { xa[t], xb[t] };
            std::array<double, 2> y {};
            for (size_t l = 0; l < 2; ++l)
                y[l] = bb[0][l] * x[l] + st[0][l];
            for (int k = 1; k <= N; ++k)
                for (size_t l = 0; l < 2; ++l)
                    st[(size_t) k - 1][l] = (k < N ? st[(size_t) k][l] : 0.0) + bb[(size_t) k][l] * x[l] - ab[(size_t) k][l] * y[l];
            xa[t] = y[0];
            xb[t] = y[1];
        }
        for (int k = 0; k < N; ++k)
        {
            sa[(size_t) k] = st[(size_t) k][0];
            stateB[(size_t) k] = st[(size_t) k][1];
        }
        a.state[(size_t) channelA][(size_t) i].re = sa;
    }
    for (int t = 0; t < numSamples; ++t)
        xa[t] = a.now.gain * xa[t];
    if (channelB >= 0)
        for (int t = 0; t < numSamples; ++t)
            xb[t] = b.now.gain * xb[t];
}

bool RootCascade::processPair (RootCascade& a, double* xa, RootCascade& b, double* xb, int numSamples) noexcept
{
    // Two cascades of the same structure (one per channel), both at rest and real: one loop.
    if (a.stages != b.stages || a.order != b.order || ! a.atRest() || ! b.atRest() || ! a.allStagesReal (0) || ! b.allStagesReal (0))
        return false;
    switch (a.order)
    {
        case 2:  runRealLanes<2> (a, 0, xa, b, 0, xb, numSamples); break;
        case 4:  runRealLanes<4> (a, 0, xa, b, 0, xb, numSamples); break;
        default: runRealLanes<6> (a, 0, xa, b, 0, xb, numSamples); break;
    }
    return true;
}

std::complex<double> RootCascade::stageResponse (const Coefficients& c, Complex zi) const noexcept
{
    Complex b {}, a {};
    for (int k = order; k >= 0; --k)
    {
        b = b * zi + Complex { c.br[(size_t) k], c.bi[(size_t) k] };
        a = a * zi + (k == 0 ? Complex { 1.0, 0.0 } : Complex { c.ar[(size_t) k], c.ai[(size_t) k] });
    }
    return b / a;
}

std::complex<double> RootCascade::response (double hz) const noexcept
{
    const auto zi = std::polar (1.0, -2.0 * std::numbers::pi * hz / fs);
    Complex h { target.gain, 0.0 };
    for (int i = 0; i < stages; ++i)
        h *= stageResponse (target.stage[(size_t) i], zi);
    return h;
}

std::complex<double> RootCascade::analogResponse (double hz) const noexcept
{
    const Complex sigma { 0.0, 2.0 * std::numbers::pi * hz / omegaScale };
    return horner (analogNum.data(), analogNumDegree, sigma) / horner (analogDen.data(), analogDenDegree, sigma);
}

} // namespace viridian::dsp
