#pragma once

#include "ui/Drawing.h"

namespace cerulean::ui
{

/** The ice-blue read-out window in the header showing the preset; clicking opens the preset menu. */
class PresetDisplay final : public juce::Button
{
public:
    PresetDisplay();

    void setPreset (const juce::String& name, const juce::String& category, bool modified);

    /** Keeps this much room free at the right edge (inside the caret) for the SAVE key. */
    void setReservedRight (float width);
    juce::Rectangle<float> reservedArea() const;

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    void resized() override;

private:
    juce::SharedResourcePointer<Fonts> fonts;
    juce::String name, category;
    bool modified = false;
    float reservedRight = 0.0f;

    drawing::CachedLayer glassLayer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetDisplay)
};

} // namespace cerulean::ui
