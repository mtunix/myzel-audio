#include "dsp/modules/crush/CrushControls.h"

#include "dsp/modules/ReadoutText.h"
#include "dsp/modules/crush/CrushVoicing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace inkcap::dsp::crush
{

namespace
{
    CrushType typeOf (int type) { return (CrushType) std::clamp (type, 0, (int) voicings.size() - 1); }
}

std::string driveText (int type, float percent)
{
    const auto db = driveDb (typeOf (type), std::clamp (percent, 0.0f, 100.0f) / 100.0);
    char text[32];
    std::snprintf (text, sizeof (text), "%+d dB", (int) std::lround (db));
    return text;
}

float driveFromText (int type, std::string_view text)
{
    // "+12 dB", "12", "12db": dB of drive; "40 %": a position.
    const auto s = readout::squeezed (text);
    std::string_view rest;
    const auto value = readout::parseNumber (s, &rest);
    if (std::isnan (value))
        return -1.0f;
    if (text.find ('%') != std::string_view::npos)
        return (float) std::clamp (value, 0.0, 100.0);
    if (! (rest.empty() || rest == "db"))
        return -1.0f;
    return (float) (100.0 * drivePosition (typeOf (type), value));
}

} // namespace inkcap::dsp::crush
