#pragma once

#include "ui/Help.h"
#include "ui/Theme.h"

namespace halite::ui
{

/**
    What a knob or a typed field edits, as travel 0 … 1 (Bindings.h binds parameters). A drag or a
    burst of wheel steps is one begin() … end(); everything else is one edit of its own.
*/
class ValueBinding
{
public:
    virtual ~ValueBinding() = default;

    virtual HelpText help() const = 0;
    virtual float position() const = 0;
    virtual float defaultPosition() const = 0;
    virtual void begin() = 0;
    virtual void setPosition (float travel) = 0;    // between begin() and end()
    virtual void end() = 0;

    /** The read-out with its unit ("−17.0 dB"), and the knob's short form ("−17.0"). */
    virtual juce::String text() const = 0;
    virtual juce::String shortText() const { return text(); }

    /** Typed entry, one edit; text the parameter does not understand changes nothing. */
    virtual void setFromText (const juce::String&) = 0;

    /** One wheel notch or arrow key, in travel; Shift takes a fifth of it. */
    virtual float step() const = 0;

    /** Where a step of `travel` from `position` lands (a binding with a detent, as Speed's Auto, moves past it). */
    virtual float stepFrom (float position, float travel) const { return position + travel; }

    /** The parameters it edits (the self-test checks every one has a control). */
    virtual juce::StringArray parameterIds() const = 0;

    void setAsEdit (float travel) { begin(); setPosition (travel); end(); }
};

/** The rules every workbench control shares (src/ui/DESIGN.md, Interaction). */
namespace interaction
{
    inline constexpr float dragPixels = 240.0f;     // the whole travel
    inline constexpr float fineFactor = 0.2f;       // Shift: drag, wheel and arrow keys
    inline constexpr int wheelGestureMs = 400;      // a burst of wheel steps is one gesture and one undo step
}

/** The editor learns what is under the pointer or in focus (HelpHost), for the status line. */
void reportHelp (juce::Component& source, const HelpText&, bool shown, bool fromFocus);

/**
    A workbench control: help for the status line, a dimmed state (a control that does nothing in
    the current mode stays editable, drawn quieter), and a per-frame update that repaints only when
    what it shows changed.
*/
class Control : public juce::Component
{
public:
    Control();

    void setHelp (HelpText h) { help = std::move (h); setDescription (help.text); }
    const HelpText& getHelp() const noexcept { return help; }

    void setDimmed (bool shouldDim);
    bool isDimmed() const noexcept { return dimmed; }

    virtual void update() {}

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

protected:
    juce::SharedResourcePointer<Fonts> fonts;
    HelpText help;
    bool dimmed = false, hovering = false;
};

/**
    A control over a ValueBinding: vertical drag (Shift fine), wheel, arrow keys (Shift fine, Page Up and
    Down ten steps, Home and End the ends), Delete or Backspace to the default, Return or a digit to type,
    Alt, Ctrl or Cmd-click to type, right-click for a menu (type a value, reset). Knobs reset on a
    double-click, fields open typed entry on one (PLAN.md, Behaviour).
*/
class ValueControl : public Control, private juce::Timer
{
public:
    explicit ValueControl (ValueBinding&);
    ~ValueControl() override;

    ValueBinding& getBinding() noexcept { return binding; }

    void update() override;

    void openTextEntry (const juce::String& initial = {});
    bool isEntryOpen() const noexcept { return entry != nullptr && entry->isVisible(); }

    /** Closes an open burst of wheel or arrow steps, so an edit of another kind is a gesture of its own. */
    void endBurst();

    /** Types the text into the open entry and presses Return (the self-test). */
    void commitEntryForTest (const juce::String& text);

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void resized() override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

protected:
    virtual juce::Rectangle<int> entryArea() const = 0;
    virtual juce::Justification entryJustification() const { return juce::Justification::centredRight; }
    virtual bool doubleClickResets() const = 0;

    bool isDragging() const noexcept { return dragging; }

    ValueBinding& binding;
    float shownPosition = -1.0f;
    juce::String shownText;

private:
    void timerCallback() override;
    void step (float travel);
    void finishEntry (bool commit);
    void showMenu();

    float dragStart = 0.0f, dragValue = 0.0f;   // the drag's own travel: a detent's read-back must not hold it
    bool pressed = false, dragging = false, inWheel = false;
    float wheelAccumulator = 0.0f;
    std::unique_ptr<juce::TextEditor> entry;
};

/**
    The disc knob (PLAN.md, UI restyle: Navy, N5; the class keeps its name): a thin track and value arc
    around a raised disc with a notch, the value read out under the disc, the label under that.
*/
class RingKnob final : public ValueControl
{
public:
    RingKnob (ValueBinding&, const juce::String& label);

    void setLabel (const juce::String& newLabel) { label = newLabel; setTitle (newLabel); repaint(); }
    void setDiameter (float d, bool compactRail);
    float getDiameter() const noexcept { return diameter; }
    bool isLarge() const noexcept { return diameter >= 60.0f; }

    /** The band's ink for the value arc (and the large knob's read-out). */
    std::function<juce::Colour()> accent;

    juce::Rectangle<float> ringArea() const;
    juce::Rectangle<float> readoutArea() const;

    /** The knob's height: the ring, the read-out's line and the label (the bar centres the discs on one line). */
    static int heightFor (float diameter, bool compactRail);

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<int> entryArea() const override;
    juce::Justification entryJustification() const override { return juce::Justification::centred; }
    bool doubleClickResets() const override { return true; }

    static float readoutHeight (bool compactRail) { return compactRail ? 14.0f : 16.0f; }
    static float readoutTop (float diameter, bool compactRail);
    static constexpr float labelHeight = 13.0f;
    static constexpr float discInset = 8.0f;    // the disc's radius under the ring's: room for the arc and a gap

    juce::String label;
    float diameter = 56.0f;
    bool compact = false;
};

/** A typed field: label on the left, the value in a box (Detect Low/High, Freq/Width, the Advanced row). */
class ValueField final : public ValueControl
{
public:
    ValueField (ValueBinding&, const juce::String& label);

    void setLabel (const juce::String& newLabel) { label = newLabel; setTitle (newLabel); repaint(); }
    void setWidths (float labelWidth, float boxWidth);
    float idealWidth() const noexcept { return labelWidth + gap + boxWidth; }
    juce::Rectangle<float> boxArea() const;

    static constexpr float height = 22.0f, gap = 8.0f;

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<int> entryArea() const override { return boxArea().toNearestInt(); }
    bool doubleClickResets() const override { return false; }

    juce::String label;
    float labelWidth = 40.0f, boxWidth = 64.0f;
};

/** A button that reports its help and draws the workbench's focus ring. */
class HelpedButton : public juce::Button
{
public:
    explicit HelpedButton (const juce::String& name);

    void setHelp (HelpText h) { help = std::move (h); setDescription (help.text); }
    const HelpText& getHelp() const noexcept { return help; }

    virtual void update() {}

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

protected:
    juce::SharedResourcePointer<Fonts> fonts;
    HelpText help;
};

/**
    Segmented choice: a well with one segment chosen (Shape, Detection, Stereo, Key, the view tabs, A/B).
    Each segment is a button in the accessibility tree; Left and Right move the choice.
*/
class Segmented final : public Control
{
public:
    Segmented (const juce::String& title, const juce::StringArray& items);
    ~Segmented() override;

    std::function<int()> getIndex;
    std::function<void (int)> setIndex;
    std::function<bool (int)> isItemDimmed;     // drawn faint, still selectable (Spectral in Live)
    std::function<void()> onPopupMenu;          // a right-click anywhere on it, in place of choosing (A/B's copy)

    /** The group's help, also each segment's: the status line explains a segment under the pointer or in focus. */
    void setHelp (HelpText h);

    /** An optional label beside (or above) the well, as the control bar draws it. */
    void setLabel (const juce::String& text, float width, bool above);
    float labelWidth() const noexcept { return label.isEmpty() || labelAbove ? 0.0f : labelSpace; }

    /** Width of the well alone. */
    float wellWidth() const;
    float idealWidth() const { return labelWidth() + wellWidth(); }
    float idealHeight() const { return label.isNotEmpty() && labelAbove ? 16.0f + wellHeight : wellHeight; }

    void setCompact (bool c) { compact = c; resized(); }
    void setSegmentFont (bool mono) { useMono = mono; }

    void update() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    juce::Button& segment (int i);
    int numSegments() const noexcept { return (int) segments.size(); }

    static constexpr float wellHeight = 26.0f;

private:
    class Segment;
    juce::Rectangle<float> wellArea() const;
    float segmentWidth (int i) const;

    juce::String label;
    float labelSpace = 0.0f;
    bool labelAbove = false, compact = false, useMono = false;
    int shown = -1;
    std::uint32_t shownDimmed = 0;
    std::vector<std::unique_ptr<Segment>> segments;
};

/** The workbench's vector icons (paths after mock E's SVGs). */
enum class Icon
{
    none,
    undo,
    redo,
    gear,
    freeze,
    play,
    sidebar,
    chevronLeft,
    chevronRight,
    caretDown,
    caretUp,
    help
};

namespace draw
{
    void icon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour ink, float stroke = 1.3f);
    void focusRing (juce::Graphics&, juce::Rectangle<float> area, float corner);

    /** A bordered button's fill: raised, `hover` while pressed, and the "on" fill (onFill) while on. */
    juce::Colour buttonFill (const Tokens&, bool on, bool down);
    juce::Colour onFill (const Tokens&);

    inline constexpr float buttonCorner = 6.0f;
}

/**
    A small bordered button: an icon or a word (undo, redo, ⚙, freeze, the sidebar toggle, Bypass).
    "On" fills it (sidebar shown); caution colours it (Bypass engaged); a latched one takes the accent
    and its own icon while on (freeze held: the display no longer shows now).
*/
class ToolButton final : public HelpedButton
{
public:
    ToolButton (const juce::String& name, Icon, const juce::String& text = {});

    void setOn (bool shouldBeOn) { if (on != shouldBeOn) { on = shouldBeOn; repaint(); } }
    bool isOn() const noexcept { return on; }
    void setCaution (bool usesCaution) { caution = usesCaution; }
    void setLatched (Icon iconWhenOn) { latched = true; onIcon = iconWhenOn; }
    void setBorderless (bool b) { borderless = b; }
    void setMonoText (bool m) { mono = m; }
    void setText (const juce::String& t) { if (text != t) { text = t; repaint(); } }
    float idealWidth() const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon, onIcon = Icon::none;
    juce::String text;
    bool on = false, caution = false, borderless = false, mono = false, latched = false;
};

/** A button that opens a menu: text and a caret (Listen, zoom). The owner builds and shows the menu in onClick. */
class MenuButton final : public HelpedButton
{
public:
    explicit MenuButton (const juce::String& name);

    void setText (const juce::String& t) { if (text != t) { text = t; repaint(); } }
    const juce::String& getText() const noexcept { return text; }
    void setMonoText (bool m) { mono = m; }
    void setSmall (bool s) { small = s; }

    /** Text in the accent: a setting that changes what you hear from the usual (Listen off Output). */
    void setAccented (bool a) { if (accented != a) { accented = a; repaint(); } }
    float idealWidth() const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String text;
    bool mono = false, small = false, accented = false;
};

/** The control bar's one filled button (Learn), with a progress fill while it works. */
class FilledButton final : public HelpedButton
{
public:
    explicit FilledButton (const juce::String& name);

    void setText (const juce::String& t) { if (text != t) { text = t; repaint(); } }
    void setProgress (float p) { if (! juce::approximatelyEqual (p, progress)) { progress = p; repaint(); } }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String text;
    float progress = -1.0f;     // < 0: not working
};

/** An on/off switch (Harsh's, inside its pill). */
class Switch final : public HelpedButton
{
public:
    explicit Switch (const juce::String& name);

    std::function<juce::Colour()> accent;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/** A band pill: the band's dot (hollow, filled when selected), the name; Harsh's carries its Switch. */
class BandPill final : public HelpedButton
{
public:
    BandPill (const juce::String& name, bool withSwitch);

    std::function<juce::Colour()> ink;          // the band's line and text colour
    void setSelected (bool s) { if (selected != s) { selected = s; setToggleState (s, juce::dontSendNotification); repaint(); } }
    bool isSelected() const noexcept { return selected; }

    Switch* getSwitch() noexcept { return toggle.get(); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void resized() override;

    static constexpr float height = 30.0f;

private:
    bool selected = false;
    std::unique_ptr<Switch> toggle;
};

} // namespace halite::ui
