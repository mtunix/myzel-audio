#pragma once

#include "ui/Theme.h"

namespace inkcap::ui
{

/**
    A soft key on the display: lower-case text over a hairline (the mock's buttons). A lit key is
    amber (a page that is open, the type in use), a caution key is coral while lit (Bypass); a
    disabled key fades to the faint ink. Keyboard: Space or Return presses it.
*/
class SoftKey final : public juce::Button
{
public:
    enum class Icon
    {
        none,
        gear
    };

    explicit SoftKey (const juce::String& text, const juce::String& accessibleTitle = {}, Icon icon = Icon::none);

    void setLit (bool shouldBeLit);
    bool isLit() const noexcept { return lit; }
    void setCaution (bool usesCaution) { caution = usesCaution; repaint(); }
    void setText (const juce::String& newText);

    /** Characters [start, start + length) drawn in amber, the rest dimmed ("a/b": the slot in use). */
    void setHighlight (int start, int length);

    /** Width of the text at the display's size: what the layout gives the key. */
    float idealWidth() const;

    /** Right-click (or Ctrl-click): a key's second action, such as Redo on undo. */
    std::function<void()> onSecondaryClick;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::SharedResourcePointer<GlassFonts> fonts;
    Icon icon;
    bool lit = false, caution = false;
    juce::Range<int> highlight;
};

/**
    A text field over a display value (double-click): Return or a click elsewhere commits, Escape
    cancels. The value's own parser reads the text, so "49 ms", "1/8" or "dark 40" mean what they
    say; text it does not understand changes nothing. The host component owns the entry.
*/
class TypedEntry final
{
public:
    using Commit = std::function<void (const juce::String&)>;

    ~TypedEntry();

    void open (juce::Component& host, juce::Rectangle<int> area, const juce::String& text, const juce::String& title,
               Commit onCommit, juce::Justification = juce::Justification::centredRight);
    void close();
    bool isOpen() const noexcept { return field != nullptr && field->isVisible(); }

    /** Types the text and presses Return (the self-test). */
    void commitForTest (const juce::String& text);

private:
    void finish (bool commitText);

    std::unique_ptr<juce::TextEditor> field;
    Commit commit;
    juce::SharedResourcePointer<GlassFonts> fonts;
};

namespace glassdraw
{
    /** A small padlock, closed, in the given ink (the mix and seed locks). */
    void padlock (juce::Graphics&, juce::Rectangle<float> area, juce::Colour ink);

    /** The settings gear. */
    void gear (juce::Graphics&, juce::Rectangle<float> area, juce::Colour ink);

    /** A chevron pointing left (back) or right. */
    void chevron (juce::Graphics&, juce::Rectangle<float> area, bool left, juce::Colour ink);

    /** Text that ends in an ellipsis instead of overflowing its area. */
    void fittedText (juce::Graphics&, const juce::String&, const juce::Font&, juce::Rectangle<float>, juce::Justification,
                     juce::Colour);

    /** The text cut to fit a width, with an ellipsis if it had to be cut. */
    juce::String fitted (const juce::String&, const juce::Font&, float width);

    float textWidth (const juce::Font&, const juce::String&);

    /** A focus outline for keyboard focus inside the display. */
    void focusRing (juce::Graphics&, juce::Rectangle<float> area, float corner);
}

} // namespace inkcap::ui
