#include "dsp/chain/Match.h"

#include "dsp/chain/MatchTables.h"

#include <algorithm>

namespace inkcap::dsp::match
{

float compensationDb (const ChainSettings& s) noexcept
{
    if (! s.match)
        return 0.0f;

    auto entry = [&s] (const auto& table, Module m, const auto& settings)
    {
        return settings.on ? lookup (table, (int) settings.type, controlsOf (settings), moduleIntensity (m, settings.amount, s.damage)) : 0.0f;
    };
    const auto glitch = s.glitch.on ? lookupGlitch (glitchTable, (int) s.glitch.type, controlsOf (s.glitch),
                                                    glitch::meanDensity (glitch::patternFor (s.glitch, s.damage, s.chaos)))
                                    : 0.0f;
    return std::min (glitch + entry (crushTable, Module::crush, s.crush) + entry (bitsTable, Module::bits, s.bits)
                         + entry (hissTable, Module::hiss, s.hiss) + entry (roomTable, Module::room, s.room),
                     maxBoostDb);
}

} // namespace inkcap::dsp::match
