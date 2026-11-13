#pragma once

#include "ui/GlassWidgets.h"
#include "ui/InkcapController.h"

namespace inkcap::ui
{

/**
    One module as a display row (the mock's): its dot, name, type, amount bar and Amount. The
    bar's faint part is the Amount, its bright part how far Damage takes it now; the dot glows
    with what the module does at the moment. Click the dot: On; click the row: the module's
    page; drag along the row: Amount (Shift: fine); double-click the number: type it (a click on
    the number alone does nothing, so the double-click never opens the page first).
*/
class ModuleRow final : public juce::Component
{
public:
    ModuleRow (InkcapController&, Module);
    ~ModuleRow() override;

    void update();
    Module getModule() const noexcept { return module; }

    /** The page is open: the row stays lit. */
    void setSelected (bool);

    /** Called on a click that is not a drag. */
    std::function<void (Module)> onOpen;

    void openAmountEntry();
    TypedEntry& getEntry() noexcept { return entry; }

    juce::Rectangle<float> dotArea() const;
    juce::Rectangle<float> barArea() const;
    juce::Rectangle<float> amountArea() const;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    juce::String describe() const;

    InkcapController& controller;
    const Module module;
    juce::SharedResourcePointer<GlassFonts> fonts;
    TypedEntry entry;

    bool on = false, selected = false, hover = false;
    float amount = 0.0f, intensity = 0.0f, activity = 0.0f;
    juce::String type;

    enum class Press { none, dot, row, number, drag };
    Press press = Press::none;
    juce::Point<float> pressPoint;
    float dragAmount = 0.0f;
    float lastX = 0.0f;
};

/** The six module rows, in signal order. */
class ModuleList final : public juce::Component
{
public:
    explicit ModuleList (InkcapController&);

    void update();
    void setSelected (int moduleIndex);
    ModuleRow& getRow (Module m) noexcept { return *rows[(size_t) m]; }

    std::function<void (Module)> onOpen;

    void resized() override;

private:
    std::array<std::unique_ptr<ModuleRow>, numModules> rows;
};

/**
    The wear matrix: 32 x 3 dots that light with Damage, in an order drawn from the seed. Calm at
    Damage 0, full at 100; with every module off the lit dots stay grey (nothing wears).
*/
class WearMatrix final : public juce::Component
{
public:
    explicit WearMatrix (InkcapController&);

    void update();
    int litDots() const noexcept { return lit; }

    void paint (juce::Graphics&) override;

    static constexpr int dots = layout::wearColumns * layout::wearRows;

private:
    InkcapController& controller;
    juce::SharedResourcePointer<GlassFonts> fonts;
    std::array<int, dots> rank {};    // the dot's place in the lighting order
    uint32_t seed = 0;
    int lit = -1;
    bool anyOn = true;
};

} // namespace inkcap::ui
