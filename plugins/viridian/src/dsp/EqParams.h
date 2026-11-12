#pragma once

#include "dsp/ModelInfo.h"

#include <array>

namespace viridian
{

/** The normalised knob positions of the slot controls, indexed by Control. */
using SlotPositions = std::array<float, numControls>;

/** The parameters' defaults: filters Off, everything else mid-travel, shapes on shelf. */
float defaultPosition (Control) noexcept;
SlotPositions defaultPositions() noexcept;

/** Positions that leave a model's network flat: gains at their flat point, filters Off (Init presets). */
SlotPositions flatPositions (Model) noexcept;

/** The physical EQ settings one model reads from the shared knob positions. */
struct EqParams
{
    Model model = Model::inductor;
    bool alt = false;
    std::array<ControlValue, numControls> values {};

    const ControlValue& operator[] (Control c) const noexcept { return values[(size_t) c]; }
};

/** Normalised positions to physical settings: the same knobs, reinterpreted per model and Alt. */
EqParams resolveEq (Model, bool altOn, const SlotPositions&) noexcept;

} // namespace viridian
