#pragma once

#include "common/ui/Drawing.h"
#include "ui/EchoModel.h"
#include "ui/Theme.h"

#include <functional>
#include <vector>

namespace mulberry::ui
{

/**
    The echo map: a glass window that draws the repeats of a click from the parameters (time,
    level, side, groove) over a beat grid, with the live wet level behind them. Two lanes: the
    left output on top, the right below, so Ping-Pong, Dual and Pattern pans read at a glance.

    In Pattern mode it is the step editor: each step is a bar in the first pattern; drag it up or
    down (one host gesture per drag) to set its level, double-click resets it.

    Bezel, screen and glass are cached layers; the repeats are re-rendered only when the shape
    changes, the live level only when it moves by a pixel column.
*/
class EchoMap final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    EchoMap();

    void setShape (const EchoShape& shape);
    const EchoShape& getShape() const noexcept { return shape; }

    /** Advances the live level by dt with the current wet level (linear RMS). */
    void tick (float dtSeconds, float wetRms);

    /** Test hook: fills the history with a synthetic echo pattern. */
    void fillHistoryForSnapshot (float peakDb);

    /** Pattern editing: the level of step k (0-based) changes, as a gesture. */
    std::function<void (int step, float level)> onStepLevel;
    std::function<void (int step)> onStepGestureStart, onStepGestureEnd, onStepReset;

    /** Where step k's bar is (local), for tests. */
    juce::Rectangle<float> stepArea (int step) const;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr float bezelWidth = 9.0f;
    static constexpr float floorDb = -48.0f;
    static constexpr float historyRate = 60.0f;
    static constexpr int historySize = 60 * 16;

private:
    juce::Rectangle<float> screenArea() const;
    juce::Rectangle<float> laneArea (int lane) const;      // local
    float xForTime (float ms) const noexcept;
    float levelHeight (float linear, juce::Rectangle<float> lane) const noexcept;
    int stepAt (juce::Point<float> p) const;

    void renderBezel (juce::Graphics& g, juce::Rectangle<float> area) const;
    void renderScreen (juce::Graphics& g, juce::Rectangle<float> area) const;
    void renderEchoes (juce::Graphics& g, juce::Rectangle<float> area) const;
    void renderGlass (juce::Graphics& g, juce::Rectangle<float> area) const;
    void drawLive (juce::Graphics& g) const;
    void drawStamp (juce::Graphics& g, const juce::String& text, juce::Colour colour) const;
    void updateDescription();

    juce::SharedResourcePointer<Fonts> fonts;
    EchoShape shape;
    float span = 2000.0f;
    std::vector<EchoEvent> events;

    std::vector<float> history;
    long long written = 0;
    float pending = 0.0f, smoothedDb = floorDb;
    int shownLevelPixels = -1;

    int draggingStep = -1;

    drawing::CachedLayer bezelLayer, screenLayer, echoLayer, glassLayer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EchoMap)
};

} // namespace mulberry::ui
