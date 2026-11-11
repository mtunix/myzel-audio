#pragma once

#include <juce_core/juce_core.h>

namespace umber::ui
{

/** What a circuit (model) sounds like, in a sentence (tooltips, the studio view). */
juce::String circuitDescription (int model);

/** A word or two on a circuit (the studio view's circuit list). */
juce::String circuitTagline (int model);

/** Tooltips, the same in both views. */
namespace tips
{
    inline constexpr auto drive    = "Drive: how hard the circuit is pushed (-6 dB to +36 dB). Drag, Shift-drag for fine, double-click to reset.";
    inline constexpr auto slam     = "Slam: +20 dB extra drive into the circuit.";
    inline constexpr auto tone     = "Tone: tilt EQ around 800 Hz, darker or brighter. Sticks at Flat.";
    inline constexpr auto lowCut   = "Low Cut: 12 dB/oct high-pass before the circuit.";
    inline constexpr auto weight   = "Weight: resonant bump at the Low Cut frequency (needs Low Cut on).";
    inline constexpr auto highCut  = "High Cut: 12 dB/oct low-pass after the circuit.";
    inline constexpr auto mix      = "Mix: blend of the dry (latency-aligned) and saturated signal.";
    inline constexpr auto output   = "Output: level trim after the circuit.";
    inline constexpr auto autoGain = "Auto: output compensation that keeps the loudness steady as you drive.";
    inline constexpr auto delta    = "Delta: listen to only what the circuit adds (the processed signal minus the dry).";
    inline constexpr auto stereo   = "Stereo: L/R saturates left and right. Mid saturates only the centre, Side only the sides; "
                                     "the other half stays clean.";
    inline constexpr auto emphasis = "Emphasis: tilts the signal into the circuit and back out again (up to 6 dB per side around 1 kHz). "
                                     "The tone stays the same; it only changes which frequencies saturate first.";
    inline constexpr auto bias     = "Bias: moves the circuit's operating point, like turning its bias trim. Each circuit answers in its own way: "
                                     "the balance of even and odd harmonics changes, and so does how early and how hard it clips.";
}

} // namespace umber::ui
