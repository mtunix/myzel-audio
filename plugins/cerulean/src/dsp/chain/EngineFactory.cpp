#include "dsp/chain/EngineFactory.h"

#include "dsp/algorithms/hall/HallEngine.h"

#if CERULEAN_ONLY_HALL

namespace cerulean::dsp
{
std::unique_ptr<Engine> createEngine (Algorithm, uint32_t seed)
{
    return std::make_unique<HallEngine> (seed);
}
} // namespace cerulean::dsp

#else

#include "dsp/algorithms/billow/BillowEngine.h"
#include "dsp/algorithms/choir/ChoirEngine.h"
#include "dsp/algorithms/echo/EchoEngine.h"
#include "dsp/algorithms/halo/HaloEngine.h"
#include "dsp/algorithms/nimbus/NimbusEngine.h"
#include "dsp/algorithms/plate/PlateEngine.h"
#include "dsp/algorithms/rise/RiseEngine.h"
#include "dsp/algorithms/room/RoomEngine.h"
#include "dsp/algorithms/spring/SpringEngine.h"

namespace cerulean::dsp
{

std::unique_ptr<Engine> createEngine (Algorithm algorithm, uint32_t seed)
{
    switch (algorithm)
    {
        case Algorithm::room:   return std::make_unique<RoomEngine> (seed);
        case Algorithm::hall:   return std::make_unique<HallEngine> (seed);
        case Algorithm::plate:  return std::make_unique<PlateEngine> (seed);
        case Algorithm::spring: return std::make_unique<SpringEngine> (seed);
        case Algorithm::nimbus: return std::make_unique<NimbusEngine> (seed);
        case Algorithm::halo:   return std::make_unique<HaloEngine> (seed);
        case Algorithm::rise:   return std::make_unique<RiseEngine> (seed);
        case Algorithm::billow: return std::make_unique<BillowEngine> (seed);
        case Algorithm::choir:  return std::make_unique<ChoirEngine> (seed);
        case Algorithm::echo:   return std::make_unique<EchoEngine> (seed);
    }
    return std::make_unique<HallEngine> (seed);
}

} // namespace cerulean::dsp

#endif
