#include "dsp/modules/warble/WarbleControls.h"

#include "dsp/modules/ReadoutText.h"
#include "dsp/modules/warble/WarbleShape.h"

#include <algorithm>
#include <cmath>

namespace inkcap::dsp::warble
{

namespace
{
    WarbleType typeOf (int type) { return (WarbleType) std::clamp (type, 0, (int) typeShapes.size() - 1); }
}

std::string rateText (int type, bool synced, float percent)
{
    const auto position = std::clamp (percent, 0.0f, 100.0f) / 100.0f;
    if (synced)
        return noteValues[(size_t) syncIndex (typeOf (type), position)].name;
    return readout::hertzText (freeHz (typeOf (type), position));
}

float rateFromText (int type, bool synced, std::string_view text)
{
    if (synced)
    {
        const auto typed = readout::squeezed (text);
        for (size_t i = 0; i < noteValues.size(); ++i)
        {
            auto name = readout::squeezed (noteValues[i].name);
            if (typed == name)
                return 100.0f * syncPosition (typeOf (type), (int) i);
        }
        return -1.0f;
    }

    const auto hz = readout::hertzFromText (text);
    if (std::isnan (hz) || hz <= 0.0)
        return -1.0f;
    return 100.0f * freePosition (typeOf (type), hz);
}

} // namespace inkcap::dsp::warble
