#pragma once

#include "common/dsp/AnalyticFilters.h"
#include "common/dsp/Oversampler2x.h"
#include "dsp/common/TapeCore.h"
#include "dsp/common/Modulation.h"
#include "dsp/common/TapeLosses.h"

#include <array>
#include <cmath>
#include <complex>
#include <functional>

namespace mulberry::dsp
{

/** A 4-pole Butterworth low-pass (two bilinear biquads, prewarped corner). */
class ButterworthLowpass4
{
public:
    void design (double hz, double sampleRate) noexcept
    {
        const auto k = std::tan (std::numbers::pi * std::min (hz, 0.45 * sampleRate) / sampleRate);
        const std::array<double, 2> q { 0.54119610014619698, 1.3065629648763766 };
        for (size_t i = 0; i < 2; ++i)
        {
            auto& s = sections[i];
            const auto norm = 1.0 / (1.0 + k / q[i] + k * k);
            s.b0 = k * k * norm;
            s.b1 = 2.0 * s.b0;
            s.b2 = s.b0;
            s.a1 = 2.0 * (k * k - 1.0) * norm;
            s.a2 = (1.0 - k / q[i] + k * k) * norm;
        }
    }

    void reset() noexcept
    {
        for (auto& s : sections)
            s.z1 = s.z2 = 0.0;
    }

    void flush() noexcept
    {
        for (auto& s : sections)
        {
            if (std::abs (s.z1) < 1.0e-30) s.z1 = 0.0;
            if (std::abs (s.z2) < 1.0e-30) s.z2 = 0.0;
        }
    }

    double process (double x) noexcept
    {
        for (auto& s : sections)
        {
            const auto y = s.b0 * x + s.z1;
            s.z1 = s.b1 * x - s.a1 * y + s.z2;
            s.z2 = s.b2 * x - s.a2 * y;
            x = y;
        }
        return x;
    }

    /** Group delay at low frequencies, in samples of the rate it runs at. */
    double lowFrequencyDelay (double sampleRate) const noexcept
    {
        const auto f = 1.0e-4 * sampleRate, df = 1.0e-3 * f;
        const auto phase = [&] (double hz) { return std::arg (response (hz, sampleRate)); };
        return -(phase (f + df) - phase (f - df)) / (2.0 * std::numbers::pi * 2.0 * df) * sampleRate;
    }

    std::complex<double> response (double hz, double sampleRate) const noexcept
    {
        const auto z1 = std::polar (1.0, -2.0 * std::numbers::pi * hz / sampleRate), z2 = z1 * z1;
        std::complex<double> h { 1.0, 0.0 };
        for (const auto& s : sections)
            h *= (s.b0 + s.b1 * z1 + s.b2 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2);
        return h;
    }

private:
    struct Section
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
    };
    std::array<Section, 2> sections {};
};

/**
    A tape machine's record path for one line, oversampled inside the loop: record amplifier,
    pre-emphasis, the record head's field, the tape (the shared Jiles-Atherton equations,
    validated in Umber's models/reel/NOTES.md, solved by TapeCore), the exact inverse de-emphasis
    and the playback losses (which depend only on the recorded wavelength, so they can be applied
    here with the speed at which the tape is written). Every repeat goes through it again, so
    loud trails compress and lose top, as on tape.

    Aliasing (NOTES.md of loop/): the nonlinear part (record amp to de-emphasis) runs at 8x, and
    the record amp's own bandwidth (a 4-pole low-pass) keeps its harmonics from being boosted by
    the pre-emphasis into the core, where they would fold back. The linear rest (output coupling,
    losses) runs at 2x, where the losses are designed.

    Small-signal gain is set by the field scale and the core's initial susceptibility (divided
    out) and the record amp's slope at its operating point (sech^2, a fraction of a dB: the amp is
    not normalised, so its slope never exceeds 1), so Drive changes colour, not level. At large
    signals the core saturates (|m| <= 1), so the path limits. Without the core (magnetic false)
    the record amp is the only nonlinearity: a charge medium.
*/
class TapeRecordPath
{
public:
    struct Voicing
    {
        double emphasisMicroseconds = 70.0;     // record pre-emphasis time constant
        double emphasisLimitHz = 12000.0;       // the boost stops rising here (no effect on the linear response)
        double ampBandwidthHz = 24000.0;        // the record amp's own low-pass (4 poles)
        double fieldScale = 1.2;                // core field (h) for a 1.0 record level
        double ampHeadroom = 2.0;               // record amp: soft limit
        double ampBias = 0.3;                   // and its asymmetry (2nd harmonic)
        TapeCore::Parameters core {};
        double coreExpansion = 1.0;             // expansion (core): divided out
        bool magnetic = true;                   // false: no head or core (a charge medium)
        double couplingHz = 0.0;                // > 0: capacitive coupling, a high-pass on write and read
    };

    static constexpr double headSaturation = 12.0;   // record-head core (as Umber's Reel)

    /**
        The largest describing-function gain of a core over its small-signal gain, over drive
        levels: a loop with little bias (low c) expands (Rayleigh region) before it saturates, by
        up to several dB. The path divides it out, so its gain never exceeds 1 at any level and
        the loop at 100 % still decays; an under-biased tape is a little quieter at low level.
    */
    static double expansion (const TapeCore::Parameters& p) noexcept
    {
        TapeCore core (p);
        const auto chi = core.initialSusceptibility();
        constexpr int perPeriod = 64;
        double worst = 1.0;
        for (double a = 0.003; a < 30.0; a *= 1.35)
        {
            core.reset();
            std::complex<double> fundamental {};
            for (int period = 0; period < 4; ++period)
                for (int i = 0; i < perPeriod; ++i)
                {
                    const auto phase = 2.0 * std::numbers::pi * i / perPeriod;
                    const auto m = core.process (a * std::sin (phase));
                    if (period == 3)
                        fundamental += m * std::polar (1.0, -phase);
                }
            const auto gain = 2.0 * std::abs (fundamental) / perPeriod / a;
            worst = std::max (worst, gain / chi);
        }
        return worst;
    }

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        voicingSet = false;
        setVoicing (voicing);
        reset();
    }

    void reset() noexcept
    {
        oversampler.reset();
        preEmphasis.reset();
        deEmphasis.reset();
        tape.reset();
        ampBandwidth.reset();
        couplingIn.reset();
        couplingOut.reset();
        lossState = {};
    }

    /** Losses for the tape speed now, designed at 2 fs (where the linear part of the path runs). */
    void setLosses (const TapeLosses::Coefficients& c) noexcept { losses = c; }

    void setVoicing (const Voicing& v) noexcept
    {
        if (voicingSet && sameVoicing (v, voicing))
            return;
        voicing = v;
        voicingSet = true;
        if (v.emphasisMicroseconds > 0.0)
        {
            const auto zeroHz = 1.0e6 / (2.0 * std::numbers::pi * v.emphasisMicroseconds);
            preEmphasis.design (zeroHz, std::max (v.emphasisLimitHz, zeroHz * 1.5), nonlinearRate());
        }
        else
        {
            preEmphasis.design (1.0e9, 1.0e9, nonlinearRate());
        }
        deEmphasis.designInverse (preEmphasis);
        ampBandwidth.design (v.ampBandwidthHz, nonlinearRate());
        ampDelay = ampBandwidth.lowFrequencyDelay (nonlinearRate()) / (double) Oversampling::factor;
        tape.setParameters (v.core);
        ampOffset = v.ampHeadroom * softLimit (v.ampBias / v.ampHeadroom);
        outputGain = v.magnetic ? 1.0 / (v.fieldScale * tape.initialSusceptibility() * std::max (v.coreExpansion, 1.0)) : 1.0;
        if (v.couplingHz > 0.0)
        {
            couplingIn.setG (myzel::dsp::analytic::prewarpedG (v.couplingHz, nonlinearRate()));
            couplingOut.setG (myzel::dsp::analytic::prewarpedG (2.0 * v.couplingHz, 2.0 * fs));
        }
    }

    /**
        The record amp's small-signal gain: the tanh stage is not normalised at its operating
        point (its slope peaks at 1 there, offset from 0), so it never gains: sech^2 (offset).
    */
    static double ampSmallSignal (const Voicing& v) noexcept
    {
        // The Padé approximant's slope: 1 - 8 u^2 (u^2 + 9) / (9 u^2 + 27)^2 ... evaluated numerically.
        const auto u = v.ampBias / v.ampHeadroom, d = 1.0e-6;
        return (softLimit (u + d) - softLimit (u - d)) / (2.0 * d);
    }

    /** The record amp's transfer: the Padé tanh approximant, flat beyond +-3 (where its slope is 0). */
    static double softLimit (double x) noexcept
    {
        const auto u = std::clamp (x, -3.0, 3.0);
        const auto u2 = u * u;
        return u * (27.0 + u2) / (27.0 + 9.0 * u2);
    }

    /** The small-signal response of the path's linear parts at a frequency (host rate fs). */
    std::complex<double> linearResponse (double hz) const noexcept
    {
        auto h = ampSmallSignal (voicing) * ampBandwidth.response (hz, nonlinearRate());
        if (voicing.couplingHz > 0.0)
            h *= couplingIn.highpassResponse (hz, nonlinearRate()) * couplingOut.highpassResponse (hz, 2.0 * fs);
        return h;
    }

    /** Group delay of the path at low frequencies (host samples): the oversampler's and the record amp's bandwidth. */
    double latencySamples() const noexcept { return oversampler.latencySamples() + ampDelay; }

    /** The oversampler's response (host-rate normalised frequency), for predictions. */
    std::complex<double> oversamplerResponse (double normalisedFrequency) const noexcept { return oversampler.response (normalisedFrequency); }

    /**
        One host sample of two lines at once (their chains are independent, so the processor
        overlaps them). hiss: RMS of the tape's noise (added before de-emphasis) in record-level
        units, drawn at 8x with the in-band density of a 2x draw.
    */
    static void processPair (TapeRecordPath& a, TapeRecordPath& b, std::array<float, 2> x, float hiss,
                             WhiteNoise& noiseA, WhiteNoise& noiseB, std::array<float, 2>& y) noexcept
    {
        std::array<double, 2> twiceA, twiceB;
        a.oversampler.outer.upsample ((double) x[0], twiceA[0], twiceA[1]);
        b.oversampler.outer.upsample ((double) x[1], twiceB[0], twiceB[1]);
        const auto h = hiss > 0.0f ? (double) hiss * std::sqrt (0.5 * (double) Oversampling::factor) : 0.0;

        for (size_t i = 0; i < 2; ++i)
        {
            std::array<double, Oversampling::perDouble> ua, ub;
            a.oversampler.upsampleFromDouble (twiceA[i], ua.data());
            b.oversampler.upsampleFromDouble (twiceB[i], ub.data());
            for (size_t j = 0; j < ua.size(); ++j)
            {
                ua[j] = a.nonlinear (ua[j], h > 0.0 ? h * (double) noiseA.next() : 0.0);
                ub[j] = b.nonlinear (ub[j], h > 0.0 ? h * (double) noiseB.next() : 0.0);
            }
            twiceA[i] = a.linear (a.oversampler.downsampleToDouble (ua.data()));
            twiceB[i] = b.linear (b.oversampler.downsampleToDouble (ub.data()));
        }

        const auto ya = a.oversampler.outer.downsample (twiceA[0], twiceA[1]);
        const auto yb = b.oversampler.outer.downsample (twiceB[0], twiceB[1]);
        y = { std::abs (ya) < 1.0e-30 ? 0.0f : (float) ya, std::abs (yb) < 1.0e-30 ? 0.0f : (float) yb };
    }

    void flush() noexcept
    {
        preEmphasis.flush();
        deEmphasis.flush();
        couplingIn.flush();
        couplingOut.flush();
        ampBandwidth.flush();
        TapeLosses::flush (lossState);
    }

private:
    using Oversampling = myzel::dsp::OversamplerCascade<8>;

    static bool sameVoicing (const Voicing& a, const Voicing& b) noexcept
    {
        const std::equal_to<double> eq;
        return eq (a.emphasisMicroseconds, b.emphasisMicroseconds) && eq (a.emphasisLimitHz, b.emphasisLimitHz)
            && eq (a.ampBandwidthHz, b.ampBandwidthHz) && eq (a.fieldScale, b.fieldScale) && eq (a.ampHeadroom, b.ampHeadroom)
            && eq (a.ampBias, b.ampBias) && eq (a.core.alpha, b.core.alpha) && eq (a.core.k, b.core.k) && eq (a.core.c, b.core.c)
            && eq (a.coreExpansion, b.coreExpansion) && a.magnetic == b.magnetic && eq (a.couplingHz, b.couplingHz);
    }
    double nonlinearRate() const noexcept { return (double) Oversampling::factor * fs; }

    double nonlinear (double x, double hiss) noexcept
    {
        // Record amp: a soft, slightly asymmetric stage (its operating point gives the 2nd harmonic),
        // and its bandwidth.
        const auto& v = voicing;
        auto y = ampBandwidth.process (v.ampHeadroom * softLimit ((x + v.ampBias) / v.ampHeadroom) - ampOffset);
        if (v.couplingHz > 0.0)
            y = couplingIn.highpass (y);

        if (v.magnetic)
        {
            // Record EQ and head: the field, with the head core's soft saturation.
            const auto h = v.fieldScale * preEmphasis.process (y);
            const auto field = h / std::sqrt (1.0 + h * h * (1.0 / (headSaturation * headSaturation)));
            const auto m = tape.process (field) + hiss / outputGain;
            y = deEmphasis.process (m) * outputGain;
        }
        else
        {
            y += hiss;
        }
        return y;
    }

    double linear (double y) noexcept
    {
        if (voicing.couplingHz > 0.0)
            y = couplingOut.highpass (y);
        return TapeLosses::processDouble (y, losses, lossState);
    }

    double fs = 48000.0;
    Voicing voicing;
    Oversampling oversampler;
    myzel::dsp::analytic::ShelfSection preEmphasis, deEmphasis;
    myzel::dsp::analytic::OnePole couplingIn, couplingOut;
    ButterworthLowpass4 ampBandwidth;
    double ampDelay = 0.0;
    bool voicingSet = false;
    TapeCore tape;
    TapeLosses::Coefficients losses;
    TapeLosses::State lossState;
    double ampOffset = 0.0, outputGain = 1.0;
};

} // namespace mulberry::dsp
