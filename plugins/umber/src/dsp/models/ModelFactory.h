#pragma once

#include "dsp/common/CircuitModel.h"
#include "plugin/Parameters.h"

#include <memory>

namespace umber::dsp
{

std::unique_ptr<CircuitModel> createModel (Model model);

} // namespace umber::dsp
