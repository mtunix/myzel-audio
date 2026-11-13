#pragma once

#include <string>
#include <string_view>

namespace inkcap::dsp::glitch
{

/** Length's read-out under a Glitch type: Skip the repeated piece, Chew and Bend the event, Stall the stop. Any: a percentage. */
std::string lengthText (int type, float percent);

/** A typed Length back to a position (0 .. 100); -1 if the text is not understood. */
float lengthFromText (int type, std::string_view text);

} // namespace inkcap::dsp::glitch
