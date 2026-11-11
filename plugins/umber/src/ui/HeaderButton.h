#pragma once

#include "ui/Theme.h"

namespace umber::ui
{

/** Small key on the header rail: a text label or a procedurally drawn icon. */
class HeaderButton final : public juce::Button
{
public:
    enum class Icon
    {
        none,
        previous,
        next,
        undo,
        redo
    };

    HeaderButton (const juce::String& text, Icon icon = Icon::none);

    /** Draws a small caret, for buttons that open a menu. */
    void setShowsCaret (bool shouldShow) { showsCaret = shouldShow; repaint(); }

    /** Selected keys (e.g. the active A/B slot) light up. */
    void setLit (bool shouldBeLit);
    bool isLit() const noexcept { return lit; }

    void setLabel (const juce::String& newText);

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) const;

    juce::SharedResourcePointer<Fonts> fonts;
    juce::String text;
    Icon icon;
    bool showsCaret = false, lit = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderButton)
};

} // namespace umber::ui
