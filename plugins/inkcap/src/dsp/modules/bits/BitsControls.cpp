#include "dsp/modules/bits/BitsControls.h"

#include "dsp/modules/ReadoutText.h"
#include "dsp/modules/bits/BitsVoicing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace inkcap::dsp::bits
{

namespace
{
    BitsType typeOf (int type) { return (BitsType) std::clamp (type, 0, (int) voicings.size() - 1); }
    double positionOf (float percent) { return std::clamp (percent, 0.0f, 100.0f) / 100.0; }
}

std::string rateText (int type, float percent)
{
    const auto hz = clockHz (typeOf (type), positionOf (percent));
    char text[32];
    if (hz < 10000.0)
        std::snprintf (text, sizeof (text), "%.2f kHz", hz / 1000.0);
    else
        std::snprintf (text, sizeof (text), "%.1f kHz", hz / 1000.0);
    return text;
}

float rateFromText (int type, std::string_view text)
{
    // "26 kHz", "26k", "26040 Hz", "26040": a bare number under 200 is kHz, else Hz.
    const auto s = readout::squeezed (text);
    std::string_view rest;
    const auto value = readout::parseNumber (s, &rest);
    if (std::isnan (value) || value <= 0.0)
        return -1.0f;

    double hz = 0.0;
    if (rest == "khz" || rest == "k")
        hz = value * 1000.0;
    else if (rest == "hz")
        hz = value;
    else if (rest.empty())
        hz = value < 200.0 ? value * 1000.0 : value;
    else
        return -1.0f;
    return (float) (100.0 * clockPosition (typeOf (type), hz));
}

std::string depthText (int type, float percent)
{
    const auto bits = depthBits (typeOf (type), positionOf (percent));
    const auto whole = std::round (bits);
    char text[32];
    if (std::abs (bits - whole) < 0.05)
        std::snprintf (text, sizeof (text), "%d bits", (int) whole);
    else
        std::snprintf (text, sizeof (text), "%.1f bits", bits);
    return text;
}

float depthFromText (int type, std::string_view text)
{
    // "12 bits", "12bit", "12": bits.
    const auto s = readout::squeezed (text);
    std::string_view rest;
    const auto value = readout::parseNumber (s, &rest);
    if (std::isnan (value) || ! (rest.empty() || rest == "bits" || rest == "bit" || rest == "b"))
        return -1.0f;
    return (float) (100.0 * depthPosition (typeOf (type), value));
}

} // namespace inkcap::dsp::bits
