// The early card's networks in a translation unit of their own: the generated headers are large.
#include "dsp/models/console/ConsoleCard.h"
#include "dsp/models/console/generated/ConsoleEarlyBack.h"
#include "dsp/models/console/generated/ConsoleEarlyFront.h"

namespace viridian::dsp::console
{

std::unique_ptr<CardChannel> makeEarlyCard()
{
    return std::make_unique<Card<generated::ConsoleEarlyFront, generated::ConsoleEarlyBack>>();
}

} // namespace viridian::dsp::console
