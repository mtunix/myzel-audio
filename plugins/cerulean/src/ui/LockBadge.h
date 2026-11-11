#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cerulean::ui
{

/**
    A small padlock printed next to the Mix legend while the mix lock is on (⚙ menu), so a
    preset that leaves Mix alone never looks like a bug. Same glyph in both views; each view
    passes its own ink colour and shows or hides it when the preset manager reports a change.
*/
class LockBadge final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    explicit LockBadge (juce::Colour inkColour) : ink (inkColour)
    {
        setInterceptsMouseClicks (true, false);
        setTooltip ("Mix lock is on: presets and A/B keep Mix and Wet as they are. Turn it off in the settings menu.");
        setTitle ("Mix lock on");
    }

    /** Glyph size in base coordinates; place it with setCentre(). */
    static constexpr int width = 9, height = 11;

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const auto body = juce::Rectangle<float> (r.getX(), r.getY() + r.getHeight() * 0.42f, r.getWidth(), r.getHeight() * 0.58f);

        juce::Path shackle;
        const auto sw = r.getWidth() * 0.62f;
        shackle.addCentredArc (r.getCentreX(), body.getY(), sw * 0.5f, r.getHeight() * 0.34f, 0.0f,
                               -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);

        g.setColour (ink);
        g.strokePath (shackle, juce::PathStrokeType (1.3f));
        g.fillRoundedRectangle (body, 1.2f);
    }

private:
    juce::Colour ink;
};

} // namespace cerulean::ui
