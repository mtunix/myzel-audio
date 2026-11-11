#include "dsp/models/ModelFactory.h"

#include "dsp/models/classa/ClassAModel.h"
#include "dsp/models/desk/DeskModel.h"
#include "dsp/models/fuzz/FuzzModel.h"
#include "dsp/models/reel/ReelModel.h"
#include "dsp/models/valve/ValveModel.h"

namespace umber::dsp
{

std::unique_ptr<CircuitModel> createModel (Model model)
{
    switch (model)
    {
        case Model::reel:   return std::make_unique<ReelModel>();
        case Model::desk:   return std::make_unique<DeskModel>();
        case Model::classA: return std::make_unique<ClassAModel>();
        case Model::valve:  return std::make_unique<ValveModel>();
        case Model::fuzz:   return std::make_unique<FuzzModel>();
    }

    return std::make_unique<ClassAModel>();
}

} // namespace umber::dsp
