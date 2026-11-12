#include "dsp/EqParams.h"

namespace viridian
{

float defaultPosition (Control c) noexcept
{
    if (c == Control::hpfFreq || c == Control::lowShape || c == Control::highShape)
        return 0.0f;
    if (c == Control::lpfFreq)
        return 1.0f;
    return 0.5f;
}

SlotPositions defaultPositions() noexcept
{
    SlotPositions p {};
    for (int i = 0; i < numControls; ++i)
        p[(size_t) i] = defaultPosition (static_cast<Control> (i));
    return p;
}

SlotPositions flatPositions (Model m) noexcept
{
    auto p = defaultPositions();

    // A control's flat point is the same with Alt on or off.
    for (int i = 0; i < numControls; ++i)
        if (const auto& c = controlInfo (m, false, static_cast<Control> (i)); c.flat >= 0.0f)
            p[(size_t) i] = c.flat;

    return p;
}

EqParams resolveEq (Model m, bool altOn, const SlotPositions& positions) noexcept
{
    EqParams eq;
    eq.model = m;
    eq.alt = altOn && info (m).alt.active;

    const auto& section = info (m).section (eq.alt);
    for (size_t i = 0; i < (size_t) numControls; ++i)
        eq.values[i] = map (section.controls[i], positions[i]);

    return eq;
}

} // namespace viridian
