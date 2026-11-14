#pragma once

#include "dsp/EqParams.h"

namespace viridian::dsp::console
{

/** The card's bands, in the order of the law tables. */
enum class Band
{
    hf = 0,
    hmf,
    lmf,
    lf
};

/** One band's pots as the netlists name them: fractions 0 ... 1 (NOTES.md, "Netlists"). */
struct BandPots
{
    double gain = 0.5;      // boost/cut wiper: 0 full cut, 0.5 centre tap, 1 full boost
    double freq = 0.5;      // both frequency gangs: 0 top frequency, 1 bottom
    double q = 0.5;         // mids: Q gang a, 0 lowest ... 1 highest
    bool bell = true;       // HF, LF: the shape switch
};

/** Every control of one card: what the networks take. */
struct Pots
{
    bool later = false;     // Alt: the later card
    bool hpfIn = false, lpfIn = false;
    double hpf = 1.0, lpf = 0.0;
    BandPots hf, hmf, lmf, lf;
};

/** The knobs' physical values (ModelInfo's Console mapping) to the card's pots: the control laws. */
Pots potsFor (const EqParams&) noexcept;

/** A band's frequency gangs for a nominal frequency: (Ri + r) (Rf + r) = 1 / ((2 pi f)^2 Ci Cf). */
double bandFraction (bool later, Band, double hz) noexcept;

/** The nominal frequency of a gang position (the inverse of bandFraction). */
double bandNominalHz (bool later, Band, double fraction) noexcept;

/** A filter's gangs for a corner: f = A / (R0 + x Rpot). */
double filterFraction (bool later, bool highPass, double hz) noexcept;

/** The boost/cut wiper whose deviation at the band's calibration point is `db` (ConsoleLaws.h). */
double wiperFor (bool later, Band, double db) noexcept;

/** The Q pot's gang a for a Q at the band's calibration point (mids). */
double gangFor (bool later, Band, double q) noexcept;

} // namespace viridian::dsp::console
