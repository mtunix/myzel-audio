#pragma once

#include "dsp/machines/TapeMachine.h"

namespace mulberry::dsp
{

/**
    Drum: a magnetic-drum echo. A heavy drum with an oxide-coated rim turns past a record head
    and four playback heads spaced evenly up to Time (the plan reads them at 1/4 .. 4/4 of it);
    Time sets the motor speed, and the drum's mass makes a change glide slowly. The heads ride
    just off the oxide, never touching it, so spacing loss dominates the tone. A valve record amp
    drives the head into the oxide (the shared Jiles-Atherton core). See NOTES.md.

    Param A (Heads): every combination of the four heads. Param B (Swell): the feedback taken
    from the playing heads ... from all four equally, the dense build-up of these units.
*/
class DrumMachine final : public TapeMachine
{
public:
    explicit DrumMachine (uint32_t seed = 1) : TapeMachine (seed) {}

    static constexpr double circumference = 0.32;       // metres: a drum about 100 mm across
    static constexpr double headArc = 0.24;             // record head to head 4 (the others at its quarters)
    static constexpr double physicalMinMs = 120.0;      // head-4 times where the losses follow the speed
    static constexpr double physicalMaxMs = 1200.0;

protected:
    Setup setup (const MachineSettings& s) const noexcept override;
    Transport::Mechanics mechanics() const noexcept override;
};

} // namespace mulberry::dsp
