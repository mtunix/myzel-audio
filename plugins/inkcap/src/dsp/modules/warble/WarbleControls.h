#pragma once

#include <string>
#include <string_view>

namespace inkcap::dsp::warble
{

/** Rate's read-out: free, a frequency around the type's own; synced, a note value from the type's window. */
std::string rateText (int type, bool synced, float percent);

/** A typed Rate back to a position (0 .. 100): "2 Hz" free, "1/8" synced; -1 if not understood. */
float rateFromText (int type, bool synced, std::string_view text);

} // namespace inkcap::dsp::warble
