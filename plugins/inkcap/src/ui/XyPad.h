#pragma once

#include "common/ui/Drawing.h"
#include "ui/InkcapController.h"
#include "ui/Theme.h"

namespace inkcap::ui
{

/**
    The XY pad: Damage to the right, Chaos up, one amber puck. The contour rings around the puck
    show how far the wear wanders: tight at Damage 0 or Chaos 0, wide at both, their outline
    drawn from the seed. Drag the puck (Shift: fine), click elsewhere to move it there,
    double-click for 0 / 0, wheel and arrow keys when focused. A drag is one host gesture per
    parameter and one undo step. In the accessibility tree it is two sliders, Damage and Chaos.
*/
class XyPad final : public juce::Component,
                    private juce::Timer
{
public:
    explicit XyPad (InkcapController&);
    ~XyPad() override;

    /** Re-reads Damage, Chaos, the seed and Bypass; repaints only when one changed. */
    void update();

    /** The puck's centre for a Damage and Chaos (0 .. 1), in local coordinates. */
    juce::Point<float> pointFor (float damage, float chaos) const;
    juce::Point<float> puckCentre() const { return pointFor (damage, chaos); }

    /** The contour rings' outlines (local coordinates): the self-test checks they widen with Chaos. */
    juce::Rectangle<float> ringBounds (int ring) const { return rings[(size_t) ring].getBounds(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    static constexpr float edge = 0.05f;          // the puck never goes nearer the border than this share
    static constexpr float grabRadius = 16.0f;    // a press this close to the puck drags it from where it is
    static constexpr float keyStep = 0.01f;       // an arrow key; Shift: a fifth of it

private:
    class Axis;

    void timerCallback() override;

    juce::Rectangle<float> travel() const;
    void beginGesture();
    void endGesture();
    void moveTo (float newDamage, float newChaos);
    void nudge (float dDamage, float dChaos);
    void rebuildRings();

    InkcapController& controller;
    juce::SharedResourcePointer<GlassFonts> fonts;
    drawing::CachedLayer background;

    float damage = 0.0f, chaos = 0.0f, wander = 0.0f;
    uint32_t seed = 0;
    bool bypassed = false, anyOn = true;
    std::array<juce::Path, 2> rings;

    bool dragging = false, inWheelGesture = false;
    float dragDamage = 0.0f, dragChaos = 0.0f;
    juce::Point<float> lastDrag;

    std::unique_ptr<Axis> damageAxis, chaosAxis;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XyPad)
};

} // namespace inkcap::ui
