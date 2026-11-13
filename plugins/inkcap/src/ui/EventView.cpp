#include "ui/EventView.h"

#include "dsp/modules/glitch/GlitchPattern.h"
#include "plugin/PluginProcessor.h"

namespace inkcap::ui
{

EventView::EventView (InkcapController& c)
    : controller (c)
{
    setInterceptsMouseClicks (false, false);
    setTitle ("Glitch: the next 4 bars");
    events.reserve (512);
}

float EventView::xFor (double beat) const
{
    const auto w = (float) getWidth() - laneX;
    return laneX + w * (float) ((beat - windowStart) / windowBeats);
}

void EventView::update()
{
    const auto& t = controller.getTelemetry();
    const auto running = t.blocks.load (std::memory_order_relaxed) > 0;

    ppq = running ? t.ppq.load (std::memory_order_relaxed) : 0.0;
    bpm = juce::jlimit (20.0, 999.0, t.bpm.load (std::memory_order_relaxed));
    const auto num = juce::jlimit (1, 32, t.timeSigNumerator.load (std::memory_order_relaxed));
    const auto den = juce::jlimit (1, 32, t.timeSigDenominator.load (std::memory_order_relaxed));
    beatsInBar = num;
    beatsPerBar = 4.0 * num / den;

    // The audio thread's seed is the one in use (a new one waits for the next slot); before any
    // audio, the one it will take.
    const auto glitchSeed = running ? t.glitchSeed.load (std::memory_order_relaxed) : controller.getSeeds().current();
    playingSlot = t.glitchEventType.load (std::memory_order_relaxed) >= 0 ? t.glitchEventSlot.load (std::memory_order_relaxed) : -1;

    const auto s = controller.getProcessor().readSettings();

    Key k;
    // Bars count from the host's last bar start when it gives one (a signature change moves them
    // off multiples of the current bar), else from 0.
    const auto barStart = t.hasBarStart.load (std::memory_order_relaxed) ? t.barStartPpq.load (std::memory_order_relaxed) : 0.0;
    k.windowStart = barStart + std::floor ((ppq - barStart) / beatsPerBar + 1.0e-9) * beatsPerBar;
    k.bpm = bpm;
    k.beatsPerBar = beatsPerBar;
    k.seed = glitchSeed;
    k.on = s.glitch.on;
    k.amount = s.glitch.amount;
    k.damage = s.damage;
    k.chaos = s.chaos;
    k.length = s.glitch.length;
    k.groove = s.glitch.groove;
    k.type = (int) s.glitch.type;
    k.rate = s.glitch.rate;

    if (! (k == key))
    {
        key = k;
        recompute();
        repaint();
    }

    const auto x = xFor (ppq);
    if (std::abs (x - playheadX) >= 0.5f)
    {
        playheadX = x;
        repaint();
    }
}

void EventView::recompute()
{
    events.clear();
    windowStart = key.windowStart;
    windowBeats = bars * beatsPerBar;
    seed = key.seed;
    gridName = glitchRates[(size_t) juce::jlimit (0, numGlitchRates - 1, key.rate)].name;

    if (! key.on)
    {
        status = "glitch is off";
        return;
    }

    const auto s = controller.getProcessor().readSettings();
    const auto pattern = dsp::glitch::patternFor (s.glitch, s.damage, s.chaos);

    if (pattern.density <= 0.0f)
    {
        status = s.damage <= 0.0f ? "damage 0: nothing fails" : "amount 0: nothing fails";
        return;
    }

    // Start early enough to catch an event that began before the window and still plays into it.
    const auto beatsPerSecond = bpm / 60.0;
    const auto end = windowStart + windowBeats;
    const auto lookBack = dsp::glitch::longestEventSeconds * beatsPerSecond;

    for (auto slot = pattern.grid.firstSlotFrom (windowStart - lookBack); pattern.grid.slotStart (slot) < end; ++slot)
    {
        if (const auto e = dsp::glitch::eventAt (seed, pattern, slot))
        {
            const auto length = dsp::glitch::durationSeconds (*e) * beatsPerSecond;
            if (e->startBeat + length > windowStart && events.size() < events.capacity())
                events.push_back ({ e->slot, e->startBeat, length, e->type });
        }
    }

    status = events.empty() ? juce::String ("no events in these bars") : juce::String();
}

void EventView::paint (juce::Graphics& g)
{
    const auto& c = Theme::glass();
    const auto font = fonts->mono (metrics::textSize);
    const auto small = fonts->mono (metrics::smallSize);
    const auto w = (float) getWidth();

    g.setFont (font);
    g.setColour (c.textDim);
    g.drawText ("next 4 bars", juce::Rectangle<float> (0.0f, 0.0f, 200.0f, 15.0f), juce::Justification::centredLeft, false);

    const auto count = (int) events.size();
    const auto right = gridName + (key.on && status.isEmpty() ? juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (count)
                                                                    + (count == 1 ? " event" : " events")
                                                              : juce::String());
    g.drawText (right, juce::Rectangle<float> (w - 220.0f, 0.0f, 220.0f, 15.0f), juce::Justification::centredRight, false);

    static constexpr std::array<const char*, 4> laneNames { "skip", "chew", "stall", "bend" };
    const auto lanesBottom = lanesY + 4.0f * laneHeight + 3.0f * laneGap;

    // Beats and bars: faint lines; the bar numbers count from the window's first bar.
    for (int beat = 0; beat <= (int) std::round (windowBeats * beatsInBar / beatsPerBar); ++beat)
    {
        const auto b = windowStart + beat * beatsPerBar / beatsInBar;
        const auto x = std::round (xFor (b));
        const auto isBar = beat % beatsInBar == 0;
        g.setColour (isBar ? c.textFaint : c.line);
        g.fillRect (juce::Rectangle<float> (x, lanesY - (isBar ? 3.0f : 0.0f), 1.0f, lanesBottom - lanesY + (isBar ? 3.0f : 0.0f)));
    }

    g.setFont (small);
    for (int lane = 0; lane < 4; ++lane)
    {
        const auto y = lanesY + (float) lane * (laneHeight + laneGap);
        g.setColour (c.textDim);
        g.drawText (laneNames[(size_t) lane], juce::Rectangle<float> (0.0f, y, laneX - 8.0f, laneHeight), juce::Justification::centredLeft, false);
        g.setColour (c.track);
        g.fillRect (juce::Rectangle<float> (laneX, y + laneHeight * 0.5f, w - laneX, 1.0f));
    }

    if (status.isNotEmpty())
    {
        g.setFont (font);
        g.setColour (c.textDim);
        g.drawText (status, juce::Rectangle<float> (laneX, lanesY, w - laneX, lanesBottom - lanesY), juce::Justification::centred, false);
        return;
    }

    const auto clip = juce::Rectangle<float> (laneX, 0.0f, w - laneX, (float) getHeight());
    g.saveState();
    g.reduceClipRegion (clip.toNearestInt());

    for (const auto& e : events)
    {
        const auto lane = (int) e.type;
        const auto y = lanesY + (float) lane * (laneHeight + laneGap) + laneHeight * 0.5f;
        const auto x0 = xFor (e.startBeat);
        const auto x1 = juce::jmax (x0 + 3.0f, xFor (e.startBeat + e.lengthBeats));
        const auto r = juce::Rectangle<float> (x0, y - 3.5f, x1 - x0, 7.0f);
        const auto past = e.startBeat + e.lengthBeats < ppq;
        const auto playing = e.slot == playingSlot;

        if (playing)
        {
            g.setColour (c.amber.withAlpha (0.25f));
            g.fillRoundedRectangle (r.expanded (2.5f), 4.0f);
        }

        g.setColour (past && ! playing ? c.amber.withAlpha (0.35f) : c.amber);
        g.fillRoundedRectangle (r, 2.5f);
    }

    g.restoreState();

    if (playheadX >= laneX && playheadX <= w)
    {
        g.setColour (c.text.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (std::round (playheadX), lanesY - 4.0f, 1.0f, lanesBottom - lanesY + 8.0f));
    }
}

} // namespace inkcap::ui
