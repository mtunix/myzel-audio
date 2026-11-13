#pragma once

#include <string>
#include <string_view>

namespace inkcap::dsp::room
{

/** Size's read-out under a Room type: "1.4 m" (Closet, Room, Hall: the longest dimension), "1.6 s" (Spring: the decay). */
std::string sizeText (int type, float percent);

/** A typed Size back to a position (0 .. 100, the type's range clamped); -1 if the text is not understood. */
float sizeFromText (int type, std::string_view text);

} // namespace inkcap::dsp::room
