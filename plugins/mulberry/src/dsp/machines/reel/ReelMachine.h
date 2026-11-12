#pragma once

#include "dsp/machines/TapeMachine.h"

namespace mulberry::dsp
{

/**
    Reel: a studio tape deck at a fixed speed with a sliding playback head. Time moves the head
    along the tape (with inertia, so a change glides in pitch) while the tape speed, and so the
    tone, stays put across the whole Time range.

    Param A (Speed): 7.5 / 15 / 30 ips; head bump, HF loss, hiss and record EQ follow it.
    Param B (Bias): under-biased (gritty, brighter, a wider hysteresis loop) through nominal to
    over-biased (cleaner, duller). See NOTES.md.
*/
class ReelMachine final : public TapeMachine
{
public:
    explicit ReelMachine (uint32_t seed = 1);

    static constexpr double metresPerInch = 0.0254;

    /** The Jiles-Atherton loop for a Bias position (-1 under .. +1 over). */
    static myzel::dsp::TapeHysteresis::Parameters coreFor (double bias) noexcept;

protected:
    Setup setup (const MachineSettings& s) const noexcept override;
    Transport::Mechanics mechanics() const noexcept override;
    bool headsSlide() const noexcept override { return true; }

private:
    static constexpr int expansionPoints = 41;
    std::array<double, expansionPoints> expansionTable {};  // over Bias -1 .. +1
};

} // namespace mulberry::dsp
