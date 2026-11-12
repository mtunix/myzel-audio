#pragma once

#include "dsp/machines/TapeMachine.h"

namespace mulberry::dsp
{

/**
    Oil Can: an electrostatic drum echo. A rotating drum in oil stores the signal as charge,
    written by a brush and read by pickups downstream; Time sets the drum speed through a slipping
    belt, so a change glides. Built from the mechanism (no published model exists; see NOTES.md):
    the charge leaks through the oil with dwell time, the brush contact is an aperture (a sinc
    loss that scales with the surface speed, the maths of a tape gap), capacitive coupling
    high-passes write and read, and a tube amplifier drives the brush.

    Param A (Brushes): one pickup ... a smeared cluster of five over an arc (<= 15 % of Time).
    Param B (Oil): fresh ... old: more leakage, a wider aperture, more belt slip.
*/
class OilCanMachine final : public TapeMachine
{
public:
    explicit OilCanMachine (uint32_t seed = 1) : TapeMachine (seed) {}

    static constexpr double circumference = 0.3;           // metres: a drum ~95 mm across
    static constexpr double pickupArc = 0.225;              // record brush to the pickup

protected:
    Setup setup (const MachineSettings& s) const noexcept override;
    Transport::Mechanics mechanics() const noexcept override;
};

} // namespace mulberry::dsp
