#include "ui/GlassWidgets.h"

namespace inkcap::ui
{

SoftKey::SoftKey (const juce::String& keyText, const juce::String& accessibleTitle, Icon i)
    : juce::Button (keyText),
      icon (i)
{
    setTitle (accessibleTitle.isNotEmpty() ? accessibleTitle : keyText);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
}

void SoftKey::setLit (bool shouldBeLit)
{
    if (lit != shouldBeLit)
    {
        lit = shouldBeLit;
        repaint();
    }
}

void SoftKey::setText (const juce::String& newText)
{
    if (newText != getButtonText())
        setButtonText (newText);
}

void SoftKey::setHighlight (int start, int length)
{
    const juce::Range<int> r (start, start + length);
    if (r != highlight)
    {
        highlight = r;
        repaint();
    }
}

float SoftKey::idealWidth() const
{
    if (icon == Icon::gear)
        return 12.0f;

    return std::ceil (glassdraw::textWidth (fonts->mono (metrics::textSize), getButtonText()));
}

void SoftKey::mouseDown (const juce::MouseEvent& e)
{
    if ((e.mods.isPopupMenu()) && onSecondaryClick != nullptr)
    {
        onSecondaryClick();
        return;
    }

    juce::Button::mouseDown (e);
}

void SoftKey::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& c = Theme::glass();
    const auto area = getLocalBounds().toFloat();
    const auto enabled = isEnabled();
    const auto focused = hasKeyboardFocus (false);

    auto ink = c.text;
    auto rule = c.textFaint;

    if (! enabled)
        ink = c.textFaint, rule = c.line;
    else if (lit)
        ink = rule = caution ? c.caution : c.amber;
    else if (highlighted || focused)
        rule = c.textDim;

    if (down)
        ink = ink.withMultipliedAlpha (0.7f);

    const auto textArea = area.withTrimmedBottom (1.0f);

    if (icon == Icon::gear)
    {
        glassdraw::gear (g, textArea.withSizeKeepingCentre (11.0f, 11.0f), ink);
    }
    else if (highlight.isEmpty() || ! enabled)
    {
        g.setColour (ink);
        g.setFont (fonts->mono (metrics::textSize));
        g.drawText (getButtonText(), textArea, juce::Justification::centred, false);
    }
    else
    {
        // Monospaced: each character has the same advance, so the highlight is a run of cells.
        const auto font = fonts->mono (metrics::textSize);
        const auto keyText = getButtonText();
        auto x = textArea.getCentreX() - glassdraw::textWidth (font, keyText) * 0.5f;
        g.setFont (font);

        for (int i = 0; i < keyText.length(); ++i)
        {
            const auto ch = keyText.substring (i, i + 1);
            const auto w = glassdraw::textWidth (font, ch);
            g.setColour (highlight.contains (i) ? c.amber : c.textDim);
            g.drawText (ch, juce::Rectangle<float> (x, textArea.getY(), w + 2.0f, textArea.getHeight()), juce::Justification::centredLeft,
                        false);
            x += w;
        }
    }

    g.setColour (focused ? c.amber : rule);
    g.fillRect (area.withTop (area.getBottom() - (focused ? 2.0f : 1.0f)));
}

//==============================================================================
TypedEntry::~TypedEntry()
{
    close();
}

void TypedEntry::open (juce::Component& owner, juce::Rectangle<int> area, const juce::String& text, const juce::String& title,
                       Commit onCommit, juce::Justification justification)
{
    close();

    const auto& c = Theme::glass();
    commit = std::move (onCommit);
    field = std::make_unique<juce::TextEditor> ("value");

    auto& f = *field;
    f.setColour (juce::TextEditor::backgroundColourId, c.glass.darker (0.4f));
    f.setColour (juce::TextEditor::textColourId, c.amber);
    f.setColour (juce::TextEditor::highlightColourId, c.amber.withAlpha (0.3f));
    f.setColour (juce::TextEditor::highlightedTextColourId, c.text);
    f.setColour (juce::TextEditor::outlineColourId, c.textFaint);
    f.setColour (juce::TextEditor::focusedOutlineColourId, c.amber);
    f.setColour (juce::CaretComponent::caretColourId, c.amber);
    f.setFont (fonts->mono (metrics::textSize));
    f.setJustification (justification);
    f.setIndents (5, 0);
    f.setBorder ({ 1, 1, 1, 1 });
    f.setTitle (title);
    f.setText (text, false);
    f.selectAll();
    f.setBounds (area);

    f.onReturnKey = [this] { finish (true); };
    f.onFocusLost = [this] { finish (true); };
    f.onEscapeKey = [this] { finish (false); };

    owner.addAndMakeVisible (f);

    if (owner.isShowing())
        f.grabKeyboardFocus();
}

void TypedEntry::finish (bool commitText)
{
    if (field == nullptr || ! field->isVisible())
        return;

    const auto text = field->getText();
    auto onCommit = commitText ? std::move (commit) : Commit {};
    commit = {};

    // The field is inside its own callback here: hide it now, delete it on the next open or close.
    field->onFocusLost = nullptr;
    field->setVisible (false);

    if (auto* parent = field->getParentComponent())
        parent->repaint();

    if (onCommit != nullptr)
        onCommit (text);
}

void TypedEntry::close()
{
    if (field != nullptr)
    {
        field->onFocusLost = nullptr;
        field->onReturnKey = nullptr;
        field->onEscapeKey = nullptr;
        field.reset();
    }
    commit = {};
}

void TypedEntry::commitForTest (const juce::String& text)
{
    if (! isOpen())
        return;

    field->setText (text, false);
    finish (true);
}

//==============================================================================
namespace glassdraw
{
    float textWidth (const juce::Font& font, const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidth (font, text);
    }

    void padlock (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink)
    {
        const auto s = juce::jmin (area.getWidth(), area.getHeight());
        const auto box = juce::Rectangle<float> (s * 0.8f, s).withCentre (area.getCentre());
        const auto body = box.withTrimmedTop (s * 0.42f);

        juce::Path shackle;
        const auto w = box.getWidth() * 0.62f;
        shackle.addCentredArc (box.getCentreX(), body.getY(), w * 0.5f, s * 0.34f, 0.0f, -juce::MathConstants<float>::halfPi,
                               juce::MathConstants<float>::halfPi, true);

        g.setColour (ink);
        g.strokePath (shackle, juce::PathStrokeType (juce::jmax (1.0f, s * 0.13f)));
        g.fillRoundedRectangle (body, s * 0.11f);
    }

    void gear (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink)
    {
        const auto c = area.getCentre();
        const auto r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        constexpr int teeth = 8;

        juce::Path p;
        for (int i = 0; i < teeth * 4; ++i)
        {
            // Each tooth: two points on the outer radius, two on the root.
            const auto a = juce::MathConstants<float>::twoPi * ((float) i - 0.5f) / (float) (teeth * 4);
            const auto rr = (i % 4 == 0 || i % 4 == 1) ? r : r * 0.74f;
            const auto pt = c + juce::Point<float> (std::sin (a), -std::cos (a)) * rr;
            if (i == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }
        p.closeSubPath();
        p.addEllipse (juce::Rectangle<float> (r * 0.68f, r * 0.68f).withCentre (c));
        p.setUsingNonZeroWinding (false);

        g.setColour (ink);
        g.fillPath (p);
    }

    void chevron (juce::Graphics& g, juce::Rectangle<float> area, bool left, juce::Colour ink)
    {
        const auto a = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 5.0f), juce::jmin (area.getHeight(), 9.0f));
        juce::Path p;
        if (left)
        {
            p.startNewSubPath (a.getRight(), a.getY());
            p.lineTo (a.getX(), a.getCentreY());
            p.lineTo (a.getRight(), a.getBottom());
        }
        else
        {
            p.startNewSubPath (a.getX(), a.getY());
            p.lineTo (a.getRight(), a.getCentreY());
            p.lineTo (a.getX(), a.getBottom());
        }
        g.setColour (ink);
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::String fitted (const juce::String& text, const juce::Font& font, float width)
    {
        if (textWidth (font, text) <= width)
            return text;

        const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
        auto cut = text;

        while (cut.isNotEmpty() && textWidth (font, cut + ellipsis) > width)
            cut = cut.dropLastCharacters (1);

        return cut.trimEnd() + ellipsis;
    }

    void fittedText (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Rectangle<float> area,
                     juce::Justification just, juce::Colour ink)
    {
        g.setColour (ink);
        g.setFont (font);
        g.drawText (fitted (text, font, area.getWidth()), area, just, false);
    }

    void focusRing (juce::Graphics& g, juce::Rectangle<float> area, float corner)
    {
        g.setColour (Theme::glass().amber.withAlpha (0.8f));
        g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.0f);
    }
}

} // namespace inkcap::ui
