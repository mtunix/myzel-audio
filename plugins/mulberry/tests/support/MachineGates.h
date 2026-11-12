#pragma once

#include "support/EchoAnalysis.h"

#include <chrono>
#include <map>
#include <string>

/**
    The quality gates of PLAN.md as measurements, shared by every machine's tests and by
    MulberryMeasure (which prints them). Each returns numbers; the tests decide pass or fail.
*/
namespace mulberry::gates
{

using namespace analysis;

/** The chain's DC blocker (10 Hz TPT one-pole high-pass) at a frequency. */
inline double dcBlockGain (double hz, double fs)
{
    const auto g = std::tan (std::numbers::pi * 10.0 / fs);
    const auto t = std::tan (std::numbers::pi * std::min (hz, 0.49 * fs) / fs);
    return t / std::sqrt (t * t + g * g);
}

inline dsp::ChainSettings machineAt (Machine m, float timeMs, float feedback = 0.0f)
{
    auto s = wetOnly (m);
    s.timeMs = { timeMs, timeMs };
    s.feedback = feedback;
    return s;
}

//==============================================================================
/** First echo arrival (group delay at 1 kHz of an impulse's first echo), in ms. */
inline double echoTimeMs (Machine m, double fs, float timeMs, float paramA = 0.5f, float paramB = 0.5f)
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = machineAt (m, timeMs);
    s.paramA = paramA;
    s.paramB = paramB;
    const auto expected = (double) timeMs * 0.001 * fs;
    const auto length = (size_t) (expected * 1.1) + (size_t) (0.05 * fs);
    // Settle the transport (and any start-up glide) before the impulse.
    const auto settle = (size_t) (0.5 * fs);
    const auto out = render (chain, s, impulse (length + settle, fs, 0.1f, settle), length + settle);
    const auto window = (size_t) (0.004 * fs);
    const auto centre = settle + (size_t) expected;
    return (groupDelay (out.left, centre - window, centre + window, 1000.0, fs) - (double) settle) * 1000.0 / fs;
}

//==============================================================================
struct RepeatLoss
{
    double frequency = 0.0;
    double measuredDb = 0.0;        // mean level step per repeat over the first repeats
    double predictedDb = 0.0;
};

/**
    Per-repeat loss: a -40 dBFS tone burst at each octave centre, Feedback fb; the mean level step
    between consecutive echoes over the first `repeats` echoes, against the machine's predicted
    pass gain times Feedback and the loop's DC blocker.
*/
inline std::vector<RepeatLoss> repeatLosses (Machine m, double fs, float timeMs, float fb, int repeats = 8,
                                             float paramA = 0.5f, float paramB = 0.5f)
{
    std::vector<RepeatLoss> result;
    for (const auto hz : { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 })
    {
        dsp::EchoChain chain;
        chain.prepare (fs, 256);
        auto s = machineAt (m, timeMs, fb);
        s.paramA = paramA;
        s.paramB = paramB;

        const auto period = (size_t) std::lround ((double) timeMs * 0.001 * fs);
        const auto settle = (size_t) (0.5 * fs);
        const auto burst = 0.6 * timeMs * 0.001;
        const auto total = settle + period * (size_t) (repeats + 2);
        auto in = toneBurst (hz, fs, burst, 0.01, total - settle);
        auto padded = Stereo::silence (total, fs);
        std::copy (in.left.begin(), in.left.end(), padded.left.begin() + (long) settle);
        std::copy (in.right.begin(), in.right.end(), padded.right.begin() + (long) settle);
        const auto out = render (chain, s, padded, total);

        const auto win = (size_t) (0.4 * timeMs * 0.001 * fs);
        const auto skip = (size_t) (0.1 * timeMs * 0.001 * fs);
        std::vector<double> levels;
        for (int k = 1; k <= repeats; ++k)
            levels.push_back (toneAmplitude (out.left, settle + (size_t) k * period + skip, win, hz, fs));

        const auto stepDb = toDb (levels.back() / levels.front()) / (double) (repeats - 1);
        const auto predicted = (double) fb * dcBlockGain (hz, fs);
        const auto* machine = chain.activeMachine();
        dsp::MachineSettings ms;
        ms.timeMs = { timeMs, timeMs };
        ms.paramA = paramA;
        ms.paramB = paramB;
        const auto pass = machine != nullptr ? machine->predictedPassGain (hz, ms) : 1.0;
        result.push_back ({ hz, stepDb, toDb (predicted * pass) });
    }

    // Where a pass loses more than 15 dB (above a clocked device's band) the tone is gone after
    // a repeat or two and images and aliases dominate what is left: not a per-repeat level.
    std::erase_if (result, [] (const RepeatLoss& r) { return r.predictedDb < -15.0; });
    return result;
}

//==============================================================================
/** First echo level (1 kHz burst at -40 dBFS) at a Drive, in dB relative to the input. */
inline double echoLevelDb (Machine m, double fs, float driveDb, float paramA = 0.5f, float paramB = 0.5f)
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = machineAt (m, 200.0f);
    s.driveDb = driveDb;
    s.paramA = paramA;
    s.paramB = paramB;
    const auto settle = (size_t) (0.5 * fs);
    const auto period = (size_t) (0.2 * fs);
    const auto total = settle + 2 * period;
    auto in = toneBurst (1000.0, fs, 0.15, 0.01, total - settle);
    auto padded = Stereo::silence (total, fs);
    std::copy (in.left.begin(), in.left.end(), padded.left.begin() + (long) settle);
    std::copy (in.right.begin(), in.right.end(), padded.right.begin() + (long) settle);
    const auto out = render (chain, s, padded, total);
    return toDb (toneAmplitude (out.left, settle + period + (size_t) (0.03 * fs), (size_t) (0.08 * fs), 1000.0, fs) / 0.01);
}

//==============================================================================
/** AES6 weighted peak flutter (percent) of the echo of a steady 3150 Hz tone. */
inline double flutterPercent (Machine m, double fs, float wobble, float paramA = 0.5f, float paramB = 0.5f, double seconds = 16.0)
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = machineAt (m, 300.0f);
    s.wobble = wobble;
    s.paramA = paramA;
    s.paramB = paramB;
    const auto in = sine (3150.0, fs, seconds, 0.05);
    const auto out = render (chain, s, in, in.size());
    return FlutterMeter::weightedPercent (out.left, (size_t) (1.0 * fs), out.size(), fs);
}

//==============================================================================
/**
    3rd-difference crest (dB) of a 330 Hz sine through a 300 -> 450 ms Time jump, and the larger
    of the steady renders at 300 and 450 ms. The 3rd difference also sees what a machine adds on
    its own (a converter's grain, a clocked device's images near Nyquist): the gate is the jump
    against -60 dB or the machine's own steady floor, whichever is higher.
*/
struct GlideResult
{
    double jumpDb = 0.0, steadyDb = -300.0;

    double excessDb() const noexcept { return jumpDb - std::max (-60.0, steadyDb + 3.0); }
};

inline GlideResult glide (Machine m, double fs, float paramA = 0.5f, float paramB = 0.5f)
{
    auto s = machineAt (m, 300.0f);
    s.paramA = paramA;
    s.paramB = paramB;
    s.wobble = 0.0f;
    auto moved = s;
    moved.timeMs = { 450.0f, 450.0f };
    const auto in = sine (330.0, fs, 5.0, 0.25);
    const auto change = (size_t) (2.0 * fs);

    GlideResult r;
    {
        dsp::EchoChain chain;
        chain.prepare (fs, 256);
        const auto out = render (chain, [&] (size_t pos) { return pos >= change ? moved : s; }, in, in.size());
        r.jumpDb = clickCrestDb (out.left, (size_t) fs);
    }
    for (const auto* steady : { &s, &moved })
    {
        dsp::EchoChain chain;
        chain.prepare (fs, 256);
        const auto out = render (chain, *steady, in, in.size());
        r.steadyDb = std::max (r.steadyDb, clickCrestDb (out.left, (size_t) fs));
    }
    return r;
}

inline double glideCrestDb (Machine m, double fs, float paramA = 0.5f, float paramB = 0.5f)
{
    return glide (m, fs, paramA, paramB).jumpDb;
}

//==============================================================================
/**
    Aliasing: a 7 kHz sine at -1 dBFS, Drive max, 44.1 kHz, first echo only. Returns the strongest
    spectral component that is neither the tone nor one of its in-band harmonics, in dBc.
*/
inline double worstAliasDbc (Machine m, float paramA = 0.5f, float paramB = 0.5f, double fs = 44100.0, float driveDb = 24.0f)
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = machineAt (m, 100.0f);
    s.driveDb = driveDb;
    s.paramA = paramA;
    s.paramB = paramB;
    const auto in = sine (7000.0, fs, 1.5, std::pow (10.0, -1.0 / 20.0));
    const auto out = render (chain, s, in, in.size());

    const size_t n = 32768;
    const auto from = out.size() - n - (size_t) (0.05 * fs);
    // Blackman-Harris window, DFT on a coarse grid of bins around every candidate frequency.
    std::vector<double> x (n);
    for (size_t i = 0; i < n; ++i)
    {
        const auto t = 2.0 * std::numbers::pi * (double) i / (double) n;
        const auto w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        x[i] = w * out.left[from + i];
    }

    auto power = [&] (double hz)
    {
        const auto w = 2.0 * std::numbers::pi * hz / fs;
        std::complex<double> acc {}, rot = std::polar (1.0, -w), ph { 1.0, 0.0 };
        for (size_t i = 0; i < n; ++i)
        {
            acc += x[i] * ph;
            ph *= rot;
            if ((i & 1023) == 1023)
                ph /= std::abs (ph);
        }
        return std::norm (acc);
    };

    const auto fundamental = power (7000.0);
    auto isHarmonic = [&] (double hz)
    {
        for (int k = 1; k * 7000.0 < fs * 0.5; ++k)
            if (std::abs (hz - k * 7000.0) < 60.0)
                return true;
        return false;
    };

    double worst = 0.0;
    const auto binHz = fs / (double) n;
    for (double hz = 50.0; hz < fs * 0.5 - 50.0; hz += 2.0 * binHz)
        if (! isHarmonic (hz))
            worst = std::max (worst, power (hz));
    return 10.0 * std::log10 (std::max (worst, 1.0e-30) / fundamental);
}

//==============================================================================
/** THD (percent, H2..H9) of the first echo of a 1 kHz tone at a level (dBFS) and Drive. */
inline double thdPercent (Machine m, double levelDb, float driveDb = 0.0f, float paramA = 0.5f, float paramB = 0.5f)
{
    const auto fs = 96000.0;
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = machineAt (m, 200.0f);
    s.driveDb = driveDb;
    s.paramA = paramA;
    s.paramB = paramB;
    const auto in = sine (1000.0, fs, 0.6, fromDb (levelDb));
    const auto out = render (chain, s, in, in.size());
    const auto from = (size_t) (0.4 * fs);
    const auto n = (size_t) (0.15 * fs);
    const auto h1 = toneAmplitude (out.left, from, n, 1000.0, fs);
    double sum = 0.0;
    for (int k = 2; k <= 9; ++k)
    {
        const auto hk = toneAmplitude (out.left, from, n, 1000.0 * k, fs);
        sum += hk * hk;
    }
    return 100.0 * std::sqrt (sum) / std::max (h1, 1.0e-30);
}

//==============================================================================
/** Peak and finiteness: Feedback 120 %, Drive max, a 0 dBFS square wave for `seconds`. */
struct StressResult
{
    double peak = 0.0;
    bool finite = true;
};

inline StressResult squareStress (Machine m, double fs, double seconds, float paramA = 0.5f, float paramB = 0.5f, Mode mode = Mode::single)
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = machineAt (m, 180.0f, 1.2f);
    s.driveDb = 24.0f;
    s.paramA = paramA;
    s.paramB = paramB;
    s.plan.mode = mode;
    s.wetOnly = false;
    s.mix = 0.5f;
    StressResult r;
    const auto block = (size_t) (fs * 0.5);
    auto buffer = Stereo::silence (block, fs);
    size_t done = 0, phase = 0;
    const auto halfPeriod = (size_t) (fs / 220.0 / 2.0);
    while (done < (size_t) (seconds * fs))
    {
        for (size_t i = 0; i < block; ++i, ++phase)
            buffer.left[i] = buffer.right[i] = ((phase / halfPeriod) % 2 == 0) ? 1.0f : -1.0f;
        dsp::ChainMeters meters;
        for (size_t pos = 0; pos < block; pos += 256)
            chain.process (buffer.left.data() + pos, buffer.right.data() + pos, (int) std::min<size_t> (256, block - pos), s, meters);
        r.finite = r.finite && allFinite (buffer);
        r.peak = std::max ({ r.peak, peak (buffer.left), peak (buffer.right) });
        done += block;
    }
    return r;
}

//==============================================================================
/** Feedback 120 % from a click: level after 10 s and its steadiness; then 90 % for 20 s. */
struct RunawayResult
{
    double levelDb = -300.0;        // RMS of the oscillation, 8..10 s
    double steadinessDb = 0.0;      // peak level 8..10 s vs 6..8 s
    double rmsChangeDb = 0.0;       // RMS 8..10 s vs 6..8 s (a flat loop keeps filling in)
    double afterPullBackDb = 0.0;   // RMS 18..20 s after Feedback 90 %, relative to levelDb
    bool finite = true;
};

inline RunawayResult runaway (Machine m, double fs, float paramA = 0.5f, float paramB = 0.5f)
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto hot = machineAt (m, 250.0f, 1.2f);
    hot.paramA = paramA;
    hot.paramB = paramB;
    auto cool = hot;
    cool.feedback = 0.9f;
    const auto total = (size_t) (30.0 * fs);
    const auto out = render (chain, [&] (size_t pos) { return pos < (size_t) (10.0 * fs) ? hot : cool; },
                             impulse (total, fs, 0.5f, (size_t) (0.01 * fs)), total);
    RunawayResult r;
    r.finite = allFinite (out);
    const auto at = [&] (double a, double b) { return rms (out.left, (size_t) (a * fs), (size_t) (b * fs)); };
    r.levelDb = toDb (at (8.0, 10.0));
    const auto peakAt = [&] (double a, double b) { return peak (out.left, (size_t) (a * fs), (size_t) (b * fs)); };
    r.steadinessDb = toDb (peakAt (8.0, 10.0) / std::max (peakAt (6.0, 8.0), 1.0e-30));
    r.rmsChangeDb = toDb (at (8.0, 10.0) / std::max (at (6.0, 8.0), 1.0e-30));
    r.afterPullBackDb = toDb (at (28.0, 30.0)) - r.levelDb;
    return r;
}

//==============================================================================
/** Trail energy per Time-long window after a noise burst: must fall strictly at Feedback <= 100 %. */
inline std::vector<double> trailEnergies (dsp::EchoChain& chain, const dsp::ChainSettings& s, double fs, int windows)
{
    const auto period = (size_t) ((double) std::max (s.timeMs[0], s.timeMs[1]) * 0.001 * fs * 4.0);
    const auto burst = (size_t) (0.05 * fs);
    const auto total = burst + period * (size_t) (windows + 2);
    auto in = Stereo::silence (total, fs);
    const auto noise = pinkNoise (burst, 99);
    for (size_t i = 0; i < burst; ++i)
        in.left[i] = in.right[i] = 0.5f * noise[i];
    const auto out = render (chain, s, in, total);
    std::vector<double> e;
    for (int w = 1; w <= windows; ++w)
    {
        const auto a = burst + (size_t) w * period, b = a + period;
        e.push_back (rms (out.left, a, b) + rms (out.right, a, b));
    }
    return e;
}

//==============================================================================
/** Hold for `seconds`: per-octave tone levels of the held loop at the start vs the end, and the seam. */
struct HoldResult
{
    double worstBandDriftDb = 0.0;
    double seamCrestDb = 0.0;
    double steadyCrestDb = -300.0;      // the same tone through the machine without Hold

    double seamExcessDb() const noexcept { return seamCrestDb - std::max (-60.0, steadyCrestDb + 3.0); }
};

inline HoldResult holdDrift (Machine m, double fs, double seconds)
{
    dsp::EchoChain chain;
    chain.prepare (fs, 256);
    auto s = machineAt (m, 250.0f, 0.5f);
    s.wobble = 0.5f;
    auto held = s;
    held.hold = true;

    // A chord of octave tones, so every band has a steady component in the loop.
    const auto total = (size_t) ((seconds + 2.0) * fs);
    auto in = Stereo::silence (total, fs);
    const std::array<double, 7> bands { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
    // Faded in, so the loop holds no click of its own and the seam is the only joint.
    const auto fadeIn = 0.05 * fs;
    for (size_t i = 0; i < (size_t) (1.0 * fs); ++i)
    {
        const auto env = i < (size_t) fadeIn ? 0.5 - 0.5 * std::cos (std::numbers::pi * (double) i / fadeIn) : 1.0;
        for (const auto hz : bands)
            in.left[i] = in.right[i] += (float) (0.03 * env * std::sin (2.0 * std::numbers::pi * hz * (double) i / fs));
    }

    const auto holdAt = (size_t) (0.8 * fs);
    const auto out = render (chain, [&] (size_t pos) { return pos >= holdAt ? held : s; }, in, total);

    // The held loop is exactly periodic: compare the same place in the loop, a minute apart.
    HoldResult r;
    const auto cycle = (size_t) std::lround (0.25 * fs);
    const auto early = holdAt + 2 * cycle;
    const auto late = early + ((total - early) / cycle - 2) * cycle;
    for (const auto hz : bands)
    {
        const auto a = toneAmplitude (out.left, early, cycle, hz, fs);
        const auto b = toneAmplitude (out.left, late, cycle, hz, fs);
        r.worstBandDriftDb = std::max (r.worstBandDriftDb, std::abs (toDb (b / a)));
    }

    // The seam, on a low tone whose own 3rd difference is far down (-79 dB at 220 Hz).
    {
        dsp::EchoChain c2;
        c2.prepare (fs, 256);
        const auto n = (size_t) (4.0 * fs);
        auto tone = Stereo::silence (n, fs);
        for (size_t i = 0; i < (size_t) (1.0 * fs); ++i)
        {
            const auto env = i < (size_t) fadeIn ? 0.5 - 0.5 * std::cos (std::numbers::pi * (double) i / fadeIn) : 1.0;
            tone.left[i] = tone.right[i] = (float) (0.25 * env * std::sin (2.0 * std::numbers::pi * 220.0 * (double) i / fs));
        }
        const auto looped = render (c2, [&] (size_t pos) { return pos >= holdAt ? held : s; }, tone, n);
        r.seamCrestDb = clickCrestDb (looped.left, early, n);

        // The machine's own floor on the same tone (a clocked device's images, a converter's grain).
        dsp::EchoChain c3;
        c3.prepare (fs, 256);
        auto still = tone;
        for (size_t i = (size_t) (1.0 * fs); i < n; ++i)
            still.left[i] = still.right[i] = (float) (0.25 * std::sin (2.0 * std::numbers::pi * 220.0 * (double) i / fs));
        const auto steady = render (c3, s, still, n);
        r.steadyCrestDb = clickCrestDb (steady.left, early, n);
    }
    return r;
}

//==============================================================================
/** CPU in percent of one core: 48 kHz stereo, the given mode, a steady pink-noise input. */
inline double cpuPercent (Machine m, Mode mode = Mode::single, double seconds = 10.0, float paramA = 0.5f, float paramB = 0.5f)
{
    const auto fs = 48000.0;
    dsp::EchoChain chain;
    chain.prepare (fs, 512);
    auto s = machineAt (m, 375.0f, 0.5f);
    s.wobble = 0.5f;
    s.wetOnly = false;
    s.paramA = paramA;
    s.paramB = paramB;
    s.plan.mode = mode;
    s.plan.steps = 16;
    s.plan.stepLevels.fill (1.0f);
    const auto noise = pinkNoise ((size_t) fs, 7);
    std::vector<float> l (512), r (512);
    const auto blocks = (size_t) (seconds * fs / 512.0);
    dsp::ChainMeters meters;
    const auto start = std::chrono::steady_clock::now();
    for (size_t b = 0; b < blocks; ++b)
    {
        for (size_t i = 0; i < 512; ++i)
            l[i] = r[i] = 0.25f * noise[(b * 512 + i) % noise.size()];
        chain.process (l.data(), r.data(), 512, s, meters);
    }
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    return 100.0 * elapsed / ((double) blocks * 512.0 / fs);
}

} // namespace mulberry::gates
