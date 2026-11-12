#pragma once

namespace myzel::dsp
{

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal"
#endif

/** Exact comparison, for the places that mean it (a parameter that did not move, an exact
    nominal point): the same as ==, without -Wfloat-equal (JUCE's exactlyEqual, JUCE-free). */
[[nodiscard]] constexpr bool isExactly (double a, double b) noexcept { return a == b; }

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic pop
#endif

} // namespace myzel::dsp
