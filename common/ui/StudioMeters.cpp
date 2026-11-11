#include "common/ui/StudioMeters.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace myzel::ui
{

namespace
{
    constexpr float readoutHeight = 16.0f, labelHeight = 16.0f;
    constexpr float scaleWidth = 28.0f, barWidth = 26.0f, barGap = 10.0f, reductionGap = 22.0f;
    constexpr float releaseSeconds = 0.3f, peakHoldSeconds = 1.5f, peakFallDbPerSecond = 15.0f;

    juce::String dbText (float db)
    {
        if (db <= -99.0f)
            return juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e");   // minus infinity

        return (db > 0.05f ? "+" : "") + juce::String (db, 1);
    }
}

StudioMeters::StudioMeters (std::vector<Channel> channels, std::optional<Reduction> r, const juce::String& tooltip)
    : reduction (std::move (r))
{
    for (auto& c : channels)
        levels.push_back ({ std::move (c) });

    setTitle ("Meters");
    setTooltip (tooltip);
}

void StudioMeters::setReductionEnabled (bool enabled)
{
    if (reductionEnabled != enabled)
    {
        reductionEnabled = enabled;
        scaleLayer.invalidate();
        repaint();
    }
}

void StudioMeters::setChannelMuted (int index, bool muted)
{
    if (juce::isPositiveAndBelow (index, numBars()) && levels[(size_t) index].muted != muted)
    {
        levels[(size_t) index].muted = muted;
        repaint();
    }
}

juce::Rectangle<float> StudioMeters::barArea (int index) const
{
    const auto top = readoutHeight + 4.0f;
    const auto height = (float) getHeight() - top - labelHeight - 2.0f;
    const auto x = scaleWidth + 2.0f + (float) index * (barWidth + barGap);
    return { x, top, barWidth, height };
}

juce::Rectangle<float> StudioMeters::reductionArea() const
{
    const auto last = barArea (numBars() - 1);
    return last.withX (last.getRight() + reductionGap);
}

float StudioMeters::yForDb (float db, juce::Rectangle<float> bar) const noexcept
{
    return juce::jmap (juce::jlimit (floorDb, topDb, db), topDb, floorDb, bar.getY(), bar.getBottom());
}

void StudioMeters::advance (Level& c, float rms, float peak, float dt)
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
    for (auto& c : levels)
    {
        const auto rms = c.source.rms != nullptr ? c.source.rms() : 0.0f;
        advance (c, rms, c.source.takePeak != nullptr ? c.source.takePeak() : rms, dt);
    }

    if (reduction.has_value() && reduction->gain != nullptr)
    {
        const auto gain = juce::jlimit (1.0e-6f, 1.0f, reduction->gain());
        reductionDb = juce::jlimit (0.0f, reductionRangeDb, -juce::Decibels::gainToDecibels (gain));
    }

    auto changed = reduction.has_value() && std::abs (reductionDb - shownReduction) > 0.2f;

    for (auto& c : levels)
    {
        const auto level = juce::jmax (floorDb - 1.0f, c.levelDb), peak = juce::jmax (floorDb - 1.0f, c.peakDb);
        changed = changed || std::abs (level - c.shownLevel) > 0.25f || std::abs (peak - c.shownPeak) > 0.1f || c.clipped != c.shownClipped;
    }

    if (changed)
        repaint();
}

void StudioMeters::setLevelsForSnapshot (const std::vector<float>& levelsDb, float grDb)
{
    for (size_t i = 0; i < levels.size() && i < levelsDb.size(); ++i)
    {
        levels[i].levelDb = levelsDb[i];
        levels[i].peakDb = levelsDb[i] + 4.5f;
        levels[i].clipped = levels[i].peakDb > 0.0f;
    }

    reductionDb = grDb;
    repaint();
}

juce::String StudioMeters::getReadoutText() const
{
    juce::StringArray parts;

    for (const auto& c : levels)
        parts.add (c.source.name.substring (0, 1) + c.source.name.substring (1).toLowerCase() + " " + dbText (c.peakDb) + " dB");

    if (reduction.has_value())
        parts.add (reduction->name.substring (0, 1) + reduction->name.substring (1).toLowerCase() + " "
                   + (reductionEnabled ? juce::String (reductionDb, 1) + " dB" : juce::String ("off")));

    return parts.joinIntoString (", ");
}

void StudioMeters::resetPeaks()
{
    for (auto& c : levels)
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
        g.drawHorizontalLine (juce::roundToInt (y), scaleWidth, barArea (numBars() - 1).getRight());
    }

    // Reduction scale (dB of gain reduction), to the right of its bar.
    if (reduction.has_value())
    {
        const auto gr = reductionArea();

        for (const auto db : { 0, 6, 12, 18, 24 })
        {
            const auto y = juce::jmap ((float) db, 0.0f, reductionRangeDb, gr.getY(), gr.getBottom());
            g.setColour (p.grid);
            g.drawHorizontalLine (juce::roundToInt (y), gr.getX(), gr.getRight() + 3.0f);
            g.setColour (p.gridText.withMultipliedAlpha (reductionEnabled ? 1.0f : 0.5f));
            g.drawText (juce::String (db), juce::Rectangle<float> (gr.getRight() + 3.0f, y - 6.0f, 20.0f, 12.0f), juce::Justification::centredLeft, false);
        }
    }

    // Names under the bars.
    g.setFont (drawing::tracked (fonts->legend (9.5f), 0.12f));

    auto name = [&] (const juce::String& text, juce::Rectangle<float> b, float alpha)
    {
        g.setColour (p.textDim.withMultipliedAlpha (alpha));
        g.drawText (text, juce::Rectangle<float> (b.getCentreX() - 22.0f, b.getBottom() + 3.0f, 44.0f, labelHeight - 2.0f),
                    juce::Justification::centred, false);
    };

    for (int i = 0; i < numBars(); ++i)
        name (levels[(size_t) i].source.name, barArea (i), 1.0f);

    if (reduction.has_value())
        name (reduction->name, reductionArea(), reductionEnabled ? 1.0f : 0.5f);
}

void StudioMeters::paint (juce::Graphics& g)
{
    const auto& p = Theme::studio();

    scaleLayer.draw (g, getLocalBounds().toFloat(), [this] (juce::Graphics& lg, juce::Rectangle<float>, float) { renderScale (lg); });

    const auto valueFont = fonts->label (10.0f);
    g.setFont (valueFont);

    for (size_t i = 0; i < levels.size(); ++i)
    {
        auto& c = levels[i];
        const auto bar = barArea ((int) i);

        g.setColour (p.meterBack);
        g.fillRoundedRectangle (bar, 2.0f);

        const auto levelY = yForDb (c.levelDb, bar);

        if (c.levelDb > floorDb)
        {
            // The fill colour up to -6 dBFS, hot above, clip red above 0.
            const auto fillArea = juce::Rectangle<float> (bar.getX(), levelY, bar.getWidth(), bar.getBottom() - levelY);
            juce::ColourGradient grad (p.meterClip, 0.0f, yForDb (topDb, bar), p.meterFill, 0.0f, bar.getBottom(), false);
            const auto proportionAt = [&] (float db) { return (yForDb (db, bar) - bar.getY()) / bar.getHeight(); };
            grad.addColour (proportionAt (0.0f), p.meterHot);
            grad.addColour (proportionAt (-6.0f), p.meterHot.interpolatedWith (p.meterFill, 0.6f));
            grad.addColour (proportionAt (-6.5f), p.meterFill);
            g.setGradientFill (grad);

            if (c.muted)
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
    if (reduction.has_value())
    {
        const auto bar = reductionArea();
        const auto alpha = reductionEnabled ? 1.0f : 0.4f;
        g.setColour (p.meterBack);
        g.fillRoundedRectangle (bar, 2.0f);

        if (reductionEnabled && reductionDb > 0.05f)
        {
            const auto h = bar.getHeight() * reductionDb / reductionRangeDb;
            g.setColour (p.reduction);
            g.fillRect (juce::Rectangle<float> (bar.getX() + 1.0f, bar.getY(), bar.getWidth() - 2.0f, h));
        }

        g.setColour (p.text.withMultipliedAlpha (alpha));
        g.drawText (reductionEnabled ? (reductionDb > 0.05f ? juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (reductionDb, 1) : juce::String ("0.0"))
                                : juce::String ("OFF"),
                    juce::Rectangle<float> (bar.getCentreX() - 22.0f, 0.0f, 44.0f, readoutHeight), juce::Justification::centred, false);
        shownReduction = reductionDb;
    }
}

} // namespace myzel::ui
