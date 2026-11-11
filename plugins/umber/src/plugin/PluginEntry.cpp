#include "plugin/PluginProcessor.h"

// Entry point used by the JUCE format wrappers (VST3, LV2, CLAP, Standalone).
// Kept out of UmberShared so the test executable doesn't need plugin defines.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new umber::UmberProcessor();
}
