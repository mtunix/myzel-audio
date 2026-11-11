#include "ui/StudioMeters.h"

#include "plugin/PluginProcessor.h"

namespace cerulean::ui
{

namespace
{
    constexpr float readoutHeight = 16.0f, labelHeight = 16.0f;
    constexpr float scaleWidth = 28.0f, barWidth = 26.0f, barGap = 10.0f, reductionGap = 22.0f;
    constexpr float releaseSeconds = 0.3f, peakHoldSeconds = 1.5f, peakFallDbPerSecond = 15.0f;

    const char* const names[] = { "IN", "WET", "OUT", "DUCK" };

    juce::String dbText (float db)
    {
        if (db <= -99.0f)
            return juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e");   // minus infinity

        return (db > 0.05f ? "+" : "") + juce::String (db, 1);
    }
}

StudioMeters::StudioMeters (MeterValues& m)
    : meters (m)
{
    setTitle ("Meters");
    setTooltip ("Input, wet and output level (bar: RMS, line: peak hold, number: peak) and the ducker's gain reduction. "
                "Click to reset the peaks.");
}

void StudioMeters::setDuckEnabled (bool enabled)
{
    if (duckEnabled != enabled)
    {
        duckEnabled = enabled;
        scaleLayer.invalidate();
        repaint();
    }
}

void StudioMeters::setWetMuted (bool muted)
{
    if (wetMuted != muted)
    {
        wetMuted = muted;
        repaint();
    }
}

juce::Rectangle<float> StudioMeters::barArea (int index) const
{
    const auto top = readoutHeight + 4.0f;
    const auto height = (float) getHeight() - top - labelHeight - 2.0f;
    auto x = scaleWidth + 2.0f + (float) index * (barWidth + barGap);

    if (index == 3)
        x = scaleWidth + 2.0f + 3.0f * barWidth + 2.0f * barGap + reductionGap;

    return { x, top, barWidth, height };
}

float StudioMeters::yForDb (float db, juce::Rectangle<float> bar) const noexcept
{
    return juce::jmap (juce::jlimit (floorDb, topDb, db), topDb, floorDb, bar.getY(), bar.getBottom());
}

void StudioMeters::advance (Channel& c, float rms, float peak, float dt)
{
    const auto rmsDb = juce::Decibels::gainToDecibels (rms, -120.0f);
    const auto peakDb = juce::Decibels::gainToDecibels (peak, -120.0f);

    c.levelDb = rmsDb >= c.levelDb ? rmsDb : c.levelDb + (rmsDb - c.levelDb) * (1.0f - std::exp (-dt / releaseSeconds));

    if (peakDb >= c.peakDb)
    {
        c.peakDb = peakDb;
        c.holdSeconds = peakHoldSeconds;
    }
    else if ((c.holdSeconds -= dt) <= 0.0f)
    {
        c.peakDb = juce::jmax (-120.0f, c.peakDb - peakFallDbPerSecond * dt);
    }

    c.clipped = c.clipped || peakDb > 0.0f;
}

void StudioMeters::tick (float dt)
{
    advance (channels[0], meters.inputRms.load (std::memory_order_relaxed), meters.inputPeak.exchange (0.0f, std::memory_order_relaxed), dt);
    advance (channels[1], meters.wetRms.load (std::memory_order_relaxed), meters.wetPeak.exchange (0.0f, std::memory_order_relaxed), dt);
    advance (channels[2], meters.outputRms.load (std::memory_order_relaxed), meters.outputPeak.exchange (0.0f, std::memory_order_relaxed), dt);

    const auto gain = juce::jlimit (1.0e-6f, 1.0f, meters.duckGain.load (std::memory_order_relaxed));
    reductionDb = juce::jlimit (0.0f, reductionRangeDb, -juce::Decibels::gainToDecibels (gain));

    auto changed = std::abs (reductionDb - shownReduction) > 0.2f;

    for (auto& c : channels)
    {
        const auto level = juce::jmax (floorDb - 1.0f, c.levelDb), peak = juce::jmax (floorDb - 1.0f, c.peakDb);
        changed = changed || std::abs (level - c.shownLevel) > 0.25f || std::abs (peak - c.shownPeak) > 0.1f || c.clipped != c.shownClipped;
    }

    if (changed)
        repaint();
}

void StudioMeters::setLevelsForSnapshot (float inDb, float wetDb, float outDb, float grDb)
{
    const float levels[] = { inDb, wetDb, outDb };

    for (size_t i = 0; i < channels.size(); ++i)
    {
        channels[i].levelDb = levels[i];
        channels[i].peakDb = levels[i] + 4.5f;
        channels[i].clipped = channels[i].peakDb > 0.0f;
    }

    reductionDb = grDb;
    repaint();
}

juce::String StudioMeters::getReadoutText() const
{
    return "In " + dbText (channels[0].peakDb) + " dB, Wet " + dbText (channels[1].peakDb) + " dB, Out " + dbText (channels[2].peakDb)
         + " dB, Duck " + (duckEnabled ? juce::String (reductionDb, 1) + " dB" : juce::String ("off"));
}

void StudioMeters::resetPeaks()
{
    for (auto& c : channels)
    {
        c.peakDb = c.levelDb;
        c.clipped = false;
    }

    repaint();
}

void StudioMeters::mouseDown (const juce::MouseEvent&)
{
    resetPeaks();
}

void StudioMeters::resized()
{
    scaleLayer.invalidate();
}

//==============================================================================
void StudioMeters::renderScale (juce::Graphics& g) const
{
    const auto& p = Theme::studio();
    const auto bar = barArea (0);

    g.setFont (fonts->label (9.5f));

    for (const auto db : { 6, 0, -6, -12, -24, -36, -48, -60 })
    {
        const auto y = yForDb ((float) db, bar);
        g.setColour (p.gridText);
        g.drawText (db > 0 ? "+" + juce::String (db) : juce::String (db), juce::Rectangle<float> (0.0f, y - 6.0f, scaleWidth - 4.0f, 12.0f),
                    juce::Justification::centredRight, false);

        g.setColour (db == 0 ? p.textFaint : p.grid);
        g.drawHorizontalLine (juce::roundToInt (y), scaleWidth, barArea (2).getRight());
    }

    // Reduction scale (dB of gain reduction), to the right of its bar.
    const auto gr = barArea (3);

    for (const auto db : { 0, 6, 12, 18, 24 })
    {
        const auto y = juce::jmap ((float) db, 0.0f, reductionRangeDb, gr.getY(), gr.getBottom());
        g.setColour (p.grid);
        g.drawHorizontalLine (juce::roundToInt (y), gr.getX(), gr.getRight() + 3.0f);
        g.setColour (p.gridText.withMultipliedAlpha (duckEnabled ? 1.0f : 0.5f));
        g.drawText (juce::String (db), juce::Rectangle<float> (gr.getRight() + 3.0f, y - 6.0f, 20.0f, 12.0f), juce::Justification::centredLeft, false);
    }

    // Names under the bars.
    g.setFont (drawing::tracked (fonts->legend (9.5f), 0.12f));

    for (int i = 0; i < 4; ++i)
    {
        const auto b = barArea (i);
        g.setColour (p.textDim.withMultipliedAlpha (i == 3 && ! duckEnabled ? 0.5f : 1.0f));
        g.drawText (names[i], juce::Rectangle<float> (b.getCentreX() - 22.0f, b.getBottom() + 3.0f, 44.0f, labelHeight - 2.0f),
                    juce::Justification::centred, false);
    }
}

void StudioMeters::paint (juce::Graphics& g)
{
    const auto& p = Theme::studio();

    scaleLayer.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float>, float) { renderScale (lg); });

    const auto valueFont = fonts->label (10.0f);
    g.setFont (valueFont);

    for (size_t i = 0; i < channels.size(); ++i)
    {
        auto& c = channels[i];
        const auto bar = barArea ((int) i);

        g.setColour (p.meterBack);
        g.fillRoundedRectangle (bar, 2.0f);

        const auto levelY = yForDb (c.levelDb, bar);

        if (c.levelDb > floorDb)
        {
            // Ice up to -6 dBFS, amber above, red above 0.
            const auto fillArea = juce::Rectangle<float> (bar.getX(), levelY, bar.getWidth(), bar.getBottom() - levelY);
            juce::ColourGradient grad (p.meterClip, 0.0f, yForDb (topDb, bar), p.meterFill, 0.0f, bar.getBottom(), false);
            const auto proportionAt = [&] (float db) { return (yForDb (db, bar) - bar.getY()) / bar.getHeight(); };
            grad.addColour (proportionAt (0.0f), p.meterHot);
            grad.addColour (proportionAt (-6.0f), p.meterHot.interpolatedWith (p.meterFill, 0.6f));
            grad.addColour (proportionAt (-6.5f), p.meterFill);
            g.setGradientFill (grad);

            if (wetMuted && i == 1)
                g.setColour (p.marker);

            g.fillRect (fillArea.reduced (1.0f, 0.0f));
        }

        if (c.peakDb > floorDb)
        {
            g.setColour (c.peakDb > 0.0f ? p.meterClip : p.meterPeak);
            g.fillRect (juce::Rectangle<float> (bar.getX() + 1.0f, yForDb (c.peakDb, bar) - 1.0f, bar.getWidth() - 2.0f, 2.0f));
        }

        g.setColour (c.clipped ? p.meterClip : p.text);
        g.drawText (dbText (c.peakDb), juce::Rectangle<float> (bar.getCentreX() - 22.0f, 0.0f, 44.0f, readoutHeight),
                    juce::Justification::centred, false);

        c.shownLevel = juce::jmax (floorDb - 1.0f, c.levelDb);
        c.shownPeak = juce::jmax (floorDb - 1.0f, c.peakDb);
        c.shownClipped = c.clipped;
    }

    // Gain reduction grows down from the top.
    {
        const auto bar = barArea (3);
        const auto alpha = duckEnabled ? 1.0f : 0.4f;
        g.setColour (p.meterBack);
        g.fillRoundedRectangle (bar, 2.0f);

        if (duckEnabled && reductionDb > 0.05f)
        {
            const auto h = bar.getHeight() * reductionDb / reductionRangeDb;
            g.setColour (p.reduction);
            g.fillRect (juce::Rectangle<float> (bar.getX() + 1.0f, bar.getY(), bar.getWidth() - 2.0f, h));
        }

        g.setColour (p.text.withMultipliedAlpha (alpha));
        g.drawText (duckEnabled ? (reductionDb > 0.05f ? juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (reductionDb, 1) : juce::String ("0.0"))
                                : juce::String ("OFF"),
                    juce::Rectangle<float> (bar.getCentreX() - 22.0f, 0.0f, 44.0f, readoutHeight), juce::Justification::centred, false);
        shownReduction = reductionDb;
    }
}

} // namespace cerulean::ui
