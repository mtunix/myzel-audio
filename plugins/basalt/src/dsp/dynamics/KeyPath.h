#pragma once

#include "common/dsp/Filters.h"
#include "common/dsp/FloatCompare.h"
#include "dsp/dynamics/Glide.h"
#include "dsp/dynamics/Levels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

// The key before the detectors: the key EQ, and the delay line that gives the detector its
// look-ahead (NOTES.md, Key path and key EQ, Look-ahead).

namespace basalt::dynamics
{

/** The latency's look-ahead share at most; the window's ring is sized for this plus the feedback hold. */
inline constexpr double maxLookaheadMs = 10.0;
inline constexpr double windowCapacityMs = 15.0;

/** A Look-ahead change glides the key tap over this long. */
inline constexpr double lookaheadGlideMs = 50.0;

/** The largest key magnitude the detectors see, +120 dBFS: its square stays finite. */
inline constexpr double keyLimit = 1.0e6;

/**
    The key as the detectors take it (NOTES.md, Key path): a NaN or an infinity would stay in the
    key EQ's, the mean squares' and the crest detectors' states for good, so it reads as silence.
    The audio itself passes untouched.
*/
inline double saneKey (double x) noexcept
{
    return std::isfinite (x) ? std::clamp (x, -keyLimit, keyLimit) : 0.0;
}

/**
    |k| as saneKey leaves it, for a model's own level path (VCA, FET, Vari-mu): a model driven
    without the chain cannot hold a NaN or an infinity in a window or a state either.
*/
inline double saneMagnitude (double k) noexcept
{
    const auto a = std::abs (k);
    return a <= keyLimit ? a : (a > keyLimit ? keyLimit : 0.0);
}

/** Samples a RunningMax must hold for the look-ahead window plus the feedback pre-hold at this rate. */
inline int windowCapacity (double sampleRate) noexcept
{
    return (int) std::ceil (windowCapacityMs * 0.001 * sampleRate) + 1;
}

/** What the key EQ reads each block; the chain decides Off from the parameters' read-outs. */
struct KeyEqSettings
{
    bool highPass = false;
    double highPassHz = 20.0;
    bool lowPass = false;
    double lowPassHz = 20000.0;
    double bellHz = 1000.0;
    double bellDb = 0.0;    // 0 dB skips the bell
    double bellQ = 1.0;
};

/**
    High-pass and low-pass (12 dB/octave Butterworth, one TPT SVF each) and one bell (an SVF at
    q = Q·A, out = x + k·(A² − 1)·bp, A = 10^(gain/40), k = 1/(Q·A): Simper's mixing), in that
    order, per detector channel in double.

    Nothing jumps (NOTES.md, Key path): frequencies and Q glide in log, the bell's gain in dB,
    and a pass filter turned on or off fades between the filtered and the plain key, each over
    20 ms on SmoothGlide's curve, the coefficients following per sample while they move. A stage
    at rest off (or at 0 dB) is skipped, and starts from rest when it comes back.
*/
class KeyEq
{
public:
    static constexpr double glideMs = 20.0;

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate;
        for (auto* stage : { &highPass, &lowPass })
        {
            stage->amount.prepare (glideMs, rate);
            stage->logHz.prepare (glideMs, rate);
        }
        bell.db.prepare (glideMs, rate);
        bell.logHz.prepare (glideMs, rate);
        bell.logQ.prepare (glideMs, rate);
        settingsValid = false;
        reset();
    }

    /** The filters at rest and every glide landed on its target. */
    void reset() noexcept
    {
        for (auto* stage : { &highPass, &lowPass })
        {
            for (auto& f : stage->filters)
                f.reset();
            stage->amount.jumpTo (stage->amount.targetValue());
            stage->logHz.jumpTo (stage->logHz.targetValue());
        }

        for (auto& f : bell.filters)
            f.reset();
        bell.db.jumpTo (bell.db.targetValue());
        bell.logHz.jumpTo (bell.logHz.targetValue());
        bell.logQ.jumpTo (bell.logQ.targetValue());

        if (settingsValid)
        {
            highPass.setCoefficients (rate);
            lowPass.setCoefficients (rate);
            bell.setCoefficients (rate);
        }
    }

    /** Per block: the targets. */
    void set (const KeyEqSettings& s) noexcept
    {
        const auto first = ! settingsValid;
        highPass.set (s.highPass, s.highPassHz, first, rate);
        lowPass.set (s.lowPass, s.lowPassHz, first, rate);
        bell.set (s, first, rate);
        settingsValid = true;
    }

    /** One frame of key, a value per channel, in place. */
    void process (double* frame, int numChannels) noexcept
    {
        std::array<double*, maxDetectorChannels> key { frame, frame + 1 };
        process (key.data(), numChannels, 1);
    }

    /** n samples of key per channel, in place. */
    void process (double* const* key, int numChannels, int n) noexcept
    {
        // At rest every stage is a fixed filter, and the channels' independent recursions run side by side.
        if (highPass.atRest() && lowPass.atRest() && bell.atRest())
        {
            const auto hp = highPass.active(), lp = lowPass.active(), peak = bell.active();
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < numChannels; ++c)
                {
                    auto x = key[c][i];
                    if (hp)
                        x = highPass.filters[(size_t) c].processHighpass (x);
                    if (lp)
                        x = lowPass.filters[(size_t) c].processLowpass (x);
                    if (peak)
                        x += bell.mix * bell.filters[(size_t) c].process (x).bp;
                    key[c][i] = x;
                }
            return;
        }

        if (highPass.active())
            highPass.process<true> (key, numChannels, n, rate);
        if (lowPass.active())
            lowPass.process<false> (key, numChannels, n, rate);
        if (bell.active())
            bell.process (key, numChannels, n, rate);
    }

    /** Every stage off (or at 0 dB) and at rest: the key passes untouched. */
    bool isFlat() const noexcept { return ! highPass.active() && ! lowPass.active() && ! bell.active(); }

private:
    struct PassStage
    {
        std::array<myzel::dsp::TptSvf, maxDetectorChannels> filters;
        SmoothGlide amount, logHz;     // amount: 0 off, 1 on

        bool active() const noexcept { return amount.moving() || amount.current() > 0.0; }
        bool atRest() const noexcept { return ! amount.moving() && ! logHz.moving(); }

        void setCoefficients (double rate) noexcept
        {
            constexpr auto butterworth = std::numbers::sqrt2 / 2.0;
            const auto hz = std::exp (logHz.current());
            for (auto& f : filters)
                f.setCoefficients (hz, butterworth, rate);
        }

        void set (bool on, double hz, bool first, double rate) noexcept
        {
            if (first || (on && ! active()))
            {
                // From rest: the frequency is where it is going, and an off stage stays as it is.
                for (auto& f : filters)
                    f.reset();
                logHz.jumpTo (std::log (hz));
                setCoefficients (rate);
                if (first)
                {
                    amount.jumpTo (on ? 1.0 : 0.0);
                    return;
                }
            }

            amount.setTarget (on ? 1.0 : 0.0);
            if (on)
                logHz.setTarget (std::log (hz));
        }

        template <bool high>
        void process (double* const* key, int numChannels, int n, double rate) noexcept
        {
            // Active and at rest means fully on.
            if (! amount.moving() && ! logHz.moving())
            {
                for (int c = 0; c < numChannels; ++c)
                {
                    auto& f = filters[(size_t) c];
                    auto* x = key[c];
                    for (int i = 0; i < n; ++i)
                        x[i] = high ? f.processHighpass (x[i]) : f.processLowpass (x[i]);
                }
                return;
            }

            for (int i = 0; i < n; ++i)
            {
                const auto mix = std::clamp (amount.next(), 0.0, 1.0);
                if (logHz.moving())
                {
                    logHz.next();
                    setCoefficients (rate);
                }

                for (int c = 0; c < numChannels; ++c)
                {
                    auto& f = filters[(size_t) c];
                    auto& x = key[c][i];
                    const auto y = high ? f.processHighpass (x) : f.processLowpass (x);
                    x = mix >= 1.0 ? y : x + mix * (y - x);
                }
            }
        }
    };

    struct BellStage
    {
        std::array<myzel::dsp::TptSvf, maxDetectorChannels> filters;
        SmoothGlide db, logHz, logQ;
        double mix = 0.0;

        bool active() const noexcept { return db.moving() || ! myzel::dsp::isExactly (db.current(), 0.0); }
        bool moving() const noexcept { return db.moving() || logHz.moving() || logQ.moving(); }
        bool atRest() const noexcept { return ! moving(); }

        void setCoefficients (double rate) noexcept
        {
            const auto a = std::pow (10.0, db.current() / 40.0);
            const auto q = std::exp (logQ.current());
            const auto hz = std::exp (logHz.current());
            for (auto& f : filters)
                f.setCoefficients (hz, q * a, rate);
            mix = (a * a - 1.0) / (q * a);
        }

        void set (const KeyEqSettings& s, bool first, double rate) noexcept
        {
            if (first || ! active())
            {
                if (! first && myzel::dsp::isExactly (s.bellDb, 0.0))
                    return;

                // At 0 dB the bell adds nothing whatever its state, so it starts from rest at its new frequency and Q.
                for (auto& f : filters)
                    f.reset();
                logHz.jumpTo (std::log (s.bellHz));
                logQ.jumpTo (std::log (s.bellQ));
                if (first)
                    db.jumpTo (s.bellDb);
                setCoefficients (rate);
            }

            db.setTarget (s.bellDb);
            logHz.setTarget (std::log (s.bellHz));
            logQ.setTarget (std::log (s.bellQ));
        }

        void process (double* const* key, int numChannels, int n, double rate) noexcept
        {
            if (! moving())
            {
                for (int c = 0; c < numChannels; ++c)
                {
                    auto& f = filters[(size_t) c];
                    auto* x = key[c];
                    for (int i = 0; i < n; ++i)
                        x[i] += mix * f.process (x[i]).bp;
                }
                return;
            }

            for (int i = 0; i < n; ++i)
            {
                if (moving())
                {
                    db.next();
                    logHz.next();
                    logQ.next();
                    setCoefficients (rate);
                }

                for (int c = 0; c < numChannels; ++c)
                    key[c][i] += mix * filters[(size_t) c].process (key[c][i]).bp;
            }
        }
    };

    PassStage highPass, lowPass;
    BellStage bell;
    bool settingsValid = false;
    double rate = 48000.0;
};

/**
    The key's delay line. The audio is delayed by the latency D; reading the key at D − LA gives
    the detector LA of look-ahead. Fractional delays read with linear interpolation (the key only,
    while Look-ahead glides; the audio never moves).
*/
class KeyDelay
{
public:
    /** Not real-time safe. */
    void prepare (int numChannels, int maximumDelaySamples)
    {
        size_t capacity = 1;
        while (capacity < (size_t) std::max (maximumDelaySamples, 0) + 2)
            capacity <<= 1;

        mask = capacity - 1;
        for (auto& r : rings)
            r.assign (numChannels > 0 ? capacity : 0, 0.0);
        channels = std::clamp (numChannels, 0, maxDetectorChannels);
        reset();
    }

    void reset() noexcept
    {
        for (auto& r : rings)
            std::fill (r.begin(), r.end(), 0.0);
        write = 0;
    }

    /** One frame, one value per channel. */
    void push (const double* frame) noexcept
    {
        ++write;
        for (int c = 0; c < channels; ++c)
            rings[(size_t) c][write & mask] = frame[c];
    }

    /** The value `delaySamples` before the newest (0 = the newest), 0 … the prepared maximum. */
    double read (int channel, double delaySamples) const noexcept
    {
        const auto& r = rings[(size_t) channel];
        const auto whole = (size_t) delaySamples;
        const auto frac = delaySamples - (double) whole;
        const auto a = r[(write - whole) & mask];

        if (frac <= 0.0)
            return a;

        const auto b = r[(write - whole - 1) & mask];
        return a + frac * (b - a);
    }

private:
    std::array<std::vector<double>, maxDetectorChannels> rings;
    size_t write = 0, mask = 0;
    int channels = 0;
};

} // namespace basalt::dynamics
