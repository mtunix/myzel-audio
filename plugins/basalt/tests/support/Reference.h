#pragma once

// BasaltMeasure's own double-precision transcription of the specified equations
// (src/dsp/dynamics/NOTES.md and src/dsp/models/clean/NOTES.md), written from the NOTES, never
// from the engine's code. The gates compare the engine with it where the NOTES ask for that
// (Clean's pink-noise static row, the Auto release rows), and the self-tests run every
// measurement on it to show the measurement reads what the NOTES say a correct engine gives.

#include "support/Rig.h"

namespace basalt::measure::reference
{

/**
    τ_R / τ_A for a measured release of ρ·τ_A: the smooth decoupled cascade (τ_R, then τ_A) falls
    to 1/e at ρ (dynamics/NOTES.md, Feed-forward ballistics, h(ρ)), solved by bisection on the
    closed form. ρ is floored at 1.01, the cascade's minimum.
*/
double releaseRatio (double rho);

/** Eq. 4's reduction x − G(x) in dB, written so that Ratio 1 (inverseRatio 1) gives exactly 0; inverseRatio 0 is ∞. */
double reduction (double levelDb, double thresholdDb, double inverseRatio, double kneeDb);

inline double inverseRatioOf (double ratio) { return std::isinf (ratio) ? 0.0 : 1.0 / ratio; }

/** Clean's RMS share b: 0 up to Attack 1 ms, 0.5 from 10 ms, linear in log Attack between (clean/NOTES.md, Detector). */
double cleanBlend (double attackMs);

/** Clean's attack calibration κ(b) = 1 + 0.118·b (clean/NOTES.md, Ballistics). */
inline double cleanKappa (double blend) { return 1.0 + 0.118 * blend; }

/**
    Clean's release drag δ in ms (clean/NOTES.md, Release with the RMS half): b·c·ΔR²·τ_RMS / (2·4.343·D)
    at D = 10 dB, ΔR = D/c + 3.01·(1 − b), c = 1 − 1/R (Ratio 2's c below Ratio 2), τ_RMS = Attack/4.
*/
double cleanReleaseDrag (double attackMs, double inverseRatio);

/** Clean's shortest release in ms: 1.01·Attack/κ(b) + δ (clean/NOTES.md, Gates). */
double cleanReleaseFloor (double attackMs, double inverseRatio);

/** Clean's Auto release factor: clamp(√(2/C²), 1/8, √2) (clean/NOTES.md, Auto release). */
double cleanAutoFactor (double crestSquared);

/**
    Clean's crest-factor detector (Giannoulis 2013 eq. 7, τ_C = 200 ms, with the guard and the
    16-sample update of clean/NOTES.md) over a key from reset: C² as held at sample `at`. Linked
    feeds max(k_L², k_R²) to one detector; otherwise `channel` is read alone.
*/
double crestSquared (const Channels& key, double rate, bool linked, int at, int channel = 0);

/**
    The reference engine as a Device. Clean: key matrix, RMS and peak levels, the look-ahead
    window, the blend, eq. 4, Link, Range, the smooth decoupled ballistics with κ, δ and h(ρ),
    the crest Auto release, the 20 ms curve fade (the new curve's ballistics beside the old,
    blended on a raised cosine), the 20 ms glides of Attack and Release in log time, the snap to
    an exact 1.0, and the delays: 10 ms in Normal (Character's oversampler is M3), 0 in Live.
    Not modelled: Gain, Auto Gain, Output, Mix, Character, the key EQ, Listen, Bypass and the
    crossfades of mode switches (they switch at once). Models other than Clean run as Clean.

    onePole: a branching one-pole with τ = Attack rising and τ = Release falling on eq. 4 of the
    peak level, nothing else: the ballistics method's known answer.
*/
std::unique_ptr<Device> makeDevice (bool onePole);

} // namespace basalt::measure::reference
