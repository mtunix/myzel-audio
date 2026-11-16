#include "ui/Widgets.h"

#include <map>

namespace halite::ui
{

namespace
{
    constexpr float fieldCorner = 5.0f, segmentCorner = 4.0f;
}

void reportHelp (juce::Component& source, const HelpText& help, bool shown, bool fromFocus)
{
    if (auto* host = source.findParentComponentOfClass<HelpHost>())
    {
        if (shown)
            host->showHelp (&source, help, fromFocus);
        else
            host->hideHelp (&source, fromFocus);
    }
}

//==============================================================================
Control::Control()
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);   // the focus ring means the keyboard is here
}

void Control::setDimmed (bool shouldDim)
{
    if (dimmed != shouldDim)
    {
        dimmed = shouldDim;
        repaint();
    }
}

void Control::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    reportHelp (*this, help, true, false);
    repaint();
}

void Control::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    reportHelp (*this, help, false, false);
    repaint();
}

void Control::focusGained (FocusChangeType)
{
    reportHelp (*this, help, true, true);
    repaint();
}

void Control::focusLost (FocusChangeType)
{
    reportHelp (*this, help, false, true);
    repaint();
}

//==============================================================================
namespace
{
    class ValueInterface final : public juce::AccessibilityValueInterface
    {
    public:
        explicit ValueInterface (ValueControl& c) : control (c), binding (c.getBinding()) {}

        bool isReadOnly() const override                        { return false; }
        double getCurrentValue() const override                 { return binding.position(); }
        juce::String getCurrentValueAsString() const override   { return binding.text(); }
        void setValue (double v) override                       { control.endBurst(); binding.setAsEdit (juce::jlimit (0.0f, 1.0f, (float) v)); }
        void setValueAsString (const juce::String& s) override  { control.endBurst(); binding.setFromText (s); }
        AccessibleValueRange getRange() const override          { return { { 0.0, 1.0 }, binding.step() }; }

    private:
        ValueControl& control;
        ValueBinding& binding;
    };
}

ValueControl::ValueControl (ValueBinding& b)
    : binding (b)
{
    setHelp (b.help());
    setTitle (b.help().title);
}

ValueControl::~ValueControl()
{
    stopTimer();
    if (inWheel || dragging)
        binding.end();
}

void ValueControl::update()
{
    // A control whose parameter follows the mode (Threshold) follows with its help.
    if (const auto h = binding.help(); h.title != help.title || h.text != help.text)
        setHelp (h);

    const auto position = binding.position();
    const auto text = binding.text();

    if (! juce::approximatelyEqual (position, shownPosition) || text != shownText)
    {
        shownPosition = position;
        shownText = text;
        repaint();
    }
}

void ValueControl::mouseDown (const juce::MouseEvent& e)
{
    if (isEntryOpen())
        finishEntry (true);

    endBurst();

    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    if (e.mods.isAltDown() || e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        openTextEntry();
        return;
    }

    pressed = true;
    dragging = false;
}

void ValueControl::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressed)
        return;

    const auto distance = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());

    // A click that wanders a pixel or two is not an edit: no gesture, no undo step.
    if (! dragging)
    {
        if (std::abs (distance) < 3.0f)
            return;

        // The edit counts from where the mouse went down, so the threshold costs no travel.
        dragging = true;
        binding.begin();
        dragStart = 0.0f;
        dragValue = binding.position();
    }

    const auto fine = e.mods.isShiftDown() ? interaction::fineFactor : 1.0f;
    dragValue = juce::jlimit (0.0f, 1.0f, dragValue + (distance - dragStart) / interaction::dragPixels * fine);
    dragStart = distance;
    binding.setPosition (dragValue);
    update();
}

void ValueControl::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        binding.end();

    pressed = dragging = false;
    repaint();
}

void ValueControl::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (doubleClickResets())
    {
        binding.setAsEdit (binding.defaultPosition());
        update();
    }
    else
    {
        openTextEntry();
    }
}

void ValueControl::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (dragging || isEntryOpen())
        return;

    auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;
    if (wheel.isReversed)
        delta = -delta;

    // A notch is one step however finely the device reports; a trackpad's small deltas add up to one.
    wheelAccumulator += delta;
    if (std::abs (wheelAccumulator) < 0.05f)
        return;

    const auto direction = wheelAccumulator > 0.0f ? 1.0f : -1.0f;
    wheelAccumulator = 0.0f;
    step (direction * binding.step() * (e.mods.isShiftDown() ? interaction::fineFactor : 1.0f));
}

void ValueControl::step (float travel)
{
    if (! inWheel)
    {
        inWheel = true;
        binding.begin();
    }

    binding.setPosition (juce::jlimit (0.0f, 1.0f, binding.stepFrom (binding.position(), travel)));
    startTimer (interaction::wheelGestureMs);
    update();
}

void ValueControl::endBurst()
{
    if (inWheel)
        timerCallback();
}

void ValueControl::timerCallback()
{
    stopTimer();

    if (inWheel)
    {
        inWheel = false;
        binding.end();
    }
}

bool ValueControl::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const auto s = binding.step() * (key.getModifiers().isShiftDown() ? interaction::fineFactor : 1.0f);

    const auto isStep = code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::downKey
                     || code == juce::KeyPress::leftKey || code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey;

    // Any other key is an edit of its own: inside the burst's gesture it would end it and drop the next steps.
    if (! isStep)
        endBurst();

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)        step (s);
    else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)  step (-s);
    else if (code == juce::KeyPress::pageUpKey)                                   step (10.0f * s);
    else if (code == juce::KeyPress::pageDownKey)                                 step (-10.0f * s);
    else if (code == juce::KeyPress::homeKey)                                     binding.setAsEdit (0.0f);
    else if (code == juce::KeyPress::endKey)                                      binding.setAsEdit (1.0f);
    else if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        binding.setAsEdit (binding.defaultPosition());
    else if (code == juce::KeyPress::returnKey)
        openTextEntry();
    else if (const auto c = key.getTextCharacter(); juce::CharacterFunctions::isDigit (c) || c == '-' || c == '+' || c == '.')
        openTextEntry (juce::String::charToString (c));
    else
        return false;

    update();
    return true;
}

void ValueControl::openTextEntry (const juce::String& initial)
{
    endBurst();
    const auto& t = theme::current();

    if (entry == nullptr)
    {
        entry = std::make_unique<juce::TextEditor> ("value");
        entry->setIndents (entryJustification() == juce::Justification::centred ? 0 : 6, 0);
        entry->setBorder ({ 1, 1, 1, 1 });
        entry->setJustification (entryJustification());
        entry->onReturnKey = [this] { finishEntry (true); };
        entry->onEscapeKey = [this] { finishEntry (false); };
        entry->onFocusLost = [this] { finishEntry (true); };
        addChildComponent (*entry);
    }

    auto& f = *entry;
    f.setColour (juce::TextEditor::backgroundColourId, t.raised);
    f.setColour (juce::TextEditor::textColourId, t.text);
    f.setColour (juce::TextEditor::highlightColourId, t.essFill.withAlpha (0.35f));
    f.setColour (juce::TextEditor::highlightedTextColourId, t.text);
    f.setColour (juce::TextEditor::outlineColourId, t.focus);
    f.setColour (juce::TextEditor::focusedOutlineColourId, t.focus);
    f.setColour (juce::CaretComponent::caretColourId, t.focus);
    f.setFont (fonts->mono (type::control));
    f.setTitle (help.title + " value");
    f.setText (initial.isEmpty() ? binding.text() : initial, false);
    f.setBounds (entryArea());
    f.setVisible (true);
    f.toFront (false);

    if (initial.isEmpty())
        f.selectAll();
    else
        f.moveCaretToEnd();

    if (isShowing())
        f.grabKeyboardFocus();
}

void ValueControl::finishEntry (bool commit)
{
    if (! isEntryOpen())
        return;

    const auto typed = entry->getText();
    const auto hadFocus = entry->hasKeyboardFocus (false);
    entry->setVisible (false);

    if (commit)
        binding.setFromText (typed);

    if (hadFocus && isShowing())
        grabKeyboardFocus();

    update();
}

void ValueControl::commitEntryForTest (const juce::String& text)
{
    if (isEntryOpen())
    {
        entry->setText (text, false);
        finishEntry (true);
    }
}

void ValueControl::showMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (help.title + "  " + binding.text());
    menu.addItem (1, "Type a value...");
    menu.addItem (2, "Reset to default");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<ValueControl> (this)] (int result)
                        {
                            if (safe == nullptr)
                                return;
                            if (result == 1)
                                safe->openTextEntry();
                            else if (result == 2)
                                safe->binding.setAsEdit (safe->binding.defaultPosition());
                        });
}

void ValueControl::resized()
{
    if (entry != nullptr)
        entry->setBounds (entryArea());
}

std::unique_ptr<juce::AccessibilityHandler> ValueControl::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<ValueInterface> (*this) });
}

//==============================================================================
RingKnob::RingKnob (ValueBinding& b, const juce::String& text)
    : ValueControl (b), label (text)
{
    setTitle (text);
}

void RingKnob::setDiameter (float d, bool compactRail)
{
    if (juce::approximatelyEqual (d, diameter) && compactRail == compact)
        return;

    diameter = d;
    compact = compactRail;
    resized();
    repaint();
}

float RingKnob::readoutTop (float d, bool compactRail)
{
    // Under the disc, not under the ring's box: the arc's opening leaves the box's bottom to the disc alone.
    return d - discInset + (compactRail ? 2.0f : 3.0f);
}

int RingKnob::heightFor (float d, bool compactRail)
{
    return (int) std::ceil (readoutTop (d, compactRail) + readoutHeight (compactRail) + labelHeight);
}

juce::Rectangle<float> RingKnob::ringArea() const
{
    return { ((float) getWidth() - diameter) * 0.5f, 0.0f, diameter, diameter };
}

juce::Rectangle<float> RingKnob::readoutArea() const
{
    return { 0.0f, readoutTop (diameter, compact), (float) getWidth(), readoutHeight (compact) };
}

juce::Rectangle<int> RingKnob::entryArea() const
{
    const auto readout = readoutArea();
    const auto w = std::min ((float) getWidth(), std::max (60.0f, diameter));
    // Two px above the read-out's line and none below, so the box clears the label.
    return juce::Rectangle<float> (w, readout.getHeight() + 2.0f).withCentre (readout.getCentre().translated (0.0f, -1.0f)).toNearestInt();
}

void RingKnob::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto ring = ringArea();
    const auto centre = ring.getCentre();
    const auto arcRadius = diameter * 0.5f - 2.0f;
    const auto discRadius = diameter * 0.5f - discInset;
    constexpr auto start = -0.75f * juce::MathConstants<float>::pi, end = 0.75f * juce::MathConstants<float>::pi;
    const auto position = juce::jlimit (0.0f, 1.0f, binding.position());
    const auto angle = start + position * (end - start);
    const auto active = hovering || isDragging();
    const auto ink = accent != nullptr ? accent() : t.essText;
    const juce::PathStrokeType arcStroke (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, end, true);
    g.setColour (active ? t.zeroLine : t.line);
    g.strokePath (track, arcStroke);

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, start + std::max (0.0075f, position) * (end - start), true);
    g.setColour (dimmed ? t.faint : ink);
    g.strokePath (value, arcStroke);

    const auto disc = juce::Rectangle<float> (discRadius * 2.0f, discRadius * 2.0f).withCentre (centre);
    g.setColour (t.raised);
    g.fillEllipse (disc);
    g.setColour (isDragging() && ! dimmed ? ink : t.zeroLine);
    g.drawEllipse (disc.reduced (0.5f), 1.0f);

    const auto along = juce::Point<float> (std::sin (angle), -std::cos (angle));
    g.setColour (dimmed ? t.secondary : t.text);
    g.drawLine ({ centre + along * (discRadius * 0.3f), centre + along * (discRadius - 3.0f) }, 2.0f);

    if (! isEntryOpen())
    {
        // The large knob reads short, in the band's ink (mock N); the small ones keep their unit where it fits.
        const auto readout = readoutArea();
        g.setFont (fonts->mono (isLarge() ? (compact ? type::knobLarge - 1.5f : type::knobLarge)
                                          : (compact ? type::knobSmall - 1.0f : type::knobSmall)));
        auto text = isLarge() ? binding.shortText() : binding.text();
        if (textWidth (g.getCurrentFont(), text) > readout.getWidth() - 2.0f)
            text = binding.shortText();

        g.setColour (dimmed ? t.secondary : isLarge() ? ink : t.text);
        g.drawText (fitted (text, g.getCurrentFont(), readout.getWidth() - 2.0f), readout, juce::Justification::centred, false);
    }

    g.setColour (active ? t.text : t.secondary);
    g.setFont (fonts->sans (type::label));
    g.drawText (label, juce::Rectangle<float> (0.0f, readoutArea().getBottom(), (float) getWidth(), labelHeight),
                juce::Justification::centredTop, false);

    if (hasKeyboardFocus (false))
        draw::focusRing (g, ring.reduced (1.0f), diameter * 0.5f);
}

//==============================================================================
ValueField::ValueField (ValueBinding& b, const juce::String& text)
    : ValueControl (b), label (text)
{
    setTitle (text);
}

void ValueField::setWidths (float labelW, float boxW)
{
    labelWidth = labelW;
    boxWidth = boxW;
    resized();
    repaint();
}

juce::Rectangle<float> ValueField::boxArea() const
{
    return { labelWidth + gap, ((float) getHeight() - height) * 0.5f, boxWidth, height };
}

void ValueField::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto box = boxArea();

    // A dimmed field is still editable: its label keeps 4.5:1, and the value read-out shows the dimming.
    g.setColour (hovering ? t.text : t.secondary);
    g.setFont (fonts->sans (type::label));
    g.drawText (fitted (label, g.getCurrentFont(), labelWidth), juce::Rectangle<float> (0.0f, 0.0f, labelWidth, (float) getHeight()),
                juce::Justification::centredLeft, false);

    g.setColour (t.raised);
    g.fillRoundedRectangle (box, fieldCorner);
    g.setColour (isDragging() ? t.focus : (hovering ? t.zeroLine : t.line));
    g.drawRoundedRectangle (box.reduced (0.5f), fieldCorner, 1.0f);

    if (! isEntryOpen())
    {
        g.setColour (dimmed ? t.secondary : t.text);
        g.setFont (fonts->mono (type::control));
        g.drawText (fitted (binding.text(), g.getCurrentFont(), box.getWidth() - 14.0f), box.reduced (7.0f, 0.0f),
                    juce::Justification::centredRight, false);
    }

    if (hasKeyboardFocus (false))
        draw::focusRing (g, box, fieldCorner);
}

//==============================================================================
HelpedButton::HelpedButton (const juce::String& name)
    : juce::Button (name)
{
    setTitle (name);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
}

void HelpedButton::mouseEnter (const juce::MouseEvent& e)
{
    juce::Button::mouseEnter (e);
    reportHelp (*this, help, true, false);
}

void HelpedButton::mouseExit (const juce::MouseEvent& e)
{
    juce::Button::mouseExit (e);
    reportHelp (*this, help, false, false);
}

void HelpedButton::focusGained (FocusChangeType cause)
{
    juce::Button::focusGained (cause);
    reportHelp (*this, help, true, true);
    repaint();
}

void HelpedButton::focusLost (FocusChangeType cause)
{
    juce::Button::focusLost (cause);
    reportHelp (*this, help, false, true);
    repaint();
}

//==============================================================================
class Segmented::Segment final : public HelpedButton
{
public:
    Segment (Segmented& o, int i, const juce::String& itemText)
        : HelpedButton (itemText), owner (o), index (i)
    {
        setRadioGroupId (1);
        onClick = [this] { if (owner.setIndex != nullptr) owner.setIndex (index); };
    }

    // A right-click opens the owner's menu and chooses nothing (a button would take it as a click).
    bool opensMenu (const juce::MouseEvent& e) const { return e.mods.isPopupMenu() && owner.onPopupMenu != nullptr; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (opensMenu (e))
            owner.onPopupMenu();
        else
            HelpedButton::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! opensMenu (e))
            HelpedButton::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! opensMenu (e))
            HelpedButton::mouseUp (e);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto& t = theme::current();
        const auto area = getLocalBounds().toFloat();
        const auto chosen = getToggleState();
        const auto faint = owner.isItemDimmed != nullptr && owner.isItemDimmed (index);

        if (chosen)
        {
            g.setColour (t.trackActive);
            g.fillRoundedRectangle (area, segmentCorner);
        }

        // Chosen but not running (Spectral in Live): the well shows the choice, the text that it waits.
        g.setColour (chosen ? (faint ? t.secondary : t.text) : (faint ? t.faint : (highlighted ? t.text : t.secondary)));
        g.setFont (owner.useMono ? fonts->mono (type::label) : fonts->sans (type::control));
        g.drawText (getButtonText(), area, juce::Justification::centred, false);

        if (hasKeyboardFocus (false))
            draw::focusRing (g, area, segmentCorner);
    }

private:
    Segmented& owner;
    int index;
};

Segmented::Segmented (const juce::String& title, const juce::StringArray& items)
{
    setTitle (title);
    setWantsKeyboardFocus (false);
    setFocusContainerType (FocusContainerType::focusContainer);

    for (int i = 0; i < items.size(); ++i)
    {
        segments.push_back (std::make_unique<Segment> (*this, i, items[i]));
        segments.back()->setTitle (title + ": " + items[i]);
        addAndMakeVisible (*segments.back());
    }
}

Segmented::~Segmented() = default;

void Segmented::setHelp (HelpText h)
{
    Control::setHelp (h);
    for (auto& s : segments)
        s->setHelp (h);
}

juce::Button& Segmented::segment (int i)
{
    return *segments[(size_t) i];
}

void Segmented::setLabel (const juce::String& text, float width, bool above)
{
    label = text;
    labelSpace = width;
    labelAbove = above;
    resized();
    repaint();
}

float Segmented::segmentWidth (int i) const
{
    const auto font = useMono ? fonts->mono (type::label) : fonts->sans (type::control);
    return std::ceil (textWidth (font, segments[(size_t) i]->getButtonText())) + (compact ? 14.0f : 18.0f);
}

float Segmented::wellWidth() const
{
    auto w = 4.0f;
    for (int i = 0; i < numSegments(); ++i)
        w += segmentWidth (i);
    return w;
}

juce::Rectangle<float> Segmented::wellArea() const
{
    auto area = getLocalBounds().toFloat();

    if (label.isNotEmpty() && labelAbove)
        area.removeFromTop (16.0f);
    else if (label.isNotEmpty())
        area.removeFromLeft (labelSpace);

    return area.withWidth (wellWidth()).withSizeKeepingCentre (wellWidth(), wellHeight).withX (area.getX());
}

void Segmented::resized()
{
    const auto well = wellArea();
    auto x = well.getX() + 2.0f;

    for (int i = 0; i < numSegments(); ++i)
    {
        const auto w = segmentWidth (i);
        segments[(size_t) i]->setBounds (juce::Rectangle<float> (x, well.getY() + 2.0f, w, well.getHeight() - 4.0f).toNearestInt());
        x += w;
    }
}

void Segmented::update()
{
    const auto index = getIndex != nullptr ? getIndex() : -1;

    std::uint32_t dimmedMask = 0;
    for (int i = 0; i < numSegments() && i < 32; ++i)
        if (isItemDimmed != nullptr && isItemDimmed (i))
            dimmedMask |= 1u << i;

    // Called every frame: repaint only on a change, or a real window repaints every segment at the frame rate.
    if (index == shown && dimmedMask == shownDimmed)
        return;

    shown = index;
    shownDimmed = dimmedMask;
    for (int i = 0; i < numSegments(); ++i)
    {
        segments[(size_t) i]->setToggleState (i == index, juce::dontSendNotification);
        segments[(size_t) i]->repaint();
    }
}

void Segmented::paint (juce::Graphics& g)
{
    const auto& t = theme::current();

    if (label.isNotEmpty())
    {
        g.setColour (t.secondary);
        g.setFont (fonts->sans (type::label));
        const auto labelArea = labelAbove ? getLocalBounds().toFloat().withHeight (14.0f)
                                          : getLocalBounds().toFloat().withWidth (labelSpace);
        g.drawText (label, labelArea, juce::Justification::centredLeft, false);
    }

    // No outline: the well is a step off its rail in both themes, the chosen segment a step off the well.
    g.setColour (t.track);
    g.fillRoundedRectangle (wellArea(), draw::buttonCorner);
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onPopupMenu != nullptr)
        onPopupMenu();
}

bool Segmented::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const auto delta = code == juce::KeyPress::rightKey ? 1 : code == juce::KeyPress::leftKey ? -1 : 0;

    if (delta == 0 || getIndex == nullptr || setIndex == nullptr)
        return false;

    const auto next = juce::jlimit (0, numSegments() - 1, getIndex() + delta);
    setIndex (next);
    segments[(size_t) next]->grabKeyboardFocus();
    update();
    return true;
}

//==============================================================================
namespace draw
{
    namespace
    {
        struct IconPath
        {
            juce::Path path;
            float box;
            bool closedFill = false;
        };

        const IconPath& iconPath (Icon icon)
        {
            static const std::map<Icon, IconPath> paths = []
            {
                std::map<Icon, IconPath> m;
                auto svg = [] (const char* d) { return juce::Drawable::parseSVGPath (d); };

                m[Icon::undo] = { svg ("M5 3 2 6l3 3 M2 6h6.5a3.5 3.5 0 0 1 0 7H6"), 14.0f };
                m[Icon::redo] = { svg ("M9 3l3 3-3 3 M12 6H5.5a3.5 3.5 0 0 0 0 7H8"), 14.0f };

                juce::Path gear = svg ("M7 1v2M7 11v2M1 7h2M11 7h2M2.8 2.8l1.4 1.4M9.8 9.8l1.4 1.4M2.8 11.2l1.4-1.4M9.8 4.2l1.4-1.4");
                gear.addEllipse (5.0f, 5.0f, 4.0f, 4.0f);
                m[Icon::gear] = { gear, 14.0f };

                m[Icon::freeze] = { svg ("M3 2v6M7 2v6"), 10.0f };
                m[Icon::play] = { svg ("M3 1.8v6.4L8.2 5Z"), 10.0f };

                juce::Path sidebar;
                sidebar.addRoundedRectangle (1.5f, 2.5f, 11.0f, 9.0f, 1.5f);
                sidebar.startNewSubPath (8.5f, 2.5f);
                sidebar.lineTo (8.5f, 11.5f);
                m[Icon::sidebar] = { sidebar, 14.0f };

                m[Icon::chevronLeft] = { svg ("M8.5 3.5 5 7l3.5 3.5"), 14.0f };
                m[Icon::chevronRight] = { svg ("M5.5 3.5 9 7l-3.5 3.5"), 14.0f };
                m[Icon::caretDown] = { svg ("M2 3.5 5 6.5 8 3.5"), 10.0f };
                m[Icon::caretUp] = { svg ("M2 6.5 5 3.5 8 6.5"), 10.0f };

                juce::Path help;
                help.addEllipse (1.5f, 1.5f, 11.0f, 11.0f);
                help.addPath (svg ("M5.3 5.4a1.8 1.8 0 1 1 2.5 1.6c-.5.3-.8.6-.8 1.2v.4 M7 10.2v.3"));
                m[Icon::help] = { help, 14.0f };
                return m;
            }();

            static const IconPath none {};
            const auto it = paths.find (icon);
            return it != paths.end() ? it->second : none;
        }
    }

    void icon (juce::Graphics& g, Icon which, juce::Rectangle<float> area, juce::Colour ink, float stroke)
    {
        const auto& ip = iconPath (which);
        if (ip.path.isEmpty())
            return;

        const auto side = std::min (area.getWidth(), area.getHeight());
        const auto scale = side / ip.box;
        const auto box = area.withSizeKeepingCentre (side, side);
        const auto transform = juce::AffineTransform::scale (scale).translated (box.getX(), box.getY());

        g.setColour (ink);
        g.strokePath (ip.path, juce::PathStrokeType (stroke * std::max (1.0f, scale * 0.85f), juce::PathStrokeType::mitered,
                                                     juce::PathStrokeType::rounded),
                      transform);
    }

    void focusRing (juce::Graphics& g, juce::Rectangle<float> area, float corner)
    {
        g.setColour (theme::current().focus);
        g.drawRoundedRectangle (area.expanded (2.0f), corner + 2.0f, 1.5f);
    }

    juce::Colour onFill (const Tokens& t)
    {
        // Light's chosen segment is the raised white itself, so an "on" button sinks to the well's tone there.
        return t.trackActive != t.raised ? t.trackActive : t.track;
    }

    juce::Colour buttonFill (const Tokens& t, bool on, bool down)
    {
        return down ? t.hover : on ? onFill (t) : t.raised;
    }
}

//==============================================================================
ToolButton::ToolButton (const juce::String& name, Icon i, const juce::String& t)
    : HelpedButton (name), icon (i), text (t)
{
}

float ToolButton::idealWidth() const
{
    if (text.isEmpty())
        return 28.0f;

    const auto font = mono ? fonts->mono (type::label) : fonts->sans (type::button);
    return std::ceil (textWidth (font, text)) + (icon != Icon::none ? 40.0f : 24.0f);
}

void ToolButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = theme::current();
    const auto area = getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = draw::buttonCorner;
    const auto engagedCaution = caution && on;
    const auto engagedLatch = latched && on;

    if (engagedCaution)
        g.setColour (t.surface.interpolatedWith (t.caution, 0.14f));
    else if (engagedLatch)
        g.setColour (t.selectedTint);
    else if (borderless)
        g.setColour (down ? t.hover : juce::Colours::transparentBlack);
    else
        g.setColour (draw::buttonFill (t, on, down));
    g.fillRoundedRectangle (area, corner);

    if (! borderless)
    {
        g.setColour (engagedCaution ? t.caution : engagedLatch ? t.essText : (highlighted && isEnabled()) ? t.zeroLine : t.line);
        g.drawRoundedRectangle (area, corner, 1.0f);
    }

    const auto ink = ! isEnabled() ? t.faint
                   : engagedCaution ? t.caution
                   : engagedLatch ? t.essText
                   : (on || highlighted) ? t.text
                   : t.secondary;

    if (text.isEmpty())
    {
        draw::icon (g, engagedLatch ? onIcon : icon, area.withSizeKeepingCentre (14.0f, 14.0f), ink);
    }
    else
    {
        auto content = area.reduced (8.0f, 0.0f);
        if (icon != Icon::none)
            draw::icon (g, icon, content.removeFromLeft (14.0f).withSizeKeepingCentre (12.0f, 12.0f), ink);

        g.setColour (ink);
        g.setFont (mono ? fonts->mono (type::label) : fonts->sans (type::button));
        g.drawText (text, content, juce::Justification::centred, false);
    }

    if (hasKeyboardFocus (false))
        draw::focusRing (g, area, corner);
}

//==============================================================================
MenuButton::MenuButton (const juce::String& name)
    : HelpedButton (name)
{
}

float MenuButton::idealWidth() const
{
    const auto font = mono ? fonts->mono (small ? type::label : type::control) : fonts->sans (type::button);
    return std::ceil (textWidth (font, text)) + (small ? 30.0f : 36.0f);
}

void MenuButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = theme::current();
    const auto area = getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = draw::buttonCorner;

    g.setColour (draw::buttonFill (t, false, down));
    g.fillRoundedRectangle (area, corner);
    g.setColour (highlighted ? t.zeroLine : t.line);
    g.drawRoundedRectangle (area, corner, 1.0f);

    const auto ink = accented ? t.essText : (small && ! highlighted ? t.secondary : t.text);
    auto content = area.reduced (small ? 8.0f : 10.0f, 0.0f);
    draw::icon (g, Icon::caretDown, content.removeFromRight (10.0f).withSizeKeepingCentre (10.0f, 10.0f), ink);
    content.removeFromRight (4.0f);

    g.setColour (ink);
    g.setFont (mono ? fonts->mono (small ? type::label : type::control) : fonts->sans (type::button));
    g.drawText (fitted (text, g.getCurrentFont(), content.getWidth()), content,
                small ? juce::Justification::centredRight : juce::Justification::centred, false);

    if (hasKeyboardFocus (false))
        draw::focusRing (g, area, corner);
}

//==============================================================================
FilledButton::FilledButton (const juce::String& name)
    : HelpedButton (name), text (name)
{
}

void FilledButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = theme::current();
    const auto area = getLocalBounds().toFloat().reduced (0.5f);
    auto fill = t.essFill;

    if (down)
        fill = fill.darker (0.12f);
    else if (highlighted)
        fill = fill.brighter (0.06f);

    g.setFont (fonts->sansSemiBold (type::button));

    if (progress >= 0.0f)
    {
        // Listening: the part still to come is the fill at 45 %, where the label reads in text
        // (onAccent on it reads 3.09:1 in Dark, under 4.5:1); the part done is the fill with the label in onAccent.
        const auto done = area.withWidth (area.getWidth() * juce::jlimit (0.0f, 1.0f, progress)).toNearestInt();
        g.setColour (fill.withMultipliedAlpha (theme::learnRemainderAlpha));
        g.fillRoundedRectangle (area, draw::buttonCorner);
        g.setColour (t.text);
        g.drawText (text, area, juce::Justification::centred, false);

        g.saveState();
        g.reduceClipRegion (done);
        g.setColour (fill);
        g.fillRoundedRectangle (area, draw::buttonCorner);
        g.setColour (t.onAccent);
        g.drawText (text, area, juce::Justification::centred, false);
        g.restoreState();
    }
    else
    {
        g.setColour (fill);
        g.fillRoundedRectangle (area, draw::buttonCorner);
        g.setColour (t.onAccent);
        g.drawText (text, area, juce::Justification::centred, false);
    }

    if (hasKeyboardFocus (false))
        draw::focusRing (g, area, draw::buttonCorner);
}

//==============================================================================
Switch::Switch (const juce::String& name)
    : HelpedButton (name)
{
    setClickingTogglesState (false);
}

void Switch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto& t = theme::current();
    const auto track = getLocalBounds().toFloat().withSizeKeepingCentre (18.0f, 10.0f);
    const auto on = getToggleState();

    if (on)
    {
        g.setColour (accent != nullptr ? accent() : t.essText);
        g.fillRoundedRectangle (track, 5.0f);
    }
    else
    {
        g.setColour (t.track);
        g.fillRoundedRectangle (track, 5.0f);
        g.setColour (highlighted ? t.zeroLine : t.line);
        g.drawRoundedRectangle (track.reduced (0.5f), 4.5f, 1.0f);
    }

    const auto knob = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ on ? track.getRight() - 5.0f : track.getX() + 5.0f, track.getCentreY() });
    g.setColour (on ? t.surface : (highlighted ? t.text : t.secondary));
    g.fillEllipse (knob);

    if (hasKeyboardFocus (false))
        draw::focusRing (g, track, 5.0f);
}

//==============================================================================
BandPill::BandPill (const juce::String& name, bool withSwitch)
    : HelpedButton (name)
{
    if (withSwitch)
    {
        toggle = std::make_unique<Switch> (name + " on");
        addAndMakeVisible (*toggle);
    }
}

void BandPill::resized()
{
    if (toggle != nullptr)
        toggle->setBounds (getLocalBounds().removeFromRight (34).withSizeKeepingCentre (28, 20));
}

void BandPill::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto& t = theme::current();
    const auto area = getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = area.getHeight() * 0.5f;
    const auto bandInk = ink != nullptr ? ink() : t.text;

    g.setColour (selected ? t.selectedTint : t.raised);
    g.fillRoundedRectangle (area, corner);
    g.setColour (selected ? bandInk : (highlighted ? t.zeroLine : t.line));
    g.drawRoundedRectangle (area, corner, 1.0f);

    // The band's ink, not its fill: Light's fills read under 3:1 on the pill (N7).
    auto content = area.reduced (12.0f, 0.0f);
    const auto dot = content.removeFromLeft (7.0f).withSizeKeepingCentre (7.0f, 7.0f);
    g.setColour (bandInk);
    if (selected)
        g.fillEllipse (dot);
    else
        g.drawEllipse (dot.reduced (0.5f), 1.0f);
    content.removeFromLeft (7.0f);

    g.setColour (selected ? bandInk : (highlighted ? t.text : t.secondary));
    g.setFont (selected ? fonts->sansSemiBold (type::button) : fonts->sans (type::button));
    g.drawText (getButtonText(), content, juce::Justification::centredLeft, false);

    if (hasKeyboardFocus (false))
        draw::focusRing (g, area, corner);
}

} // namespace halite::ui
