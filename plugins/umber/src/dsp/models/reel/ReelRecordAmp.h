#pragma once

#include <vector>

namespace umber::dsp::reel
{

/**
    Record amplifier: one 12AX7-class common-cathode triode stage, solved at circuit level.

    @code
                         B+ 250 V
                            |
                           [Rp] 100k
                            |
                            +----||----+---> to record EQ / head driver
                            |  (Cc)    |
                  plate   |/          [RL] 470k   (next stage grid leak)
       vin --[Rs]---------|  12AX7     |
              33k  grid   |\          GND
                            |  cathode
                           [Rk] 1.5k (unbypassed: local feedback)
                            |
                           GND
    @endcode

    Plate current: Koren's triode model (1996). Grid current (grid conduction on positive
    peaks): Dempwolf / Holters / Zölzer, "A physically-motivated triode model for circuit
    simulations" (DAFx 2011). The unbypassed cathode resistor makes the stage memoryless,
    and the coupling capacitor Cc is a short at audio, so the plate swing seen by the
    next stage is a static function of vin with the AC load line Rp || RL (Cc holds the
    quiescent plate voltage). Cc's slow charge drift is applied after the table as a 3 Hz
    1st-order high-pass (Cc into the next stage's grid leak).

    The static solution (a 3-unknown nonlinear system: grid, cathode and plate node
    voltages) is solved once with Newton–Raphson to ~1e-12 V and stored in a shared,
    immutable table (value + exact slope per node, cubic Hermite interpolation).
*/
struct TriodeStage
{
    // Koren 12AX7
    static constexpr double mu  = 100.0;
    static constexpr double ex  = 1.4;
    static constexpr double kg1 = 1060.0;
    static constexpr double kp  = 600.0;
    static constexpr double kvb = 300.0;

    // Dempwolf grid-current law: Ig = G * (ln (1 + e^(C vgk)) / C)^xi
    static constexpr double gridG  = 6.06e-4;
    static constexpr double gridXi = 1.354;
    static constexpr double gridC  = 13.9;

    // circuit
    static constexpr double supply   = 250.0;
    static constexpr double plateR   = 100.0e3;
    static constexpr double cathodeR = 1.5e3;
    static constexpr double loadR    = 470.0e3;
    static constexpr double sourceR  = 33.0e3;

    struct Solution
    {
        double grid = 0.0, cathode = 0.0, plate = 0.0;
        double plateSlope = 0.0;    // d(plate)/d(vin)
        bool converged = false;
    };

    static double plateCurrent (double vgk, double vpk) noexcept;
    static double gridCurrent (double vgk) noexcept;

    /** Quiescent (DC, vin = 0, no AC load) operating point. */
    static Solution quiescent() noexcept;

    /**
        Solves the stage for an instantaneous input voltage, with the coupling capacitor
        holding the quiescent plate voltage (AC load line Rp || RL). `guess` warm-starts NR.
    */
    static Solution solve (double vin, const Solution& guess) noexcept;
};

/**
    Shared, immutable lookup of the record-amp transfer: input volts -> plate swing
    (Vp - Vp0, volts; inverting). Built once on first use (from prepare(), never on the
    audio thread) and shared by every ReelModel instance.
*/
class RecordAmpCurve
{
public:
    static const RecordAmpCurve& instance();

    static constexpr double inputRange   = 64.0;    // volts; input is clamped to +-inputRange
    static constexpr int    numIntervals = 8192;

    /** Plate swing in volts for an input of vin volts (cubic Hermite, C1-continuous). */
    double process (double vin) const noexcept
    {
        double u = (vin + inputRange) * invStep;

        if (! (u > 0.0))                    // also catches NaN
            u = 0.0;
        else if (u > numIntervals - 1.0e-9)
            u = numIntervals - 1.0e-9;

        const auto i = (int) u;
        const auto t = u - i;
        const auto& a = nodes[(size_t) i];
        const auto& b = nodes[(size_t) i + 1];

        // cubic Hermite with slopes scaled to the interval
        const auto d0 = a.slope * step;
        const auto d1 = b.slope * step;
        const auto c2 = 3.0 * (b.value - a.value) - 2.0 * d0 - d1;
        const auto c3 = 2.0 * (a.value - b.value) + d0 + d1;
        return a.value + t * (d0 + t * (c2 + t * c3));
    }

    /** d(plate swing)/d(vin) at vin = 0 (negative: the stage inverts). */
    double smallSignalGain() const noexcept { return gainAtZero; }
    double quiescentPlateVolts() const noexcept { return quiescentPlate; }
    bool allConverged() const noexcept { return converged; }

private:
    RecordAmpCurve();

    struct Node { double value, slope; };

    static constexpr double step    = 2.0 * inputRange / numIntervals;
    static constexpr double invStep = numIntervals / (2.0 * inputRange);

    std::vector<Node> nodes;
    double gainAtZero = 0.0, quiescentPlate = 0.0;
    bool converged = true;
};

} // namespace umber::dsp::reel
