#pragma once

#include <string>
#include <string_view>

namespace inkcap::dsp::bits
{

/** Rate's read-out under a Bits type: the converter's clock ("26.0 kHz", "8.00 kHz"). */
std::string rateText (int type, float percent);

/** A typed Rate ("26 kHz", "26k", "26040 Hz") back to a position (0 .. 100); -1 if the text is not understood. */
float rateFromText (int type, std::string_view text);

/** Depth's read-out under a Bits type: the converter's bits ("12 bits", "9.5 bits"). */
std::string depthText (int type, float percent);

/** A typed Depth ("12 bits", "12") back to a position (0 .. 100); -1 if the text is not understood. */
float depthFromText (int type, std::string_view text);

} // namespace inkcap::dsp::bits
