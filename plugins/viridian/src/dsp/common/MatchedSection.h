#pragma once

#include "dsp/common/BSpline.h"
#include "dsp/common/FrequencyMap.h"

#include <array>
#include <complex>

namespace viridian::dsp
{

/**
    An analog section of order 1 or 2: num (sigma) / den (sigma), coefficients ascending in
    sigma = s / omegaScale (rad/s). A numerator of lower degree has its missing zeros at infinity.
*/
struct AnalogSection
{
    std::array<double, 3> num { 1.0, 0.0, 0.0 }, den { 1.0, 0.0, 0.0 };
    double omegaScale = 1.0;

    std::complex<double> response (double hz) const noexcept;
};

/**
    One analog section as a digital filter matched in magnitude through the FrequencyMap (method
    and measurements: tools/netlist/README.md, "Matched sections"). The primary digital roots form
    the main stage, a TPT SVF with output mixing (a TPT one-pole for a first-order section); the
    roots that correct the map form one direct-form filter with unity DC gain. Between designs
    every coefficient follows the quartic B-spline through the last five (BSpline.h; one segment
    of `rampSamples` per design, as RootCascade's), so a corner in a control's path does not reach
    the audio. Real-time safe; not thread safe.
*/
class MatchedSection
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int maxCorrectionOrder = 2 * (FrequencyMap::maxDegree - 1);

    /** The main stage's coefficients: s' = j tan (pi f / fs) / g, y = m0 x + m1 bp + m2 lp. */
    struct Stage
    {
        int order = 2;                      // 2: SVF, 1: one-pole (m2 unused)
        double g = 1.0, k = 2.0;
        double m0 = 1.0, m1 = 0.0, m2 = 0.0;

        std::complex<double> response (double hz, double sampleRate) const noexcept;
    };

    /** The correcting filter: b (z^-1) / a (z^-1), a[0] = 1. */
    struct Correction
    {
        int order = 0;
        std::array<double, maxCorrectionOrder + 1> b {}, a {};

        std::complex<double> response (double hz, double sampleRate) const noexcept;
    };

    void design (const FrequencyMap&, const AnalogSection&, int rampSamples = 0) noexcept;

    void reset() noexcept;

    /** Forgets earlier designs (the roots' tracks): the next design is a first one, as after construction. */
    void restart() noexcept;

    /** Runs the section in place over `n` samples of each channel. */
    void process (double* const* channels, int numChannels, int n) noexcept;

    /** Flushes states below 1e-30 to zero (call once per control block). */
    void flushDenormals() noexcept;

    /** The digital response of the latest design: what process() settles to. */
    std::complex<double> response (double hz) const noexcept;

    const AnalogSection& analog() const noexcept { return section; }
    const Stage& stage() const noexcept { return target; }
    const Correction& correction() const noexcept { return targetCorrection; }

    /** True when the last design met an unstable analog pole (it was reflected to stay stable). */
    bool reflectedUnstablePole() const noexcept { return reflected; }

private:
    static constexpr int maxRoots = 2 * FrequencyMap::maxDegree;

    AnalogSection section;
    double fs = 48000.0;
    bool reflected = false, designed = false;

    void blend (double t) noexcept;
    void restAtTarget() noexcept;
    void shiftPast (const Stage&, const Correction&) noexcept;

    // The designs before the latest, the oldest first: with it, the coefficients' spline points.
    Stage target, now;
    Correction targetCorrection, nowCorrection;
    std::array<Stage, spline::points - 1> past {};
    std::array<Correction, spline::points - 1> pastCorrection {};
    int rampLength = 0, rampPosition = 0, settle = 0;

    std::array<FrequencyMap::Track, 2> poleTracks {}, zeroTracks {};
    int lastOrder = 0, lastDegree = 0;

    struct ChannelState
    {
        double s1 = 0.0, s2 = 0.0;
        std::array<double, maxCorrectionOrder> z {};
    };
    std::array<ChannelState, maxChannels> state {};
};

} // namespace viridian::dsp
