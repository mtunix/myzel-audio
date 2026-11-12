#pragma once

#include "dsp/Machines.h"
#include "dsp/machines/Machine.h"

#include <memory>

namespace mulberry::dsp
{

/** Creates a machine (allocates; call before prepare()). */
std::unique_ptr<EchoMachine> createMachine (mulberry::Machine m, uint32_t seed);

} // namespace mulberry::dsp
