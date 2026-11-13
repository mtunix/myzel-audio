#include "dsp/modules/glitch/GlitchControls.h"

#include "dsp/modules/ReadoutText.h"
#include "dsp/modules/glitch/GlitchPattern.h"

#include <algorithm>
#include <cmath>

namespace inkcap::dsp::glitch
{

namespace
{
    bool timed (int type) { return type >= (int) GlitchType::skip && type <= (int) GlitchType::bend; }
}

std::string lengthText (int type, float percent)
{
    if (! timed (type))
        return readout::percentText (percent);
    return readout::secondsText (lengthSecondsFor ((GlitchType) type, std::clamp (percent, 0.0f, 100.0f) / 100.0f));
}

float lengthFromText (int type, std::string_view text)
{
    if (! timed (type))
        return readout::percentFromText (text);

    const auto seconds = readout::secondsFromText (text);
    if (std::isnan (seconds) || seconds <= 0.0)
        return -1.0f;
    return 100.0f * lengthPositionFor ((GlitchType) type, seconds);
}

} // namespace inkcap::dsp::glitch
