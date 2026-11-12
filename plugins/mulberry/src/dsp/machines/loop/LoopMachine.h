#pragma once

#include "dsp/machines/TapeMachine.h"

namespace mulberry::dsp
{

/**
    Loop: a tape-loop echo. Erase, record and three playback heads at d, 2d and 3d along a loop of
    tape; Time sets the capstan speed (v = d / Time), so changing it bends every echo in flight.

    Param A (Heads): the classic seven head combinations (the plan's head set; feedback from the
    sum of the active heads). Param B (Wear): dropouts from a fixed map of bad spots along the
    loop (they come round every pass), more spacing loss, a weaker erase head (a faint ghost of the
    previous pass) and more wow. See NOTES.md.
*/
class LoopMachine final : public TapeMachine
{
public:
    explicit LoopMachine (uint32_t seed = 1) : TapeMachine (seed) {}

    static constexpr double headSpacing = 0.03;         // metres from the record head to head 1
    static constexpr double loopLength = 1.2;           // metres of tape in the loop
    static constexpr double physicalMinMs = 50.0;       // head-1 times where losses follow speed
    static constexpr double physicalMaxMs = 1000.0;

protected:
    Setup setup (const MachineSettings& s) const noexcept override;
    Transport::Mechanics mechanics() const noexcept override;
};

} // namespace mulberry::dsp
