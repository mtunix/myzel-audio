#include "dsp/models/console/ConsoleControls.h"

#include "dsp/models/console/ConsoleLaws.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace viridian::dsp::console
{

namespace
{
    /** A band's time constants: input R and C, feedback R and C (both R plus one 50k gang). */
    struct BandRc
    {
        double ri, ci, rf, cf;
    };

    // NOTES.md, "Control laws" (HF: Cf is C4 + C5, the bell's).
    constexpr double gang = 50.0e3;
    constexpr std::array<BandRc, 4> earlyRc { { { 4.3e3, 2.2e-9, 5.1e3, 2.42e-9 },
                                                 { 4.3e3, 4.7e-9, 5.1e3, 4.7e-9 },
                                                 { 4.3e3, 15.0e-9, 5.1e3, 15.0e-9 },
                                                 { 4.3e3, 80.0e-9, 5.1e3, 80.0e-9 } } };
    constexpr std::array<BandRc, 4> laterRc { { { 3.0e3, 2.2e-9, 3.6e3, 2.42e-9 },
                                                 { 3.0e3, 4.7e-9, 3.6e3, 4.7e-9 },
                                                 { 3.0e3, 22.0e-9, 3.6e3, 22.0e-9 },
                                                 { 4.3e3, 80.0e-9, 4.7e3, 80.0e-9 } } };

    /** f = a / (r0 + x pot), a fitted to the -3 dB corners (NOTES.md). */
    struct FilterLaw
    {
        double a, r0, pot;
    };

    constexpr FilterLaw earlyHpf { 556122.0, 1.8e3, 50.0e3 }, earlyLpf { 3.40573e7, 470.0, 10.0e3 };
    constexpr FilterLaw laterHpf { 737359.0, 1.8e3, 47.0e3 }, laterLpf { 3.40573e7, 680.0, 10.0e3 };

    const BandRc& rcOf (bool later, Band b) noexcept { return (later ? laterRc : earlyRc)[(size_t) b]; }
    const BandLaws& lawsOf (bool later, Band b) noexcept { return (later ? laterLaws : earlyLaws)[(size_t) b]; }

    /** Linear interpolation in a uniform table, clamped to its ends (and exact there: a pot at its end is a short). */
    double lookup (const LawTable& t, double x) noexcept
    {
        const auto pos = std::clamp ((x - t.x0) / (t.x1 - t.x0), 0.0, 1.0) * (lawPoints - 1);
        const auto i = std::min ((int) pos, lawPoints - 2);
        const auto f = pos - i;
        return (1.0 - f) * t.y[(size_t) i] + f * t.y[(size_t) i + 1];
    }

    BandPots band (const EqParams& eq, bool later, Band b, Control freq, Control gain, Control q, Control shape) noexcept
    {
        BandPots p;
        p.freq = bandFraction (later, b, eq[freq].value);
        p.gain = wiperFor (later, b, eq[gain].value);
        if (lawsOf (later, b).hasQ)
            p.q = gangFor (later, b, eq[q].value);
        p.bell = eq[shape].value >= 0.5f;
        return p;
    }
} // namespace

double bandFraction (bool later, Band b, double hz) noexcept
{
    const auto& rc = rcOf (later, b);
    const auto w = 2.0 * std::numbers::pi * std::max (hz, 1.0);
    const auto k = 1.0 / (w * w * rc.ci * rc.cf);
    const auto s = rc.ri + rc.rf;
    const auto r = 0.5 * (-s + std::sqrt (std::max (s * s - 4.0 * (rc.ri * rc.rf - k), 0.0)));
    return std::clamp (r / gang, 0.0, 1.0);
}

double bandNominalHz (bool later, Band b, double fraction) noexcept
{
    const auto& rc = rcOf (later, b);
    const auto r = gang * fraction;
    return 1.0 / (2.0 * std::numbers::pi * std::sqrt ((rc.ri + r) * rc.ci * (rc.rf + r) * rc.cf));
}

double filterFraction (bool later, bool highPass, double hz) noexcept
{
    const auto& law = highPass ? (later ? laterHpf : earlyHpf) : (later ? laterLpf : earlyLpf);
    return std::clamp ((law.a / std::max (hz, 1.0) - law.r0) / law.pot, 0.0, 1.0);
}

double wiperFor (bool later, Band b, double db) noexcept
{
    const auto& laws = lawsOf (later, b);
    if (db >= 0.0)
        return std::clamp (lookup (laws.boost, db), 0.5, 1.0);
    return std::clamp (lookup (laws.cut, db), 0.0, 0.5);
}

double gangFor (bool later, Band b, double q) noexcept
{
    const auto& laws = lawsOf (later, b);
    if (! laws.hasQ)
        return 0.5;
    return std::clamp (lookup (laws.q, std::log (std::max (q, 1.0e-3))), 0.0, 1.0);
}

Pots potsFor (const EqParams& eq) noexcept
{
    Pots p;
    p.later = eq.alt;
    const auto& hpf = eq[Control::hpfFreq];
    const auto& lpf = eq[Control::lpfFreq];
    p.hpfIn = ! hpf.off;
    p.lpfIn = ! lpf.off;
    if (p.hpfIn)
        p.hpf = filterFraction (p.later, true, hpf.value);
    if (p.lpfIn)
        p.lpf = filterFraction (p.later, false, lpf.value);
    p.lf = band (eq, p.later, Band::lf, Control::lowFreq, Control::lowGain, Control::lowQ, Control::lowShape);
    p.lmf = band (eq, p.later, Band::lmf, Control::lmFreq, Control::lmGain, Control::lmQ, Control::lowShape);
    p.hmf = band (eq, p.later, Band::hmf, Control::hmFreq, Control::hmGain, Control::hmQ, Control::highShape);
    p.hf = band (eq, p.later, Band::hf, Control::highFreq, Control::highGain, Control::highQ, Control::highShape);
    p.lmf.bell = p.hmf.bell = true;
    return p;
}

} // namespace viridian::dsp::console
