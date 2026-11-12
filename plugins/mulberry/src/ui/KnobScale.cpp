#include "ui/KnobScale.h"

#include "common/ui/PanelArtwork.h"
#include "ui/Theme.h"

namespace mulberry::ui
{

KnobScale::KnobScale()
{
    setInterceptsMouseClicks (false, false);
}

void KnobScale::setKnobCentre (juce::Point<float> centre, float diameter)
{
    // Room for the scale's labels on every side of the knob.
    width = 2.0f * (diameter * 0.5f + 72.0f);
    above = diameter * 0.5f + 36.0f;
    setBounds (juce::Rectangle<float> (centre.x - width * 0.5f, centre.y - above, width, above + diameter * 0.5f + 56.0f).toNearestInt());
}

void KnobScale::setStyle (const KnobStyle& style, bool animate)
{
    if (animate)
    {
        previous = current;
        std::swap (currentLayer, previousLayer);
        hasPrevious = true;
        fade = 0.0f;
    }
    else
    {
        hasPrevious = false;
        fade = 1.0f;
        previousLayer.invalidate();
    }

    current = style;
    currentLayer.invalidate();
    repaint();
}

bool KnobScale::tick (float dt)
{
    if (! hasPrevious)
        return false;

    fade = juce::jmin (1.0f, fade + dt / metrics::legendFadeSeconds);

    if (fade >= 1.0f)
    {
        hasPrevious = false;
        previousLayer.invalidate();
    }

    repaint();
    return hasPrevious;
}

void KnobScale::resized()
{
    currentLayer.invalidate();
    previousLayer.invalidate();
}

void KnobScale::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto centre = juce::Point<float> (width * 0.5f, above);

    auto render = [this, centre] (const KnobStyle& s)
    {
        return [this, centre, &s] (juce::Graphics& lg, juce::Rectangle<float>, float)
        {
            renderKnobScale (lg, centre, s, *fonts);
        };
    };

    // Ease in-out, and the old legend leaves a little faster than the new one arrives.
    const auto x = fade * fade * (3.0f - 2.0f * fade);

    if (hasPrevious)
        previousLayer.draw (g, area, render (previous), juce::jlimit (0.0f, 1.0f, 1.0f - x * 1.3f));

    currentLayer.draw (g, area, render (current), hasPrevious ? x : 1.0f);
}

} // namespace mulberry::ui
