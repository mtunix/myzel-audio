#pragma once

#include <array>

namespace viridian::dsp::spline
{

/**
    The path every coefficient of a network follows between its control-rate designs: a uniform
    quartic B-spline through the last five designs, one segment per control block. Its third
    derivative is continuous, so the square-root corner in a root's path where two roots of
    different cascade stages meet reaches the audio smoothed (tools/netlist/README.md, "Interacting
    blocks").
*/
inline constexpr int points = 5;

/** Segments after the last design until the path rests on it. */
inline constexpr int settleSegments = points - 2;

/** The control points' weights at t in [0, 1] of a segment, the oldest first. */
inline void weights (double t, std::array<double, points>& w) noexcept
{
    const double u = 1.0 - t, t2 = t * t, t3 = t2 * t, t4 = t3 * t;
    w[0] = u * u * u * u / 24.0;
    w[1] = (-4.0 * t4 + 12.0 * t3 - 6.0 * t2 - 12.0 * t + 11.0) / 24.0;
    w[2] = (6.0 * t4 - 12.0 * t3 - 6.0 * t2 + 12.0 * t + 11.0) / 24.0;
    w[4] = t4 / 24.0;
    w[3] = 1.0 - w[0] - w[1] - w[2] - w[4];
}

} // namespace viridian::dsp::spline
