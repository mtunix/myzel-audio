#pragma once

#include "dsp/ModuleInfo.h"

#include "common/dsp/TapeCore.h"
#include "common/dsp/TapeLosses.h"
#include "common/dsp/TriodeStage.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace inkcap::dsp::crush
{

/**
    What each Crush type is, in one place (NOTES.md, Types and Voicing): voiced by measurement in
    M3, with an aliasing or a level rule fixing some numbers (marked).
*/
struct Voicing
{
    double minDriveDb, maxDriveDb;  // Drive 0 % and 100 %: the gain into the stage at full intensity
    double level;                   // the stage's input units per full scale, at 0 dB of drive
    double splitHz;                 // the stage saturates the band below; the band above passes (aliasing rule)
    double highShare;               // the band above at full intensity (1: through untouched)
};

constexpr std::array<Voicing, 4> voicings { {
    { 4.0, 22.0, 0.5, 4000.0, 1.0 },        // Tape: Langevin units (h / a)
    { 6.0, 24.0, 0.7, 3500.0, 1.0 },        // Cassette: less headroom
    { 6.0, 30.0, 0.15, 4000.0, 1.0 },       // Valve: grid volts
    { 12.0, 36.0, 0.2, 2000.0, 0.25 },      // Fuzz: the clipper's units; it eats the highs
} };

inline const Voicing& voicingOf (CrushType type) noexcept
{
    return voicings[(size_t) std::clamp ((int) type, 0, (int) voicings.size() - 1)];
}

/** Drive (0 .. 1 position) as dB into the stage at full intensity: linear in dB over the type's range. */
inline double driveDb (CrushType type, double position) noexcept
{
    const auto& v = voicingOf (type);
    return v.minDriveDb + std::clamp (position, 0.0, 1.0) * (v.maxDriveDb - v.minDriveDb);
}

/** The inverse of driveDb: a position, clamped to the type's range. */
inline double drivePosition (CrushType type, double db) noexcept
{
    const auto& v = voicingOf (type);
    return std::clamp ((db - v.minDriveDb) / (v.maxDriveDb - v.minDriveDb), 0.0, 1.0);
}

/** A tape: its speed, its head (Bertram's lengths) and its coating (the Jiles-Atherton loop). */
struct TapeVoicing
{
    double speed;                           // m/s
    myzel::dsp::TapeGeometry geometry;
    myzel::dsp::TapeCore::Parameters core;
};

constexpr double metresPerInch = 0.0254;

/**
    Tape: a 7.5 ips studio machine, its repro head a little worn. A well-biased coating: the
    irreversible part kinks the loop at each reversal, and at 2x those kinks' harmonics fold back,
    at every Drive, not only at its top: c 0.97 keeps the whole Drive range under -82 dBc
    (Mulberry's c 0.9: -78 at the top; NOTES.md, Aliasing).
*/
inline TapeVoicing tapeVoicing() noexcept
{
    return { 7.5 * metresPerInch,
             { .gap = 2.5e-6, .spacing = 0.3e-6, .thickness = 1.0e-6, .bumpWavelength = 5.4e-3 },
             { .alpha = 0.01, .k = 0.5, .c = 0.97 } };
}

/** Cassette: 1 7/8 ips, a cassette head's spacing and a thinner coating; a wider, less linear loop. */
inline TapeVoicing cassetteVoicing() noexcept
{
    return { 1.875 * metresPerInch,
             { .gap = 1.5e-6, .spacing = 0.5e-6, .thickness = 0.6e-6, .bumpWavelength = 5.4e-3 },
             { .alpha = 0.01, .k = 0.6, .c = 0.97 } };
}

/** Valve: one 12AX7 common-cathode stage, the values of Umber's Valve (validated there). */
inline myzel::dsp::tubes::TriodeStageComponents valveComponents() noexcept
{
    myzel::dsp::tubes::TriodeStageComponents c {};
    c.supplyVolts     = 262.0;
    c.supplyResistor  = 10.0e3;
    c.supplyCap       = 22.0e-6;
    c.inputCap        = 100.0e-9;
    c.gridLeak        = 220.0e3;
    c.gridStopper     = 47.0e3;
    c.plateLoad       = 100.0e3;
    c.gridPlateCap    = 1.7e-12;
    c.plateCap        = 10.0e-12;
    c.cathodeResistor = 1.5e3;
    c.cathodeCap      = 47.0e-6;
    c.outputCap       = 47.0e-9;
    c.load            = 1.0e6;
    return c;
}

/** Fuzz: a biased transistor clipper, tanh (x + bias) - tanh (bias), then its collector's bandwidth. */
struct FuzzVoicing
{
    static constexpr double bias = 0.35;            // asymmetry: the even harmonics
    static constexpr double collectorHz = 6500.0;   // the clipped edges' slew
    static constexpr double couplingHz = 12.0;      // the bias offset's DC goes here
};

/** Tone: a first-order tilt around the pivot, +-maxDb per side at full intensity. */
struct ToneVoicing
{
    static constexpr double pivotHz = 1000.0;
    static constexpr double maxDb = 6.0;
};

} // namespace inkcap::dsp::crush
