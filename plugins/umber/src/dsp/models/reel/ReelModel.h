#pragma once

#include "dsp/common/BiasRamp.h"
#include "dsp/common/CircuitModel.h"
#include "dsp/models/reel/ReelFilters.h"
#include "dsp/models/reel/ReelHysteresis.h"

namespace umber::dsp
{

namespace reel { class RecordAmpCurve; }

/**
    Reel: the record path of a generic vintage 15 ips tape machine.

    @code
      x -> record amp -> Cc -> record EQ -> record head -> tape -> de-emphasis -> LF roll-off -> head bump -> gap loss -> y
           12AX7 stage   HPF   50 us pre-   core          Jiles-   exact          2nd order     +2 dB @75 Hz  1-pole fit
           (static NR    3 Hz  emphasis,    saturation    Atherton inverse of     12 Hz         (peak)        (-1 dB @20k)
           table)              pole 24k     (soft limit)  RK4      record EQ
    @endcode

    - Record amp: triode stage solved at circuit level (ReelRecordAmp.h). Mild 2nd harmonic.
    - Record EQ: NAB-style 50 us high-frequency pre-emphasis (limited at 24 kHz by the amp's
      bandwidth). Pre-emphasised highs reach saturation first, so HF compresses as drive rises.
    - Record head: soft core saturation limits the write field (also bounds the solver's work).
    - Tape: Jiles-Atherton hysteresis with bias-linearised parameters (ReelHysteresis.h).
    - Playback: exact inverse de-emphasis (flat at low level), LF roll-off and head bump, and the
      gap / head-to-tape spacing loss.

    The interstage coupling cap (Cc into the next grid leak) is modelled as a 3 Hz high-pass; after
    asymmetric overloads it discharges slowly, as in the real circuit.

    Calibration: 1.0 at the input == 0.5 V at the grid; small-signal gain normalised to unity at
    1 kHz analytically in prepare(). Non-inverting overall (the triode's inversion is undone).
    See NOTES.md for the full design, validation and measurements.
*/
class ReelModel final : public CircuitModel
{
public:
    ReelModel();

    void prepare (double oversampledRate) override;
    void reset() noexcept override;
    void process (float* samples, int numSamples) noexcept override;

    /** Bias: shifts the record amp's grid DC by up to +-biasRangeVolts (cold = negative). */
    void setBias (double amount) noexcept override;

    // ---- circuit / tape constants (documented in NOTES.md) --------------------------------
    static constexpr double inputVolts      = 0.5;      // grid volts for a 1.0 input sample
    static constexpr double couplingHz      = 3.0;      // interstage coupling cap Cc into RL
    static constexpr double emphasisZeroHz  = 3183.1;   // 50 us
    static constexpr double emphasisPoleHz  = 24000.0;  // record amp bandwidth limit
    static constexpr double fieldPerVolt    = 0.115;    // normalised field h per plate volt
    static constexpr double headSaturation  = 12.0;     // record-head core saturation (h units)
    static constexpr double rolloffHz       = 12.0;     // playback LF roll-off below the bump
    static constexpr double rolloffQ        = 0.6;
    static constexpr double bumpHz          = 75.0;     // head bump (contour effect)
    static constexpr double bumpDb          = 2.0;
    static constexpr double bumpQ           = 0.9;
    static constexpr double gapLossHz       = 40000.0;  // gap + spacing loss, 1-pole fit
    static constexpr double gapLossMatchHz  = 15000.0;
    static constexpr double biasRangeVolts  = 1.6;      // grid DC shift at the Bias extremes

private:
    const reel::RecordAmpCurve* amp = nullptr;
    double sampleRate = 192000.0;
    double outputGain = 1.0;
    double bumpGain = 0.0;
    BiasRamp bias;                                          // in grid volts
    double biasVolts = 0.0, biasPlateOffset = 0.0;

    void applyBiasVolts (double volts) noexcept;

    reel::OnePole coupling;
    reel::ShelfSection preEmphasis, deEmphasis;
    reel::TapeHysteresis tape;
    reel::Svf rolloff, bump;
    reel::OnePole gapLoss;
};

} // namespace umber::dsp
