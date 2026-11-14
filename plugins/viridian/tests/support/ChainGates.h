#pragma once

#include "dsp/chain/EqChain.h"
#include "dsp/models/console/ConsoleControls.h"
#include "dsp/models/console/ConsoleModel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <functional>
#include <numbers>
#include <random>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

/**
    The quality gates of PLAN.md ("Measurement and quality gates") as measurements through the real
    chain, shared by Viridian's tests and by ViridianMeasure, which prints them and writes the
    curves tools/measure/compare.py checks against ngspice. Each returns numbers; the callers
    decide pass or fail.
*/
namespace viridian::gates
{

using Complex = std::complex<double>;

inline constexpr double twoPi = 2.0 * std::numbers::pi;
inline constexpr double rates[] { 44100.0, 48000.0, 96000.0, 192000.0 };

inline double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-300)); }
inline double fromDb (double db) { return std::pow (10.0, db / 20.0); }

struct Stereo
{
    std::vector<float> left, right;

    size_t size() const noexcept { return left.size(); }
    static Stereo silence (size_t n) { return { std::vector<float> (n), std::vector<float> (n) }; }
};

using SettingsAt = std::function<dsp::ChainSettings (size_t position)>;

/** Renders through a prepared chain in blocks; the settings may change per block. */
inline Stereo render (dsp::EqChain& chain, const SettingsAt& settings, Stereo signal, int block = 512, int channels = 2)
{
    // As the processor runs it: decaying tails would otherwise reach denormals and crawl.
    const juce::ScopedNoDenormals noDenormals;
    for (size_t pos = 0; pos < signal.size(); pos += (size_t) block)
    {
        const auto n = (int) std::min<size_t> ((size_t) block, signal.size() - pos);
        float* ptrs[] { signal.left.data() + pos, signal.right.data() + pos };
        chain.process (ptrs, channels, n, settings (pos));
    }
    return signal;
}

inline Stereo render (dsp::EqChain& chain, const dsp::ChainSettings& s, Stereo signal, int block = 512)
{
    return render (chain, [&s] (size_t) { return s; }, std::move (signal), block);
}

inline Stereo sine (double hz, double fs, double seconds, double amplitude)
{
    auto s = Stereo::silence ((size_t) (seconds * fs));
    for (size_t i = 0; i < s.size(); ++i)
        s.left[i] = s.right[i] = (float) (amplitude * std::sin (twoPi * hz * (double) i / fs));
    return s;
}

/** The DFT of x[from ... from + n) at one frequency (a phasor renormalised every 1024 steps). */
inline Complex dft (const std::vector<float>& x, double hz, double fs, size_t from = 0, size_t n = 0)
{
    n = n == 0 ? x.size() - from : n;
    const auto w = twoPi * hz / fs;
    const auto step = std::polar (1.0, -w);
    Complex acc {}, phasor { 1.0, 0.0 };
    for (size_t k = 0; k < n; ++k)
    {
        acc += (double) x[from + k] * phasor;
        phasor = (k & 1023) == 1023 ? std::polar (1.0, -w * (double) (k + 1)) : phasor * step;
    }
    return acc;
}

/** Amplitude of a tone in x[from ... from + n), Hann window. */
inline double toneAmplitude (const std::vector<float>& x, size_t from, size_t n, double hz, double fs)
{
    const auto w = twoPi * hz / fs;
    Complex acc {};
    double sum = 0.0;
    for (size_t k = 0; k < n; ++k)
    {
        const auto hann = 0.5 - 0.5 * std::cos (twoPi * (double) k / (double) n);
        acc += hann * (double) x[from + k] * std::polar (1.0, -w * (double) k);
        sum += hann;
    }
    return 2.0 * std::abs (acc) / sum;
}

/** Peak of the 3rd difference over RMS, in dB (the siblings' click detector). */
inline double clickCrestDb (const std::vector<float>& x, size_t start)
{
    double peak = 0.0, sum = 0.0;
    for (size_t i = std::max<size_t> (start, 3); i < x.size(); ++i)
        peak = std::max (peak, (double) std::abs ((double) x[i] - 3.0 * x[i - 1] + 3.0 * x[i - 2] - (double) x[i - 3]));
    for (size_t i = start; i < x.size(); ++i)
        sum += (double) x[i] * x[i];
    return toDb (peak / std::sqrt (sum / (double) (x.size() - start)));
}

/**
    A tone at `amplitude` in quadrature: sine on the left, cosine on the right, both faded in over
    10 ms. Through a chain whose channels share one network (Tolerance 0) and whose amps are linear
    there, the two outputs are the responses to the tone at phase 0 and at 90 degrees, and every
    other phase is their combination (worstPhaseCrestDb()).
*/
inline Stereo quadrature (double hz, double fs, double seconds, double amplitude)
{
    auto s = Stereo::silence ((size_t) (seconds * fs));
    const auto fade = 0.01 * fs;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const auto w = (double) i < fade ? 0.5 - 0.5 * std::cos (std::numbers::pi * (double) i / fade) : 1.0;
        const auto phase = twoPi * hz * (double) i / fs;
        s.left[i] = (float) (w * amplitude * std::sin (phase));
        s.right[i] = (float) (w * amplitude * std::cos (phase));
    }
    return s;
}

/**
    clickCrestDb() at the tone's worst phase, from a quadrature() render: at phase p the output is
    cos p L + sin p R, so its 3rd difference peaks at most at max sqrt (dL^2 + dR^2), and its mean
    square is at least the smaller eigenvalue of the two channels' mean-square matrix. The ratio
    bounds the crest at every phase from above (the tone's level barely depends on its phase, so
    the bound is tight); a jump at a fixed instant then cannot miss where it lands on the tone.
*/
inline double worstPhaseCrestDb (const Stereo& y, size_t start)
{
    double peak = 0.0, ll = 0.0, rr = 0.0, lr = 0.0;
    for (size_t i = std::max<size_t> (start, 3); i < y.size(); ++i)
    {
        const auto dl = (double) y.left[i] - 3.0 * y.left[i - 1] + 3.0 * y.left[i - 2] - (double) y.left[i - 3];
        const auto dr = (double) y.right[i] - 3.0 * y.right[i - 1] + 3.0 * y.right[i - 2] - (double) y.right[i - 3];
        peak = std::max (peak, dl * dl + dr * dr);
    }
    for (size_t i = start; i < y.size(); ++i)
    {
        ll += (double) y.left[i] * y.left[i];
        rr += (double) y.right[i] * y.right[i];
        lr += (double) y.left[i] * y.right[i];
    }
    const auto smallest = 0.5 * (ll + rr) - std::sqrt (0.25 * (ll - rr) * (ll - rr) + lr * lr);
    return toDb (std::sqrt (peak) / std::sqrt (std::max (smallest, 1.0e-300) / (double) (y.size() - start)));
}

/**
    worstPhaseCrestDb() against the tone's level around each sample instead of over the whole
    render: the 3rd difference over the RMS within +-`window` seconds, the worst over every sample
    and phase. A clean tone reads the same at any level (-76.6 dB at 44.1 kHz); a change of level
    by tens of dB (a preset's Output against its EQ) no longer reads as a click where the tone is
    loud, and a click where it is quiet reads larger.
*/
inline double worstPhaseLocalCrestDb (const Stereo& y, size_t start, double fs, double window = 0.005)
{
    const auto n = y.size(), w = (size_t) (window * fs);
    std::vector<double> ll (n + 1), rr (n + 1), lr (n + 1);
    for (size_t i = 0; i < n; ++i)
    {
        ll[i + 1] = ll[i] + (double) y.left[i] * y.left[i];
        rr[i + 1] = rr[i] + (double) y.right[i] * y.right[i];
        lr[i + 1] = lr[i] + (double) y.left[i] * y.right[i];
    }
    double worst = 0.0;
    for (size_t i = std::max<size_t> (start, 3); i < n; ++i)
    {
        const auto dl = (double) y.left[i] - 3.0 * y.left[i - 1] + 3.0 * y.left[i - 2] - (double) y.left[i - 3];
        const auto dr = (double) y.right[i] - 3.0 * y.right[i - 1] + 3.0 * y.right[i - 2] - (double) y.right[i - 3];
        const auto a = i - std::min (i, w), b = std::min (n, i + w + 1);
        const auto sl = ll[b] - ll[a], sr = rr[b] - rr[a], slr = lr[b] - lr[a];
        const auto smallest = 0.5 * (sl + sr) - std::sqrt (0.25 * (sl - sr) * (sl - sr) + slr * slr);
        const auto level = std::max (smallest, 1.0e-300) / (double) (b - a);
        worst = std::max (worst, (dl * dl + dr * dr) / level);
    }
    return toDb (std::sqrt (worst));
}

//==============================================================================
/** Console with its Init positions on one card. */
inline dsp::ChainSettings consoleFlat (bool alt)
{
    dsp::ChainSettings s;
    s.model = Model::console;
    s.alt = alt;
    s.positions = flatPositions (Model::console);
    return s;
}

/** Console with every band and both filters at work (the chain tests' setting). */
inline dsp::ChainSettings consoleWorking (bool alt)
{
    auto s = consoleFlat (alt);
    s.positions[(size_t) Control::hpfFreq] = 0.3f;
    s.positions[(size_t) Control::lpfFreq] = 0.7f;
    s.positions[(size_t) Control::lowGain] = 0.7f;
    s.positions[(size_t) Control::lmGain] = 0.25f;
    s.positions[(size_t) Control::lmQ] = 0.8f;
    s.positions[(size_t) Control::hmGain] = 0.85f;
    s.positions[(size_t) Control::highGain] = 0.3f;
    s.positions[(size_t) Control::highShape] = 1.0f;
    return s;
}

/** The same knobs with every gain centred and both filters Off: the network the bands act on. */
inline dsp::ChainSettings bandsFlat (dsp::ChainSettings s)
{
    for (const auto c : { Control::lowGain, Control::lmGain, Control::hmGain, Control::highGain })
        s.positions[(size_t) c] = 0.5f;
    s.positions[(size_t) Control::hpfFreq] = 0.0f;
    s.positions[(size_t) Control::lpfFreq] = 1.0f;
    return s;
}

/** The card's pots for chain settings: what the networks run (and what ngspice is given). */
inline dsp::console::Pots consolePots (const dsp::ChainSettings& s)
{
    return dsp::console::potsFor (resolveEq (Model::console, s.alt, s.positions));
}

//==============================================================================
/** The curve grid: ngspice's "dec 40" points from 20 Hz to 20 kHz (the comparison's frequencies). */
inline std::vector<double> curveHz()
{
    std::vector<double> f;
    for (int k = 0; k <= 120; ++k)
        f.push_back (20.0 * std::pow (10.0, k / 40.0));
    return f;
}

/** The coupling capacitors' slow tails at 20 Hz: 2 s leaves 0.004 dB of truncation, 4 s 0.0005 dB. */
inline constexpr double responseSeconds = 4.0;

/** -60 dBFS: the amps are linear far below the gates' resolution. */
inline constexpr double responseLevel = 1.0e-3;

/** The response of the chain from an impulse (left channel), DFT at each frequency. */
inline std::vector<Complex> chainResponse (dsp::EqChain& chain, const dsp::ChainSettings& s, double fs, const std::vector<double>& hz)
{
    chain.rebuild (s);
    auto x = Stereo::silence ((size_t) (responseSeconds * fs));
    x.left[0] = x.right[0] = (float) responseLevel;
    const auto y = render (chain, s, std::move (x));
    std::vector<Complex> h;
    for (const auto f : hz)
        h.push_back (dft (y.left, f, fs) / responseLevel);
    return h;
}

/** One Console setting through the chain: the network's response there, measured and drawn. */
struct Curve
{
    std::vector<Complex> measured;      // chain with EQ In over chain with EQ In off: the network as heard
    std::vector<Complex> display;       // the realised digital response the curve window draws
    std::vector<Complex> analog;        // the netlists' H(s) the networks evaluate
    dsp::console::Pots pots;
};

/** ampsOnly: chainResponse() of the same chain with EQ In off (the amps, oversamplers, DC block). */
inline Curve consoleCurve (dsp::EqChain& chain, const dsp::ChainSettings& s, double fs, const std::vector<double>& hz,
                           const std::vector<Complex>& ampsOnly)
{
    Curve c;
    c.measured = chainResponse (chain, s, fs, hz);
    for (size_t k = 0; k < hz.size(); ++k)
        c.measured[k] /= ampsOnly[k];
    const auto& model = dynamic_cast<const dsp::ConsoleModel&> (chain.model (Model::console));
    for (const auto f : hz)
    {
        c.display.push_back (model.networkResponse (f));
        c.analog.push_back (model.networkAnalogResponse (f));
    }
    c.pots = consolePots (s);
    return c;
}

inline std::vector<Complex> ampsOnlyResponse (dsp::EqChain& chain, double fs, const std::vector<double>& hz)
{
    auto s = consoleFlat (false);
    s.eqIn = false;
    return chainResponse (chain, s, fs, hz);
}

/** Worst |dB difference| of two responses over the frequencies up to maxHz. */
inline double worstDb (const std::vector<Complex>& a, const std::vector<Complex>& b, const std::vector<double>& hz,
                       double maxHz = 20000.0)
{
    double worst = 0.0;
    for (size_t k = 0; k < hz.size(); ++k)
        if (hz[k] <= maxHz * (1.0 + 1.0e-9))
            worst = std::max (worst, std::abs (toDb (std::abs (a[k])) - toDb (std::abs (b[k]))));
    return worst;
}

//==============================================================================
struct NamedSettings
{
    std::string name;
    dsp::ChainSettings settings;
};

/**
    The Console's comparison grid on one card: Init; each band's frequency over eleven positions
    at full boost and full cut (mids at highest and lowest Q, the HF and LF bands as bell and
    shelf); each gain and Q over eleven positions; each filter from Off over its travel; the shape
    keys' four combinations with every band at work; and seeded random settings.
*/
inline std::vector<NamedSettings> consoleGrid (bool alt, int randomSettings = 24)
{
    std::vector<NamedSettings> grid;
    const auto card = std::string (alt ? "later" : "early");
    auto add = [&] (const std::string& name, const dsp::ChainSettings& s) { grid.push_back ({ card + ": " + name, s }); };
    auto position = [] (int i) { return (float) i / 10.0f; };
    auto text = [] (float v) { return std::to_string ((int) std::lround (v * 100.0f)) + " %"; };

    add ("Init", consoleFlat (alt));

    struct Band
    {
        const char* name;
        Control freq, gain, q, shape;
        bool hasQ, hasShape;
    };
    const Band bands[] {
        { "LF", Control::lowFreq, Control::lowGain, Control::lowQ, Control::lowShape, false, true },
        { "LMF", Control::lmFreq, Control::lmGain, Control::lmQ, Control::lowShape, true, false },
        { "HMF", Control::hmFreq, Control::hmGain, Control::hmQ, Control::highShape, true, false },
        { "HF", Control::highFreq, Control::highGain, Control::highQ, Control::highShape, false, true },
    };

    for (const auto& b : bands)
    {
        const auto shapes = b.hasShape ? std::vector<float> { 1.0f, 0.0f } : std::vector<float> { 1.0f };
        for (const auto shape : shapes)
        {
            const auto form = std::string (b.hasShape ? (shape > 0.5f ? " bell" : " shelf") : "");
            for (const auto gain : { 1.0f, 0.0f })
                for (const auto q : b.hasQ && gain > 0.5f ? std::vector<float> { 1.0f, 0.0f } : std::vector<float> { 1.0f })
                    for (int i = 0; i <= 10; ++i)
                    {
                        auto s = consoleFlat (alt);
                        s.positions[(size_t) b.shape] = shape;
                        s.positions[(size_t) b.gain] = gain;
                        s.positions[(size_t) b.q] = q;
                        s.positions[(size_t) b.freq] = position (i);
                        add (std::string (b.name) + form + (gain > 0.5f ? " boost" : " cut") + (b.hasQ ? ", Q " + text (q) : "")
                                 + ", freq " + text (position (i)),
                             s);
                    }
            for (int i = 0; i <= 10; ++i)
            {
                auto s = consoleFlat (alt);
                s.positions[(size_t) b.shape] = shape;
                s.positions[(size_t) b.q] = 1.0f;
                s.positions[(size_t) b.gain] = position (i);
                add (std::string (b.name) + form + ", gain " + text (position (i)), s);
            }
        }
        if (b.hasQ)
            for (int i = 0; i <= 10; ++i)
            {
                auto s = consoleFlat (alt);
                s.positions[(size_t) b.gain] = 1.0f;
                s.positions[(size_t) b.q] = position (i);
                add (std::string (b.name) + " boost, Q " + text (position (i)), s);
            }
    }

    // The filters: Off, then the travel from just past the Off zone to its other end.
    for (const auto c : { Control::hpfFreq, Control::lpfFreq })
        for (int i = 0; i <= 10; ++i)
        {
            auto s = consoleFlat (alt);
            const auto travel = filterOffZone + (1.0f - filterOffZone) * position (i);
            s.positions[(size_t) c] = c == Control::hpfFreq ? (i == 0 ? 0.0f : travel) : (i == 0 ? 1.0f : 1.0f - travel);
            add (std::string (c == Control::hpfFreq ? "HPF " : "LPF ") + (i == 0 ? "Off" : text (s.positions[(size_t) c])), s);
        }

    for (const auto lowShape : { 0.0f, 1.0f })
        for (const auto highShape : { 0.0f, 1.0f })
        {
            auto s = consoleWorking (alt);
            s.positions[(size_t) Control::lowShape] = lowShape;
            s.positions[(size_t) Control::highShape] = highShape;
            add (std::string ("every band at work, LF ") + (lowShape > 0.5f ? "bell" : "shelf") + ", HF "
                     + (highShape > 0.5f ? "bell" : "shelf"),
                 s);
        }

    // Interactions: the high-pass joined to working bands (the low-pass out), every band at its
    // extreme at once, the mids cut over Q, bands on top of each other, every frequency at one end
    // of its travel, and the filters just past their Off zones.
    for (const auto lowShape : { 0.0f, 1.0f })
    {
        auto s = consoleWorking (alt);
        s.positions[(size_t) Control::lpfFreq] = 1.0f;
        s.positions[(size_t) Control::lowShape] = lowShape;
        add (std::string ("high-pass in, low-pass out, every band at work, LF ") + (lowShape > 0.5f ? "bell" : "shelf"), s);
    }
    for (const auto gain : { 1.0f, 0.0f })
        for (const auto bell : { 1.0f, 0.0f })
        {
            auto s = consoleFlat (alt);
            for (const auto c : { Control::lowGain, Control::lmGain, Control::hmGain, Control::highGain })
                s.positions[(size_t) c] = gain;
            s.positions[(size_t) Control::lmQ] = s.positions[(size_t) Control::hmQ] = 1.0f;
            s.positions[(size_t) Control::lowShape] = s.positions[(size_t) Control::highShape] = bell;
            add (std::string ("every band ") + (gain > 0.5f ? "boosted" : "cut") + (bell > 0.5f ? ", bells" : ", shelves"), s);
        }
    for (const auto& [name, gain, q] : { std::tuple { "LMF", Control::lmGain, Control::lmQ }, std::tuple { "HMF", Control::hmGain, Control::hmQ } })
        for (int i = 0; i <= 10; i += 2)
        {
            auto s = consoleFlat (alt);
            s.positions[(size_t) gain] = 0.0f;
            s.positions[(size_t) q] = position (i);
            add (std::string (name) + " cut, Q " + text (position (i)), s);
        }
    for (const auto f : { 0.0f, 0.5f, 1.0f })
    {
        auto s = consoleWorking (alt);
        for (const auto c : { Control::lowFreq, Control::lmFreq, Control::hmFreq, Control::highFreq })
            s.positions[(size_t) c] = f;
        add ("every band at work, every frequency at " + text (f), s);
    }
    {
        // Overlapping bands: LF and LMF near 200 Hz, HMF and HF near 3 kHz (each pair in both travels).
        auto s = consoleFlat (alt);
        s.positions[(size_t) Control::lowGain] = s.positions[(size_t) Control::lmGain] = 1.0f;
        s.positions[(size_t) Control::hmGain] = s.positions[(size_t) Control::highGain] = 0.0f;
        s.positions[(size_t) Control::lowFreq] = 0.75f;
        s.positions[(size_t) Control::lmFreq] = alt ? 0.15f : 0.05f;
        s.positions[(size_t) Control::hmFreq] = 0.65f;
        s.positions[(size_t) Control::highFreq] = 0.4f;
        s.positions[(size_t) Control::lmQ] = s.positions[(size_t) Control::hmQ] = 0.5f;
        add ("overlapping bands, lows boosted, highs cut", s);
    }
    {
        auto s = consoleWorking (alt);
        s.positions[(size_t) Control::hpfFreq] = filterOffZone + 0.001f;
        s.positions[(size_t) Control::lpfFreq] = 1.0f - filterOffZone - 0.001f;
        add ("filters just past their Off zones, every band at work", s);
    }

    std::mt19937 rng (alt ? 2u : 1u);
    std::uniform_real_distribution<float> uniform (0.0f, 1.0f);

    // Flat away from Init: every gain centred, filters Off, the other knobs anywhere. The circuit's
    // own lift is largest with the HF shelf at its bottom and the HMF at its top, lowest Q.
    {
        auto s = consoleFlat (alt);
        s.positions[(size_t) Control::highShape] = 0.0f;
        s.positions[(size_t) Control::highFreq] = 0.0f;
        s.positions[(size_t) Control::hmFreq] = 1.0f;
        s.positions[(size_t) Control::hmQ] = 0.0f;
        add ("flat, HF shelf at its bottom, HMF at its top, lowest Q", s);
    }
    for (int k = 0; k < randomSettings / 3; ++k)
    {
        auto s = consoleFlat (alt);
        for (const auto c : { Control::lowFreq, Control::lowShape, Control::lmFreq, Control::lmQ, Control::hmFreq, Control::hmQ,
                              Control::highFreq, Control::highShape })
            s.positions[(size_t) c] = uniform (rng);
        add ("flat, random " + std::to_string (k + 1), s);
    }

    for (int k = 0; k < randomSettings; ++k)
    {
        auto s = consoleFlat (alt);
        for (auto& p : s.positions)
            p = uniform (rng);
        add ("random " + std::to_string (k + 1), s);
    }
    return grid;
}

//==============================================================================
/** Flat is flat: the chain's departure from its amps-only response, and from the network's own. */
struct FlatResult
{
    double fromAmps = 0.0;       // dB, 20 Hz ... 20 kHz
    double fromNetwork = 0.0;    // dB: the measured response against the netlists' H(s)
};

inline FlatResult flatness (const dsp::ChainSettings& flat, double fs)
{
    dsp::EqChain chain;
    chain.prepare (fs, 512, 2, flat);
    const auto hz = curveHz();
    const auto amps = ampsOnlyResponse (chain, fs, hz);
    const auto c = consoleCurve (chain, flat, fs, hz, amps);
    FlatResult r;
    for (size_t k = 0; k < hz.size(); ++k)
    {
        r.fromAmps = std::max (r.fromAmps, std::abs (toDb (std::abs (c.measured[k]))));
        r.fromNetwork = std::max (r.fromNetwork, std::abs (toDb (std::abs (c.measured[k])) - toDb (std::abs (c.analog[k]))));
    }
    return r;
}

//==============================================================================
/** Drive compensation: the small-signal level (1 kHz, -60 dBFS) over Drive -12 ... +24 dB, worst dB from Drive 0. */
inline double driveDeviationDb (const dsp::ChainSettings& base, double fs)
{
    auto levelAt = [&] (float drive)
    {
        auto s = base;
        s.driveDb = drive;
        dsp::EqChain chain;
        chain.prepare (fs, 512, 2, s);
        const auto out = render (chain, s, sine (1000.0, fs, 0.5, 1.0e-3));
        return toDb (toneAmplitude (out.left, (size_t) (0.25 * fs), (size_t) (0.2 * fs), 1000.0, fs));
    };
    const auto reference = levelAt (0.0f);
    double worst = 0.0;
    for (float drive = -12.0f; drive <= 24.0f; drive += 3.0f)
        worst = std::max (worst, std::abs (levelAt (drive) - reference));
    return worst;
}

//==============================================================================
/** THD (H2 ... H9 below 0.45 fs) of a tone at a level and Drive, in dB re the fundamental. */
inline double thdDb (const dsp::ChainSettings& base, double hz, double levelDb, float driveDb, double fs = 96000.0)
{
    auto s = base;
    s.driveDb = driveDb;
    dsp::EqChain chain;
    chain.prepare (fs, 512, 2, s);
    const auto out = render (chain, s, sine (hz, fs, 1.0, fromDb (levelDb)));
    // A whole number of 50 Hz periods: every harmonic sits on a bin.
    const auto from = (size_t) (0.5 * fs), n = (size_t) (0.4 * fs);
    const auto h1 = toneAmplitude (out.left, from, n, hz, fs);
    double sum = 0.0;
    for (int k = 2; k <= 9 && k * hz < 0.45 * fs; ++k)
    {
        const auto hk = toneAmplitude (out.left, from, n, k * hz, fs);
        sum += hk * hk;
    }
    return toDb (std::sqrt (sum) / std::max (h1, 1.0e-30));
}

//==============================================================================
/**
    What an alias is read against: the tone at the output; at the input; or at the stage that clips
    it, the output's tone with the network's cut at the tone undone: every alias against the tone
    before the network cuts it, exact for the input amp's aliases and optimistic by the cut for any
    T8 adds where the network cuts the tone (PLAN.md Provisional 11).
*/
enum class AliasReference
{
    output,
    input,
    clippingStage
};

/**
    Aliasing: a sine at -1 dBFS (7 kHz, 44.1 kHz, Drive as given; PLAN: every Drive) through the
    chain; the strongest non-harmonic component in dBc, Mulberry's measurement with a
    Blackman-Harris window over 32768 samples. A periodic tone through the chain gives lines only
    at the tone's harmonics folded about multiples of fs (every rate inside the chain is one of
    them), so the DFT is read exactly there: a line between two bins reads at its full height.
    Read against `reference` (PLAN.md Provisional 11).
*/
inline double worstAliasDbc (const dsp::ChainSettings& base, float driveDb = 24.0f, double fs = 44100.0, double toneHz = 7000.0,
                             AliasReference reference = AliasReference::clippingStage)
{
    auto s = base;
    s.driveDb = driveDb;
    dsp::EqChain chain;
    chain.prepare (fs, 512, 2, s);
    auto signal = sine (toneHz, fs, 1.5, fromDb (-1.0));
    const auto in = signal.left;
    const auto out = render (chain, s, std::move (signal));

    const size_t n = 32768;
    const auto from = out.size() - n - (size_t) (0.05 * fs);
    std::vector<float> x (n), input (n);
    for (size_t i = 0; i < n; ++i)
    {
        const auto t = twoPi * (double) i / (double) n;
        const auto w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        x[i] = (float) (w * out.left[from + i]);
        input[i] = (float) (w * in[from + i]);
    }
    auto power = [&] (double hz) { return std::norm (dft (x, hz, fs)); };
    auto tone = reference == AliasReference::input ? std::norm (dft (input, toneHz, fs)) : power (toneHz);
    if (reference == AliasReference::clippingStage && s.model == Model::console)
    {
        const auto& model = dynamic_cast<const dsp::ConsoleModel&> (chain.model (Model::console));
        tone /= std::min (1.0, std::norm (model.networkResponse (toneHz)));
    }

    // Each harmonic k f0 folded into 0 ... fs/2; the folds repeat once k f0 passes a multiple of fs
    // by every residue, which 4096 harmonics cover for any tone a test uses.
    std::vector<double> aliases;
    for (int k = 1; k <= 4096; ++k)
    {
        auto f = std::fmod (k * toneHz, fs);
        f = f > 0.5 * fs ? fs - f : f;
        const bool harmonic = std::abs (std::remainder (f, toneHz)) < 1.0e-6 && f <= 0.5 * fs;
        const bool seen = std::any_of (aliases.begin(), aliases.end(), [f] (double a) { return std::abs (a - f) < 1.0e-6; });
        if (! harmonic && ! seen && f > 20.0)       // DC (the DC block, the window's leakage) excluded
            aliases.push_back (f);
    }

    double worst = 0.0;
    for (const auto f : aliases)
        worst = std::max (worst, power (f));
    return 10.0 * std::log10 (std::max (worst, 1.0e-30) / tone);
}

/**
    Settings that drive T8, the card's output amp, harder than flat: each band at full boost at
    the ends and the middle of its travel (shapes and the mids' Q both ways) and every band
    boosted at once. Aliasing there is reported (console NOTES.md, "Measured").
*/
inline std::vector<NamedSettings> boostedSettings (bool alt)
{
    std::vector<NamedSettings> out;
    struct Band
    {
        const char* name;
        Control freq, gain, form;
        bool q;
    };
    const Band bands[] { { "LF", Control::lowFreq, Control::lowGain, Control::lowShape, false },
                         { "LMF", Control::lmFreq, Control::lmGain, Control::lmQ, true },
                         { "HMF", Control::hmFreq, Control::hmGain, Control::hmQ, true },
                         { "HF", Control::highFreq, Control::highGain, Control::highShape, false } };
    for (const auto& b : bands)
        for (const auto form : { 0.0f, 1.0f })
            for (const auto f : { 0.0f, 0.5f, 0.75f, 0.9f, 1.0f })
            {
                auto s = consoleFlat (alt);
                s.positions[(size_t) b.gain] = 1.0f;
                s.positions[(size_t) b.freq] = f;
                s.positions[(size_t) b.form] = form;
                out.push_back ({ std::string (b.name) + " boosted, " + (b.q ? (form > 0.5f ? "highest Q" : "lowest Q") : (form > 0.5f ? "bell" : "shelf"))
                                     + ", freq " + std::to_string ((int) std::lround (100.0f * f)) + " %",
                                 s });
            }
    for (const auto form : { 0.0f, 1.0f })
    {
        auto s = consoleFlat (alt);
        for (const auto c : { Control::lowGain, Control::lmGain, Control::hmGain, Control::highGain })
            s.positions[(size_t) c] = 1.0f;
        s.positions[(size_t) Control::lowShape] = s.positions[(size_t) Control::highShape] = form;
        out.push_back ({ std::string ("every band boosted, ") + (form > 0.5f ? "bells" : "shelves"), s });
    }
    return out;
}

/**
    Settings that cut the tone after the input amp has clipped it: each band at full cut at the
    ends and the middle of its travel (shapes and the mids' Q both ways), every band cut at once,
    each filter at the end of its travel and both together; and seeded random settings, every knob
    and key anywhere.
*/
inline std::vector<NamedSettings> cutSettings (bool alt, int randomSettings = 20)
{
    std::vector<NamedSettings> out;
    for (auto b : boostedSettings (alt))
    {
        const auto at = b.name.find (" boosted");
        b.name.replace (at, 8, " cut");
        for (const auto c : { Control::lowGain, Control::lmGain, Control::hmGain, Control::highGain })
            if (b.settings.positions[(size_t) c] > 0.75f)
                b.settings.positions[(size_t) c] = 0.0f;
        out.push_back (std::move (b));
    }
    for (const auto& [name, hpf, lpf] : { std::tuple { "HPF at its top", 1.0f, 1.0f }, std::tuple { "LPF at its bottom", 0.0f, 0.0f },
                                          std::tuple { "HPF at its top, LPF at its bottom", 1.0f, 0.0f } })
    {
        auto s = consoleFlat (alt);
        s.positions[(size_t) Control::hpfFreq] = hpf;
        s.positions[(size_t) Control::lpfFreq] = lpf;
        out.push_back ({ name, s });
    }
    std::mt19937 rng (alt ? 32u : 31u);
    std::uniform_real_distribution<float> uniform (0.0f, 1.0f);
    for (int k = 0; k < randomSettings; ++k)
    {
        auto s = consoleFlat (alt);
        for (auto& p : s.positions)
            p = uniform (rng);
        out.push_back ({ "random " + std::to_string (k + 1), s });
    }
    return out;
}

//==============================================================================
/** Automation: the 3rd-difference crest of a 330 Hz sine (-12 dBFS) while settings change per block. */
inline double automationCrestDb (const SettingsAt& settings, double fs, double seconds = 2.0, double amplitude = 0.25)
{
    dsp::EqChain chain;
    chain.prepare (fs, 32, 2, settings (0));
    const auto out = render (chain, settings, sine (330.0, fs, seconds, amplitude), 32);
    return std::max (clickCrestDb (out.left, (size_t) (0.05 * fs)), clickCrestDb (out.right, (size_t) (0.05 * fs)));
}

/** One knob swept end to end and back over `seconds` from the base settings, at 32-sample blocks. */
inline double sweepCrestDb (const dsp::ChainSettings& base, Control c, double fs, double seconds = 2.0)
{
    const auto total = (size_t) (seconds * fs);
    return automationCrestDb (
        [&] (size_t pos)
        {
            auto s = base;
            const auto t = (double) pos / (double) total;
            s.positions[(size_t) c] = (float) (t < 0.5 ? 2.0 * t : 2.0 - 2.0 * t);
            return s;
        },
        fs, seconds);
}

/** One gain knob jumping between its ends every 50 ms, a 330 Hz sine at -40 dBFS (the amps linear): the crest. */
inline double gainJumpCrestDb (const dsp::ChainSettings& base, float dsp::ChainSettings::* knob, float low, float high, double fs)
{
    dsp::EqChain chain;
    const auto period = (size_t) (0.05 * fs);
    const SettingsAt settings = [&] (size_t pos)
    {
        auto s = base;
        s.*knob = (pos / period) % 2 == 0 ? low : high;
        return s;
    };
    chain.prepare (fs, 32, 2, settings (0));
    const auto out = render (
        chain,
        settings,
        sine (330.0, fs, 1.0, 0.01), 32);
    return std::max (clickCrestDb (out.left, (size_t) (0.05 * fs)), clickCrestDb (out.right, (size_t) (0.05 * fs)));
}

/** Drive jumping -12 <-> +24 dB every 50 ms. */
inline double driveJumpCrestDb (const dsp::ChainSettings& base, double fs)
{
    return gainJumpCrestDb (base, &dsp::ChainSettings::driveDb, -12.0f, 24.0f, fs);
}

/** Output jumping -24 <-> +24 dB every 50 ms. */
inline double outputJumpCrestDb (const dsp::ChainSettings& base, double fs)
{
    return gainJumpCrestDb (base, &dsp::ChainSettings::outputDb, -24.0f, 24.0f, fs);
}

/** A 330 Hz tone at -40 dBFS (the amps linear) in quadrature() through the chain at 32-sample
    blocks, one knob jumping from `from` to `to` halfway; the settings otherwise `base`. */
inline Stereo knobJump (const dsp::ChainSettings& base, Control c, float from, float to, double fs, double seconds = 1.0)
{
    dsp::EqChain chain;
    chain.prepare (fs, 32, 2, base);
    const auto at = (size_t) (0.5 * seconds * fs);
    return render (
        chain,
        [&] (size_t pos)
        {
            auto s = base;
            s.positions[(size_t) c] = pos < at ? from : to;
            return s;
        },
        quadrature (330.0, fs, seconds, 0.01), 32);
}

/** A knob's travel: both ends and the middle (a filter's ends just inside its Off zone). At an end a pot segment is a short. */
inline std::array<float, 3> travelPoints (Control c)
{
    if (c == Control::hpfFreq)
        return { filterOffZone, 0.5f, 1.0f };
    if (c == Control::lpfFreq)
        return { 0.0f, 0.5f, 1.0f - filterOffZone };
    return { 0.0f, 0.5f, 1.0f };
}

/** The 3rd-difference crest of a knob jump (knobJump()) at the tone's worst phase. */
inline double jumpCrestDb (const dsp::ChainSettings& base, Control c, float from, float to, double fs)
{
    return worstPhaseCrestDb (knobJump (base, c, from, to, fs), (size_t) (0.05 * fs));
}

/** A knob jump's overshoot: the peak within 30 ms after it over the larger steady peak, in dB. */
inline double jumpOvershootDb (const dsp::ChainSettings& base, Control c, float from, float to, double fs)
{
    const auto out = knobJump (base, c, from, to, fs);
    const auto at = out.size() / 2, settle = (size_t) (0.1 * fs);
    auto peak = [&] (size_t a, size_t b)
    {
        double p = 0.0;
        for (size_t i = a; i < b; ++i)
            p = std::max (p, (double) std::abs (out.left[i]));
        return p;
    };
    const auto steady = std::max (peak (at - settle, at), peak (out.size() - settle, out.size()));
    return toDb (peak (at, at + (size_t) (0.03 * fs)) / steady);
}

/** A knob of a group moved together: rising, or falling while the others rise. */
struct Moving
{
    Control control;
    bool falling = false;
};

using Group = std::vector<Moving>;

/**
    Several knobs swept end to end and back together over `seconds`, at 32-sample blocks; a 330 Hz
    sine at -40 dBFS, where bands boosting together leave the amps linear.
*/
inline double groupSweepCrestDb (const dsp::ChainSettings& base, const Group& group, double fs, double seconds = 2.0)
{
    const auto total = (size_t) (seconds * fs);
    return automationCrestDb (
        [&] (size_t pos)
        {
            auto s = base;
            const auto t = (double) pos / (double) total;
            const auto up = (float) (t < 0.5 ? 2.0 * t : 2.0 - 2.0 * t);
            for (const auto& m : group)
                s.positions[(size_t) m.control] = m.falling ? 1.0f - up : up;
            return s;
        },
        fs, seconds, 0.01);
}

/**
    Several knobs jumping together in one block, halfway through a 330 Hz tone at -40 dBFS, from
    one of their travelPoints() to another (a falling knob from the mirrored point to its mirror):
    the 3rd-difference crest at the tone's worst phase.
*/
inline double groupJumpCrestDb (const dsp::ChainSettings& base, const Group& group, int from, int to, double fs)
{
    dsp::EqChain chain;
    const auto at = (size_t) (0.5 * fs);
    const SettingsAt settings = [&] (size_t pos)
    {
        auto s = base;
        const auto point = pos < at ? from : to;
        for (const auto& m : group)
            s.positions[(size_t) m.control] = travelPoints (m.control)[(size_t) (m.falling ? 2 - point : point)];
        return s;
    };
    chain.prepare (fs, 32, 2, settings (0));
    return worstPhaseCrestDb (render (chain, settings, quadrature (330.0, fs, 1.0, 0.01), 32), (size_t) (0.05 * fs));
}

/** The worst groupJumpCrestDb() over every pair of travel points. */
inline double groupJumpsCrestDb (const dsp::ChainSettings& base, const Group& group, double fs)
{
    double worst = -300.0;
    for (int from = 0; from < 3; ++from)
        for (int to = 0; to < 3; ++to)
            if (from != to)
                worst = std::max (worst, groupJumpCrestDb (base, group, from, to, fs));
    return worst;
}

/**
    A setting the knob groups move from, with the filters it holds Off: the knobs that do not move
    keep it, and a filter held Off stays out of every group and every random setting.
*/
struct Base
{
    std::string name;
    dsp::ChainSettings settings;
    bool hpfOff = false, lpfOff = false;
};

/**
    The bases on one card: every shape combination and every front network the switches select
    (both filters in; the high-pass in and the low-pass Off, the later card's run-time join; the
    high-pass Off and the low-pass in; both Off, the bands alone), every band at work.
*/
inline std::vector<Base> consoleBases (bool alt)
{
    auto with = [&] (float lowShape, float highShape, bool hpfOff, bool lpfOff)
    {
        auto s = consoleWorking (alt);
        s.positions[(size_t) Control::lowShape] = lowShape;
        s.positions[(size_t) Control::highShape] = highShape;
        if (hpfOff)
            s.positions[(size_t) Control::hpfFreq] = 0.0f;
        if (lpfOff)
            s.positions[(size_t) Control::lpfFreq] = 1.0f;
        return s;
    };
    return { { "LF shelf, HF bell, both filters in", with (0.0f, 1.0f, false, false), false, false },
             { "LF bell, HF shelf, both filters in", with (1.0f, 0.0f, false, false), false, false },
             { "LF shelf, HF shelf, high-pass in, low-pass Off", with (0.0f, 0.0f, false, true), false, true },
             { "LF bell, HF bell, both filters Off", with (1.0f, 1.0f, true, true), true, true },
             { "LF bell, HF shelf, high-pass Off, low-pass in", with (1.0f, 0.0f, true, false), true, false } };
}

/** The Console's knob groups moved together: each band's frequency and gain, the mids with Q, every knob (but the filters `base` holds Off). */
inline std::vector<std::pair<std::string, Group>> consoleGroups (const Base& base = {})
{
    std::vector<std::pair<std::string, Group>> groups;
    Group all;
    for (const auto c : { Control::hpfFreq, Control::lowFreq, Control::lowGain, Control::lmFreq, Control::lmGain, Control::lmQ,
                          Control::hmFreq, Control::hmGain, Control::hmQ, Control::highFreq, Control::highGain, Control::lpfFreq })
        if (! (c == Control::hpfFreq && base.hpfOff) && ! (c == Control::lpfFreq && base.lpfOff))
            all.push_back ({ c });
    for (const auto crossed : { false, true })
    {
        const auto suffix = std::string (crossed ? ", gain against" : "");
        for (const auto& [name, freq, gain] : { std::tuple { "LF", Control::lowFreq, Control::lowGain },
                                                std::tuple { "LMF", Control::lmFreq, Control::lmGain },
                                                std::tuple { "HMF", Control::hmFreq, Control::hmGain },
                                                std::tuple { "HF", Control::highFreq, Control::highGain } })
            groups.push_back ({ std::string (name) + " frequency and gain" + suffix, { { freq }, { gain, crossed } } });
        for (const auto& [name, freq, gain, q] : { std::tuple { "LMF", Control::lmFreq, Control::lmGain, Control::lmQ },
                                                   std::tuple { "HMF", Control::hmFreq, Control::hmGain, Control::hmQ } })
            groups.push_back ({ std::string (name) + " frequency, gain and Q" + suffix, { { freq }, { gain, crossed }, { q } } });
        auto every = all;
        if (crossed)
            for (size_t k = 1; k < every.size(); k += 2)
                every[k].falling = true;
        groups.push_back ({ std::string ("every knob") + (crossed ? ", every other against" : ""), every });
    }
    return groups;
}

using SettingsPair = std::pair<dsp::ChainSettings, dsp::ChainSettings>;

/**
    The long runs' seeds (ViridianMeasure, "[.report]"): not the ctest's pairs, but not held out
    either (M1c chose the control block on them; PLAN.md Open 14).
*/
inline constexpr unsigned measureSeeds = 7919;

/** The Console's knobs, the shape keys apart. */
inline constexpr Control consoleKnobs[] { Control::hpfFreq, Control::lowFreq, Control::lowGain, Control::lmFreq, Control::lmGain, Control::lmQ,
                                          Control::hmFreq, Control::hmGain, Control::hmQ, Control::highFreq, Control::highGain, Control::lpfFreq };

/**
    `pairs` seeded random pairs of settings from `base`: every knob at a random position (an end of
    its travel a quarter of the time), the filters just inside their Off zones, or Off where `base`
    holds them; its shapes. The first pairs are the same whatever the count; another `seeds`
    (measureSeeds) gives other pairs. The draws depend on the card and `seeds` only: the bases of
    one card share their knob positions and differ in the filters and shapes.
*/
inline std::vector<SettingsPair> randomJumpPairs (const Base& base, int pairs, unsigned seeds = 0)
{
    std::mt19937 rng ((base.settings.alt ? 11u : 12u) + seeds);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    std::vector<SettingsPair> out;
    for (int k = 0; k < pairs; ++k)
    {
        auto a = base.settings, b = a;
        for (const auto c : consoleKnobs)
        {
            a.positions[(size_t) c] = u (rng) < 0.25f ? (u (rng) < 0.5f ? 0.0f : 1.0f) : u (rng);
            b.positions[(size_t) c] = u (rng) < 0.25f ? (u (rng) < 0.5f ? 0.0f : 1.0f) : u (rng);
        }
        for (auto* s : { &a, &b })
        {
            auto& hpf = s->positions[(size_t) Control::hpfFreq];
            auto& lpf = s->positions[(size_t) Control::lpfFreq];
            hpf = base.hpfOff ? 0.0f : std::max (hpf, filterOffZone + 0.001f);
            lpf = base.lpfOff ? 1.0f : std::min (lpf, 1.0f - filterOffZone - 0.001f);
        }
        out.push_back ({ a, b });
    }
    return out;
}

/**
    `pairs` seeded random pairs of whole Console settings, as a preset change or an A/B switch
    makes them, the first on the card `alt`: every knob as randomJumpPairs(), each filter Off a
    quarter of the time, the shape keys, the card, EQ In off a tenth of the time, Drive -12 ...
    +6 dB (with the bands boosting the tone the amps stay linear), Output -24 ... +24 dB. Stereo,
    Tolerance 0 and Noise 0 stay: the worst-phase measure needs both channels through one network.
*/
inline std::vector<SettingsPair> presetJumpPairs (bool alt, int pairs, unsigned seeds = 0)
{
    std::mt19937 rng ((alt ? 21u : 22u) + seeds);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    auto setting = [&] (bool card)
    {
        auto s = consoleFlat (card);
        for (const auto c : consoleKnobs)
            s.positions[(size_t) c] = u (rng) < 0.25f ? (u (rng) < 0.5f ? 0.0f : 1.0f) : u (rng);
        auto& hpf = s.positions[(size_t) Control::hpfFreq];
        auto& lpf = s.positions[(size_t) Control::lpfFreq];
        hpf = u (rng) < 0.25f ? 0.0f : std::max (hpf, filterOffZone + 0.001f);
        lpf = u (rng) < 0.25f ? 1.0f : std::min (lpf, 1.0f - filterOffZone - 0.001f);
        s.positions[(size_t) Control::lowShape] = u (rng) < 0.5f ? 0.0f : 1.0f;
        s.positions[(size_t) Control::highShape] = u (rng) < 0.5f ? 0.0f : 1.0f;
        s.eqIn = u (rng) >= 0.1f;
        s.driveDb = -12.0f + 18.0f * u (rng);
        s.outputDb = -24.0f + 48.0f * u (rng);
        return s;
    };
    std::vector<SettingsPair> out;
    for (int k = 0; k < pairs; ++k)
    {
        const auto a = setting (alt);
        const auto b = setting (u (rng) < 0.5f);
        out.push_back ({ a, b });
    }
    return out;
}

/**
    The worst of measure (0 ... n-1) and its index, on `threads` threads (the lowest index of a
    tie: the same answer however the work is split).
*/
inline std::pair<double, int> worstOf (int n, int threads, const std::function<double (int)>& measure)
{
    std::vector<double> results ((size_t) std::max (n, 0), -300.0);
    std::atomic<int> next { 0 };
    auto work = [&]
    {
        for (int k = next++; k < n; k = next++)
            results[(size_t) k] = measure (k);
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < std::min (threads, n); ++t)
        pool.emplace_back (work);
    work();
    for (auto& t : pool)
        t.join();
    double worst = -300.0;
    int at = -1;
    for (int k = 0; k < n; ++k)
        if (results[(size_t) k] > worst)
            worst = results[(size_t) k], at = k;
    return { worst, at };
}

/** A 330 Hz tone at -40 dBFS in quadrature() through the chain at 32-sample blocks, the settings per block. */
inline Stereo toneThrough (const SettingsAt& settings, double fs)
{
    dsp::EqChain chain;
    chain.prepare (fs, 32, 2, settings (0));
    return render (chain, settings, quadrature (330.0, fs, 1.0, 0.01), 32);
}

/** The crest of toneThrough() at the tone's worst phase: against its whole level, or its level around each sample (`local`). */
inline double toneCrestDb (const SettingsAt& settings, double fs, bool local = false)
{
    const auto y = toneThrough (settings, fs);
    const auto start = (size_t) (0.05 * fs);
    return local ? worstPhaseLocalCrestDb (y, start, fs) : worstPhaseCrestDb (y, start);
}

/** Each pair jumped from its first setting to its second in one block, halfway through the tone: the worst crest, and its pair. */
inline std::pair<double, int> jumpPairsCrestDb (const std::vector<SettingsPair>& pairs, double fs, int threads = 1, bool local = false)
{
    const auto mid = (size_t) (0.5 * fs);
    return worstOf ((int) pairs.size(), threads,
                    [&] (int k)
                    {
                        const auto& [a, b] = pairs[(size_t) k];
                        return toneCrestDb ([&] (size_t pos) { return pos < mid ? a : b; }, fs, local);
                    });
}

/**
    Each pair toggled in one block from its first setting to its second and back every `period`
    seconds from 0.25 s on, as A/B switching does, often before a glide or a switch change has
    finished: the worst crest, and its pair.
*/
inline std::pair<double, int> togglePairsCrestDb (const std::vector<SettingsPair>& pairs, double fs, double period, int threads = 1,
                                                  bool local = false)
{
    const auto first = (size_t) (0.25 * fs), every = (size_t) (period * fs);
    return worstOf ((int) pairs.size(), threads,
                    [&] (int k)
                    {
                        const auto& [a, b] = pairs[(size_t) k];
                        return toneCrestDb ([&] (size_t pos) { return pos < first || ((pos - first) / every) % 2 == 1 ? a : b; }, fs, local);
                    });
}

/**
    Each pair jumped as jumpPairsCrestDb() does: the tone's largest level in the 0.3 s after the
    jump over the larger of its steady levels before and after, in dB (the worst, and its pair).
    Each control glides at its own pace, so a level moved one way by Output and the other by the
    bands overshoots in between.
*/
inline std::pair<double, int> jumpPairsOvershootDb (const std::vector<SettingsPair>& pairs, double fs, int threads = 1)
{
    const auto mid = (size_t) (0.5 * fs);
    return worstOf ((int) pairs.size(), threads,
                    [&] (int k)
                    {
                        const auto& [a, b] = pairs[(size_t) k];
                        const auto y = toneThrough ([&] (size_t pos) { return pos < mid ? a : b; }, fs);
                        // In quadrature the envelope is exact: sqrt (L^2 + R^2).
                        auto peak = [&] (size_t from, size_t to)
                        {
                            double p = 0.0;
                            for (size_t i = from; i < to; ++i)
                                p = std::max (p, (double) y.left[i] * y.left[i] + (double) y.right[i] * y.right[i]);
                            return std::sqrt (p);
                        };
                        const auto steady = std::max (peak (mid - (size_t) (0.05 * fs), mid), peak (y.size() - (size_t) (0.1 * fs), y.size()));
                        return toDb (peak (mid, mid + (size_t) (0.3 * fs)) / steady);
                    });
}

/**
    Every knob of the Console jumped at once between `pairs` random settings from `base`
    (randomJumpPairs(); 200 in ctest, 1000 of measureSeeds in ViridianMeasure: 30 and 100 missed
    the worst cases): the worst crest at the tone's worst phase, and its pair.
*/
inline std::pair<double, int> randomJumpsCrestDb (const Base& base, double fs, int pairs = 200, int threads = 1, unsigned seeds = 0)
{
    return jumpPairsCrestDb (randomJumpPairs (base, pairs, seeds), fs, threads);
}

/** The thread count for the long runs: every core. */
inline int allCores()
{
    return (int) std::max (1u, std::thread::hardware_concurrency());
}

//==============================================================================
/** The aliasing gate's tones (PLAN.md Settled 16, Provisional 4 and 6: every setting, every Drive). */
inline constexpr std::array<double, 5> aliasTones { 1000.0, 3000.0, 7000.0, 11000.0, 15000.0 };

/** One render of the aliasing gate: a setting, a Drive and a tone at -1 dBFS, 44.1 kHz. */
struct AliasCase
{
    std::string name;
    dsp::ChainSettings settings;
    float driveDb = 0.0f;
    double toneHz = 7000.0;
};

/**
    The aliasing gate's grid: Init, every band at work, the boosted settings (boostedSettings) and
    the cut and random ones (cutSettings) on both cards, at Drive -12 ... +24 dB in 3 dB steps and
    every tone of aliasTones: 14 170 renders.
*/
inline std::vector<AliasCase> aliasGrid()
{
    std::vector<AliasCase> out;
    for (const bool alt : { false, true })
    {
        std::vector<NamedSettings> settings { { "Init", consoleFlat (alt) }, { "every band at work", consoleWorking (alt) } };
        for (auto& b : boostedSettings (alt))
            settings.push_back (std::move (b));
        for (auto& c : cutSettings (alt))
            settings.push_back (std::move (c));
        for (const auto& [name, s] : settings)
            for (float drive = -12.0f; drive <= 24.0f; drive += 3.0f)
                for (const auto tone : aliasTones)
                    out.push_back ({ std::string (alt ? "later" : "early") + " card, " + name, s, drive, tone });
    }
    return out;
}

/**
    Tones from 2 to 15 kHz in 100 Hz steps at Init on both cards, Drive +12 and +24 dB: somewhere
    on the way a clipped harmonic falls within reach of Nyquist, where the chain's 2x pairs pass
    half of it (*ChainBandLimit*).
*/
inline std::vector<AliasCase> aliasToneScan()
{
    std::vector<AliasCase> out;
    for (const bool alt : { false, true })
        for (int tone = 2000; tone <= 15000; tone += 100)
            for (const auto drive : { 12.0f, 24.0f })
                out.push_back ({ std::string (alt ? "later" : "early") + " card, Init", consoleFlat (alt), drive, (double) tone });
    return out;
}

/**
    Tones from 18 to 21.8 kHz at Init on both cards, Drive 0 ... +24 dB in 6 dB steps, read against
    the input tone (AliasReference::input): input near Nyquist, which the chain's 2x pairs image half
    into the input amp (ChainBandLimit in front of the first pair).
*/
inline std::vector<AliasCase> aliasNyquistScan (int stepHz = 100)
{
    std::vector<AliasCase> out;
    for (const bool alt : { false, true })
        for (int tone = 18000; tone <= 21800; tone += stepHz)
            for (const auto drive : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
                out.push_back ({ std::string (alt ? "later" : "early") + " card, Init", consoleFlat (alt), drive, (double) tone });
    return out;
}

/** worstAliasDbc() of every case, on `threads` threads. */
inline std::vector<double> aliasDbc (const std::vector<AliasCase>& cases, int threads, AliasReference reference = AliasReference::clippingStage)
{
    std::vector<double> results (cases.size(), 0.0);
    std::atomic<size_t> next { 0 };
    auto work = [&]
    {
        for (size_t k = next++; k < cases.size(); k = next++)
            results[k] = worstAliasDbc (cases[k].settings, cases[k].driveDb, 44100.0, cases[k].toneHz, reference);
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < std::min (threads, (int) cases.size()); ++t)
        pool.emplace_back (work);
    work();
    for (auto& t : pool)
        t.join();
    return results;
}

//==============================================================================
/**
    The longest single process() call, in ms, over the 0.2 s after `after` replaces `before`
    (stereo noise, blocks of `block`); the best of `repetitions` fresh runs, so a stray
    preemption does not count. A switch or model change does its designs inside some call.
*/
inline double worstCallbackMs (const dsp::ChainSettings& before, const dsp::ChainSettings& after, double fs, int block,
                               int repetitions = 5)
{
    double best = 1.0e9;
    for (int r = 0; r < repetitions; ++r)
    {
        dsp::EqChain chain;
        chain.prepare (fs, block, 2, before);
        const juce::ScopedNoDenormals noDenormals;
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> dist (-0.25f, 0.25f);
        std::vector<float> l ((size_t) block), r2 ((size_t) block);
        const auto settle = (int) (0.1 * fs) / block, measured = (int) (0.2 * fs) / block;
        double worst = 0.0;
        for (int b = 0; b < settle + measured; ++b)
        {
            for (int i = 0; i < block; ++i)
                l[(size_t) i] = dist (rng), r2[(size_t) i] = dist (rng);
            float* ptrs[] { l.data(), r2.data() };
            const auto start = std::chrono::steady_clock::now();
            chain.process (ptrs, 2, block, b < settle ? before : after);
            const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
            if (b >= settle)
                worst = std::max (worst, ms);
        }
        best = std::min (best, worst);
    }
    return best;
}

/** A switch or model change for the callback measurement: the settings before and after. */
struct Change
{
    std::string name;
    dsp::ChainSettings before, after;
};

/** Every switch change on both cards (each way), Alt, and every model change to and from the Console. */
inline std::vector<Change> consoleChanges()
{
    std::vector<Change> out;
    auto with = [] (dsp::ChainSettings s, const std::function<void (dsp::ChainSettings&)>& edit)
    {
        edit (s);
        return s;
    };
    auto both = [&] (const std::string& name, const dsp::ChainSettings& a, const dsp::ChainSettings& b)
    {
        out.push_back ({ name + " (on)", a, b });
        out.push_back ({ name + " (off)", b, a });
    };
    for (const bool alt : { false, true })
    {
        const auto card = std::string (alt ? "later: " : "early: ");
        const auto base = consoleWorking (alt);
        both (card + "LF shape", base, with (base, [] (auto& s) { s.positions[(size_t) Control::lowShape] = 1.0f - s.positions[(size_t) Control::lowShape]; }));
        both (card + "HF shape", base, with (base, [] (auto& s) { s.positions[(size_t) Control::highShape] = 1.0f - s.positions[(size_t) Control::highShape]; }));
        both (card + "high-pass Off", base, with (base, [] (auto& s) { s.positions[(size_t) Control::hpfFreq] = 0.0f; }));
        both (card + "low-pass Off", base, with (base, [] (auto& s) { s.positions[(size_t) Control::lpfFreq] = 1.0f; }));
        both (card + "EQ In off", base, with (base, [] (auto& s) { s.eqIn = false; }));
        both (card + "Tolerance 50 %", base, with (base, [] (auto& s) { s.tolerance = 0.5f; }));
        const auto mid = with (base, [] (auto& s) { s.stereo = StereoMode::mid; });
        const auto side = with (base, [] (auto& s) { s.stereo = StereoMode::side; });
        out.push_back ({ card + "Stereo -> Mid", base, mid });
        out.push_back ({ card + "Mid -> Side", mid, side });
        out.push_back ({ card + "Side -> Stereo", side, base });
        out.push_back ({ card + "Alt to the other card", base, with (base, [] (auto& s) { s.alt = ! s.alt; }) });
        for (int m = 0; m < numModels; ++m)
            if (m != (int) Model::console)
            {
                const auto other = with (base, [m] (auto& s) { s.model = static_cast<Model> (m); });
                out.push_back ({ card + "Console -> " + info ((Model) m).name, base, other });
                out.push_back ({ card + info ((Model) m).name + " -> Console", other, base });
            }
    }
    return out;
}

/**
    The longest single process() call, in ms, while every knob moves every block (each its own
    sine over the whole travel, a filter within its range) and a switch changes every 50 ms in
    turn (Alt, the LF shape, the HF shape, the high-pass to Off, the low-pass to Off, each for
    50 ms), stereo noise at -12 dBFS, blocks of `block`; the best of `repetitions` fresh runs.
    Pot designs, switch designs and, at Drive, the amps' 8x path all fall into the same callbacks.
*/
inline double automatedCallbackMs (const dsp::ChainSettings& base, double fs, int block, double seconds = 1.0, int repetitions = 5, bool switches = true)
{
    const Control knobs[] { Control::hpfFreq, Control::lowFreq, Control::lowGain, Control::lmFreq, Control::lmGain, Control::lmQ,
                            Control::hmFreq, Control::hmGain, Control::hmQ, Control::highFreq, Control::highGain, Control::lpfFreq };
    auto settingsAt = [&] (double t)
    {
        auto s = base;
        for (size_t k = 0; k < std::size (knobs); ++k)
        {
            const auto wave = 0.5 + 0.5 * std::sin (twoPi * t / (0.31 + 0.13 * (double) k) + (double) k);
            auto& p = s.positions[(size_t) knobs[k]];
            p = (float) wave;
            if (knobs[k] == Control::hpfFreq)
                p = filterOffZone + 0.001f + (1.0f - filterOffZone - 0.001f) * (float) wave;
            if (knobs[k] == Control::lpfFreq)
                p = (1.0f - filterOffZone - 0.001f) * (float) wave;
        }
        switch (switches ? (int) (t / 0.05) % 5 : -1)
        {
            case -1: break;
            case 0: s.alt = ! s.alt; break;
            case 1: s.positions[(size_t) Control::lowShape] = 1.0f - s.positions[(size_t) Control::lowShape]; break;
            case 2: s.positions[(size_t) Control::highShape] = 1.0f - s.positions[(size_t) Control::highShape]; break;
            case 3: s.positions[(size_t) Control::hpfFreq] = 0.0f; break;
            default: s.positions[(size_t) Control::lpfFreq] = 1.0f; break;
        }
        return s;
    };

    double best = 1.0e9;
    for (int r = 0; r < repetitions; ++r)
    {
        dsp::EqChain chain;
        chain.prepare (fs, block, 2, settingsAt (0.0));
        const juce::ScopedNoDenormals noDenormals;
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> dist (-0.25f, 0.25f);
        std::vector<float> l ((size_t) block), r2 ((size_t) block);
        const auto blocks = (int) (seconds * fs) / block, settle = (int) (0.1 * fs) / block;
        double worst = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
                l[(size_t) i] = dist (rng), r2[(size_t) i] = dist (rng);
            const auto s = settingsAt ((double) b * block / fs);
            float* ptrs[] { l.data(), r2.data() };
            const auto start = std::chrono::steady_clock::now();
            chain.process (ptrs, 2, block, s);
            const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
            if (b >= settle)
                worst = std::max (worst, ms);
        }
        best = std::min (best, worst);
    }
    return best;
}

/**
    The first process() call after prepare() and after reset(), in ms (stereo noise, blocks of
    `block`): the chain designs its active model there. The best of `repetitions` runs.
*/
inline std::pair<double, double> firstCallbackMs (const dsp::ChainSettings& s, double fs, int block, int repetitions = 5)
{
    std::pair<double, double> best { 1.0e9, 1.0e9 };
    std::vector<float> l ((size_t) block, 0.1f), r ((size_t) block, -0.1f);
    const juce::ScopedNoDenormals noDenormals;
    auto timed = [&] (dsp::EqChain& chain)
    {
        float* ptrs[] { l.data(), r.data() };
        const auto start = std::chrono::steady_clock::now();
        chain.process (ptrs, 2, block, s);
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    };
    for (int k = 0; k < repetitions; ++k)
    {
        dsp::EqChain chain;
        chain.prepare (fs, block, 2, s);
        best.first = std::min (best.first, timed (chain));
        for (int b = 0; b < 100; ++b)
            timed (chain);
        chain.reset();
        best.second = std::min (best.second, timed (chain));
    }
    return best;
}

//==============================================================================
/** The CPU gates' input: 512 samples per channel, repeated. */
struct CpuInput
{
    std::vector<float> left, right;
};

/**
    Uniform noise peaking at `peak` (full scale 1). The chain's band-limits make it nearly Gaussian:
    at -1 dBFS its peaks reach 1.07 of the swing in the input amp and 1.15 in T8 at Init.
*/
inline CpuInput cpuNoise (float peak)
{
    std::mt19937 rng (9);
    std::uniform_real_distribution<float> dist (-peak, peak);
    CpuInput in { std::vector<float> (512), std::vector<float> (512) };
    for (size_t i = 0; i < 512; ++i)
        in.left[i] = dist (rng), in.right[i] = dist (rng);
    return in;
}

/**
    A sine of `periods` per 512 samples (11: 1031.25 Hz at 48 kHz; 160: 15 kHz) peaking at `peak`, a
    cosine on the right: more of its time near its peak than noise or music spends.
*/
inline CpuInput cpuSine (float peak, int periods = 11)
{
    CpuInput in { std::vector<float> (512), std::vector<float> (512) };
    for (size_t i = 0; i < 512; ++i)
    {
        const auto phase = twoPi * (double) periods * (double) i / 512.0;
        in.left[i] = (float) (peak * std::sin (phase));
        in.right[i] = (float) (peak * std::cos (phase));
    }
    return in;
}

/** CPU, % of one core, 48 kHz stereo, 512-sample blocks, `input` repeated; moving: the four band frequencies glide. */
inline double cpuPercent (const dsp::ChainSettings& base, bool moving, double seconds, const CpuInput& input)
{
    const double fs = 48000.0;
    const int block = 512;
    dsp::EqChain chain;
    chain.prepare (fs, block, 2, base);
    const juce::ScopedNoDenormals noDenormals;
    const auto& noiseL = input.left;
    const auto& noiseR = input.right;
    std::vector<float> l (block), r (block);
    auto s = base;
    const auto blocks = (int) (seconds * fs) / block;
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        if (moving)
            for (const auto c : { Control::lowFreq, Control::lmFreq, Control::hmFreq, Control::highFreq })
                s.positions[(size_t) c] = 0.5f + 0.4f * std::sin ((float) b * 0.02f);
        std::copy (noiseL.begin(), noiseL.end(), l.begin());
        std::copy (noiseR.begin(), noiseR.end(), r.begin());
        float* ptrs[] { l.data(), r.data() };
        chain.process (ptrs, 2, block, s);
    }
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    return 100.0 * elapsed / ((double) blocks * block / fs);
}

/** cpuPercent() with uniform noise peaking at `peak`. */
inline double cpuPercent (const dsp::ChainSettings& base, bool moving, double seconds = 10.0, float peak = 0.25f)
{
    return cpuPercent (base, moving, seconds, cpuNoise (peak));
}

/** The Console's rails (+-16 V) at the chain's output at Drive 0, in dBFS (0 dBFS is 8.70 V). */
inline double consoleRailsDbfs() { return toDb (dsp::ConsoleModel::swing / dsp::ConsoleModel::voltsPerUnit()); }

/**
    The level of cpuNoise(), in whole dB, just below the one whose output first reaches the rails:
    for a boosted setting at Drive 0, T8 driven up to its rails and not into them.
*/
inline double railNoisePeakDb (const dsp::ChainSettings& s)
{
    dsp::EqChain chain;
    chain.prepare (48000.0, 512, 2, s);
    const auto rails = fromDb (consoleRailsDbfs());
    for (double db = -40.0; db <= 6.0; db += 1.0)
    {
        chain.reset();
        const auto in = cpuNoise ((float) fromDb (db));
        Stereo signal { std::vector<float> (12288), std::vector<float> (12288) };
        for (size_t i = 0; i < signal.size(); ++i)
            signal.left[i] = in.left[i % 512], signal.right[i] = in.right[i % 512];
        const auto out = render (chain, [&] (size_t) { return s; }, std::move (signal));
        double peak = 0.0;
        for (size_t i = 2048; i < out.size(); ++i)
            peak = std::max ({ peak, (double) std::abs (out.left[i]), (double) std::abs (out.right[i]) });
        if (peak >= rails)
            return db - 1.0;
    }
    return 6.0;
}

/** One CPU gate point: a setting and its input at rest, gated against the budget or, into the rails, the ceiling. */
struct CpuCase
{
    std::string name;
    dsp::ChainSettings settings;
    CpuInput input;
    bool intoRails = false;
};

/**
    PLAN.md, Performance budget (Settled 22, Provisional 10), one card. Up to the rails at Drive 0
    (the budget): Init and every band at work with Noise and Tolerance at 0 and 100 %, noise peaking
    at -12 dBFS and at -1 dBFS (its sparse peaks touch the rails), and a sine just under them (1 kHz
    at +5 dBFS at Init, 0.97 of the swing in both amps; +4 dBFS with every band at work, 0.88 / 0.96
    in T8 on the early / later card; 15 kHz at +5 dBFS at Init, 0.97 in both, where the segments near
    the peak are too fast to sample the knee); the boosted settings that cost most into the rails,
    with noise up to T8's rails (railNoisePeakDb()). Into the rails (the ceiling): those settings at Drive +24 dB with noise
    peaking at -12, -6 and -1 dBFS, and at Drive 0 with noise peaking at +4 dBFS and, boosted, at
    -1 dBFS.
*/
inline std::vector<CpuCase> consoleCpuCases (bool alt)
{
    std::vector<CpuCase> out;
    auto text = [] (double db) { return (db >= 0.0 ? "+" : "") + std::to_string ((int) std::lround (db)) + " dBFS"; };
    std::vector<NamedSettings> plain { { "Init", consoleFlat (alt) }, { "every band at work", consoleWorking (alt) } };
    std::vector<NamedSettings> boosted;
    const auto all = boostedSettings (alt);
    for (const auto* name : { "HF boosted, bell, freq 100 %", "HF boosted, shelf, freq 90 %", "every band boosted, shelves" })
        boosted.push_back (*std::find_if (all.begin(), all.end(), [&] (const NamedSettings& n) { return n.name == name; }));

    for (const auto& [name, base] : plain)
        for (const auto spread : { 0.0f, 1.0f })
        {
            auto s = base;
            s.noise = s.tolerance = spread;
            const auto with = std::string (spread > 0.0f ? ", Noise and Tolerance 100 %" : "");
            const auto sineDb = name == "Init" ? 5.0 : 4.0;
            out.push_back ({ name + with + ", noise peaking at -12 dBFS", s, cpuNoise ((float) fromDb (-12.0)) });
            out.push_back ({ name + with + ", noise peaking at -1 dBFS", s, cpuNoise ((float) fromDb (-1.0)) });
            out.push_back ({ name + with + ", sine peaking at " + text (sineDb), s, cpuSine ((float) fromDb (sineDb)) });
            if (name == "Init")
                out.push_back ({ name + with + ", 15 kHz sine peaking at +5 dBFS", s, cpuSine ((float) fromDb (5.0), 160) });
        }
    for (const auto& [name, base] : boosted)
    {
        auto s = base;
        s.noise = s.tolerance = 1.0f;
        const auto db = railNoisePeakDb (s);
        out.push_back ({ name + ", noise up to T8's rails (" + text (db) + ")", s, cpuNoise ((float) fromDb (db)) });
    }

    std::vector<NamedSettings> loud = plain;
    loud.insert (loud.end(), boosted.begin(), boosted.end());
    for (const auto& [name, base] : loud)
    {
        auto s = base;
        s.noise = s.tolerance = 1.0f;
        auto driven = s;
        driven.driveDb = 24.0f;
        for (const auto db : { -12.0, -6.0, -1.0 })
            out.push_back ({ name + ", Drive +24 dB, noise peaking at " + text (db), driven, cpuNoise ((float) fromDb (db)), true });
        const bool isBoosted = name != "Init" && name != "every band at work";
        for (const auto db : isBoosted ? std::vector<double> { -1.0, 4.0 } : std::vector<double> { 4.0 })
            out.push_back ({ name + ", Drive 0, noise peaking at " + text (db), s, cpuNoise ((float) fromDb (db)), true });
    }
    return out;
}

/** The median of `runs` calls of `measure` (CPU figures: robust to a stray slow run either way). */
inline double medianOf (int runs, const std::function<double()>& measure)
{
    std::vector<double> v;
    for (int k = 0; k < runs; ++k)
        v.push_back (measure());
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}

/**
    CPU, % of one core, 48 kHz stereo, 512-sample blocks, over the blocks in which a model change
    runs two models: the Model knob toggles between `base`'s model and `other` every 0.25 s.
*/
inline double modelChangeCpuPercent (const dsp::ChainSettings& base, Model other, double seconds = 10.0)
{
    const double fs = 48000.0;
    const int block = 512;
    dsp::EqChain chain;
    chain.prepare (fs, block, 2, base);
    const juce::ScopedNoDenormals noDenormals;
    std::mt19937 rng (9);
    std::uniform_real_distribution<float> dist (-0.25f, 0.25f);
    std::vector<float> noiseL (block), noiseR (block), l (block), r (block);
    for (int i = 0; i < block; ++i)
        noiseL[(size_t) i] = dist (rng), noiseR[(size_t) i] = dist (rng);
    auto s = base;
    const auto blocks = (int) (seconds * fs) / block, period = (int) (0.25 * fs) / block;
    double busy = 0.0;
    int counted = 0;
    for (int b = 0; b < blocks; ++b)
    {
        s.model = (b / period) % 2 == 0 ? base.model : other;
        std::copy (noiseL.begin(), noiseL.end(), l.begin());
        std::copy (noiseR.begin(), noiseR.end(), r.begin());
        float* ptrs[] { l.data(), r.data() };
        const auto changing = chain.isChangingModel();
        const auto start = std::chrono::steady_clock::now();
        chain.process (ptrs, 2, block, s);
        const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        if (changing || chain.isChangingModel())
        {
            busy += elapsed;
            ++counted;
        }
    }
    return counted > 0 ? 100.0 * busy / ((double) counted * block / fs) : 0.0;
}

} // namespace viridian::gates
