#include "dsp/chain/MachineFactory.h"

#include "dsp/machines/bucket/BucketMachine.h"
#include "dsp/machines/clean/CleanMachine.h"
#include "dsp/machines/oilcan/OilCanMachine.h"
#include "dsp/machines/rack/RackMachine.h"
#include "dsp/machines/loop/LoopMachine.h"
#include "dsp/machines/reel/ReelMachine.h"

namespace mulberry::dsp
{

std::unique_ptr<EchoMachine> createMachine (mulberry::Machine m, uint32_t seed)
{
    switch (m)
    {
        case mulberry::Machine::rack:   return std::make_unique<RackMachine> (seed);
        case mulberry::Machine::bucket: return std::make_unique<BucketMachine> (seed);
        case mulberry::Machine::oilCan: return std::make_unique<OilCanMachine> (seed);
        case mulberry::Machine::loop:   return std::make_unique<LoopMachine> (seed);
        case mulberry::Machine::reel:   return std::make_unique<ReelMachine> (seed);
        case mulberry::Machine::clean:
            break;
    }
    return std::make_unique<CleanMachine> (seed);
}

} // namespace mulberry::dsp
