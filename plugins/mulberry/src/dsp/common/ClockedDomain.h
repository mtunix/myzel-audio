#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

namespace mulberry::dsp
{

/**
    An analog low-pass as parallel first-order complex modes, one per conjugate pole pair:
    H(s) = sum_i [ r_i / (s - p_i) + conj (r_i) / (s - conj (p_i)) ].
*/
template <int Modes>
struct ModalFilter
{
    std::array<std::complex<double>, (size_t) Modes> poles {}, residues {};

    /** Butterworth low-pass of order 2 Modes, unity DC gain, cutoff in Hz. */
    static ModalFilter butterworth (double cutoffHz) noexcept
    {
        ModalFilter f;
        constexpr int order = 2 * Modes;
        const auto wc = 2.0 * std::numbers::pi * cutoffHz;
        std::array<std::complex<double>, (size_t) order> all {};
        for (int k = 0; k < order; ++k)
            all[(size_t) k] = wc * std::polar (1.0, std::numbers::pi * (2.0 * k + 1.0 + order) / (2.0 * order));

        std::complex<double> gain { 1.0, 0.0 };
        for (const auto& p : all)
            gain *= -p;

        int m = 0;
        for (int k = 0; k < order && m < Modes; ++k)
        {
            if (all[(size_t) k].imag() <= 0.0)
                continue;
            std::complex<double> den { 1.0, 0.0 };
            for (int j = 0; j < order; ++j)
                if (j != k)
                    den *= all[(size_t) k] - all[(size_t) j];
            f.poles[(size_t) m] = all[(size_t) k];
            f.residues[(size_t) m] = gain / den;
            ++m;
        }
        return f;
    }

    std::complex<double> response (double hz) const noexcept
    {
        const std::complex<double> s { 0.0, 2.0 * std::numbers::pi * hz };
        std::complex<double> h {};
        for (int i = 0; i < Modes; ++i)
            h += residues[(size_t) i] / (s - poles[(size_t) i]) + std::conj (residues[(size_t) i]) / (s - std::conj (poles[(size_t) i]));
        return h;
    }
};

/**
    The clocked domain of Holters & Parker's combined model of a bucket-brigade device and its
    input and output filters (DAFx-18), shared by Bucket and Rack.

    - Input filter: its modes run at the host rate, the input taken as impulses at the sample
      instants. At each clock instant inside a host period the device samples the filter's exact
      continuous-time output there, from the modes' free response since the period start.
    - Output filter: the device's held output is a staircase; each step, at its exact instant,
      drives the output modes (the exact response of a mode to a step), whose state is read at the
      host sample instants.

    Nothing is interpolated at the host rate: the device's own aliasing and clock images come out
    as on the chip, and the implementation adds none of its own. Clock instants are carried as a
    phase in double precision.

    Per host sample: beginSample() returns the number of clock events in the period; for each
    event, sampleInput(), then pushOutput() for every bus whose held value changes, then
    nextEvent(); finally output() for each bus and endSample (u) with the host input sample.
*/
template <int Modes, int Buses>
class ClockedDomain
{
public:
    void prepare (double sampleRate, const ModalFilter<Modes>& input, const ModalFilter<Modes>& output) noexcept
    {
        fs = sampleRate;
        T = 1.0 / sampleRate;
        setFilters (input, output);
        reset();
    }

    void setFilters (const ModalFilter<Modes>& input, const ModalFilter<Modes>& output) noexcept
    {
        in = input;
        out = output;
        for (size_t i = 0; i < (size_t) Modes; ++i)
        {
            inPole[i] = std::exp (in.poles[i] * T);
            outPole[i] = std::exp (out.poles[i] * T);
            outGain[i] = -out.residues[i] / out.poles[i];   // a mode's steady state per unit held value
        }
        clockStep = -1.0;
    }

    void reset() noexcept
    {
        x = {};
        w = {};
        held = {};
        clockPhase = 0.0;
        events = 0;
    }

    /** Clock rate for the coming host period (Hz). */
    void setClock (double hz) noexcept
    {
        const auto step = std::clamp (hz / fs, 1.0e-6, 64.0);
        if (std::abs (step - clockStep) > 1.0e-15)
        {
            clockStep = step;
            for (size_t i = 0; i < (size_t) Modes; ++i)
            {
                inStep[i] = std::exp (in.poles[i] * T / clockStep);
                outStep[i] = std::exp (-out.poles[i] * T / clockStep);
            }
        }
    }

    /** Starts a host period; returns the number of clock events in it. */
    int beginSample() noexcept
    {
        // The output states move to the end of this period; the steps in it are added there.
        for (auto& bus : w)
            for (size_t i = 0; i < (size_t) Modes; ++i)
                bus[i] *= outPole[i];

        const auto start = clockPhase;
        clockPhase += clockStep;
        events = (int) (std::floor (clockPhase) - std::floor (start));
        if (events > 0)
        {
            const auto first = (std::floor (start) + 1.0 - start) / clockStep;   // in host periods
            for (size_t i = 0; i < (size_t) Modes; ++i)
            {
                inPhasor[i] = std::exp (in.poles[i] * T * first);
                outPhasor[i] = std::exp (out.poles[i] * T * (1.0 - first));
            }
        }
        if (clockPhase > 1.0e9)
            clockPhase -= std::floor (clockPhase);
        return events;
    }

    /** The input filter's exact output at the current clock instant. */
    double sampleInput() const noexcept
    {
        double v = 0.0;
        for (size_t i = 0; i < (size_t) Modes; ++i)
            v += 2.0 * (in.residues[i] * inPhasor[i] * x[i]).real();
        return v;
    }

    /** The device's new held value on a bus at the current clock instant. */
    void pushOutput (int bus, double value) noexcept
    {
        const auto delta = value - held[(size_t) bus];
        held[(size_t) bus] = value;
        if (std::abs (delta) > 0.0)
            for (size_t i = 0; i < (size_t) Modes; ++i)
                w[(size_t) bus][i] -= delta * outGain[i] * outPhasor[i];
    }

    void nextEvent() noexcept
    {
        for (size_t i = 0; i < (size_t) Modes; ++i)
        {
            inPhasor[i] *= inStep[i];
            outPhasor[i] *= outStep[i];
        }
    }

    /** The output filter on a bus at the end of the period (after all events). */
    double output (int bus) const noexcept
    {
        // Each mode is its free part w plus the steady state of the held value.
        double v = 0.0;
        for (size_t i = 0; i < (size_t) Modes; ++i)
            v += 2.0 * (w[(size_t) bus][i] + outGain[i] * held[(size_t) bus]).real();
        return v;
    }

    /** Ends the period with the host input sample (an impulse of area u T at the period end). */
    void endSample (double u) noexcept
    {
        for (size_t i = 0; i < (size_t) Modes; ++i)
            x[i] = inPole[i] * x[i] + u * T;
    }

    void flush() noexcept
    {
        auto f = [] (std::complex<double>& c) { if (std::abs (c.real()) < 1.0e-30 && std::abs (c.imag()) < 1.0e-30) c = {}; };
        for (auto& v : x) f (v);
        for (auto& bus : w)
            for (auto& v : bus) f (v);
    }

    int eventsThisSample() const noexcept { return events; }
    double clockRate() const noexcept { return clockStep * fs; }

private:
    double fs = 48000.0, T = 1.0 / 48000.0;
    ModalFilter<Modes> in, out;
    std::array<std::complex<double>, (size_t) Modes> inPole {}, outPole {}, outGain {};
    std::array<std::complex<double>, (size_t) Modes> inStep {}, outStep {}, inPhasor {}, outPhasor {};
    std::array<std::complex<double>, (size_t) Modes> x {};
    std::array<std::array<std::complex<double>, (size_t) Modes>, (size_t) Buses> w {};
    std::array<double, (size_t) Buses> held {};
    double clockPhase = 0.0, clockStep = -1.0;
    int events = 0;
};

} // namespace mulberry::dsp
