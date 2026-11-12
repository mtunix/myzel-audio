#include "ui/EchoMap.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <melatonin_blur/melatonin_blur.h>

namespace mulberry::ui
{

namespace
{
    constexpr float headerHeight = 18.0f;
    constexpr float liveOffsetDb = 3.0f;

    juce::String msText (float ms)
    {
        return ms < 1000.0f ? juce::String (juce::roundToInt (ms)) + " ms" : juce::String (ms / 1000.0f, 2) + " s";
    }
}

EchoMap::EchoMap()
{
    setOpaque (false);
    history.assign ((size_t) historySize, floorDb);
    setTitle ("Echo map");
    setTooltip ("The echoes of a click, from your settings: left output on top, right below, over a beat grid "
                "when synced, with the live wet level behind. In Pattern mode, drag the steps to set their levels.");
}

void EchoMap::setShape (const EchoShape& s)
{
    if (s == shape && ! events.empty())
        return;

    shape = s;
    span = chooseSpan (shape);
    events = echoEvents (shape, span);
    echoLayer.invalidate();
    screenLayer.invalidate();
    updateDescription();
    repaint();
}

void EchoMap::updateDescription()
{
    const auto modes = std::array<const char*, numModes> { "single", "dual", "ping-pong", "pattern" };
    setDescription (juce::String (info (shape.machine).name) + ", " + modes[(size_t) shape.plan.mode] + ", "
                    + msText (shape.timeMs[0]) + ", feedback " + juce::String (juce::roundToInt (shape.feedback * 100.0f)) + " %");
}

juce::Rectangle<float> EchoMap::screenArea() const
{
    return getLocalBounds().toFloat().reduced (bezelWidth);
}

juce::Rectangle<float> EchoMap::laneArea (int lane) const
{
    auto plot = screenArea().reduced (8.0f, 0.0f).withTrimmedTop (headerHeight).withTrimmedBottom (16.0f);
    const auto h = (plot.getHeight() - 6.0f) * 0.5f;
    return lane == 0 ? plot.withHeight (h) : plot.withTrimmedTop (h + 6.0f);
}

float EchoMap::xForTime (float ms) const noexcept
{
    const auto plot = laneArea (0);
    return plot.getX() + plot.getWidth() * std::clamp (ms / span, 0.0f, 1.0f);
}

float EchoMap::levelHeight (float linear, juce::Rectangle<float> lane) const noexcept
{
    const auto db = juce::Decibels::gainToDecibels (linear, -120.0f);
    return lane.getHeight() * std::clamp ((db - floorDb) / -floorDb, 0.0f, 1.0f);
}

juce::Rectangle<float> EchoMap::stepArea (int step) const
{
    const auto n = std::clamp (shape.plan.steps, 1, dsp::maxPatternSteps);
    if (shape.plan.mode != Mode::pattern || step < 0 || step >= n)
        return {};
    const auto stepLength = 1.0f / (float) n;
    const auto swing = (step % 2 == 1) ? shape.plan.groove * stepLength : 0.0f;
    const auto t = ((float) (step + 1) * stepLength + swing) * shape.timeMs[0] + shape.plan.feelMs;
    const auto x = xForTime (t);
    const auto top = laneArea (0).getY(), bottom = laneArea (1).getBottom();
    return { x - 6.0f, top, 12.0f, bottom - top };
}

int EchoMap::stepAt (juce::Point<float> p) const
{
    for (int k = 0; k < dsp::maxPatternSteps; ++k)
        if (stepArea (k).expanded (3.0f, 0.0f).contains (p))
            return k;
    return -1;
}

void EchoMap::tick (float dt, float wetRms)
{
    const auto db = std::clamp (juce::Decibels::gainToDecibels (wetRms, -120.0f) + liveOffsetDb, floorDb, 6.0f);
    smoothedDb = db >= smoothedDb ? db : smoothedDb + (db - smoothedDb) * (1.0f - std::exp (-dt / 0.05f));

    pending += dt;
    const auto period = 1.0f / historyRate;
    for (int pushed = 0; pending >= period && pushed < 16; ++pushed)
    {
        history[(size_t) (written % historySize)] = smoothedDb;
        ++written;
        pending -= period;
    }
    pending = std::min (pending, period);

    // Repaint only when the newest column moves visibly.
    const auto pixels = juce::roundToInt ((smoothedDb - floorDb) * 2.0f) + (int) (written / 4);
    if (pixels != shownLevelPixels && (smoothedDb > floorDb + 0.5f || shownLevelPixels >= 0))
    {
        shownLevelPixels = smoothedDb > floorDb + 0.5f ? pixels : -1;
        repaint (screenArea().toNearestInt());
    }
}

void EchoMap::fillHistoryForSnapshot (float peakDb)
{
    for (int i = 0; i < historySize; ++i)
    {
        const auto t = (float) i / historyRate;
        const auto pulse = std::exp (-std::fmod (t, 0.375f) * 9.0f);
        history[(size_t) i] = std::max (floorDb, peakDb + juce::Decibels::gainToDecibels (pulse * (1.0f - t / 20.0f), -120.0f));
    }
    written = historySize;
    shownLevelPixels = 0;
    repaint();
}

void EchoMap::resized()
{
    bezelLayer.invalidate();
    screenLayer.invalidate();
    echoLayer.invalidate();
    glassLayer.invalidate();
}

void EchoMap::paint (juce::Graphics& g)
{
    const auto screen = screenArea();
    bezelLayer.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderBezel (lg, a); });

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (screen.toNearestInt());

    screenLayer.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderScreen (lg, a); });

    if (shownLevelPixels >= 0)
        drawLive (g);

    echoLayer.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderEchoes (lg, a); },
                    shape.bypassed ? 0.28f : 1.0f);

    const auto& pal = Theme::palette();
    if (shape.bypassed)
        drawStamp (g, "BYPASSED", pal.caution);
    else if (shape.hold)
        drawStamp (g, "HOLD", pal.accent);

    glassLayer.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderGlass (lg, a); });
}

void EchoMap::drawLive (juce::Graphics& g) const
{
    const auto& ext = Theme::extras();
    const auto lane0 = laneArea (0), lane1 = laneArea (1);
    const auto plot = lane0.getUnion (lane1);
    const auto columns = juce::jmax (1, (int) plot.getWidth());
    const auto entriesPerColumn = std::max (1.0, (double) historySize / (double) columns);

    juce::Path area;
    area.startNewSubPath (plot.getRight(), plot.getBottom());
    for (int k = 0; k <= columns; ++k)
    {
        const auto entry = written - 1 - (long long) ((double) k * entriesPerColumn);
        const auto db = entry >= 0 ? history[(size_t) (entry % historySize)] : floorDb;
        const auto h = plot.getHeight() * std::clamp ((db - floorDb) / -floorDb, 0.0f, 1.0f);
        area.lineTo (plot.getRight() - (float) k, plot.getBottom() - h);
    }
    area.lineTo (plot.getX(), plot.getBottom());
    area.closeSubPath();
    g.setGradientFill (juce::ColourGradient (ext.liveFill, 0.0f, plot.getY(), ext.liveFill.withMultipliedAlpha (0.1f), 0.0f, plot.getBottom(), false));
    g.fillPath (area);
}

void EchoMap::drawStamp (juce::Graphics& g, const juce::String& text, juce::Colour colour) const
{
    const auto& ext = Theme::extras();
    const auto font = drawing::tracked (fonts->legend (13.0f), 0.30f);
    const auto w = juce::GlyphArrangement::getStringWidth (font, text) + 22.0f;
    const auto box = juce::Rectangle<float> (w, 24.0f).withCentre (screenArea().getCentre().translated (0.0f, 4.0f));
    g.setColour (ext.screenBottom.withAlpha (0.85f));
    g.fillRoundedRectangle (box, 4.0f);
    drawing::fillGlow (g, box.getCentre(), w * 0.6f, colour, 0.12f);
    g.setColour (colour);
    g.drawRoundedRectangle (box, 4.0f, 1.4f);
    g.setFont (font);
    g.drawText (text, box.translated (2.0f, 0.0f), juce::Justification::centred, false);
}

//==============================================================================
void EchoMap::renderBezel (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& ext = Theme::extras();
    const auto outer = area.reduced (1.0f);

    juce::Path frame;
    frame.addRoundedRectangle (outer, 9.0f);
    drawing::drawContactShadow (g, frame, 4.0f, 0.8f);
    g.setGradientFill (juce::ColourGradient (ext.bezel.brighter (0.6f), outer.getX(), outer.getY(),
                                             ext.bezel, outer.getX() + outer.getWidth() * 0.4f, outer.getBottom(), false));
    g.fillPath (frame);

    juce::Path edge;
    edge.addRoundedRectangle (outer.reduced (0.5f), 8.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x70ffffff), outer.getX(), outer.getY(),
                                             juce::Colour (0x00ffffff), outer.getX() + 40.0f, outer.getY() + 40.0f, false));
    g.strokePath (edge, juce::PathStrokeType (1.0f));

    const auto opening = screenArea();
    juce::Path chamfer;
    chamfer.addRoundedRectangle (opening.expanded (2.5f), 5.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff070306), opening.getX(), opening.getY(),
                                             ext.bezel.brighter (1.2f), opening.getRight(), opening.getBottom(), false));
    g.fillPath (chamfer);
}

void EchoMap::renderScreen (juce::Graphics& g, juce::Rectangle<float>) const
{
    const auto& ext = Theme::extras();
    const auto screen = screenArea();
    g.setGradientFill (juce::ColourGradient (ext.screenTop, 0.0f, screen.getY(), ext.screenBottom, 0.0f, screen.getBottom(), false));
    g.fillRect (screen);

    juce::ColourGradient bloom (ext.spike.withAlpha (0.06f), screen.getX() + screen.getWidth() * 0.35f, screen.getCentreY(),
                                ext.spike.withAlpha (0.0f), screen.getRight(), screen.getBottom() + 20.0f, true);
    g.setGradientFill (bloom);
    g.fillRect (screen);

    // Level grid every 12 dB in both lanes.
    for (int lane = 0; lane < 2; ++lane)
    {
        const auto a = laneArea (lane);
        g.setColour (ext.screenGrid);
        for (float db = -12.0f; db > floorDb + 1.0f; db -= 12.0f)
            g.drawHorizontalLine (juce::roundToInt (a.getBottom() - a.getHeight() * (db - floorDb) / -floorDb), a.getX(), a.getRight());
        g.setColour (ext.screenGrid.withMultipliedAlpha (1.8f));
        g.drawHorizontalLine (juce::roundToInt (a.getBottom()), a.getX(), a.getRight());
    }

    // Time grid: beats when synced (every beat, bars brighter), else round milliseconds.
    const auto top = laneArea (0).getY(), bottom = laneArea (1).getBottom();
    const auto labelFont = drawing::tracked (fonts->label (9.0f), 0.02f);
    g.setFont (labelFont);
    auto drawTick = [&] (float ms, bool strong, const juce::String& label)
    {
        const auto x = xForTime (ms);
        g.setColour (strong ? ext.screenGridBeat : ext.screenGrid);
        g.drawVerticalLine (juce::roundToInt (x), top, bottom);
        if (label.isNotEmpty())
        {
            const auto tw = juce::GlyphArrangement::getStringWidth (labelFont, label) + 2.0f;
            auto box = juce::Rectangle<float> (tw, 11.0f).withCentre ({ x, bottom + 8.0f }).constrainedWithin (screen.reduced (3.0f, 0.0f));
            g.setColour (ext.screenTextDim);
            g.drawText (label, box, juce::Justification::centred, false);
        }
    };

    if (shape.synced && shape.bpm > 0.0)
    {
        const auto beat = (float) (60000.0 / shape.bpm);
        const auto beats = (int) std::round (span / beat);
        const auto every = beats > 8 ? 4 : 1;
        for (int b = 0; b <= beats; ++b)
            drawTick ((float) b * beat, b % 4 == 0, b % every == 0 ? juce::String (b) : juce::String());
    }
    else
    {
        const auto step = span > 6000.0f ? 1000.0f : span > 2500.0f ? 500.0f : span > 1000.0f ? 250.0f : 100.0f;
        for (float t = 0.0f; t <= span + 0.5f; t += step)
            drawTick (t, std::fmod (t, step * 4.0f) < 0.5f, std::fmod (t, step * 2.0f) < 0.5f ? msText (t) : juce::String());
    }

    // Header line: machine, time and feedback; the lane names.
    const auto headerFont = drawing::tracked (fonts->legend (9.5f), 0.12f);
    g.setFont (headerFont);
    g.setColour (ext.screenText);
    const auto head = screen.withHeight (headerHeight).reduced (8.0f, 0.0f);
    auto title = juce::String (info (shape.machine).name).toUpperCase() + "  " + msText (shape.timeMs[0]);
    if (shape.plan.mode == Mode::dual)
        title << " / " << msText (shape.timeMs[1]);
    if (shape.foldedOctaves > 0)
        title << juce::String::fromUTF8 (" \xc2\xb7 folded");
    g.drawText (title, head, juce::Justification::centredLeft, false);
    g.setColour (shape.feedback > 1.0f ? ext.screenText : ext.screenTextDim);
    g.drawText ("FB " + juce::String (juce::roundToInt (shape.feedback * 100.0f)) + (shape.feedback > 1.0f ? " % RUN" : " %"),
                head, juce::Justification::centredRight, false);

    g.setColour (ext.screenTextDim);
    g.setFont (drawing::tracked (fonts->legend (8.0f), 0.2f));
    g.drawText ("L", laneArea (0).withWidth (12.0f).translated (-9.0f, 0.0f).withTrimmedTop (2.0f).withHeight (10.0f), juce::Justification::centred, false);
    g.drawText ("R", laneArea (1).withWidth (12.0f).translated (-9.0f, 0.0f).withTrimmedTop (2.0f).withHeight (10.0f), juce::Justification::centred, false);
}

void EchoMap::renderEchoes (juce::Graphics& g, juce::Rectangle<float>) const
{
    const auto& ext = Theme::extras();
    const std::array<juce::Rectangle<float>, 2> lanes { laneArea (0), laneArea (1) };

    // The dry note at 0, then every repeat, on the side(s) it plays.
    for (int lane = 0; lane < 2; ++lane)
    {
        const auto& a = lanes[(size_t) lane];
        g.setColour (ext.spikeDim);
        g.fillRect (juce::Rectangle<float> (1.5f, a.getHeight()).withPosition (xForTime (0.0f) - 0.75f, a.getY()));
    }

    for (const auto& e : events)
        for (int lane = 0; lane < 2; ++lane)
        {
            const auto level = lane == 0 ? e.left : e.right;
            if (level < 0.004f)
                continue;
            const auto& a = lanes[(size_t) lane];
            const auto h = levelHeight (level, a);
            const auto x = xForTime (e.timeMs);
            const auto colour = lane == 0 ? ext.spike : ext.spikeRight;
            g.setColour (colour.withMultipliedAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (5.0f, h).withPosition (x - 2.5f, a.getBottom() - h));
            g.setColour (colour);
            g.fillRect (juce::Rectangle<float> (2.0f, h).withPosition (x - 1.0f, a.getBottom() - h));
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ x, a.getBottom() - h }));
        }

    // Pattern: the steps' levels as editable bars in the first pattern.
    if (shape.plan.mode == Mode::pattern)
        for (int k = 0; k < std::clamp (shape.plan.steps, 1, dsp::maxPatternSteps); ++k)
        {
            const auto area = stepArea (k);
            const auto level = std::clamp (shape.plan.stepLevels[(size_t) k], 0.0f, 1.0f);
            const auto bar = area.withTop (area.getBottom() - area.getHeight() * level);
            g.setColour (ext.stepFill);
            g.fillRoundedRectangle (bar.reduced (2.0f, 0.0f), 2.0f);
            g.setColour (ext.stepEdge);
            g.drawHorizontalLine (juce::roundToInt (bar.getY()), bar.getX() + 1.0f, bar.getRight() - 1.0f);
        }
}

void EchoMap::renderGlass (juce::Graphics& g, juce::Rectangle<float>) const
{
    const auto area = screenArea();
    juce::Path screenPath;
    screenPath.addRectangle (area);
    melatonin::InnerShadow recess ({
        { juce::Colour (0xc0000000), 10, { 2, 4 }, 0 },
        { juce::Colour (0x70000000), 2, { 0, 1 }, 0 },
    });
    recess.render (g, screenPath);

    juce::ColourGradient sheen (juce::Colour (0x1cffffff), area.getX(), area.getY(), juce::Colour (0x00ffffff),
                                area.getX() + area.getWidth() * 0.45f, area.getY() + area.getHeight() * 0.65f, false);
    sheen.addColour (0.45, juce::Colour (0x0affffff));
    g.setGradientFill (sheen);
    g.fillRect (area);
}

//==============================================================================
void EchoMap::mouseDown (const juce::MouseEvent& e)
{
    if (shape.plan.mode != Mode::pattern)
        return;
    draggingStep = stepAt (e.position);
    if (draggingStep >= 0)
    {
        if (onStepGestureStart != nullptr)
            onStepGestureStart (draggingStep);
        mouseDrag (e);
    }
}

void EchoMap::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingStep < 0 || onStepLevel == nullptr)
        return;
    const auto area = stepArea (draggingStep);
    const auto level = std::clamp ((area.getBottom() - e.position.y) / area.getHeight(), 0.0f, 1.0f);
    onStepLevel (draggingStep, level);
}

void EchoMap::mouseUp (const juce::MouseEvent&)
{
    if (draggingStep >= 0 && onStepGestureEnd != nullptr)
        onStepGestureEnd (draggingStep);
    draggingStep = -1;
}

void EchoMap::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (shape.plan.mode != Mode::pattern || onStepReset == nullptr)
        return;
    if (const auto k = stepAt (e.position); k >= 0)
        onStepReset (k);
}

} // namespace mulberry::ui
