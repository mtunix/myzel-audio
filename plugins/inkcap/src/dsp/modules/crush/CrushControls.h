#pragma once

#include <string>
#include <string_view>

namespace inkcap::dsp::crush
{

/** Drive's read-out under a Crush type: the dB into its stage at full intensity ("+13 dB"). */
std::string driveText (int type, float percent);

/** A typed Drive ("+13 dB", "13", or "40 %") back to a position (0 .. 100); -1 if the text is not understood. */
float driveFromText (int type, std::string_view text);

} // namespace inkcap::dsp::crush
