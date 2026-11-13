#include "ui/ModuleList.h"

#include "plugin/Parameters.h"

namespace inkcap::ui
{

namespace
{
    // The row's columns, left to right (layout:: in Theme.h).
    constexpr float dotX = layout::rowPadX;
    constexpr float nameX = dotX + layout::dotWidth + layout::rowGap;
    constexpr float typeX = nameX + layout::nameWidth + layout::rowGap;
    constexpr float barX = typeX + layout::typeWidth + layout::rowGap;

    constexpr float dragThreshold = 3.0f;

    /** A display-only shuffle for the wear matrix (not a DSP stream: the order is a picture of the seed). */
    uint32_t mix32 (uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }
}

ModuleRow::ModuleRow (InkcapController& c, Module m)
    : controller (c), module (m)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle (InkcapController::moduleName (m));
    update();
}

ModuleRow::~ModuleRow()
{
    // The editor can close mid-drag: the host must still see the gesture end.
    if (press == Press::drag)
        controller.endEdit (params::amountIds[(size_t) module]);
}

juce::Rectangle<float> ModuleRow::dotArea() const
{
    return { dotX - 4.0f, 0.0f, layout::dotWidth + 8.0f, (float) getHeight() };
}

juce::Rectangle<float> ModuleRow::amountArea() const
{
    return { (float) getWidth() - layout::rowPadX - layout::amountWidth, 0.0f, layout::amountWidth, (float) getHeight() };
}

juce::Rectangle<float> ModuleRow::barArea() const
{
    const auto right = amountArea().getX() - layout::rowGap;
    return { barX, 0.0f, right - barX, (float) getHeight() };
}

void ModuleRow::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

juce::String ModuleRow::describe() const
{
    return InkcapController::moduleName (module) + ", " + type + ", amount " + juce::String (juce::roundToInt (amount * 100.0f)) + " %, "
         + (on ? "on" : "off");
}

void ModuleRow::update()
{
    const auto newOn = controller.isOn (module);
    const auto newAmount = controller.amount (module);
    const auto newIntensity = controller.intensity (module);
    const auto newType = controller.typeName (module);
    const auto newActivity = newOn ? juce::jlimit (0.0f, 1.0f, controller.getTelemetry().modules[(size_t) module].activity.load (std::memory_order_relaxed))
                                   : 0.0f;

    const auto changed = newOn != on || ! juce::approximatelyEqual (newAmount, amount) || ! juce::approximatelyEqual (newIntensity, intensity)
                      || newType != type;

    if (changed || std::abs (newActivity - activity) > 0.02f)
    {
        on = newOn, amount = newAmount, intensity = newIntensity, type = newType, activity = newActivity;
        if (changed)
            setDescription (describe());
        repaint();
    }
}

void ModuleRow::paint (juce::Graphics& g)
{
    const auto& c = Theme::glass();
    const auto area = getLocalBounds().toFloat();

    if (selected)
        g.setColour (c.rowSelected), g.fillRoundedRectangle (area, metrics::rowCorner);
    else if (hover)
        g.setColour (c.rowHover), g.fillRoundedRectangle (area, metrics::rowCorner);

    const auto fg = on ? c.text : c.textDim;
    const auto font = fonts->mono (metrics::textSize);
    g.setFont (font);

    // The dot: amber when on, with a halo while the module is audibly at work.
    const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ dotX + layout::dotWidth * 0.5f - 1.0f, area.getCentreY() });
    if (on && activity > 0.02f)
    {
        g.setColour (c.amber.withAlpha (0.18f * activity));
        g.fillEllipse (dot.expanded (4.0f * activity + 1.0f));
    }
    g.setColour (on ? c.amber : c.textFaint);
    g.fillEllipse (dot);

    g.setColour (fg);
    g.drawText (moduleInfo (module).name, juce::Rectangle<float> (nameX, 0.0f, layout::nameWidth, area.getHeight()),
                juce::Justification::centredLeft, false);
    glassdraw::fittedText (g, type, font, { typeX, 0.0f, layout::typeWidth, area.getHeight() }, juce::Justification::centredLeft, c.textDim);

    // The bar: the track, the Amount (faint), and where Damage takes it now (bright).
    const auto bar = barArea().withSizeKeepingCentre (barArea().getWidth(), 3.0f);
    g.setColour (c.track);
    g.fillRoundedRectangle (bar, 1.5f);

    if (amount > 0.0f)
    {
        g.setColour (c.textFaint);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (3.0f, bar.getWidth() * amount)), 1.5f);
    }

    if (on && intensity > 0.0f)
    {
        g.setColour (press == Press::drag ? c.amber : c.text);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (3.0f, bar.getWidth() * intensity)), 1.5f);
    }

    g.setColour (press == Press::drag ? c.amber : fg);
    g.drawText (juce::String (juce::roundToInt (amount * 100.0f)), amountArea(), juce::Justification::centredRight, false);

    if (hasKeyboardFocus (false))
        glassdraw::focusRing (g, area, metrics::rowCorner);
}

void ModuleRow::mouseDown (const juce::MouseEvent& e)
{
    press = dotArea().contains (e.position) ? Press::dot
          : amountArea().expanded (8.0f, 0.0f).contains (e.position) ? Press::number
          : Press::row;
    pressPoint = e.position;
    lastX = e.position.x;
}

void ModuleRow::mouseDrag (const juce::MouseEvent& e)
{
    if ((press == Press::row || press == Press::number) && e.position.getDistanceFrom (pressPoint) > dragThreshold)
    {
        // The drag counts from where the press was, so crossing the threshold loses nothing.
        press = Press::drag;
        dragAmount = controller.normalised (params::amountIds[(size_t) module]);
        controller.beginEdit (params::amountIds[(size_t) module]);
        lastX = pressPoint.x;
        repaint();
    }

    if (press != Press::drag)
        return;

    const auto factor = e.mods.isShiftDown() ? metrics::fineFactor : 1.0f;
    dragAmount += (e.position.x - lastX) / barArea().getWidth() * factor;
    lastX = e.position.x;

    auto& p = controller.parameter (params::amountIds[(size_t) module]);
    p.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragAmount));
    update();
}

void ModuleRow::mouseUp (const juce::MouseEvent& e)
{
    const auto was = press;
    press = Press::none;

    if (was == Press::drag)
    {
        controller.endEdit (params::amountIds[(size_t) module]);
        repaint();
        return;
    }

    if (! getLocalBounds().toFloat().contains (e.position) || e.getNumberOfClicks() > 1)
        return;

    if (was == Press::dot)
        controller.toggle (params::onIds[(size_t) module]);
    else if (was == Press::row && onOpen != nullptr)
        onOpen (module);

    update();
}

void ModuleRow::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (amountArea().expanded (8.0f, 0.0f).contains (e.position))
        openAmountEntry();
}

void ModuleRow::openAmountEntry()
{
    const auto id = params::amountIds[(size_t) module];
    const auto a = amountArea().expanded (22.0f, -4.0f).toNearestInt();
    entry.open (*this, a.withX (getWidth() - (int) layout::rowPadX - a.getWidth() + 4), juce::String (juce::roundToInt (amount * 100.0f)),
                InkcapController::moduleName (module) + " amount",
                [this, id] (const juce::String& t) { controller.setFromText (id, t); update(); });
}

bool ModuleRow::keyPressed (const juce::KeyPress& key)
{
    const auto id = params::amountIds[(size_t) module];
    const auto step = key.getModifiers().isShiftDown() ? 0.002f : 0.01f;

    if (key.getKeyCode() == juce::KeyPress::returnKey || key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        if (onOpen != nullptr)
            onOpen (module);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
    {
        controller.setNormalised (id, controller.normalised (id) + (key.getKeyCode() == juce::KeyPress::rightKey ? step : -step));
        update();
        return true;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> ModuleRow::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (
        *this, juce::AccessibilityRole::button,
        juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press, [this] { if (onOpen != nullptr) onOpen (module); })
                                    .addAction (juce::AccessibilityActionType::toggle,
                                                [this] { controller.toggle (params::onIds[(size_t) module]); update(); }));
}

//==============================================================================
ModuleList::ModuleList (InkcapController& c)
{
    setTitle ("Modules");

    for (int i = 0; i < numModules; ++i)
    {
        auto& row = rows[(size_t) i];
        row = std::make_unique<ModuleRow> (c, (Module) i);
        row->onOpen = [this] (Module m) { if (onOpen != nullptr) onOpen (m); };
        addAndMakeVisible (*row);
    }
}

void ModuleList::update()
{
    for (auto& row : rows)
        row->update();
}

void ModuleList::setSelected (int moduleIndex)
{
    for (int i = 0; i < numModules; ++i)
        rows[(size_t) i]->setSelected (i == moduleIndex);
}

void ModuleList::resized()
{
    for (int i = 0; i < numModules; ++i)
        rows[(size_t) i]->setBounds (juce::Rectangle<float> (0.0f, (float) i * layout::rowPitch, (float) getWidth(), layout::rowHeight)
                                         .toNearestInt());
}

//==============================================================================
WearMatrix::WearMatrix (InkcapController& c)
    : controller (c)
{
    setInterceptsMouseClicks (false, false);
    setTitle ("Wear");
    update();
}

void WearMatrix::update()
{
    const auto newSeed = controller.getSeeds().current();
    const auto newLit = juce::roundToInt (controller.damage() * (float) dots);
    const auto newOn = controller.anyModuleOn();

    if (newSeed != seed || lit < 0)
    {
        seed = newSeed;
        std::array<std::pair<uint32_t, int>, dots> keys;
        for (int i = 0; i < dots; ++i)
            keys[(size_t) i] = { mix32 (seed ^ mix32 ((uint32_t) i + 0x9e3779b9u)), i };
        std::sort (keys.begin(), keys.end());
        for (int r = 0; r < dots; ++r)
            rank[(size_t) keys[(size_t) r].second] = r;
        lit = -1;
    }

    if (newLit != lit || newOn != anyOn)
    {
        lit = newLit, anyOn = newOn;
        setDescription (juce::String (lit) + " of " + juce::String (dots) + " lit");
        repaint();
    }
}

void WearMatrix::paint (juce::Graphics& g)
{
    const auto& c = Theme::glass();
    g.setFont (fonts->mono (metrics::textSize));
    g.setColour (c.textDim);
    g.drawText ("wear", juce::Rectangle<float> (0.0f, 0.0f, 100.0f, 15.0f), juce::Justification::centredLeft, false);

    // A grid of 32 equal columns with 4 px gaps; each 5 px dot sits at its cell's left edge.
    const auto pitchX = ((float) getWidth() + layout::wearGap) / (float) layout::wearColumns;
    const auto top = (float) getHeight() - (layout::wearDot * layout::wearRows + layout::wearGap * (layout::wearRows - 1));
    const auto litColour = anyOn ? c.amber : c.textFaint;

    for (int i = 0; i < dots; ++i)
    {
        const auto col = i % layout::wearColumns, row = i / layout::wearColumns;
        g.setColour (rank[(size_t) i] < lit ? litColour : c.track);
        g.fillEllipse ((float) col * pitchX, top + (float) row * (layout::wearDot + layout::wearGap), layout::wearDot, layout::wearDot);
    }
}

} // namespace inkcap::ui
