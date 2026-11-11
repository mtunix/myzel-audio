#pragma once

#include "ui/Drawing.h"

namespace umber::ui
{

/** The amber read-out window in the header showing the preset; clicking opens the preset menu. */
class PresetDisplay final : public juce::Button
{
public:
    PresetDisplay();

    void setPreset (const juce::String& name, const juce::String& category, bool modified);

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    void resized() override;

private:
    juce::SharedResourcePointer<Fonts> fonts;
    juce::String name, category;
    bool modified = false;

    drawing::CachedLayer glassLayer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetDisplay)
};

} // namespace umber::ui
