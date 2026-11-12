#include "ui/AdvancedDrawer.h"

#include "plugin/PluginProcessor.h"
#include "common/ui/PanelArtwork.h"
#include "ui/Theme.h"

namespace mulberry::ui
{

namespace
{
    // Deck-local geometry (base units). One row of twelve cells in labelled groups: group
    // label, titles, knobs, values; the ADVANCED tab sits under it (owned by the panel).
    constexpr float cellWidth = 70.0f, groupGap = 12.0f;
    constexpr float knobDiameter = 36.0f;
    constexpr float rowTop = 0.0f;
    constexpr float groupLabelDy = 12.0f, titleDy = 31.0f, knobDy = 62.0f, valueDy = 94.0f;
    constexpr float keyWidth = 56.0f, keyHeight = 28.0f;
    constexpr int numCells = 14, numGroups = 7;

    /** Centre x of a cell, given its index and the index of its group (groups are separated by a gap). */
    float cellX (int cell, int group) noexcept
    {
        const auto total = (float) numCells * cellWidth + (float) (numGroups - 1) * groupGap;
        const auto x0 = ((float) metrics::baseWidth - total) * 0.5f;
        return x0 + cellWidth * ((float) cell + 0.5f) + groupGap * (float) group;
    }

    juce::RangedAudioParameter& parameter (MulberryProcessor& p, const char* id)
    {
        auto* param = p.getState().getParameter (id);
        jassert (param != nullptr);
        return *param;
    }
}

//==============================================================================
struct AdvancedDrawer::Cell
{
    juce::String id, title;
    juce::Point<float> centre;
    std::unique_ptr<Knob> knob;
    std::unique_ptr<LampButton> key;
    std::unique_ptr<juce::ButtonParameterAttachment> keyAttachment;
    juce::String shownValue;
    bool dimmed = false, shownDimmed = false;

    juce::Rectangle<float> valueArea() const
    {
        return juce::Rectangle<float> (cellWidth - 4.0f, 16.0f).withCentre ({ centre.x, centre.y - knobDy + valueDy });
    }
};

class AdvancedDrawer::Tray final : public juce::Component
{
public:
    struct Group
    {
        juce::String title;
        float x0, x1, y;
    };

    std::vector<std::unique_ptr<Cell>> cells;
    std::vector<Group> groups;
    drawing::CachedLayer background;
    juce::SharedResourcePointer<Fonts> fonts;

    void resized() override { background.invalidate(); }

    void paint (juce::Graphics& g) override
    {
        const auto& ext = Theme::extras();

        background.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> a, float scale)
        {
            renderTray (lg, a, scale);
        });

        const auto font = drawing::tracked (fonts->label (metrics::drawerValueSize), 0.02f);
        g.setFont (font);

        for (const auto& c : cells)
        {
            g.setColour (ext.drawerValue.withMultipliedAlpha (c->dimmed ? 0.45f : 1.0f));
            g.drawText (c->shownValue, c->valueArea(), juce::Justification::centred, false);
        }
    }

private:
    void renderTray (juce::Graphics& g, juce::Rectangle<float> a, float scale)
    {
        const auto& pal = Theme::palette();

        // Ground: the panel's own powder coat, continuing below it, with its grain.
        {
            juce::Image ground (juce::Image::ARGB, juce::jmax (1, (int) std::ceil (a.getWidth() * scale)),
                                juce::jmax (1, (int) std::ceil (a.getHeight() * scale)), true);
            {
                juce::Graphics sg (ground);
                sg.addTransform (juce::AffineTransform::scale (scale));
                sg.setGradientFill (juce::ColourGradient (pal.panelBottom, 0.0f, 0.0f, pal.panelBottom.darker (0.07f), 0.0f, a.getHeight(), false));
                sg.fillRect (a);
            }
            drawing::addGrain (ground, 0.024f, 0.0f, 0xad7);
            g.drawImageTransformed (ground, juce::AffineTransform::scale (1.0f / scale));
        }

        // The seam to the panel above: an engraved line with a soft shadow under the panel's edge.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x30000000), 0.0f, 0.0f, juce::Colour (0x00000000), 0.0f, 7.0f, false));
        g.fillRect (a.withHeight (7.0f));
        g.setColour (pal.grooveDark);
        g.drawHorizontalLine (0, a.getX(), a.getRight());
        g.setColour (pal.panelEdgeLight);
        g.drawHorizontalLine (1, a.getX(), a.getRight());

        // The instrument's lower corners move down to the deck.
        const auto inset = 15.0f;
        drawing::drawScrew (g, { a.getX() + inset, a.getBottom() - inset }, 5.0f, 1.3f);
        drawing::drawScrew (g, { a.getRight() - inset, a.getBottom() - inset }, 5.0f, -0.4f);

        // Group legends with engraved rules out to the group's edges.
        const auto groupFont = drawing::tracked (fonts->legend (10.0f), 0.30f);

        for (const auto& grp : groups)
        {
            const auto cx = 0.5f * (grp.x0 + grp.x1);
            const auto half = juce::GlyphArrangement::getStringWidth (groupFont, grp.title) * 0.5f + 8.0f;
            const auto area = juce::Rectangle<float> (half * 2.0f + 20.0f, 12.0f).withCentre ({ cx, grp.y });
            drawing::drawLegend (g, grp.title, groupFont, area, juce::Justification::centred, pal.legendDim);

            if (cx - half - (grp.x0 + 10.0f) > 6.0f)
            {
                drawing::drawGroove (g, { grp.x0 + 10.0f, grp.y, cx - half, grp.y });
                drawing::drawGroove (g, { cx + half, grp.y, grp.x1 - 10.0f, grp.y });
            }
        }

        // Cell titles and the knobs' tick marks.
        const auto titleFont = drawing::tracked (fonts->legend (metrics::drawerLegendSize), 0.12f);

        for (const auto& c : cells)
        {
            if (c->knob != nullptr)
            {
                auto style = c->knob->getStyle();
                style.title = {};
                style.marks.clear();
                renderKnobScale (g, c->centre, style, *fonts);
            }

            if (c->title.isNotEmpty())
                drawing::drawLegend (g, c->title, titleFont,
                                     juce::Rectangle<float> (cellWidth, 14.0f).withCentre ({ c->centre.x, c->centre.y - knobDy + titleDy }),
                                     juce::Justification::centred, pal.legend);
        }
    }
};

//==============================================================================
AdvancedDrawer::AdvancedDrawer (MulberryProcessor& p, ValueBubble& bubble)
    : processor (p),
      tray (std::make_unique<Tray>())
{
    setInterceptsMouseClicks (false, true);
    setVisible (false);
    setOpaque (true);
    addAndMakeVisible (*tray);

    auto* um = &processor.getUndoManager();
    auto& t = *tray;

    auto smallStyle = [] (const juce::String& title, int steps = 0, bool detent = false)
    {
        KnobStyle s;
        s.title = title;
        s.diameter = knobDiameter;
        s.ticks = steps > 1 ? steps : 2;
        s.steps = steps;
        s.centreDetent = detent;
        s.dragPixelsForFullRange = steps > 1 ? 16.0f * (float) steps : 200.0f;
        s.legendOffset = valueDy - knobDy;
        return s;
    };

    auto addKnob = [&] (float x, const char* id, const juce::String& title, const char* tip, int steps = 0, bool detent = false)
    {
        auto cell = std::make_unique<Cell>();
        cell->id = id;
        cell->title = title;
        cell->centre = { x, rowTop + knobDy };
        cell->knob = std::make_unique<Knob> (parameter (processor, id), um, smallStyle (title, steps, detent));
        cell->knob->setValueBubble (&bubble);
        cell->knob->setTooltip (tip);
        cell->knob->setBounds (layout::knobBounds (cell->centre, knobDiameter).toNearestInt());
        t.addAndMakeVisible (*cell->knob);
        t.cells.push_back (std::move (cell));
    };

    auto addKey = [&] (float x, const char* id, const juce::String& label, const char* tip)
    {
        auto cell = std::make_unique<Cell>();
        cell->id = id;
        cell->centre = { x, rowTop + knobDy };
        cell->key = std::make_unique<LampButton> (label, LampButton::Shape::square);
        cell->key->setKeyBounds (juce::Rectangle<float> (keyWidth, keyHeight).withCentre (cell->centre));
        cell->key->setClickingTogglesState (true);
        cell->key->setTooltip (tip);
        t.addAndMakeVisible (*cell->key);
        cell->keyAttachment = std::make_unique<juce::ButtonParameterAttachment> (parameter (processor, id), *cell->key, um);
        cell->key->snapLamp();
        t.cells.push_back (std::move (cell));
    };

    auto group = [&t] (const char* title, int first, int last, int groupIndex)
    {
        t.groups.push_back ({ title, cellX (first, groupIndex) - cellWidth * 0.5f, cellX (last, groupIndex) + cellWidth * 0.5f, rowTop + groupLabelDy });
    };

    // TIME
    addKnob (cellX (0, 0), params::groove, "GROOVE", "Groove: swing, 50 % (straight) to 75 %. Odd echoes (Single, Ping-Pong) or even steps (Pattern) move late.");
    addKnob (cellX (1, 0), params::feel, "FEEL", "Feel: pushes every echo early or drags it late, up to 30 ms (a 1/64 note when synced). Sticks at the beat.", 0, true);
    group ("TIME", 0, 1, 0);

    // STEREO
    addKnob (cellX (2, 1), params::width, "WIDTH", "Width: how far the echoes spread (Single, Ping-Pong, Pattern pans). 0 is a mono echo.");
    addKnob (cellX (3, 1), params::crossFeed, "X-FEED", "Cross-feed (Dual): how much of each side's feedback goes into the other.");
    addKnob (cellX (4, 1), params::panRule, "PAN RULE", "Pan rule (Pattern): centre, alternate, sweep or scatter, times Width.", numPanRules);
    group ("STEREO", 2, 4, 1);

    // MOTION
    addKnob (cellX (5, 2), params::wobbleRate, "RATE", "Wobble Rate: the machine's own instability rates, 0.25x - 4x. Sticks at 1x.", 0, true);
    addKnob (cellX (6, 2), params::wobbleShape, "SHAPE", "Wobble Shape: periodic (capstan, drum, LFO) against random (drift, scrape). 50 % is the machine's own mix.", 0, true);
    group ("MOTION", 5, 6, 2);

    // MACHINE
    addKnob (cellX (7, 3), params::noise, "NOISE", "Noise: the machine's constant floor (hiss, BBD noise, clock bleed). 50 % is its spec; 0 is silent.");
    addKey (cellX (8, 3), params::trails, "TRAILS", "Trails: on Bypass, machine and preset changes the old echoes ring out instead of being cut.");
    group ("MACHINE", 7, 8, 3);

    // DUCK
    addKnob (cellX (9, 4), params::duckAmount, "AMOUNT", "Duck Amount: how far the echoes dip while you play (with Duck on).");
    addKnob (cellX (10, 4), params::duckThreshold, "THRESHOLD", "Duck Threshold: the playing level where ducking starts.");
    addKnob (cellX (11, 4), params::duckRelease, "RELEASE", "Duck Release: how fast the echoes bloom back when you stop playing.");
    group ("DUCK", 9, 11, 4);

    // TEMPO, HOLD
    addKnob (cellX (12, 5), params::bpm, "BPM", "BPM: the tempo for synced times when the host gives none (e.g. Standalone).");
    group ("TEMPO", 12, 12, 5);
    addKnob (cellX (13, 6), params::holdMode, "MODE", "Hold Mode: Latch toggles Hold on each press; Momentary holds only while the key is held.", 2);
    group ("HOLD", 13, 13, 6);

    refreshValues (true);
}

AdvancedDrawer::~AdvancedDrawer() = default;

Knob* AdvancedDrawer::findKnob (const juce::String& id) const
{
    for (const auto& c : tray->cells)
        if (c->id == id && c->knob != nullptr)
            return c->knob.get();

    return nullptr;
}

LampButton* AdvancedDrawer::findKey (const juce::String& id) const
{
    for (const auto& c : tray->cells)
        if (c->id == id && c->key != nullptr)
            return c->key.get();

    return nullptr;
}

juce::String AdvancedDrawer::getValueTextFor (const juce::String& id) const
{
    for (const auto& c : tray->cells)
        if (c->id == id)
            return c->shownValue;

    return {};
}

void AdvancedDrawer::setOpen (bool shouldBeOpen)
{
    targetOpen = shouldBeOpen;
    setVisible (shouldBeOpen);

    if (shouldBeOpen)
        refreshValues (true);
}

bool AdvancedDrawer::tick (float dt)
{
    if (! targetOpen)
        return false;

    refreshValues (false);
    auto moving = false;

    for (auto& c : tray->cells)
        if (c->key != nullptr)
            moving = c->key->tick (dt) || moving;

    return moving;
}

void AdvancedDrawer::refreshValues (bool force)
{
    auto& meters = processor.getMeters();
    const auto hostTempo = meters.hostHasTempo.load (std::memory_order_relaxed);
    const auto mode = static_cast<Mode> (juce::roundToInt (processor.getState().getRawParameterValue (params::mode)->load()));

    for (auto& c : tray->cells)
    {
        juce::String text;
        auto dimmed = false;

        if (c->key != nullptr)
        {
            text = c->key->getToggleState() ? "On" : "Off";
        }
        else if (c->knob != nullptr)
        {
            text = c->knob->getValueText();

            if (c->id == params::crossFeed)
                dimmed = mode != Mode::dual;
            else if (c->id == params::panRule)
                dimmed = mode != Mode::pattern;
            else if (c->id == params::bpm && hostTempo)
            {
                dimmed = true;
                text = "Host " + juce::String (meters.bpm.load (std::memory_order_relaxed), 1);
            }
        }

        c->dimmed = dimmed;

        if (force || text != c->shownValue || dimmed != c->shownDimmed)
        {
            c->shownValue = text;
            c->shownDimmed = dimmed;

            if (c->knob != nullptr)
            {
                c->knob->setAlpha (dimmed ? 0.5f : 1.0f);

                // The host owns the tempo: BPM is read-only then.
                if (c->id == params::bpm)
                    c->knob->setEnabled (! hostTempo);
            }

            tray->repaint (c->valueArea().getSmallestIntegerContainer());
        }
    }
}

void AdvancedDrawer::resized()
{
    tray->setBounds (getLocalBounds());
}

//==============================================================================
AdvancedTab::AdvancedTab()
    : juce::Button ("ADVANCED")
{
    setTitle ("Advanced parameters");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setTooltip ("Advanced: groove and feel, stereo, wobble rate and shape, noise and trails, duck, tempo and hold mode, "
                "on a deck below the panel. The window grows while it is open.");
}

void AdvancedTab::setOpenState (bool isOpen)
{
    if (open != isOpen)
    {
        open = isOpen;
        setDescription (open ? "Open" : "Closed");
        repaint();
    }
}

void AdvancedTab::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& pal = Theme::palette();
    const auto area = getLocalBounds().toFloat();
    const auto font = drawing::tracked (fonts->legend (10.5f), 0.30f);
    const auto legend = juce::String ("ADVANCED");
    const auto textWidth = juce::GlyphArrangement::getStringWidth (font, legend);

    // LED, legend, chevron: one group, centred.
    const auto groupWidth = 12.0f + 10.0f + textWidth + 6.0f + 8.0f;
    auto x = area.getCentreX() - groupWidth * 0.5f;
    const auto cy = area.getCentreY() + (down ? 0.5f : 0.0f);
    const auto ink = highlighted || down ? pal.legend : pal.legend.withMultipliedAlpha (0.88f);

    const auto led = juce::Rectangle<float> (12.0f, 3.0f).withCentre ({ x + 6.0f, cy });
    g.setColour (pal.lensOffRim);
    g.fillRoundedRectangle (led.expanded (1.0f), 2.0f);

    if (open)
    {
        drawing::fillGlow (g, led.getCentre(), 9.0f, pal.accent, 0.5f);
        g.setColour (pal.accent);
    }
    else
    {
        g.setColour (pal.lensOff);
    }

    g.fillRoundedRectangle (led, 1.5f);
    x += 12.0f + 10.0f;

    const auto textArea = juce::Rectangle<float> (x, cy - 7.0f, textWidth + 2.0f, 14.0f);
    drawing::drawLegend (g, legend, font, textArea, juce::Justification::centredLeft, ink);
    x += textWidth + 6.0f;

    // Chevron: down while closed (the deck opens below), up while open.
    juce::Path chevron;
    const auto c = juce::Point<float> (x + 4.0f, cy);
    if (open)
        chevron.addTriangle (c.x - 3.5f, c.y + 2.0f, c.x + 3.5f, c.y + 2.0f, c.x, c.y - 2.5f);
    else
        chevron.addTriangle (c.x - 3.5f, c.y - 2.0f, c.x + 3.5f, c.y - 2.0f, c.x, c.y + 2.5f);
    g.setColour (ink);
    g.fillPath (chevron);

    // Engraved rules out to the edges.
    const auto left = area.getCentreX() - groupWidth * 0.5f - 12.0f, right = area.getCentreX() + groupWidth * 0.5f + 12.0f;
    drawing::drawGroove (g, { area.getX() + 4.0f, area.getCentreY(), left, area.getCentreY() });
    drawing::drawGroove (g, { right, area.getCentreY(), area.getRight() - 4.0f, area.getCentreY() });

    if (hasKeyboardFocus (false))
    {
        g.setColour (pal.focus);
        g.drawRoundedRectangle (juce::Rectangle<float> (groupWidth + 16.0f, area.getHeight() - 1.0f).withCentre (area.getCentre()), 4.0f, 1.4f);
    }
}

} // namespace mulberry::ui
