#include "plugin/PluginProcessor.h"

// Entry point used by the JUCE format wrappers (VST3, LV2, CLAP, Standalone).
// Kept out of HaliteShared so the test executable doesn't need plugin defines.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new halite::HaliteProcessor();
}
