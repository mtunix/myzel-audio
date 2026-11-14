#pragma once

#include "dsp/models/EqModel.h"

#include <memory>

namespace viridian::dsp
{

/** The model behind each Model value; models not built yet are a PlaceholderModel. */
std::unique_ptr<EqModel> createModel (Model);

} // namespace viridian::dsp
