#include "ui/StatusLine.h"

#include "ui/Widgets.h"

namespace halite::ui
{

StatusLine::StatusLine (HaliteController& c)
    : controller (c)
{
    setTitle ("Status");
    setOpaque (true);
}

void StatusLine::update()
{
    const auto h = controller.currentHelp();
    const auto s = controller.modeSummary();

    if (h.title != help.title || h.text != help.text || s != summary)
    {
        help = h;
        summary = s;
        setDescription (help.title.isEmpty() ? summary : help.title + ": " + help.text);
        repaint();
    }
}

void StatusLine::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    g.fillAll (t.canvas);
    g.setColour (t.lineSoft);
    g.fillRect (0, 0, getWidth(), 1);

    // Right: the mode summary, clear of the corner resizer. It yields to the help when space is short.
    auto area = getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedRight (6.0f);
    const auto monoFont = fonts->mono (type::label);
    const auto summaryWidth = std::min (textWidth (monoFont, summary) + 2.0f, area.getWidth() * 0.5f);

    g.setFont (monoFont);
    g.setColour (t.secondary);
    g.drawText (fitted (summary, monoFont, summaryWidth), area.removeFromRight (summaryWidth), juce::Justification::centredRight, false);
    area.removeFromRight (16.0f);

    if (help.title.isEmpty())
        return;

    const auto titleFont = fonts->sansSemiBold (type::control);
    const auto titleWidth = std::min (textWidth (titleFont, help.title) + 2.0f, area.getWidth());
    g.setFont (titleFont);
    g.setColour (t.text);
    g.drawText (help.title, area.removeFromLeft (titleWidth), juce::Justification::centredLeft, false);
    area.removeFromLeft (10.0f);

    const auto textFont = fonts->sans (type::control);
    g.setFont (textFont);
    g.setColour (t.secondary);
    g.drawText (fitted (help.text, textFont, area.getWidth()), area, juce::Justification::centredLeft, false);
}

//==============================================================================
namespace
{
    constexpr float cardWidth = 360.0f;

    class DialogButton final : public HelpedButton
    {
    public:
        DialogButton (const juce::String& label, bool isPrimary) : HelpedButton (label), primary (isPrimary) {}

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto& t = theme::current();
            const auto area = getLocalBounds().toFloat().reduced (0.5f);

            if (primary)
            {
                g.setColour (down ? t.essFill.darker (0.12f) : highlighted ? t.essFill.brighter (0.06f) : t.essFill);
                g.fillRoundedRectangle (area, 6.0f);
            }
            else
            {
                g.setColour (down ? t.hover : t.raised);
                g.fillRoundedRectangle (area, 6.0f);
                g.setColour (t.line);
                g.drawRoundedRectangle (area, 6.0f, 1.0f);
            }

            g.setColour (primary ? t.onAccent : t.text);
            g.setFont (primary ? fonts->sansSemiBold (type::button) : fonts->sans (type::button));
            g.drawText (getButtonText(), area, juce::Justification::centred, false);

            if (hasKeyboardFocus (false))
                draw::focusRing (g, area, 6.0f);
        }

    private:
        bool primary;
    };
}

Dialog::Dialog()
    : okButton (std::make_unique<DialogButton> ("OK", true)),
      cancelButton (std::make_unique<DialogButton> ("Cancel", false))
{
    setVisible (false);
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);
    setTitle ("Dialog");

    field.setIndents (8, 5);
    field.setTitle ("Name");
    field.onReturnKey = [this] { confirm(); };
    field.onEscapeKey = [this] { close(); };
    field.onTextChange = [this] { if (error.isNotEmpty()) { error.clear(); repaint(); } };
    addChildComponent (field);

    okButton->onClick = [this] { confirm(); };
    cancelButton->onClick = [this] { close(); };
    addAndMakeVisible (*okButton);
    addAndMakeVisible (*cancelButton);
}

Dialog::~Dialog() = default;

void Dialog::open (Options o)
{
    options = std::move (o);
    error.clear();
    ++generation;

    const auto& t = theme::current();
    field.setColour (juce::TextEditor::backgroundColourId, t.background);
    field.setColour (juce::TextEditor::textColourId, t.text);
    field.setColour (juce::TextEditor::highlightColourId, t.essFill.withAlpha (0.35f));
    field.setColour (juce::TextEditor::outlineColourId, t.line);
    field.setColour (juce::TextEditor::focusedOutlineColourId, t.focus);
    field.setColour (juce::CaretComponent::caretColourId, t.focus);
    field.setFont (fonts->sans (type::preset));
    field.setVisible (options.withTextField);
    field.setText (options.initialText, false);

    okButton->setButtonText (options.confirmLabel);
    cancelButton->setVisible (options.withCancel);

    setVisible (true);
    toFront (true);
    resized();

    if (options.withTextField)
    {
        if (isShowing())
            field.grabKeyboardFocus();
        field.selectAll();
    }
    else if (isShowing())
    {
        grabKeyboardFocus();
    }
}

void Dialog::close()
{
    setVisible (false);
    options = {};
}

void Dialog::confirm()
{
    if (! isOpen())
        return;

    const auto text = field.getText().trim();

    if (options.onConfirm != nullptr)
    {
        // The callback may open the dialog again: take what it needs first.
        auto callback = options.onConfirm;
        const auto opened = generation;
        const auto message = callback (text);

        if (generation != opened)
            return;

        if (message.isNotEmpty())
        {
            error = message;
            repaint();
            return;
        }
    }

    close();
}

void Dialog::confirmForTest (const juce::String& text)
{
    if (options.withTextField)
        field.setText (text, false);
    confirm();
}

juce::TextLayout Dialog::messageLayout (float width) const
{
    juce::AttributedString text;
    text.append (options.message, fonts->sans (type::control + 1.0f), theme::current().secondary);
    text.setLineSpacing (2.0f);
    juce::TextLayout layout;
    layout.createLayout (text, width);
    return layout;
}

juce::Rectangle<float> Dialog::card() const
{
    // Padding, title and the gaps around the message, then the field and the buttons: the card grows with its message.
    const auto width = std::min (cardWidth, (float) getWidth() - 32.0f);
    const auto below = (options.withTextField ? 16.0f + 30.0f : 0.0f) + 8.0f + 28.0f;
    const auto fitted = 40.0f + 20.0f + 8.0f + messageLayout (width - 40.0f).getHeight() + below;
    const auto height = std::min (std::max (options.withTextField ? 196.0f : 160.0f, std::ceil (fitted)), (float) getHeight() - 32.0f);
    return juce::Rectangle<float> (width, height).withCentre (getLocalBounds().toFloat().getCentre());
}

void Dialog::resized()
{
    const auto c = card().reduced (20.0f);
    auto buttons = juce::Rectangle<float> (c.getX(), c.getBottom() - 28.0f, c.getWidth(), 28.0f);
    okButton->setBounds (buttons.removeFromRight (96.0f).toNearestInt());
    buttons.removeFromRight (8.0f);
    cancelButton->setBounds (buttons.removeFromRight (88.0f).toNearestInt());
    field.setBounds (juce::Rectangle<float> (c.getX(), c.getBottom() - 28.0f - 16.0f - 30.0f, c.getWidth(), 30.0f).toNearestInt());
}

void Dialog::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    g.fillAll (t.overlay);

    const auto c = card();
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (c.translated (0.0f, 3.0f).expanded (1.0f), 10.0f);
    g.setColour (t.surface);
    g.fillRoundedRectangle (c, 8.0f);
    g.setColour (t.line);
    g.drawRoundedRectangle (c.reduced (0.5f), 8.0f, 1.0f);

    auto inner = c.reduced (20.0f);
    g.setColour (t.text);
    g.setFont (fonts->sansSemiBold (type::wordmark));
    g.drawText (options.title, inner.removeFromTop (20.0f), juce::Justification::centredLeft, true);
    inner.removeFromTop (8.0f);

    const auto messageBottom = options.withTextField ? field.getY() - 8.0f : (float) okButton->getY() - 8.0f;
    messageLayout (inner.getWidth()).draw (g, inner.withBottom (messageBottom));

    if (error.isNotEmpty())
    {
        g.setColour (t.essText);
        g.setFont (fonts->sans (type::label));
        g.drawText (error, juce::Rectangle<float> (inner.getX(), (float) field.getBottom() + 2.0f, inner.getWidth(), 14.0f),
                    juce::Justification::centredLeft, true);
    }
}

void Dialog::mouseDown (const juce::MouseEvent& e)
{
    // A click beside the card cancels, as Escape does.
    if (! card().contains (e.position))
        close();
}

bool Dialog::keyPressed (const juce::KeyPress& key)
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

} // namespace halite::ui
