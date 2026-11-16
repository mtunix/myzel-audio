#include "ui/DisplayScales.h"

#include "dsp/feed/SpectrumAnalyser.h"
#include "plugin/Parameters.h"

#include <cmath>

namespace halite::ui::scales
{

namespace
{
    std::int64_t floorDiv (std::int64_t a, std::int64_t b) noexcept
    {
        const auto q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    std::int64_t ceilDiv (std::int64_t a, std::int64_t b) noexcept
    {
        return -floorDiv (-a, b);
    }

    const juce::String minusSign (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
}

float DbAxis::yOf (float db) const noexcept
{
    const auto p = (juce::jlimit (lowDb, highDb, db) - lowDb) / (highDb - lowDb);
    return bottom + (top - bottom) * p;
}

float DbAxis::dbAt (float y) const noexcept
{
    const auto p = (y - bottom) / (top - bottom);
    return juce::jlimit (lowDb, highDb, lowDb + (highDb - lowDb) * p);
}

DbAxis sibilanceAxis (Detection detection, float top, float bottom) noexcept
{
    if (detection == Detection::allround)
        return { params::absThresholdMinDb, 0.0f, top, bottom };

    return { params::thresholdMinDb, params::thresholdMaxDb, top, bottom };
}

DbTicks sibilanceTicks (float axisHeight) noexcept
{
    // Labels at least about 20 px apart: the axis spans 60 dB in both modes.
    if (axisHeight >= 120.0f)
        return { 6, 12 };
    if (axisHeight >= 60.0f)
        return { 12, 24 };
    return { 24, 24 };
}

//==============================================================================
ColumnMap ColumnMap::make (double zoomSeconds, double recordsPerSecond, int columns) noexcept
{
    ColumnMap m;
    m.span = std::max<std::int64_t> (1, std::llround (zoomSeconds * recordsPerSecond));
    m.columns = std::max (1, columns);
    return m;
}

std::int64_t ColumnMap::columnOf (std::int64_t record) const noexcept
{
    return floorDiv (record * columns, span);
}

std::int64_t ColumnMap::firstRecordOf (std::int64_t column) const noexcept
{
    return ceilDiv (column * span, columns);
}

//==============================================================================
TimelineGeometry TimelineGeometry::forBounds (juce::Rectangle<float> bounds, Detection detection) noexcept
{
    TimelineGeometry g;
    g.plot = bounds.withTop (bounds.getY() + topLine).withBottom (bounds.getBottom() - axisHeight);
    const auto h = std::max (1.0f, g.plot.getHeight());
    g.centreY = g.plot.getCentreY();
    g.halfHeight = std::max (1.0f, h * 0.5f - 2.0f);
    g.cutPxPerDb = h * 0.40f / params::rangeMaxDb;
    g.harshPxPerDb = h * 0.16f / params::harshRangeMaxDb;
    g.sibilance = sibilanceAxis (detection, g.plot.getY() + h * 0.12f, g.plot.getBottom() - h * 0.04f);
    return g;
}

CloudGeometry CloudGeometry::forBounds (juce::Rectangle<float> bounds, Detection detection) noexcept
{
    CloudGeometry g;
    g.plot = bounds.withTrimmedLeft (left).withTrimmedRight (right).withTrimmedTop (top).withTrimmedBottom (bottom);
    if (g.plot.getHeight() < 1.0f)
        g.plot.setHeight (1.0f);
    g.sibilance = sibilanceAxis (detection, g.plot.getY(), g.plot.getBottom());
    return g;
}

//==============================================================================
SpectrumGeometry SpectrumGeometry::forBounds (juce::Rectangle<float> bounds) noexcept
{
    SpectrumGeometry g;
    g.plot = bounds.withTrimmedLeft (left).withTrimmedRight (right).withTrimmedTop (1.0f).withTrimmedBottom (axisHeight);
    if (g.plot.getHeight() < 1.0f)
        g.plot.setHeight (1.0f);

    const auto h = g.plot.getHeight();
    g.zeroY = std::round (g.plot.getY() + juce::jlimit (20.0f, 34.0f, h * 0.17f)) + 0.5f;
    g.cutPxPerDb = std::max (0.5f, (g.plot.getBottom() - g.zeroY) * 0.8f / params::rangeMaxDb);
    g.level.top = g.plot.getY();
    g.level.bottom = g.plot.getBottom();
    return g;
}

float SpectrumGeometry::xOf (double hz) const noexcept
{
    return plot.getX() + plot.getWidth() * (float) dsp::SpectrumAnalyser::proportionOf (hz);
}

double SpectrumGeometry::hzAt (float x) const noexcept
{
    return dsp::SpectrumAnalyser::frequencyAt ((double) ((x - plot.getX()) / std::max (1.0f, plot.getWidth())));
}

float SpectrumGeometry::cutY (float cutDb) const noexcept
{
    return juce::jlimit (zeroY, plot.getBottom(), zeroY - std::min (0.0f, cutDb) * cutPxPerDb);
}

//==============================================================================
double BandMarks::harshCentre() const noexcept
{
    return std::sqrt (std::max (1.0, bands.harshLow) * std::max (1.0, bands.harshHigh));
}

juce::Point<float> handlePosition (const SpectrumGeometry& g, const BandMarks& m, SpectrumTarget t)
{
    switch (t)
    {
        case SpectrumTarget::essLow:      return { g.xOf (m.bands.essLow), g.zeroY };
        case SpectrumTarget::essHigh:     return { g.xOf (m.bands.essHigh), g.zeroY };
        case SpectrumTarget::harshCentre: return { g.xOf (m.harshCentre()), g.zeroY };
        case SpectrumTarget::harshLow:    return { g.xOf (m.bands.harshLow), g.zeroY };
        case SpectrumTarget::harshHigh:   return { g.xOf (m.bands.harshHigh), g.zeroY };
        case SpectrumTarget::none:
        case SpectrumTarget::essBand:
        case SpectrumTarget::harshBand:   break;
    }
    return {};
}

SpectrumTarget hitTest (const SpectrumGeometry& g, const BandMarks& m, juce::Point<float> p, bool harshSelected) noexcept
{
    using T = SpectrumTarget;

    const auto harsh = m.harshShown;
    const auto harshFirst = harsh && harshSelected;

    // The nearest of a group within `reach`, by a distance function.
    auto nearest = [&] (std::initializer_list<T> group, float reach, auto distance)
    {
        auto best = T::none;
        auto bestDistance = reach;
        for (const auto t : group)
            if (const auto d = distance (t); d <= bestDistance)
            {
                best = t;
                bestDistance = d;
            }
        return best;
    };

    auto handleDistance = [&] (T t) { return handlePosition (g, m, t).getDistanceFrom (p); };
    auto edgeDistance = [&] (T t)
    {
        if (p.y < g.plot.getY() || p.y > g.plot.getBottom())
            return std::numeric_limits<float>::max();
        return std::abs (handlePosition (g, m, t).x - p.x);
    };

    const auto essHandles = { T::essLow, T::essHigh };
    const auto harshHandles = { T::harshCentre };
    const auto essEdges = { T::essLow, T::essHigh };
    const auto harshEdges = { T::harshLow, T::harshHigh };

    if (harshFirst)
    {
        if (const auto t = nearest (harshHandles, handleGrab, handleDistance); t != T::none) return t;
        if (const auto t = nearest (essHandles, handleGrab, handleDistance); t != T::none) return t;
        if (const auto t = nearest (harshEdges, edgeGrab, edgeDistance); t != T::none) return t;
        if (const auto t = nearest (essEdges, edgeGrab, edgeDistance); t != T::none) return t;
    }
    else
    {
        if (const auto t = nearest (essHandles, handleGrab, handleDistance); t != T::none) return t;
        if (harsh)
            if (const auto t = nearest (harshHandles, handleGrab, handleDistance); t != T::none) return t;
        if (const auto t = nearest (essEdges, edgeGrab, edgeDistance); t != T::none) return t;
        if (harsh)
            if (const auto t = nearest (harshEdges, edgeGrab, edgeDistance); t != T::none) return t;
    }

    if (p.y < g.plot.getY() || p.y > g.plot.getBottom())
        return T::none;

    const auto inEss = p.x >= g.xOf (m.bands.essLow) && p.x <= g.xOf (m.bands.essHigh);
    const auto inHarsh = harsh && p.x >= g.xOf (m.bands.harshLow) && p.x <= g.xOf (m.bands.harshHigh);

    if (inEss && inHarsh)
    {
        const auto essCentre = g.xOf (std::sqrt (m.bands.essLow * m.bands.essHigh));
        const auto harshCentre = g.xOf (m.harshCentre());
        return std::abs (p.x - harshCentre) < std::abs (p.x - essCentre) ? T::harshBand : T::essBand;
    }

    if (inEss)
        return T::essBand;
    if (inHarsh)
        return T::harshBand;
    return T::none;
}

double harshWidthFor (double centreHz, double edgeHz) noexcept
{
    return 2.0 * std::abs (std::log2 (std::max (1.0, edgeHz) / std::max (1.0, centreHz)));
}

//==============================================================================
juce::String secondsLabel (int secondsBack)
{
    return secondsBack == 0 ? juce::String ("now") : minusSign + juce::String (std::abs (secondsBack)) + " s";
}

juce::String signedDb (float db)
{
    const auto rounded = std::round (db * 10.0f) / 10.0f;
    if (std::abs (rounded) < 0.05f)
        return "0.0";

    const auto digits = juce::String (std::abs (rounded), 1);
    return (rounded > 0.0f ? juce::String ("+") : minusSign) + digits;
}

} // namespace halite::ui::scales
