#include "ui/DisplayToolbar.h"

namespace halite::ui
{

namespace
{
    struct LegendItem
    {
        const char* text;
        bool line;      // a line swatch (Sibilance), otherwise a filled one
    };

    constexpr LegendItem legendItems[] { { "Output", false }, { "Removed", false }, { "Sibilance", true }, { "Harsh", false } };

    juce::Colour swatch (int i)
    {
        const auto& t = theme::current();
        switch (i)
        {
            case 0:  return t.signal;
            case 1:  return t.essFill;
            case 2:  return t.text;
            default: return t.harshFill;
        }
    }

    // The Cloud's sentence around its two marks, drawn as the plot draws them (CloudView::drawDot).
    const juce::String cloudLegend[] { juce::CharPointer_UTF8 ("Each ess, in dB over the voice \xc2\xb7 "), " before ", " after" };
    constexpr float cloudMark = 9.0f;

    float cloudLegendWidth (const juce::Font& font)
    {
        auto w = 2.0f * cloudMark;
        for (const auto& text : cloudLegend)
            w += textWidth (font, text);
        return w;
    }
}

DisplayToolbar::DisplayToolbar (HaliteController& c)
    : controller (c)
{
    setTitle ("Display toolbar");
    setFocusContainerType (FocusContainerType::focusContainer);

    auto order = 1;
    for (auto* comp : std::initializer_list<juce::Component*> { &tabs, &zoom, &freeze, &events })
    {
        addAndMakeVisible (comp);
        comp->setExplicitFocusOrder (order++);
    }

    tabs.setHelp (helpFor ("view"));
    tabs.setCompact (true);
    tabs.getIndex = [this] { return (int) controller.getDisplayView(); };
    tabs.setIndex = [this] (int i) { controller.setDisplayView ((DisplayView) i); };

    zoom.setSmall (true);
    zoom.setMonoText (true);
    zoom.setHelp (helpFor ("zoom"));
    zoom.onClick = [this] { if (onZoomMenu != nullptr) onZoomMenu(); };

    freeze.setHelp (helpFor ("freeze"));
    freeze.setLatched (Icon::play);
    freeze.onClick = [this] { if (onFreeze != nullptr) onFreeze(); else freeze.setOn (! freeze.isOn()); };

    events.setHelp (helpFor ("events"));
    events.onClick = [this] { if (onToggleSidebar != nullptr) onToggleSidebar(); };
}

void DisplayToolbar::update()
{
    tabs.update();

    const auto view = controller.getDisplayView();
    zoom.setText (juce::String (juce::roundToInt (controller.getZoom (view))) + " s");

    if (view != shownView)
    {
        shownView = view;
        resized();
        repaint();
    }
}

float DisplayToolbar::legendWidth() const
{
    const auto font = fonts->sans (type::label);

    if (shownView == DisplayView::cloud)
        return cloudLegendWidth (font) + 4.0f;

    auto w = 0.0f;
    for (const auto& item : legendItems)
        w += (item.line ? 12.0f : 10.0f) + 5.0f + textWidth (font, item.text) + 12.0f;
    return w;
}

void DisplayToolbar::resized()
{
    auto area = getLocalBounds().reduced (12, 0);
    const auto h = getHeight();
    const auto controlHeight = std::min (22, h - 8);

    tabs.setBounds (area.removeFromLeft ((int) tabs.wellWidth()).withSizeKeepingCentre ((int) tabs.wellWidth(), (int) Segmented::wellHeight));
    area.removeFromLeft (12);

    events.setBounds (area.removeFromRight (24).withSizeKeepingCentre (24, controlHeight));
    area.removeFromRight (6);
    freeze.setBounds (area.removeFromRight (24).withSizeKeepingCentre (24, controlHeight));
    area.removeFromRight (6);
    // Wide enough for the longest zoom ("60 s"), so the button never resizes or cuts its text.
    const auto zoomWidth = (int) std::ceil (textWidth (fonts->mono (type::label), "60 s")) + 32;
    zoom.setBounds (area.removeFromRight (zoomWidth).withSizeKeepingCentre (zoomWidth, controlHeight));
    area.removeFromRight (12);

    const auto needed = (int) std::ceil (legendWidth());
    legend = needed <= area.getWidth() ? area.withWidth (needed) : juce::Rectangle<int>();
}

void DisplayToolbar::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();

    g.fillAll (t.background);
    g.setColour (t.lineSoft);
    g.fillRect (bounds.withTop (bounds.getBottom() - 1.0f));

    if (legend.isEmpty())
        return;

    g.setFont (fonts->sans (type::label));
    auto area = legend.toFloat();

    if (shownView == DisplayView::cloud)
    {
        const auto cy = area.getCentreY();
        auto x = area.getX();
        for (int i = 0; i < (int) std::size (cloudLegend); ++i)
        {
            const auto w = textWidth (g.getCurrentFont(), cloudLegend[i]);
            g.setColour (t.secondary);
            g.drawText (cloudLegend[i], juce::Rectangle<float> (x, area.getY(), w + 1.0f, area.getHeight()),
                        juce::Justification::centredLeft, false);
            x += w;

            if (i == 0)
            {
                g.setColour (t.essText.withAlpha (0.6f));
                g.drawEllipse (juce::Rectangle<float> (x + 0.75f, cy - 3.75f, 7.5f, 7.5f), 1.5f);
            }
            else if (i == 1)
            {
                g.setColour (t.essText);
                g.fillEllipse (juce::Rectangle<float> (x + 0.5f, cy - 4.0f, 8.0f, 8.0f));
            }
            x += cloudMark;
        }
        return;
    }

    for (int i = 0; i < (int) std::size (legendItems); ++i)
    {
        const auto& item = legendItems[i];
        const auto cy = area.getCentreY();

        const auto box = juce::Rectangle<float> (area.getX(), cy - 4.5f, 9.0f, 9.0f);
        g.setColour (swatch (i));
        if (item.line)
            g.fillRect (juce::Rectangle<float> (area.getX(), cy - 0.75f, 12.0f, 1.5f));
        else
            g.fillRoundedRectangle (box, 2.0f);

        // The waveform's two tones (peak around its RMS core), and the removed part's edge where it has one.
        if (i == 0)
        {
            g.setColour (t.signalCore);
            g.fillRect (box.reduced (2.0f));
        }
        else if (i == 1)
        {
            g.setColour (t.removedEdge);
            g.drawRoundedRectangle (box.reduced (0.5f), 1.5f, 1.0f);
        }

        area.removeFromLeft ((item.line ? 12.0f : 10.0f) + 5.0f);
        const auto w = textWidth (g.getCurrentFont(), item.text) + 1.0f;
        g.setColour (t.secondary);
        g.drawText (item.text, area.removeFromLeft (w), juce::Justification::centredLeft, false);
        area.removeFromLeft (11.0f);
    }
}

} // namespace halite::ui
