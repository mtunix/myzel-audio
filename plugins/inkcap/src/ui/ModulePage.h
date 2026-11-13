#pragma once

#include "ui/EventView.h"
#include "ui/GlassWidgets.h"
#include "ui/InkcapController.h"

namespace inkcap::ui
{

/**
    A control as a display row: its name, a bar and its read-out ("49 ms", "dark 40", "1/16").
    Drag along the row (Shift: fine), wheel, or the arrow keys when focused; double-click (or
    Return) to type a value, Alt-click for the default. Every edit is one gesture and one undo
    step. Bipolar rows fill from the middle, stepped rows show their steps, a switch row toggles
    on a click.
*/
class ValueRow final : public juce::Component,
                       private juce::Timer
{
public:
    enum class Kind
    {
        continuous,
        bipolar,
        steps,
        toggle
    };

    ValueRow (InkcapController&, const char* parameterId, const juce::String& label, Kind);
    ~ValueRow() override;

    void update();

    const char* getParameterId() const noexcept { return id; }
    Kind getKind() const noexcept { return kind; }
    juce::String getValueText() const { return valueText; }

    void openTextEntry();
    TypedEntry& getEntry() noexcept { return entry; }

    /** For Amount: where Damage takes it now (0 .. 1). The bar shows the Amount faint, this bright, as the module rows do. */
    void setReach (std::function<float()> fn) { reach = std::move (fn); }

    juce::Rectangle<float> barArea() const;
    juce::Rectangle<float> valueArea() const;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void timerCallback() override;
    void setFromGesture (float value01);
    void step (int direction, bool fine);
    int numSteps() const;

    InkcapController& controller;
    const char* id;
    const juce::String label;
    const Kind kind;
    juce::SharedResourcePointer<GlassFonts> fonts;
    TypedEntry entry;

    std::function<float()> reach;
    float value = 0.0f, reached = -1.0f;
    juce::String valueText;
    bool hover = false, dragging = false, inWheelGesture = false, pressed = false;
    float dragValue = 0.0f, lastX = 0.0f;
    float wheelAccumulator = 0.0f;

    friend class ModulePage;
};

/**
    A module's page in the right half of the display: its name with On, the type tabs, its
    controls as rows and, for Glitch, the next 4 bars of events. The XY pad stays beside it.
*/
class ModulePage final : public juce::Component
{
public:
    explicit ModulePage (InkcapController&);
    ~ModulePage() override;

    void setModule (Module);
    Module getModule() const noexcept { return module; }

    void update();

    /** The back key, Escape. */
    std::function<void()> onClose;

    SoftKey& getOnKey() noexcept { return onKey; }

    /** The line under the rows: what Damage makes of the module now. */
    juce::String getCaption() const { return caption; }
    juce::Button& getBackKey() noexcept;
    SoftKey* getTab (int typeIndex);
    ValueRow* findRow (const char* parameterId);
    EventView& getEvents() noexcept { return events; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr float tabsY = 38.0f, rowsY = 72.0f, eventsY = 216.0f;

private:
    class BackKey;

    void rebuild();

    InkcapController& controller;
    Module module = Module::glitch;
    juce::SharedResourcePointer<GlassFonts> fonts;

    std::unique_ptr<BackKey> backKey;
    SoftKey onKey { "on", "Module on" };
    std::vector<std::unique_ptr<SoftKey>> tabs;
    std::vector<std::unique_ptr<ValueRow>> rows;
    EventView events;

    juce::String caption;
    bool on = false;
    int type = -1;
    float activity = 0.0f;
};

} // namespace inkcap::ui
