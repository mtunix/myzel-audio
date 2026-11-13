#include "dsp/modules/room/RoomControls.h"

#include "dsp/modules/ReadoutText.h"
#include "dsp/modules/room/RoomVoicing.h"

#include <cmath>
#include <cstdio>

namespace inkcap::dsp::room
{

std::string sizeText (int type, float percent)
{
    const auto size = sizeAt (type, (double) percent / 100.0);
    const auto& range = sizeRange (type);
    char text[32];
    // Two decimals below 3, one below 30: every step reads back within 1.5 % of the position.
    if (size < 2.995)
        std::snprintf (text, sizeof (text), "%.2f %s", size, range.unit);
    else if (size < 29.95)
        std::snprintf (text, sizeof (text), "%.1f %s", size, range.unit);
    else
        std::snprintf (text, sizeof (text), "%d %s", (int) std::lround (size), range.unit);
    return text;
}

float sizeFromText (int type, std::string_view text)
{
    // "4.2", "4.2 m", "4.2m"; Spring also "1.8 s" or "1800 ms". Outside the range: its nearest end.
    const auto s = readout::squeezed (text);
    std::string_view unit;
    auto value = readout::parseNumber (s, &unit);
    if (std::isnan (value))
        return -1.0f;

    if (sizeRange (type).unit[0] == 's')
    {
        if (unit == "ms")
            value *= 0.001;
        else if (! (unit.empty() || unit == "s" || unit == "sec"))
            return -1.0f;
    }
    else if (! (unit.empty() || unit == "m"))
    {
        return -1.0f;
    }
    return (float) (100.0 * positionOf (type, value));
}

} // namespace inkcap::dsp::room
