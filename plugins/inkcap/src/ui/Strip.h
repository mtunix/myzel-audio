#pragma once

#include "ui/GlassWidgets.h"
#include "ui/InkcapController.h"

namespace inkcap::ui
{

/**
    A round module key on the body: up when off, pressed in with its LED lit when on. The LED
    breathes with what the module does. Click or Space: On (one undo step).
*/
class ModuleKey final : public juce::Button
{
public:
    ModuleKey (InkcapController&, Module);

    void update();
    Module getModule() const noexcept { return module; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::String hint() const;

    InkcapController& controller;
    const Module module;
    juce::SharedResourcePointer<GlassFonts> fonts;
    bool on = false;
    float activity = 0.0f;
};

/**
    A small knob on the body (Mix, Output): vertical drag (Shift: fine), wheel, arrow keys,
    double-click for the default, right-click or Alt-click to type a value. Its read-out shows
    in the status line while it is hovered or turned.
*/
class StripKnob final : public juce::Component,
                        private juce::Timer
{
public:
    StripKnob (InkcapController&, const char* parameterId, const juce::String& label);
    ~StripKnob() override;

    void update();
    const char* getParameterId() const noexcept { return id; }

    /** Shows the padlock after the label (the mix lock). */
    void setLocked (bool);
    bool isLocked() const noexcept { return locked; }

    void openTextEntry();
    TypedEntry& getEntry() noexcept { return entry; }

    juce::Rectangle<float> knobArea() const;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    static constexpr float dragPixels = 200.0f;

private:
    void timerCallback() override;
    void showHint();
    void setFromGesture (float value01);

    InkcapController& controller;
    const char* id;
    const juce::String label;
    juce::SharedResourcePointer<GlassFonts> fonts;
    TypedEntry entry;

    float value = -1.0f, dragValue = 0.0f, lastY = 0.0f;
    bool hover = false, dragging = false, inWheelGesture = false, locked = false;
};

/** A pill key on the body: Lock (light, latching, with an LED) or Reroll (dark, momentary). */
class Pill final : public juce::Button
{
public:
    Pill (const juce::String& text, bool dark, bool latching);

    void setLit (bool);
    bool isLit() const noexcept { return lit; }
    float idealWidth() const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::SharedResourcePointer<GlassFonts> fonts;
    const bool dark, latching;
    bool lit = false;
};

/** The physical strip under the display: six module keys, Mix and Output, Lock and Reroll. */
class Strip final : public juce::Component
{
public:
    explicit Strip (InkcapController&);

    void update();

    ModuleKey& getKey (Module m) noexcept { return *keys[(size_t) m]; }
    StripKnob& getMix() noexcept { return mix; }
    StripKnob& getOutput() noexcept { return output; }
    Pill& getLock() noexcept { return lock; }
    Pill& getReroll() noexcept { return reroll; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    InkcapController& controller;
    std::array<std::unique_ptr<ModuleKey>, numModules> keys;
    StripKnob mix, output;
    Pill lock { "Lock", false, true }, reroll { "Reroll", true, false };
    float seamX = 0.0f;
};

} // namespace inkcap::ui
