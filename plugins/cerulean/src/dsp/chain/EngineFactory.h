#pragma once

#include "dsp/Algorithms.h"
#include "dsp/Engine.h"

namespace cerulean::dsp
{

/** Creates the engine for an algorithm (allocates; call prepare() before use). */
std::unique_ptr<Engine> createEngine (Algorithm algorithm, uint32_t seed);

} // namespace cerulean::dsp
