#include "ui/Texts.h"

namespace umber::ui
{

juce::String circuitDescription (int model)
{
    switch (model)
    {
        case 0:  return "Reel: tube record amp into magnetic tape. Soft compression, head bump, rounded top.";
        case 1:  return "Desk: console line amp with transformers. Weighty low end that saturates first.";
        case 2:  return "Class A: single-ended discrete transistor stage. Forward, bright, rich in even harmonics.";
        case 3:  return "Valve: triode preamp stage. Warm and dynamic, with a grid-conduction bias shift.";
        case 4:  return "Fuzz: overdriven pentode / two-stage clipper. Dense, aggressive, hard-edged.";
        default: return {};
    }
}

juce::String circuitTagline (int model)
{
    switch (model)
    {
        case 0:  return "Tape";
        case 1:  return "Console";
        case 2:  return "Transistor";
        case 3:  return "Triode";
        case 4:  return "Pentode";
        default: return {};
    }
}

} // namespace umber::ui
