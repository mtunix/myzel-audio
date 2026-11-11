#pragma once

#include "ui/MainPanel.h"
#include "ui/UmberLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace umber
{

class UmberProcessor;

/**
    Umber's editor: hosts the procedurally drawn panel and scales it (75-200 %, fixed
    aspect ratio). The scale is stored in the processor's UI state, so it survives sessions.
*/
class UmberEditor final : public juce::AudioProcessorEditor
{
public:
    explicit UmberEditor (UmberProcessor& p);
    ~UmberEditor() override;

    void setScale (float newScale);
    float getScale() const noexcept { return scale; }

    ui::MainPanel& getMainPanel() noexcept { return *panel; }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    UmberProcessor& processor;
    ui::UmberLookAndFeel lookAndFeel;     // declared first: outlives every child
    std::unique_ptr<ui::MainPanel> panel;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UmberEditor)
};

} // namespace umber
