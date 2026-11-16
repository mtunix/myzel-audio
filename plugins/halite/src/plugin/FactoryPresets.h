#pragma once

#include "common/plugin/PresetManager.h"

namespace halite
{

/**
    Halite's presets (PLAN.md, Presets): the sound parameters (params::soundParameterIds, all but
    Bypass and Listen), no lockable mix (Halite has none), and the factory set. The latency is a
    session setting, so a preset never changes what the host sees.
*/
myzel::plugin::PresetFormat presetFormat();

} // namespace halite
