#include "dsp/chain/Engines.h"

#include "dsp/models/clean/CleanModel.h"
#include "dsp/models/fet/FetModel.h"
#include "dsp/models/opto/OptoModel.h"
#include "dsp/models/upward/UpwardModel.h"
#include "dsp/models/varimu/VariMuModel.h"
#include "dsp/models/vca/VcaModel.h"

namespace basalt::dsp
{

std::unique_ptr<dynamics::DynamicsModel> makeEngine (Engine e)
{
    switch (e)
    {
        case Engine::clean:   return std::make_unique<models::CleanModel>();
        case Engine::vcaPeak: return std::make_unique<models::VcaModel> (models::VcaModel::Detector::peak);
        case Engine::vcaRms:  return std::make_unique<models::VcaModel> (models::VcaModel::Detector::rms);
        case Engine::fet:     return std::make_unique<models::FetModel>();
        case Engine::opto:    return std::make_unique<models::OptoModel>();
        case Engine::variMu:  return std::make_unique<models::VariMuModel>();
        case Engine::upward:  return std::make_unique<models::UpwardModel>();
    }
    return std::make_unique<models::CleanModel>();
}

} // namespace basalt::dsp
