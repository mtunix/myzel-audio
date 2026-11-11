#include "ui/InlineDialog.h"

#include "ui/Drawing.h"

#include <melatonin_blur/melatonin_blur.h>

namespace cerulean::ui
{

namespace
{
    constexpr float cardWidth = 340.0f;
    constexpr float cardHeightPlain = 150.0f;
    constexpr float cardHeightWithField = 182.0f;
}

/** A text key for the dialog (primary = ice lit). */
class InlineDialog::DialogButton final : public juce::Button
{
public:
    DialogButton (const juce::String& label, bool isPrimary)
        : juce::Button (label), primary (isPrimary)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
    }

    bool destructive = false;

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto& pal = Theme::palette();
        const auto area = getLocalBounds().toFloat().reduced (0.5f).translated (0.0f, down ? 0.6f : 0.0f);

        auto top = primary ? (destructive ? pal.keyTop : pal.iceDeep) : pal.keyTop;
        auto bottom = primary ? (destructive ? pal.keyBottom : pal.iceDeep.darker (0.35f)) : pal.keyBottom;

        if (highlighted)
        {
            top = top.brighter (0.15f);
            bottom = bottom.brighter (0.15f);
        }

        g.setColour (juce::Colour (0x80000000));
        g.fillRoundedRectangle (area.translated (0.4f, 1.2f), 4.0f);
        g.setGradientFill (juce::ColourGradient (top, area.getX(), area.getY(), bottom, area.getX(), area.getBottom(), false));
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (juce::Colour (0x30ffffff));
        g.drawRoundedRectangle (area.reduced (0.5f), 3.5f, 0.8f);

        g.setColour (primary ? (destructive ? pal.danger : pal.iceCore) : pal.keyText);
        g.setFont (drawing::tracked (juce::SharedResourcePointer<Fonts>()->legend (11.5f), 0.12f));
        g.drawText (getButtonText().toUpperCase(), area, juce::Justification::centred, false);

        if (hasKeyboardFocus (false))
        {
            g.setColour (pal.focusOnDark);
            g.drawRoundedRectangle (area.expanded (1.5f), 5.5f, 1.2f);
        }
    }

private:
    bool primary;
};

//==============================================================================
InlineDialog::InlineDialog()
    : okButton (std::make_unique<DialogButton> ("OK", true)),
      cancelButton (std::make_unique<DialogButton> ("Cancel", false))
{
    setVisible (false);
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    const auto& pal = Theme::palette();
    textField.setColour (juce::TextEditor::backgroundColourId, pal.displayGlass);
    textField.setColour (juce::TextEditor::textColourId, pal.displayText);
    textField.setColour (juce::TextEditor::highlightColourId, pal.ice.withAlpha (0.35f));
    textField.setColour (juce::TextEditor::highlightedTextColourId, pal.iceCore);
    textField.setColour (juce::TextEditor::outlineColourId, juce::Colour (0x40ffffff));
    textField.setColour (juce::TextEditor::focusedOutlineColourId, pal.ice);
    textField.setColour (juce::CaretComponent::caretColourId, pal.ice);
    textField.setFont (fonts->label (14.0f));
    textField.setIndents (8, 5);
    textField.setTitle ("Name");
    textField.onReturnKey = [this] { confirm(); };
    textField.onEscapeKey = [this] { close(); };
    textField.onTextChange = [this] { if (error.isNotEmpty()) { error.clear(); repaint(); } };
    addChildComponent (textField);

    okButton->onClick = [this] { confirm(); };
    cancelButton->onClick = [this] { close(); };
    addAndMakeVisible (*okButton);
    addAndMakeVisible (*cancelButton);
}

InlineDialog::~InlineDialog() = default;

void InlineDialog::open (Options newOptions)
{
    options = std::move (newOptions);
    error.clear();
    okButton->setButtonText (options.confirmLabel);
    okButton->destructive = options.destructive;
    textField.setVisible (options.withTextField);
    textField.setText (options.initialText, false);

    setVisible (true);
    toFront (true);
    resized();

    if (options.withTextField)
    {
        if (isShowing())
            textField.grabKeyboardFocus();

        textField.selectAll();
    }
    else if (isShowing())
    {
        okButton->grabKeyboardFocus();
    }

    repaint();
}

void InlineDialog::close()
{
    setVisible (false);
    options = {};
}

void InlineDialog::confirm()
{
    if (! isVisible())
        return;

    auto callback = options.onConfirm;
    const auto text = textField.getText();

    if (callback != nullptr)
    {
        const auto message = callback (text);

        if (message.isNotEmpty())
        {
            error = message;
            repaint();
            return;
        }
    }

    close();
}

juce::Rectangle<float> InlineDialog::cardArea() const
{
    const auto h = options.withTextField ? cardHeightWithField : cardHeightPlain;
    return juce::Rectangle<float> (cardWidth, h).withCentre (getLocalBounds().toFloat().getCentre());
}

void InlineDialog::resized()
{
    const auto card = cardArea();
    auto bottom = card.reduced (18.0f).removeFromBottom (30.0f);
    okButton->setBounds (bottom.removeFromRight (100.0f).toNearestInt());
    bottom.removeFromRight (10.0f);
    cancelButton->setBounds (bottom.removeFromRight (90.0f).toNearestInt());

    textField.setBounds (juce::Rectangle<float> (card.getX() + 18.0f, card.getY() + 86.0f, card.getWidth() - 36.0f, 30.0f).toNearestInt());
}

void InlineDialog::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palette();
    g.fillAll (pal.overlayDim);

    const auto card = cardArea();
    juce::Path cardPath;
    cardPath.addRoundedRectangle (card, 8.0f);

    melatonin::DropShadow shadow (juce::Colour (0xc0000000), 24, { 4, 10 });
    shadow.render (g, cardPath);

    g.setGradientFill (juce::ColourGradient (pal.headerTop.brighter (0.25f), card.getX(), card.getY(),
                                             pal.headerBottom, card.getX(), card.getBottom(), false));
    g.fillPath (cardPath);
    g.setColour (pal.ice.withAlpha (0.45f));
    g.fillRect (card.getX() + 8.0f, card.getY(), card.getWidth() - 16.0f, 2.0f);
    g.setColour (juce::Colour (0x24ffffff));
    g.strokePath (cardPath, juce::PathStrokeType (1.0f));

    auto text = card.reduced (18.0f, 16.0f);
    g.setColour (pal.headerLegend);
    g.setFont (drawing::tracked (fonts->legend (13.0f), 0.16f));
    g.drawText (options.title.toUpperCase(), text.removeFromTop (20.0f), juce::Justification::centredLeft, true);

    text.removeFromTop (6.0f);
    g.setColour (error.isNotEmpty() ? pal.danger : pal.headerLegendDim);
    g.setFont (fonts->label (12.5f));
    g.drawFittedText (error.isNotEmpty() ? error : options.message, text.removeFromTop (36.0f).toNearestInt(),
                      juce::Justification::topLeft, 2);
}

void InlineDialog::mouseDown (const juce::MouseEvent& e)
{
    if (! cardArea().contains (e.position))
        close();
}

bool InlineDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        confirm();
        return true;
    }

    return false;
}

} // namespace cerulean::ui
