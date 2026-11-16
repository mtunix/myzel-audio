#include "ui/MeterStrip.h"

#include "ui/DisplayScales.h"
#include "ui/Widgets.h"

namespace halite::ui
{

namespace
{
    constexpr float trackTop = 10.0f, belowTracks = 34.0f;

    // 0 … 1 down the track: 0 dBFS at the top.
    float levelPosition (float db) { return juce::jlimit (0.0f, 1.0f, db / scales::meterLowDb); }
    float cutPosition (float db)   { return juce::jlimit (0.0f, 1.0f, db / scales::cutMeterDb); }

    juce::String readout (float cutDb)
    {
        return cutDb >= 0.05f ? scales::signedDb (-cutDb) : juce::String ("0.0");
    }

    // A rounded bar as a rectangle and two short rounded ends: a tall rounded path costs the renderer
    // an edge table over its whole height, which was two thirds of the strip's repaint.
    void fillBar (juce::Graphics& g, juce::Rectangle<float> r, float radius)
    {
        const auto end = 2.0f * radius;
        if (r.getHeight() <= 2.0f * end)
        {
            g.fillRoundedRectangle (r, radius);
            return;
        }
        g.fillRoundedRectangle (r.withHeight (end), radius);
        g.fillRect (r.reduced (0.0f, radius));
        g.fillRoundedRectangle (r.withTop (r.getBottom() - end), radius);
    }
}

MeterStrip::MeterStrip (DisplayModel& m)
    : model (m)
{
    setTitle ("Meters: input, output and cut");
    setDescription ("Input and output peak, hold and RMS in dBFS; the cut of each band. Click a meter to reset its hold.");
    setOpaque (true);
}

juce::Rectangle<float> MeterStrip::track (int which) const
{
    const auto bottom = (float) getHeight() - belowTracks;
    const auto h = std::max (1.0f, bottom - trackTop);
    switch (which)
    {
        case 0:  return { 4.0f, trackTop, 8.0f, h };
        case 1:  return { 16.0f, trackTop, 8.0f, h };
        default: return { 31.0f, trackTop, 13.0f, h };
    }
}

int MeterStrip::meterAt (float x) const noexcept
{
    return x < 14.0f ? 0 : x < 27.5f ? 1 : 2;
}

std::array<float, 8> MeterStrip::shownPositions() const
{
    // What the strip draws, in px down the track, so a change under half a pixel does not repaint.
    const auto h = track (0).getHeight();
    const auto& in = model.meter (0);
    const auto& out = model.meter (1);
    return { std::round (levelPosition (in.peakDb) * h * 2.0f), std::round (levelPosition (in.rmsDb) * h * 2.0f),
             std::round (levelPosition (in.holdDb) * h * 2.0f), std::round (levelPosition (out.peakDb) * h * 2.0f),
             std::round (levelPosition (out.rmsDb) * h * 2.0f), std::round (levelPosition (out.holdDb) * h * 2.0f),
             std::round (cutPosition (model.essCut()) * h * 2.0f) + 4096.0f * std::round (cutPosition (model.cutHold (0)) * h * 2.0f),
             std::round (cutPosition (model.harshCut()) * h * 2.0f) + 4096.0f * std::round (cutPosition (model.cutHold (1)) * h * 2.0f) };
}

void MeterStrip::update (double dt)
{
    // Thirty times a second is as fast as a level can be read; the read-out ten times.
    sinceRepaint += dt;
    sinceReadout += dt;
    if (sinceRepaint < 1.0 / 30.0 - 0.002 && theme::currentName() == shownTheme)
        return;
    sinceRepaint = 0.0;

    auto text = shownReadout;
    if (sinceReadout >= 0.1)
    {
        text = readout (model.essCut());
        sinceReadout = 0.0;
    }

    const auto positions = shownPositions();
    if (positions != shown || text != shownReadout || theme::currentName() != shownTheme)
    {
        shown = positions;
        shownReadout = text;
        shownTheme = theme::currentName();
        ++repaintsAsked;
        repaint();
    }
}

void MeterStrip::paintStatic (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (t.panel);
    g.setColour (t.lineSoft);
    g.fillRect (bounds.withWidth (1.0f));

    for (int i = 0; i < 3; ++i)
    {
        g.setColour (t.track);
        g.fillRoundedRectangle (track (i), 2.0f);
    }

    const auto labelY = track (2).getBottom() + 5.0f;
    const char* labels[] { "In", "Out", "Cut" };
    g.setFont (fonts->sans (type::meter));
    g.setColour (t.secondary);
    for (int i = 0; i < 3; ++i)
        g.drawText (labels[i], juce::Rectangle<float> (18.0f, 11.0f).withCentre ({ track (i).getCentreX(), labelY + 5.5f }),
                    juce::Justification::centred, false);
}

void MeterStrip::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();
    const auto scale = std::max (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());

    if (! cached.isValid() || cachedFor != std::tuple (getWidth(), getHeight(), scale, theme::currentName()))
    {
        cached = opaque::make (juce::roundToInt ((float) getWidth() * scale), juce::roundToInt ((float) getHeight() * scale), scale);
        juce::Graphics sg (cached);
        sg.addTransform (juce::AffineTransform::scale (scale));
        paintStatic (sg);
        cachedFor = { getWidth(), getHeight(), scale, theme::currentName() };
    }
    opaque::draw (g, cached, 0, 0, scale);

    // In and Out: peak, RMS over it, the hold as a line.
    for (int which = 0; which < 2; ++which)
    {
        const auto r = track (which);
        const auto& m = model.meter (which);
        auto barFrom = [&] (float db) { return r.withTop (r.getY() + levelPosition (db) * r.getHeight()); };

        if (m.peakDb > scales::meterLowDb)
        {
            g.setColour (t.meterPeak);
            fillBar (g, barFrom (m.peakDb), 2.0f);
        }
        if (m.rmsDb > scales::meterLowDb)
        {
            g.setColour (t.meterRms);
            g.fillRect (barFrom (m.rmsDb));
        }
        if (m.holdDb > scales::meterLowDb)
        {
            g.setColour (t.text);
            g.fillRect (r.getX(), std::round (r.getY() + levelPosition (m.holdDb) * r.getHeight()) - 1.0f, r.getWidth(), 2.0f);
        }
    }

    // Cut: Ess and Harsh side by side, hanging from the top; the deepest since the reset as a line.
    const auto cutTrack = track (2);
    const auto halves = std::array { cutTrack.withWidth (6.0f), cutTrack.withTrimmedLeft (7.0f) };
    const float cuts[] { model.essCut(), model.harshCut() };
    const juce::Colour colours[] { t.essText, t.harshText };

    for (int b = 0; b < 2; ++b)
    {
        const auto& r = halves[(size_t) b];
        if (cuts[b] > 0.0f)
        {
            g.setColour (colours[b]);
            fillBar (g, r.withHeight (std::max (1.0f, cutPosition (cuts[b]) * r.getHeight())), 2.0f);
        }
        if (const auto hold = model.cutHold (b); hold >= 0.05f)
        {
            g.setColour (colours[b].withMultipliedAlpha (0.7f));
            g.fillRect (r.getX(), std::round (r.getY() + cutPosition (hold) * r.getHeight()) - 1.0f, r.getWidth(), 2.0f);
        }
    }

    // The Ess cut now, under the labels.
    const auto text = shownReadout.isEmpty() ? readout (model.essCut()) : shownReadout;
    readouts.draw (g, { 0.0f, cutTrack.getBottom() + 18.0f, bounds.getWidth(), 11.0f }, text, juce::Colours::transparentBlack,
                   text == "0.0" ? t.secondary : t.essText, fonts->mono (type::meter), 0.0f);
}

void MeterStrip::mouseDown (const juce::MouseEvent& e)
{
    model.resetHold (meterAt (e.position.x));
    repaint();
}

void MeterStrip::mouseEnter (const juce::MouseEvent& e)
{
    mouseMove (e);
}

void MeterStrip::mouseMove (const juce::MouseEvent& e)
{
    static const char* ids[] { "meterIn", "meterOut", "meterCut" };
    reportHelp (*this, helpFor (ids[meterAt (e.position.x)]), true, false);
}

void MeterStrip::mouseExit (const juce::MouseEvent&)
{
    reportHelp (*this, {}, false, false);
}

} // namespace halite::ui
