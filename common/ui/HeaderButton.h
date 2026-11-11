#pragma once

#include "common/ui/Theme.h"

namespace myzel::ui
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
        redo,
        settings
    };

    HeaderButton (const juce::String& text, Icon icon = Icon::none);

    /** Draws a small caret, for buttons that open a menu. */
    void setShowsCaret (bool shouldShow) { showsCaret = shouldShow; repaint(); }

    /** Selected keys (e.g. the active A/B slot) light up. */
    void setLit (bool shouldBeLit);
    bool isLit() const noexcept { return lit; }

    void setLabel (const juce::String& newText);

    /** Lit in the caution colour instead of the accent: for states that take the sound away (Bypass). */
    void setCaution (bool shouldUseCaution);

    /** One half of a segmented pair (PANEL | STUDIO): square inner corners. */
    enum class Segment { none, left, right };
    void setSegment (Segment s) { segment = s; repaint(); }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) const;

    juce::SharedResourcePointer<Fonts> fonts;
    juce::String text;
    Icon icon;
    bool showsCaret = false, lit = false, caution = false;
    Segment segment = Segment::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderButton)
};

} // namespace myzel::ui
