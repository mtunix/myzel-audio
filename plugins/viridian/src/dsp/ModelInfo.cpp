#include "dsp/ModelInfo.h"

#include <algorithm>
#include <cmath>

namespace viridian
{

namespace
{
    // Step tables from each model's NOTES.md (sources there).
    constexpr std::array<float, 4>  passiveLowHz       { 20.0f, 30.0f, 60.0f, 100.0f };
    constexpr std::array<float, 11> passiveDipHz       { 200.0f, 300.0f, 500.0f, 700.0f, 1000.0f, 1500.0f,
                                                         2000.0f, 3000.0f, 4000.0f, 5000.0f, 7000.0f };
    constexpr std::array<float, 7>  passiveHighBoostHz { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
    constexpr std::array<float, 3>  passiveAttenSelHz  { 5000.0f, 10000.0f, 20000.0f };

    constexpr std::array<float, 4> inductorHpfHz { 50.0f, 80.0f, 160.0f, 300.0f };
    constexpr std::array<float, 4> inductorLowHz { 35.0f, 60.0f, 110.0f, 220.0f };
    constexpr std::array<float, 6> inductorMidHz { 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };
    constexpr std::array<float, 5> inductorLpfHz { 6000.0f, 8000.0f, 10000.0f, 14000.0f, 18000.0f };

    constexpr std::array<float, 7>  proportionalLowHz     { 30.0f, 40.0f, 50.0f, 100.0f, 200.0f, 300.0f, 400.0f };
    constexpr std::array<float, 7>  proportionalLowMidHz  { 75.0f, 150.0f, 180.0f, 240.0f, 500.0f, 700.0f, 1000.0f };
    constexpr std::array<float, 7>  proportionalHighMidHz { 800.0f, 1500.0f, 3000.0f, 5000.0f, 8000.0f, 10000.0f, 12500.0f };
    constexpr std::array<float, 7>  proportionalHighHz    { 2500.0f, 5000.0f, 7000.0f, 10000.0f, 12500.0f, 15000.0f, 20000.0f };
    constexpr std::array<float, 11> proportionalGainDb    { -12.0f, -9.0f, -6.0f, -4.0f, -2.0f, 0.0f,
                                                            2.0f, 4.0f, 6.0f, 9.0f, 12.0f };

    constexpr ControlInfo dim() { return {}; }

    constexpr ControlInfo fixed (const char* legend, Unit unit, float value)
    {
        return { .kind = Kind::fixed, .legend = legend, .unit = unit, .min = value, .max = value };
    }

    constexpr ControlInfo stepped (const char* legend, Unit unit, std::span<const float> detents,
                                   OffAt off = OffAt::none, float flat = -1.0f)
    {
        return { .kind = Kind::stepped, .legend = legend, .unit = unit, .detents = detents, .off = off, .flat = flat };
    }

    constexpr ControlInfo frequency (float minHz, float maxHz, OffAt off = OffAt::none)
    {
        return { .kind = Kind::continuous, .legend = "FREQ", .unit = Unit::hz, .min = minHz, .max = maxHz,
                 .logScale = true, .off = off };
    }

    constexpr ControlInfo gainDb (float range)
    {
        return { .kind = Kind::continuous, .legend = "GAIN", .unit = Unit::db, .min = -range, .max = range, .flat = 0.5f };
    }

    constexpr ControlInfo q (float minQ, float maxQ)
    {
        return { .kind = Kind::continuous, .legend = "Q", .unit = Unit::q, .min = minQ, .max = maxQ, .logScale = true };
    }

    /** The passive model's 0 .. 10 knobs; flatAtStart: the knob does nothing at 0. */
    constexpr ControlInfo dial (const char* legend, bool flatAtStart)
    {
        return { .kind = Kind::continuous, .legend = legend, .unit = Unit::dial, .min = 0.0f, .max = 10.0f,
                 .flat = flatAtStart ? 0.0f : -1.0f };
    }

    constexpr ControlInfo shape (const char* bellName = "Bell")
    {
        return { .kind = Kind::toggle, .legend = "SHAPE", .offName = "Shelf", .onName = bellName };
    }

    constexpr ControlInfo proportionalGain() { return stepped ("GAIN", Unit::db, proportionalGainDb, OffAt::none, 0.5f); }

    // Order: hpfFreq, lowFreq, lowGain, lowQ, lowShape, lmFreq, lmGain, lmQ, hmFreq, hmGain, hmQ,
    // highFreq, highGain, highQ, highShape, lpfFreq.

    constexpr EqSection passive { {
        dim(),
        stepped ("FREQ", Unit::hz, passiveLowHz), dial ("BOOST", true), dial ("ATTEN", true), dim(),
        stepped ("FREQ", Unit::hz, passiveDipHz), dial ("DIP", true), dim(),
        stepped ("FREQ", Unit::hz, passiveHighBoostHz), dial ("BOOST", true), dial ("BANDWIDTH", false),
        stepped ("ATTEN SEL", Unit::hz, passiveAttenSelHz), dial ("ATTEN", true), dim(), dim(),
        dim(),
    } };

    constexpr EqSection inductor { {
        stepped ("FREQ", Unit::hz, inductorHpfHz, OffAt::start),
        stepped ("FREQ", Unit::hz, inductorLowHz), gainDb (16.0f), dim(), dim(),
        stepped ("FREQ", Unit::hz, inductorMidHz), gainDb (18.0f), dim(),
        dim(), dim(), dim(),
        fixed ("FREQ", Unit::hz, 12000.0f), gainDb (16.0f), dim(), dim(),
        stepped ("FREQ", Unit::hz, inductorLpfHz, OffAt::end),
    } };

    // Alt switches the band-pass filters in: fixed, so their knobs stay dim either way.
    constexpr EqSection proportional (bool filtersIn)
    {
        return { {
            filtersIn ? fixed ("FREQ", Unit::hz, 50.0f) : dim(),
            stepped ("FREQ", Unit::hz, proportionalLowHz), proportionalGain(), dim(), shape ("Peak"),
            stepped ("FREQ", Unit::hz, proportionalLowMidHz), proportionalGain(), dim(),
            stepped ("FREQ", Unit::hz, proportionalHighMidHz), proportionalGain(), dim(),
            stepped ("FREQ", Unit::hz, proportionalHighHz), proportionalGain(), dim(), shape ("Peak"),
            filtersIn ? fixed ("FREQ", Unit::hz, 15000.0f) : dim(),
        } };
    }

    // Alt off: the early card; on: the later one. Every range is the circuit's own travel under the
    // control laws (console/NOTES.md, "Control laws"), rounded inwards; the low-pass stops at the
    // published 22 kHz. Gains and Q are the values at each band's calibration point.
    constexpr EqSection console (bool later)
    {
        if (later)
            return { {
                frequency (15.11f, 409.64f, OffAt::start),
                frequency (36.50f, 442.53f), gainDb (18.0f), fixed ("Q", Unit::q, 2.12f), shape(),
                frequency (135.73f, 2201.33f), gainDb (18.0f), q (0.50f, 4.26f),
                frequency (635.33f, 10304.11f), gainDb (18.0f), q (0.54f, 4.06f),
                frequency (1294.14f, 20988.88f), gainDb (18.0f), fixed ("Q", Unit::q, 0.94f), shape(),
                frequency (3188.89f, 22000.0f, OffAt::end),
            } };

        return { {
            frequency (10.74f, 308.96f, OffAt::start),
            frequency (36.37f, 424.83f), gainDb (15.2f), fixed ("Q", Unit::q, 0.91f), shape(),
            frequency (193.98f, 2265.74f), gainDb (15.0f), q (0.49f, 2.89f),
            frequency (619.08f, 7231.08f), gainDb (15.0f), q (0.53f, 2.78f),
            frequency (1261.03f, 14729.29f), gainDb (16.1f), fixed ("Q", Unit::q, 0.62f), shape(),
            frequency (3252.85f, 22000.0f, OffAt::end),
        } };
    }

    constexpr EqSection mastering { {
        frequency (10.0f, 500.0f, OffAt::start),
        frequency (15.0f, 800.0f), gainDb (15.0f), q (0.4f, 4.0f), shape(),
        frequency (120.0f, 8000.0f), gainDb (15.0f), q (0.4f, 4.0f),
        frequency (400.0f, 26000.0f), gainDb (15.0f), q (0.4f, 4.0f),
        frequency (400.0f, 26000.0f), gainDb (15.0f), q (0.4f, 4.0f), shape(),
        frequency (2000.0f, 40000.0f, OffAt::end),
    } };

    constexpr std::array<ModelInfo, numModels> infos { {
        { Model::passive, "Passive", "passive", "Passive program EQ, valve amp",
          {}, passive, passive, 5.0f },
        { Model::inductor, "Inductor", "inductor", "Class-A, inductor mids",
          { true, "HI-Q", "Narrower Low-Mid bell" }, inductor, inductor, 5.0f },
        { Model::proportional, "Proportional", "proportional", "Discrete, proportional Q",
          { true, "FILTERS", "Band-pass filters in: 50 Hz high-pass, 15 kHz low-pass" },
          proportional (false), proportional (true), 4.0f },
        { Model::console, "Console", "console", "Console channel EQ",
          { true, "TIGHT", "Later curves: wider gain, narrower Q, steeper high-pass" },
          console (false), console (true), 3.0f, 10.0f },
        { Model::mastering, "Mastering", "mastering", "Clean parametric",
          {}, mastering, mastering, 3.0f },
    } };

    float clamp01 (float x) noexcept
    {
        return x > 0.0f ? (x < 1.0f ? x : 1.0f) : 0.0f;   // NaN reads as 0
    }

    bool hasOff (const ControlInfo& c) noexcept { return c.off != OffAt::none; }
}

Slot slotOf (Control c) noexcept
{
    switch (c)
    {
        case Control::hpfFreq:  return Slot::hpf;
        case Control::lowFreq:
        case Control::lowGain:
        case Control::lowQ:
        case Control::lowShape: return Slot::low;
        case Control::lmFreq:
        case Control::lmGain:
        case Control::lmQ:      return Slot::lowMid;
        case Control::hmFreq:
        case Control::hmGain:
        case Control::hmQ:      return Slot::highMid;
        case Control::highFreq:
        case Control::highGain:
        case Control::highQ:
        case Control::highShape: return Slot::high;
        case Control::lpfFreq:  return Slot::lpf;
    }
    return Slot::low;
}

const std::array<ModelInfo, numModels>& modelInfos() noexcept
{
    return infos;
}

const ModelInfo& info (Model m) noexcept
{
    return infos[(size_t) std::clamp ((int) m, 0, numModels - 1)];
}

const ControlInfo& controlInfo (Model m, bool altOn, Control c) noexcept
{
    return info (m).section (altOn)[c];
}

int numPositions (const ControlInfo& c) noexcept
{
    switch (c.kind)
    {
        case Kind::stepped: return (int) c.detents.size() + (hasOff (c) ? 1 : 0);
        case Kind::toggle:  return 2;
        case Kind::dim:
        case Kind::fixed:
        case Kind::continuous: break;
    }
    return 0;
}

int positionIndex (const ControlInfo& c, float position) noexcept
{
    const auto n = numPositions (c);
    if (n <= 1)
        return 0;
    return std::clamp ((int) std::lround (clamp01 (position) * (float) (n - 1)), 0, n - 1);
}

float detentPosition (const ControlInfo& c, int index) noexcept
{
    const auto n = numPositions (c);
    if (n <= 1)
        return 0.0f;
    return (float) std::clamp (index, 0, n - 1) / (float) (n - 1);
}

float snap (const ControlInfo& c, float position) noexcept
{
    if (c.kind == Kind::stepped || c.kind == Kind::toggle)
        return detentPosition (c, positionIndex (c, position));
    return position;
}

ControlValue map (const ControlInfo& c, float position) noexcept
{
    const auto x = clamp01 (position);

    switch (c.kind)
    {
        case Kind::dim:
            return {};

        case Kind::fixed:
            return { c.min, -1, false };

        case Kind::toggle:
        {
            const auto index = positionIndex (c, x);
            return { (float) index, index, false };
        }

        case Kind::stepped:
        {
            const auto index = positionIndex (c, x);
            const auto n = numPositions (c);

            if ((c.off == OffAt::start && index == 0) || (c.off == OffAt::end && index == n - 1))
                return { 0.0f, index, true };

            return { c.detents[(size_t) (c.off == OffAt::start ? index - 1 : index)], index, false };
        }

        case Kind::continuous:
        {
            auto t = x;

            if (c.off == OffAt::start)
            {
                if (x < filterOffZone)
                    return { 0.0f, -1, true };
                t = (x - filterOffZone) / (1.0f - filterOffZone);
            }
            else if (c.off == OffAt::end)
            {
                if (x > 1.0f - filterOffZone)
                    return { 0.0f, -1, true };
                t = x / (1.0f - filterOffZone);
            }

            t = clamp01 (t);
            const auto value = c.logScale ? c.min * std::pow (c.max / c.min, t)
                                          : c.min + (c.max - c.min) * t;
            return { value, -1, false };
        }
    }

    return {};
}

float positionOf (const ControlInfo& c, float value) noexcept
{
    switch (c.kind)
    {
        case Kind::dim:
        case Kind::fixed:
            return 0.0f;

        case Kind::toggle:
            return value >= 0.5f ? 1.0f : 0.0f;

        case Kind::stepped:
        {
            auto best = 0;
            auto bestDistance = 0.0f;

            for (size_t i = 0; i < c.detents.size(); ++i)
            {
                const auto d = c.detents[i];
                const auto distance = c.unit == Unit::hz && value > 0.0f ? std::abs (std::log (value / d))
                                                                         : std::abs (value - d);
                if (i == 0 || distance < bestDistance)
                {
                    best = (int) i;
                    bestDistance = distance;
                }
            }

            return detentPosition (c, c.off == OffAt::start ? best + 1 : best);
        }

        case Kind::continuous:
        {
            auto t = 0.0f;
            if (c.logScale)
                t = value > 0.0f ? std::log (value / c.min) / std::log (c.max / c.min) : 0.0f;
            else
                t = (value - c.min) / (c.max - c.min);
            t = clamp01 (t);

            if (c.off == OffAt::start)
                return filterOffZone + t * (1.0f - filterOffZone);
            if (c.off == OffAt::end)
                return t * (1.0f - filterOffZone);
            return t;
        }
    }

    return 0.0f;
}

float offPosition (const ControlInfo& c) noexcept
{
    switch (c.off)
    {
        case OffAt::start: return 0.0f;
        case OffAt::end:   return 1.0f;
        case OffAt::none:  break;
    }
    return -1.0f;
}

} // namespace viridian
