#include "ui/TailDisplay.h"

#include "plugin/Parameters.h"

#include <melatonin_blur/melatonin_blur.h>

namespace cerulean::ui
{

namespace
{
    constexpr float topDb = 0.0f;
    constexpr float plotLeft = 12.0f, plotRight = 12.0f, plotTop = 30.0f, plotBottom = 20.0f;
    constexpr float liveOffsetDb = 3.0f;   // RMS -> about the peak of a sustained note

    float timeStep (float span) noexcept
    {
        constexpr float steps[] = { 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 15.0f };

        for (const auto s : steps)
            if (span / s <= 6.01f)
                return s;

        return 15.0f;
    }

    juce::String timeLabel (float t, float step)
    {
        if (step < 1.0f)
            return juce::String (t, step < 0.1f ? 2 : (step < 0.5f ? 2 : 1)).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");

        return juce::String (juce::roundToInt (t));
    }

    juce::String decayText (float s)
    {
        if (s < 1.0f)
            return juce::String (juce::roundToInt (s * 1000.0f)) + " ms";

        return juce::String (s, s < 10.0f ? 2 : 1) + " s";
    }
}

TailDisplay::TailDisplay()
    : history ((size_t) historySize, tail::floorDb)
{
    setInterceptsMouseClicks (true, false);
    setTitle ("Tail display");
    setTooltip ("Tail: the decay drawn from the settings (pre-delay gap, early reflections, low/mid/high band decay). "
                "The live wet level runs across it. Flat while frozen.");
    span = tail::spanSeconds (shape);
    updateDescription();
}

void TailDisplay::setShape (const TailShape& newShape)
{
    if (newShape == shape)
        return;

    shape = newShape;

    // The time axis ignores Freeze, so the flat line sits where the tail was.
    const auto newSpan = tail::spanSeconds (shape);

    if (std::abs (newSpan - span) > 1.0e-4f)
    {
        span = newSpan;
        screenLayer.invalidate();
        shownColumn = -1;
    }

    envelopeLayer.invalidate();
    updateDescription();
    repaint (screenArea().toNearestInt());
}

void TailDisplay::updateDescription()
{
    const auto name = params::algorithmNames()[juce::jlimit (0, numAlgorithms - 1, (int) shape.algorithm)];
    setDescription (name + ", decay " + decayText (shape.decaySeconds) + ", pre-delay "
                    + juce::String (juce::roundToInt (shape.preDelayMs)) + " ms" + (shape.freeze ? (shape.layering ? ", frozen, layering" : ", frozen") : "")
                    + (shape.bypassed ? ", bypassed" : ""));
}

//==============================================================================
float TailDisplay::historyAt (long long entry) const noexcept
{
    if (entry < 0 || entry >= written || entry < written - historySize)
        return tail::floorDb;

    return history[(size_t) (entry % historySize)];
}

long long TailDisplay::currentColumn() const noexcept
{
    const auto plotWidth = juce::jmax (1.0f, plotArea().getWidth());
    const auto entriesPerColumn = span * historyRate / plotWidth;
    return (long long) std::floor ((double) written / (double) entriesPerColumn);
}

void TailDisplay::tick (float dt, float wetRms)
{

    const auto db = juce::jlimit (tail::floorDb, 6.0f, juce::Decibels::gainToDecibels (wetRms, -120.0f) + liveOffsetDb);

    // Instant attack, ~40 ms release: steady without hiding the decay slope.
    smoothedDb = db >= smoothedDb ? db : smoothedDb + (db - smoothedDb) * (1.0f - std::exp (-dt / 0.04f));

    pending += dt;
    const auto period = 1.0f / historyRate;
    auto pushed = 0;

    while (pending >= period && pushed < 16)
    {
        history[(size_t) (written % historySize)] = smoothedDb;
        ++written;
        pending -= period;
        ++pushed;
    }

    pending = juce::jmin (pending, period);

    const auto column = currentColumn();

    if (column == shownColumn)
        return;

    shownColumn = column;

    // Anything above the floor still inside the window?
    auto visible = false;
    const auto window = (long long) std::ceil (span * historyRate) + 2;

    for (auto e = written - 1; e >= juce::jmax (0LL, written - window); --e)
    {
        if (historyAt (e) > tail::floorDb + 0.5f)
        {
            visible = true;
            break;
        }
    }

    if (visible || liveVisible)
        repaint (screenArea().toNearestInt());

    liveVisible = visible;
}

void TailDisplay::fillHistoryForSnapshot (float peakDb, float holdSeconds)
{
    // A played phrase that stopped a little while ago: a few notes, then the real tail decays.
    const auto total = (int) (span * historyRate);
    const auto rt = juce::jmax (0.1f, tail::bandRt60 (shape, tail::Band::mid));
    juce::Random rng (11);

    for (int i = 0; i < total; ++i)
    {
        const auto t = (float) i / historyRate;
        const auto playEnd = span * 0.38f;
        float db;

        if (shape.freeze)
            db = peakDb - 2.0f + 0.6f * std::sin (t * 7.0f);
        else if (t < holdSeconds)
            db = tail::floorDb;
        else if (t < playEnd)
            db = peakDb - 4.0f * std::abs (std::sin ((t - holdSeconds) * 5.0f)) - 1.5f * rng.nextFloat();
        else
            db = peakDb - 2.0f - 60.0f * (t - playEnd) / rt - 0.8f * rng.nextFloat();

        history[(size_t) (written % historySize)] = juce::jmax (tail::floorDb, db);
        ++written;
    }

    smoothedDb = history[(size_t) ((written - 1) % historySize)];
    shownColumn = -1;
    liveVisible = true;
    repaint();
}

//==============================================================================
juce::Rectangle<float> TailDisplay::screenArea() const
{
    return getLocalBounds().toFloat().reduced (bezelWidth);
}

juce::Rectangle<float> TailDisplay::plotArea() const
{
    const auto s = screenArea().withZeroOrigin();
    return { s.getX() + plotLeft, s.getY() + plotTop, s.getWidth() - plotLeft - plotRight, s.getHeight() - plotTop - plotBottom };
}

float TailDisplay::xForTime (float t, juce::Rectangle<float> plot) const noexcept
{
    return plot.getX() + plot.getWidth() * t / span;
}

float TailDisplay::yForDb (float db, juce::Rectangle<float> plot) const noexcept
{
    const auto x = (topDb - db) / (topDb - tail::floorDb);
    return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, x);
}

void TailDisplay::resized()
{
    bezelLayer.invalidate();
    screenLayer.invalidate();
    envelopeLayer.invalidate();
    glassLayer.invalidate();
    shownColumn = -1;
}

void TailDisplay::paint (juce::Graphics& g)
{
    const auto screen = screenArea();

    bezelLayer.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderBezel (lg, a); });

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (screen.toNearestInt());

    screenLayer.draw (g, screen, [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderScreen (lg, a); });

    if (liveVisible)
        drawLive (g, screen);

    // Bypassed: the envelope steps back behind the stamp.
    envelopeLayer.draw (g, screen, [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderEnvelope (lg, a); },
                        shape.bypassed ? 0.28f : 1.0f);

    if (shape.bypassed)
        drawBypassStamp (g, screen);

    glassLayer.draw (g, screen, [this] (juce::Graphics& lg, juce::Rectangle<float> a, float) { renderGlass (lg, a); });
}

void TailDisplay::drawLive (juce::Graphics& g, juce::Rectangle<float> screen) const
{
    const auto& pal = Theme::palette();
    const auto plot = plotArea().translated (screen.getX(), screen.getY());
    const auto columns = juce::jmax (1, (int) std::round (plot.getWidth()));
    const auto entriesPerColumn = (double) span * historyRate / (double) columns;
    const auto newest = currentColumn();

    juce::Path area, line;
    const auto floorY = yForDb (tail::floorDb, plot);
    area.startNewSubPath (plot.getRight(), floorY);

    for (int k = 0; k <= columns; ++k)
    {
        const auto column = newest - k;
        const auto first = (long long) std::floor ((double) column * entriesPerColumn);
        const auto last = juce::jmax (first, (long long) std::floor ((double) (column + 1) * entriesPerColumn) - 1);
        auto value = tail::floorDb;

        for (auto e = first; e <= last; ++e)
            value = juce::jmax (value, historyAt (e));

        const auto x = plot.getRight() - (float) k;
        const auto y = yForDb (value, plot);
        area.lineTo (x, y);

        if (k == 0)
            line.startNewSubPath (x, y);
        else
            line.lineTo (x, y);
    }

    area.lineTo (plot.getX() - 1.0f, floorY);
    area.closeSubPath();

    g.setGradientFill (juce::ColourGradient (pal.liveFill, 0.0f, plot.getY(),
                                             pal.liveFill.withMultipliedAlpha (0.15f), 0.0f, floorY, false));
    g.fillPath (area);
    g.setColour (pal.liveLine.withAlpha (0.7f));
    g.strokePath (line, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void TailDisplay::drawBypassStamp (juce::Graphics& g, juce::Rectangle<float> screen) const
{
    const auto& pal = Theme::palette();
    const auto alpha = 1.0f;
    const auto font = drawing::tracked (fonts->legend (17.0f), 0.30f);
    const auto text = juce::String ("BYPASSED");
    const auto w = juce::GlyphArrangement::getStringWidth (font, text) + 26.0f;
    const auto box = juce::Rectangle<float> (w, 32.0f).withCentre (screen.getCentre().translated (0.0f, -4.0f));

    g.setColour (pal.screenBottom.withAlpha (0.85f * alpha));
    g.fillRoundedRectangle (box, 4.0f);
    drawing::fillGlow (g, box.getCentre(), w * 0.6f, pal.caution, 0.12f * alpha);
    g.setColour (pal.caution.withAlpha (alpha));
    g.drawRoundedRectangle (box, 4.0f, 1.6f);
    g.setFont (font);
    g.drawText (text, box.translated (2.0f, 0.0f), juce::Justification::centred, false);

    g.setColour (pal.caution.withMultipliedAlpha (0.75f * alpha));
    g.setFont (fonts->label (10.5f));
    g.drawText (shape.trailsRinging ? juce::String::fromUTF8 ("dry only \xc2\xb7 the tail rings out") : juce::String ("dry only"),
                box.withY (box.getBottom() + 6.0f).withHeight (14.0f).expanded (60.0f, 0.0f), juce::Justification::centred, false);
}

//==============================================================================
void TailDisplay::renderBezel (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& pal = Theme::palette();
    const auto outer = area.reduced (1.0f);

    juce::Path frame;
    frame.addRoundedRectangle (outer, 9.0f);
    drawing::drawContactShadow (g, frame, 4.0f, 0.8f);

    g.setGradientFill (juce::ColourGradient (pal.bezel.brighter (0.6f), outer.getX(), outer.getY(),
                                             pal.bezel, outer.getX() + outer.getWidth() * 0.4f, outer.getBottom(), false));
    g.fillPath (frame);

    juce::Path edge;
    edge.addRoundedRectangle (outer.reduced (0.5f), 8.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x70ffffff), outer.getX(), outer.getY(),
                                             juce::Colour (0x00ffffff), outer.getX() + 40.0f, outer.getY() + 40.0f, false));
    g.strokePath (edge, juce::PathStrokeType (1.0f));

    // Inner chamfer down to the glass: dark on the upper-left (facing away from the light).
    const auto opening = screenArea();
    juce::Path chamfer;
    chamfer.addRoundedRectangle (opening.expanded (2.5f), 5.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff04070a), opening.getX(), opening.getY(),
                                             juce::Colour (0xff46505c), opening.getRight(), opening.getBottom(), false));
    g.fillPath (chamfer);
}

void TailDisplay::renderScreen (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& pal = Theme::palette();
    const auto w = area.getWidth(), h = area.getHeight();
    const auto plot = plotArea();

    g.setGradientFill (juce::ColourGradient (pal.screenTop, 0.0f, 0.0f, pal.screenBottom, 0.0f, h, false));
    g.fillRect (area);

    // A faint backlight bloom behind the plot.
    juce::ColourGradient bloom (pal.traceMid.withAlpha (0.07f), w * 0.35f, h * 0.45f,
                                pal.traceMid.withAlpha (0.0f), w * 0.95f, h * 1.1f, true);
    g.setGradientFill (bloom);
    g.fillRect (area);

    // Grid: every 12 dB, and the time steps.
    g.setColour (pal.screenGrid);

    for (float db = -12.0f; db > tail::floorDb + 1.0f; db -= 12.0f)
        g.drawHorizontalLine (juce::roundToInt (yForDb (db, plot)), plot.getX(), plot.getRight());

    const auto step = timeStep (span);
    const auto labelFont = drawing::tracked (fonts->label (9.5f), 0.02f);
    g.setFont (labelFont);

    for (int i = 0;; ++i)
    {
        const auto t = (float) i * step;

        if (t > span + 1.0e-4f)
            break;

        const auto x = xForTime (t, plot);
        g.setColour (pal.screenGrid);
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());

        auto text = timeLabel (t, step);

        if (t + step > span + 1.0e-4f)
            text << " s";

        const auto tw = juce::GlyphArrangement::getStringWidth (labelFont, text) + 2.0f;
        auto box = juce::Rectangle<float> (tw, 12.0f).withCentre ({ x, plot.getBottom() + 10.0f });
        box = box.constrainedWithin (area.reduced (3.0f, 0.0f));
        g.setColour (pal.screenTextDim);
        g.drawText (text, box, juce::Justification::centred, false);
    }

    // Floor line.
    g.setColour (pal.screenGrid.withMultipliedAlpha (1.8f));
    g.drawHorizontalLine (juce::roundToInt (plot.getBottom()), plot.getX(), plot.getRight());
}

void TailDisplay::renderEnvelope (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& pal = Theme::palette();
    const auto plot = plotArea();
    const auto floorY = yForDb (tail::floorDb, plot);
    const auto t0 = shape.preDelayMs * 0.001f;

    // Header: algorithm on the left, decay (or FREEZE) on the right.
    {
        const auto name = params::algorithmNames()[juce::jlimit (0, numAlgorithms - 1, (int) shape.algorithm)].toUpperCase();
        const auto nameFont = drawing::tracked (fonts->legend (10.5f), 0.24f);
        g.setColour (pal.screenText);
        g.setFont (nameFont);
        g.drawText (name, juce::Rectangle<float> (plot.getX(), 8.0f, 120.0f, 14.0f), juce::Justification::centredLeft, false);

        // Pre-delay, dimmer, after the name.
        const auto nameWidth = juce::GlyphArrangement::getStringWidth (nameFont, name);
        g.setColour (pal.screenTextDim);
        g.setFont (drawing::tracked (fonts->label (10.5f), 0.02f));
        g.drawText ("PRE " + juce::String (juce::roundToInt (shape.preDelayMs)) + " ms",
                    juce::Rectangle<float> (plot.getX() + nameWidth + 10.0f, 8.0f, 90.0f, 14.0f), juce::Justification::centredLeft, false);

        const auto headerRight = juce::Rectangle<float> (area.getRight() - plotRight - 140.0f, 8.0f, 140.0f, 14.0f);

        if (shape.freeze)
        {
            const auto font = drawing::tracked (fonts->legend (10.5f), 0.24f);
            const juce::String label (shape.layering ? "FREEZE + LAYER" : "FREEZE");
            const auto tw = juce::GlyphArrangement::getStringWidth (font, label);
            drawing::fillGlow (g, { headerRight.getRight() - tw * 0.5f, headerRight.getCentreY() }, tw * 0.9f, pal.ice, 0.22f);
            g.setColour (pal.iceCore);
            g.setFont (font);
            g.drawText (label, headerRight, juce::Justification::centredRight, false);
        }
        else
        {
            g.setColour (pal.screenText);
            g.setFont (drawing::tracked (fonts->label (12.0f), 0.02f));
            g.drawText (decayText (shape.decaySeconds), headerRight, juce::Justification::centredRight, false);
        }
    }

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.expanded (1.0f, plotTop - 4.0f).withBottom (floorY + 1.0f).toNearestInt());

    const auto xStart = juce::jmax (plot.getX(), xForTime (t0, plot));
    const auto stepPx = 1.5f;
    auto timeAt = [&] (float x) { return (x - plot.getX()) / plot.getWidth() * span; };

    // Early reflections (or echo repeats), behind the tail.
    {
        std::array<tail::Spike, 12> spikes {};
        const auto n = tail::spikes (shape, spikes);
        const auto isEcho = shape.algorithm == Algorithm::echo;
        auto lastX = xStart - 3.0f;

        for (int i = 0; i < n; ++i)
        {
            // A picture, not a measurement: the early cluster keeps a readable spacing even
            // when the time axis spans many seconds.
            auto x = xForTime (spikes[(size_t) i].timeS, plot);

            if (! isEcho)
                x = juce::jmax (x, lastX + 3.5f);

            lastX = x;

            if (x > plot.getRight())
                break;

            const auto y = yForDb (spikes[(size_t) i].levelDb, plot);
            const auto alpha = (shape.freeze ? 0.3f : 1.0f) * (isEcho ? 0.85f : 0.55f);
            g.setColour (pal.traceMid.withAlpha (alpha));
            g.drawLine (x, floorY, x, y, isEcho ? 1.4f : 1.0f);
            g.setColour (pal.traceMid.withAlpha (juce::jmin (1.0f, alpha * 1.4f)));
            g.fillEllipse (juce::Rectangle<float> (2.4f, 2.4f).withCentre ({ x, y }));
        }
    }

    // Pre-delay: a dotted run along the floor from the dry hit to where the reverb starts.
    if (t0 > 0.0f && xStart > plot.getX() + 3.0f)
    {
        g.setColour (pal.screenText.withAlpha (0.45f));

        for (auto x = plot.getX(); x < xStart - 1.0f; x += 4.0f)
            g.fillRect (juce::Rectangle<float> (x, floorY - 2.0f, 1.6f, 1.2f));

        g.fillRect (juce::Rectangle<float> (plot.getX(), floorY - 6.0f, 1.2f, 6.0f));
    }

    auto curve = [&] (tail::Band band)
    {
        juce::Path p;
        auto first = true;

        for (auto x = xStart; x <= plot.getRight() + stepPx; x += stepPx)
        {
            const auto y = yForDb (tail::levelDb (shape, band, juce::jmax (t0, timeAt (x))), plot);

            if (first)
            {
                p.startNewSubPath (x, floorY);
                first = false;
            }

            p.lineTo (x, y);
        }

        return p;
    };

    if (shape.freeze)
    {
        // Held: one flat line, lit.
        const auto y = yForDb (tail::tailStartDb, plot);
        juce::Path flat;
        flat.startNewSubPath (xStart, floorY);
        flat.lineTo (xStart, y);
        flat.lineTo (plot.getRight() + 2.0f, y);

        juce::Path under (flat);
        under.lineTo (plot.getRight() + 2.0f, floorY);
        under.closeSubPath();
        g.setGradientFill (juce::ColourGradient (pal.ice.withAlpha (0.20f), 0.0f, y, pal.ice.withAlpha (0.0f), 0.0f, floorY, false));
        g.fillPath (under);

        g.setColour (pal.ice.withAlpha (0.22f));
        g.strokePath (flat, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (pal.iceCore);
        g.strokePath (flat, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }

    // Band fan: the region between the low and high band decays.
    {
        juce::Path fan;
        auto first = true;
        std::vector<juce::Point<float>> lower;

        for (auto x = xStart; x <= plot.getRight() + stepPx; x += stepPx)
        {
            const auto t = juce::jmax (t0, timeAt (x));
            const auto lo = tail::levelDb (shape, tail::Band::low, t);
            const auto hi = tail::levelDb (shape, tail::Band::high, t);
            const auto yTop = yForDb (juce::jmax (lo, hi), plot);
            lower.push_back ({ x, yForDb (juce::jmin (lo, hi), plot) });

            if (first)
            {
                fan.startNewSubPath (x, yTop);
                first = false;
            }
            else
            {
                fan.lineTo (x, yTop);
            }
        }

        for (auto it = lower.rbegin(); it != lower.rend(); ++it)
            fan.lineTo (*it);

        fan.closeSubPath();
        g.setGradientFill (juce::ColourGradient (pal.traceBand.withMultipliedAlpha (0.42f), plot.getX(), 0.0f,
                                                 pal.traceBand.withMultipliedAlpha (0.16f), plot.getRight(), 0.0f, false));
        g.fillPath (fan);

        g.setColour (pal.traceBand.withMultipliedAlpha (0.8f));
        g.strokePath (curve (tail::Band::low), juce::PathStrokeType (0.8f));
        g.strokePath (curve (tail::Band::high), juce::PathStrokeType (0.8f));
    }

    // Mid band: the main line, with a soft glow and a faint fill beneath.
    {
        const auto mid = curve (tail::Band::mid);

        juce::Path under (mid);
        under.lineTo (plot.getRight() + stepPx, floorY);
        under.closeSubPath();
        g.setGradientFill (juce::ColourGradient (pal.traceMid.withAlpha (0.16f), 0.0f, yForDb (tail::tailStartDb, plot),
                                                 pal.traceMid.withAlpha (0.0f), 0.0f, floorY, false));
        g.fillPath (under);

        g.setColour (pal.traceMid.withAlpha (0.18f));
        g.strokePath (mid, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (pal.traceMid);
        g.strokePath (mid, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void TailDisplay::renderGlass (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto w = area.getWidth(), h = area.getHeight();

    // Recess shadow cast by the bezel onto the screen.
    juce::Path screenPath;
    screenPath.addRectangle (area);
    melatonin::InnerShadow recess ({
        { juce::Colour (0xc0000000), 10, { 2, 4 }, 0 },
        { juce::Colour (0x70000000), 2, { 0, 1 }, 0 },
    });
    recess.render (g, screenPath);

    // Glass reflection: a broad soft diagonal sheen from the top-left (no hard edges).
    juce::ColourGradient sheen (juce::Colour (0x1cffffff), 0.0f, 0.0f, juce::Colour (0x00ffffff), w * 0.45f, h * 0.65f, false);
    sheen.addColour (0.45, juce::Colour (0x0affffff));
    g.setGradientFill (sheen);
    g.fillRect (area);

    // A thin bright edge line where the glass meets the bezel (bottom-right catches light).
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x00ffffff), w * 0.5f, h * 0.5f,
                                             juce::Colour (0x26ffffff), w, h, false));
    g.drawRect (area, 1.0f);
}

} // namespace cerulean::ui
