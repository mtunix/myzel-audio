#include "ui/StudioTail.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <optional>

namespace cerulean::ui
{

namespace
{
    constexpr float topDb = 0.0f;
    constexpr float plotLeft = 44.0f, plotRight = 16.0f, plotTop = studio::tailHeadHeight, plotBottom = 24.0f;
    constexpr float headRowY = 22.0f, headRowHeight = 30.0f;   // the read-outs, level with the Decay value
    constexpr float handleRadius = 6.0f, handleHitRadius = 11.0f;
    constexpr float liveOffsetDb = 3.0f;   // RMS -> about the peak of a sustained note (as on the panel)

    juce::String formatTime (float s)
    {
        if (s <= 0.0f)
            return "0";

        if (s < 1.0f)
            return juce::String (juce::roundToInt (s * 1000.0f)) + " ms";

        const auto whole = std::abs (s - std::round (s)) < 0.001f;
        return juce::String (s, whole ? 0 : 1) + " s";
    }

    juce::String formatRt (float s)
    {
        return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms"
                        : juce::String (s, s < 10.0f ? 2 : 1) + " s";
    }

    float gridStep (float span)
    {
        constexpr float steps[] = { 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 15.0f };

        for (const auto s : steps)
            if (span / s <= 8.0f)
                return s;

        return 15.0f;
    }

    /** Line style per band: low dashed, mid solid, high dotted (so the bands never rely on colour alone). */
    void strokeBand (juce::Graphics& g, const juce::Path& path, tail::Band band, juce::Colour colour)
    {
        g.setColour (colour);

        if (band == tail::Band::mid)
        {
            g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }

        juce::Path dashed;
        const float longDash[] = { 6.0f, 4.0f };
        const float dots[] = { 1.5f, 3.0f };
        juce::PathStrokeType stroke (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        if (band == tail::Band::low)
            stroke.createDashedStroke (dashed, path, longDash, 2);
        else
            stroke.createDashedStroke (dashed, path, dots, 2);

        g.fillPath (dashed);
    }

    juce::Colour bandColour (tail::Band b)
    {
        const auto& p = Theme::studio();
        return b == tail::Band::low ? p.bandLow : b == tail::Band::high ? p.bandHigh : p.bandMid;
    }
}

StudioTail::StudioTail()
    : history ((size_t) historySize, tail::floorDb)
{
    setOpaque (false);
    setInterceptsMouseClicks (true, false);
    setTitle ("Tail graph");
    setTooltip ("The decay of the low, mid and high bands as set by Decay, Low End and Tone (target RT60 per band), "
                "with the pre-delay, the early reflections (Echo: the repeats) and the live wet level.");
}

void StudioTail::attachHandles (juce::RangedAudioParameter& decay, juce::RangedAudioParameter& preDelay, juce::UndoManager* um)
{
    decayParam = &decay;
    preDelayParam = &preDelay;
    decayAttachment = std::make_unique<juce::ParameterAttachment> (decay, [] (float) {}, um);
    preDelayAttachment = std::make_unique<juce::ParameterAttachment> (preDelay, [] (float) {}, um);
}

void StudioTail::setPreDelayDraggable (bool canDrag)
{
    if (! canDrag && dragged == Handle::preDelay)
    {
        preDelayAttachment->endGesture();
        dragged = Handle::none;
    }

    preDelayDraggable = canDrag;
    repaint();
}

void StudioTail::setHeadLayout (float readoutLeft, float readoutRight)
{
    headLeft = readoutLeft;
    headRight = readoutRight;
    staticLayer.invalidate();
    repaint();
}

void StudioTail::setShape (const TailShape& newShape)
{
    if (newShape == shape && staticLayer.isValid())
        return;

    shape = newShape;

    // The axis holds still under a dragged handle; it rescales on release.
    if (dragged == Handle::none)
        span = tail::spanSeconds (shape);

    shownColumn = -1;
    staticLayer.invalidate();
    setDescription (getReadoutText());
    repaint();
}

juce::String StudioTail::getReadoutText() const
{
    if (shape.freeze)
        return shape.layering ? "Frozen, layering" : "Frozen";

    return "Low " + formatRt (tail::bandRt60 (shape, tail::Band::low))
         + ", Mid " + formatRt (tail::bandRt60 (shape, tail::Band::mid))
         + ", High " + formatRt (tail::bandRt60 (shape, tail::Band::high));
}

juce::Rectangle<float> StudioTail::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (plotLeft).withTrimmedRight (plotRight)
                                     .withTrimmedTop (plotTop).withTrimmedBottom (plotBottom);
}

float StudioTail::xForTime (float t) const noexcept
{
    const auto plot = plotArea();
    return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, t / span);
}

float StudioTail::yForDb (float db) const noexcept
{
    const auto plot = plotArea();
    return juce::jmap (juce::jlimit (tail::floorDb, topDb, db), topDb, tail::floorDb, plot.getY(), plot.getBottom());
}

float StudioTail::historyAt (long long entry) const noexcept
{
    if (entry < 0 || entry >= written || entry < written - historySize)
        return tail::floorDb;

    return history[(size_t) (entry % historySize)];
}

void StudioTail::resized()
{
    staticLayer.invalidate();
}

//==============================================================================
void StudioTail::tick (float dt, float wetRms)
{
    const auto db = juce::jlimit (tail::floorDb, 6.0f, juce::Decibels::gainToDecibels (wetRms, -120.0f) + liveOffsetDb);
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

    const auto entriesPerColumn = span * historyRate / juce::jmax (1.0f, plotArea().getWidth());
    const auto column = (long long) std::floor ((double) written / (double) entriesPerColumn);

    if (column == shownColumn)
        return;

    shownColumn = column;

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
        repaint (plotArea().expanded (2.0f).toNearestInt());

    liveVisible = visible;
}

void StudioTail::fillHistoryForSnapshot (float peakDb, float holdSeconds)
{
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

    liveVisible = true;
    shownColumn = -1;
    repaint();
}

//==============================================================================
void StudioTail::paint (juce::Graphics& g)
{
    staticLayer.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float> a, float)
    {
        renderStatic (lg, a);
    });

    if (liveVisible)
        drawLive (g);

    drawHandles (g);
}

//==============================================================================
std::optional<juce::Point<float>> StudioTail::getDecayHandle() const
{
    if (decayParam == nullptr || shape.freeze || shape.bypassed)
        return std::nullopt;

    const auto t = shape.preDelayMs * 0.001f + std::max (0.05f, shape.decaySeconds);

    if (t > span)
        return std::nullopt;

    return juce::Point<float> (xForTime (t), plotArea().getBottom());
}

std::optional<juce::Point<float>> StudioTail::getPreDelayHandle() const
{
    if (preDelayParam == nullptr || ! preDelayDraggable || shape.bypassed)
        return std::nullopt;

    return juce::Point<float> (xForTime (shape.preDelayMs * 0.001f), plotArea().getBottom());
}

StudioTail::Handle StudioTail::handleAt (juce::Point<float> p) const
{
    // Decay first: it is the main control, and it is never under the pre-delay marker for long.
    if (const auto d = getDecayHandle(); d.has_value() && d->getDistanceFrom (p) <= handleHitRadius)
        return Handle::decay;

    if (const auto pre = getPreDelayHandle(); pre.has_value() && pre->getDistanceFrom (p) <= handleHitRadius)
        return Handle::preDelay;

    return Handle::none;
}

juce::ParameterAttachment* StudioTail::attachmentFor (Handle h) const
{
    return h == Handle::decay ? decayAttachment.get() : h == Handle::preDelay ? preDelayAttachment.get() : nullptr;
}

void StudioTail::setHovered (Handle h)
{
    if (hovered == h)
        return;

    hovered = h;
    setMouseCursor (h == Handle::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::LeftRightResizeCursor);
    repaint();
}

void StudioTail::mouseMove (const juce::MouseEvent& e) { setHovered (handleAt (e.position)); }
void StudioTail::mouseExit (const juce::MouseEvent&)   { if (dragged == Handle::none) setHovered (Handle::none); }

void StudioTail::mouseDown (const juce::MouseEvent& e)
{
    const auto h = handleAt (e.position);
    auto* att = attachmentFor (h);

    if (att == nullptr || e.mods.isPopupMenu())
        return;

    dragged = h;
    dragStartValue = h == Handle::decay ? shape.decaySeconds : shape.preDelayMs;
    dragStartTime = span * (e.position.x - plotArea().getX()) / plotArea().getWidth();
    att->beginGesture();
    repaint();
}

void StudioTail::mouseDrag (const juce::MouseEvent& e)
{
    auto* att = attachmentFor (dragged);

    if (att == nullptr)
        return;

    const auto t = span * (e.position.x - plotArea().getX()) / plotArea().getWidth();
    const auto delta = (t - dragStartTime) * (e.mods.isShiftDown() ? 0.1f : 1.0f);
    auto& param = dragged == Handle::decay ? *decayParam : *preDelayParam;
    const auto value = dragged == Handle::decay ? dragStartValue + delta : dragStartValue + delta * 1000.0f;
    att->setValueAsPartOfGesture (param.getNormalisableRange().snapToLegalValue (value));
}

void StudioTail::mouseUp (const juce::MouseEvent& e)
{
    if (auto* att = attachmentFor (dragged))
        att->endGesture();

    dragged = Handle::none;

    // Rescale the axis to the new decay.
    span = tail::spanSeconds (shape);
    shownColumn = -1;
    staticLayer.invalidate();
    setHovered (handleAt (e.position));
    repaint();
}

void StudioTail::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = handleAt (e.position);

    if (auto* att = attachmentFor (h))
    {
        auto& param = h == Handle::decay ? *decayParam : *preDelayParam;
        att->setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
    }
}

void StudioTail::drawHandles (juce::Graphics& g) const
{
    const auto& p = Theme::studio();

    auto draw = [&] (std::optional<juce::Point<float>> pos, Handle h, bool solid)
    {
        if (! pos.has_value())
            return;

        const auto active = hovered == h || dragged == h;
        const auto r = handleRadius + (active ? 1.0f : 0.0f);

        if (active)
        {
            g.setColour (p.accent.withAlpha (0.3f));
            g.fillEllipse (juce::Rectangle<float> (2.0f * (r + 5.0f), 2.0f * (r + 5.0f)).withCentre (*pos));
        }

        const auto dot = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (*pos);
        g.setColour (solid ? p.accent : p.inset);
        g.fillEllipse (dot);
        g.setColour (p.accent);
        g.drawEllipse (dot.reduced (0.75f), 1.5f);
    };

    draw (getPreDelayHandle(), Handle::preDelay, false);
    draw (getDecayHandle(), Handle::decay, true);
}

void StudioTail::renderStatic (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& p = Theme::studio();
    const auto plot = plotArea();

    // Read-outs, in the head of the card between the Decay value and Freeze.
    {
        // Right-aligned legend: swatch, band name and its RT60; FROZEN instead while frozen.
        auto x = headRight > 0.0f ? headRight : area.getRight() - studio::cardPad;
        const auto y = headRowY + headRowHeight * 0.5f;
        const auto nameFont = drawing::tracked (fonts->legend (studio::smallSize), 0.10f);
        const auto valueFont = fonts->label (studio::valueSize - 1.0f);

        auto item = [&] (const juce::String& name, const juce::String& value, std::optional<tail::Band> band, juce::Colour colour)
        {
            const auto vw = juce::GlyphArrangement::getStringWidth (valueFont, value);
            const auto nw = juce::GlyphArrangement::getStringWidth (nameFont, name);
            x -= vw;
            g.setFont (valueFont);
            g.setColour (p.text);
            g.drawText (value, juce::Rectangle<float> (x, headRowY, vw + 1.0f, headRowHeight), juce::Justification::centredLeft, false);
            x -= 6.0f + nw;
            g.setFont (nameFont);
            g.setColour (p.textDim);
            g.drawText (name, juce::Rectangle<float> (x, headRowY, nw + 1.0f, headRowHeight), juce::Justification::centredLeft, false);

            if (band.has_value())
            {
                x -= 20.0f;
                juce::Path swatch;
                swatch.startNewSubPath (x, y);
                swatch.lineTo (x + 16.0f, y);
                strokeBand (g, swatch, *band, colour);
            }

            x -= 14.0f;
        };

        auto badge = [&] (const juce::String& text, juce::Colour fill, juce::Colour edge, juce::Colour ink)
        {
            const auto font = drawing::tracked (fonts->legend (studio::smallSize), 0.14f);
            const auto w = juce::GlyphArrangement::getStringWidth (font, text) + 18.0f;
            const auto r = juce::Rectangle<float> (x - w, y - 9.0f, w, 18.0f);
            g.setColour (fill);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (edge);
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (ink);
            g.setFont (font);
            g.drawText (text, r, juce::Justification::centred, false);
            x = r.getX() - 18.0f;
        };

        if (shape.bypassed)
        {
            badge (juce::String::fromUTF8 ("BYPASSED \xc2\xb7 DRY ONLY"), p.cautionDeep, p.caution, p.caution.brighter (0.4f));
        }
        else if (shape.freeze)
        {
            badge (shape.layering ? "FROZEN + LAYER" : "FROZEN", p.accentDeep, p.accentDeep, juce::Colours::white);
        }
        else
        {
            item ("HIGH", formatRt (tail::bandRt60 (shape, tail::Band::high)), tail::Band::high, p.bandHigh);
            item ("MID", formatRt (tail::bandRt60 (shape, tail::Band::mid)), tail::Band::mid, p.bandMid);
            item ("LOW", formatRt (tail::bandRt60 (shape, tail::Band::low)), tail::Band::low, p.bandLow);
        }

        if (x - headLeft > 90.0f)
            item ("PRE", formatTime (shape.preDelayMs * 0.001f), std::nullopt, {});
    }

    // Plot ground.
    g.setColour (p.inset);
    g.fillRoundedRectangle (plot.expanded (1.0f), 3.0f);

    // dB grid.
    const auto smallFont = fonts->label (studio::smallSize - 0.5f);
    g.setFont (smallFont);

    for (auto db = 0; db >= (int) tail::floorDb; db -= 12)
    {
        const auto y = yForDb ((float) db);
        g.setColour (p.grid);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (p.gridText);
        g.drawText (db == 0 ? "0 dB" : juce::String (db), juce::Rectangle<float> (0.0f, y - 7.0f, plotLeft - 6.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    // Time grid.
    const auto step = gridStep (span);

    for (auto t = 0.0f; t <= span + 0.0001f; t += step)
    {
        const auto x = xForTime (t);
        g.setColour (p.grid);
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (p.gridText);
        const auto label = juce::Rectangle<float> (56.0f, 14.0f).withCentre ({ x, plot.getBottom() + 12.0f });
        const auto just = t < 0.0001f ? juce::Justification::centredLeft : juce::Justification::centred;
        g.drawText (formatTime (t), just == juce::Justification::centredLeft ? label.withX (x) : label, just, false);
    }

    g.reduceClipRegion (plot.toNearestInt().expanded (1));

    // Pre-delay.
    const auto t0 = shape.preDelayMs * 0.001f;

    if (xForTime (t0) - plot.getX() > 2.0f)
    {
        juce::Path pre;
        pre.startNewSubPath (xForTime (t0), plot.getY());
        pre.lineTo (xForTime (t0), plot.getBottom());
        juce::Path dashed;
        const float dash[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, pre, dash, 2);
        g.setColour (p.textFaint);
        g.fillPath (dashed);
    }

    // Early reflections (or Echo's repeats).
    {
        std::array<tail::Spike, 12> spikes {};
        const auto n = tail::spikes (shape, spikes);
        g.setColour (p.early);

        for (int i = 0; i < n; ++i)
        {
            const auto x = xForTime (spikes[(size_t) i].timeS);
            g.fillRect (juce::Rectangle<float> (x - 0.75f, yForDb (spikes[(size_t) i].levelDb), 1.5f, plot.getBottom() - yForDb (spikes[(size_t) i].levelDb)));
        }
    }

    // The three bands: high and low first, mid (the Decay setting) on top.
    const auto columns = juce::jmax (2, (int) plot.getWidth() / 2);

    for (const auto band : { tail::Band::high, tail::Band::low, tail::Band::mid })
    {
        juce::Path path;
        auto started = false;

        for (int i = 0; i <= columns; ++i)
        {
            const auto t = span * (float) i / (float) columns;

            if (t < t0)
                continue;

            const auto db = tail::levelDb (shape, band, t);
            const auto pt = juce::Point<float> (xForTime (t), yForDb (db));

            if (! started)
            {
                path.startNewSubPath (pt.x, plot.getBottom());
                path.lineTo (pt);
                started = true;
            }
            else
            {
                path.lineTo (pt);
            }

            if (db <= tail::floorDb + 0.01f && t > t0 + 0.01f)
                break;
        }

        if (band == tail::Band::mid)
        {
            // A soft fill under the mid band.
            auto fill = path;
            fill.lineTo (fill.getCurrentPosition().x, plot.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (p.bandMid.withAlpha (0.16f), 0.0f, plot.getY(),
                                                     p.bandMid.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
            g.fillPath (fill);
        }

        strokeBand (g, path, band, bandColour (band));
    }

    // Bypassed: the picture steps back (the badge in the head says why).
    if (shape.bypassed)
    {
        g.setColour (p.inset.withAlpha (0.7f));
        g.fillRect (plot.expanded (1.0f));
    }
}

void StudioTail::drawLive (juce::Graphics& g) const
{
    const auto& p = Theme::studio();
    const auto plot = plotArea();
    const auto columns = juce::jmax (1, (int) std::round (plot.getWidth()));
    const auto entriesPerColumn = (double) span * historyRate / (double) columns;
    const auto newest = (long long) std::floor ((double) written / entriesPerColumn);

    juce::Path area, line;
    const auto floorY = yForDb (tail::floorDb);
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
        const auto y = yForDb (value);
        area.lineTo (x, y);

        if (k == 0)
            line.startNewSubPath (x, y);
        else
            line.lineTo (x, y);
    }

    area.lineTo (plot.getX(), floorY);
    area.closeSubPath();

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (plot.toNearestInt());
    g.setGradientFill (juce::ColourGradient (p.liveFill, 0.0f, plot.getY(), p.liveFill.withMultipliedAlpha (0.1f), 0.0f, floorY, false));
    g.fillPath (area);
    g.setColour (p.liveLine);
    g.strokePath (line, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace cerulean::ui
