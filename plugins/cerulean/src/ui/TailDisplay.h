#pragma once

#include "ui/Drawing.h"
#include "ui/TailModel.h"

#include <vector>

namespace cerulean::ui
{

/**
    The tail display: a glass window (in the bezel where Umber has its VU meter) that draws
    the decay envelope from the parameters and runs the live wet level across it.

    - Envelope (cached): pre-delay gap, early cluster, a fan between the low and high band decays
      with the mid band as the bright line. Re-rendered only when the shape changes.
    - Live level (per frame): a scrolling history of the wet level, newest at the right edge,
      on the same time and dB axes. Repainted only when it moves by a pixel column, and not at
      all while it is silent.
    - Frozen: the envelope is a flat line and "FREEZE" lights.
    - Bypassed: the envelope dims and an amber BYPASSED stamp says what is going on (and that
      the old tail is ringing out, with Trails).

    Bezel, screen and glass are cached layers.
*/
class TailDisplay final : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    TailDisplay();

    /** Sets the shape to draw (cheap if unchanged). */
    void setShape (const TailShape& shape);
    const TailShape& getShape() const noexcept { return shape; }

    /** Advances the live trace by dt with the current wet level (linear RMS). */
    void tick (float dtSeconds, float wetRms);

    /** Test hook: fills the history with a synthetic decaying note. */
    void fillHistoryForSnapshot (float peakDb, float holdSeconds);

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr float bezelWidth = 9.0f;
    static constexpr float historyRate = 60.0f;       // entries per second
    static constexpr int historySize = 60 * 64;       // 64 s, the longest span

private:
    juce::Rectangle<float> screenArea() const;
    juce::Rectangle<float> plotArea() const;          // screen-local
    float xForTime (float t, juce::Rectangle<float> plot) const noexcept;
    float yForDb (float db, juce::Rectangle<float> plot) const noexcept;
    float historyAt (long long entry) const noexcept;
    long long currentColumn() const noexcept;

    void renderBezel (juce::Graphics& g, juce::Rectangle<float> area) const;
    void renderScreen (juce::Graphics& g, juce::Rectangle<float> area) const;
    void renderEnvelope (juce::Graphics& g, juce::Rectangle<float> area) const;
    void renderGlass (juce::Graphics& g, juce::Rectangle<float> area) const;
    void drawLive (juce::Graphics& g, juce::Rectangle<float> screen) const;
    void drawBypassStamp (juce::Graphics& g, juce::Rectangle<float> screen) const;
    void updateDescription();

    juce::SharedResourcePointer<Fonts> fonts;
    TailShape shape;
    float span = 4.0f;

    std::vector<float> history;       // dBFS, ring buffer
    long long written = 0;            // entries written so far
    float pending = 0.0f;             // time not yet turned into an entry
    float smoothedDb = tail::floorDb;
    long long shownColumn = -1;
    bool liveVisible = false;

    drawing::CachedLayer bezelLayer, screenLayer, envelopeLayer, glassLayer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TailDisplay)
};

} // namespace cerulean::ui
