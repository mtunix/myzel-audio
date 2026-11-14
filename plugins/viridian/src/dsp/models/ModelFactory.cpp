#include "dsp/models/ModelFactory.h"

#include "dsp/models/PlaceholderModel.h"
#include "dsp/models/console/ConsoleModel.h"

namespace viridian::dsp
{

std::unique_ptr<EqModel> createModel (Model m)
{
    switch (m)
    {
        case Model::console:
            return std::make_unique<ConsoleModel>();
        case Model::passive:
        case Model::inductor:
        case Model::proportional:
        case Model::mastering:
            break;
    }
    return std::make_unique<PlaceholderModel>();
}

} // namespace viridian::dsp
