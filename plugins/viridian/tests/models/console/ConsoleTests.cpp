#include "dsp/models/console/ConsoleCard.h"
#include "dsp/models/console/ConsoleControls.h"
#include "dsp/models/console/ConsoleModel.h"
#include "models/console/reference/ConsoleReference.h"

#include "common/dsp/Oversampler2x.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

using namespace viridian;
using namespace viridian::dsp;
namespace ref = viridian::testing::console;

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;
    constexpr double rates[] { 44100.0, 48000.0, 96000.0, 192000.0 };

    double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-300)); }

    std::unique_ptr<console::CardChannel> cardFor (bool later, double fs, const console::Pots& p)
    {
        auto c = later ? console::makeLaterCard() : console::makeEarlyCard();
        c->prepare (fs);
        c->reset (p);
        return c;
    }

    console::Pots potsOf (const ref::ReferenceSetting& s)
    {
        const auto& v = s.pots;
        console::Pots p;
        p.later = s.later;
        p.hpfIn = v[0] > 0.5;
        p.hpf = v[1];
        p.lpfIn = v[2] > 0.5;
        p.lpf = v[3];
        p.hf = { v[4], v[5], 0.5, v[6] > 0.5 };
        p.hmf = { v[7], v[8], v[9], true };
        p.lmf = { v[10], v[11], v[12], true };
        p.lf = { v[13], v[14], 0.5, v[15] > 0.5 };
        return p;
    }

    /** Peak of the 3rd difference over RMS, in dB (the siblings' click detector). */
    double clickCrestDb (const std::vector<double>& x, size_t start)
    {
        double peak = 0.0, sum = 0.0;
        for (size_t i = std::max<size_t> (start, 3); i < x.size(); ++i)
            peak = std::max (peak, std::abs (x[i] - 3.0 * x[i - 1] + 3.0 * x[i - 2] - x[i - 3]));
        for (size_t i = start; i < x.size(); ++i)
            sum += x[i] * x[i];
        return toDb (peak / std::sqrt (sum / (double) (x.size() - start)));
    }

    /** The band's calibration frequency (NOTES.md, "Calibration"). */
    double calibrationHz (console::Band b)
    {
        switch (b)
        {
            case console::Band::hf:  return 12000.0;
            case console::Band::hmf: return 3000.0;
            case console::Band::lmf: return 1000.0;
            case console::Band::lf:  return 80.0;
        }
        return 1000.0;
    }

    console::BandPots& bandOf (console::Pots& p, console::Band b)
    {
        switch (b)
        {
            case console::Band::hf:  return p.hf;
            case console::Band::hmf: return p.hmf;
            case console::Band::lmf: return p.lmf;
            case console::Band::lf:  return p.lf;
        }
        return p.hf;
    }

    /** The band's deviation at its calibration point (the analog network): peak dB and its Q. */
    std::pair<double, double> deviation (bool later, console::Band b, double wiper, double gang)
    {
        console::Pots p;
        p.later = later;
        auto& band = bandOf (p, b);
        band.freq = console::bandFraction (later, b, calibrationHz (b));
        band.q = gang;
        band.bell = true;
        auto flat = p;
        band.gain = wiper;
        const auto on = cardFor (later, 48000.0, p), off = cardFor (later, 48000.0, flat);
        std::vector<double> f, d;
        for (double x = calibrationHz (b) / 10.0; x < std::min (calibrationHz (b) * 10.0, 100000.0); x *= std::pow (10.0, 1.0 / 2000.0))
        {
            f.push_back (x);
            d.push_back (toDb (std::abs (on->analogResponse (x) / off->analogResponse (x))));
        }
        const auto sign = wiper >= 0.5 ? 1.0 : -1.0;
        size_t peak = 0;
        for (size_t i = 0; i < d.size(); ++i)
            if (sign * d[i] > sign * d[peak])
                peak = i;
        auto edge = [&] (int step)
        {
            for (auto i = (long) peak; i + step >= 0 && i + step < (long) d.size(); i += step)
                if (sign * d[(size_t) (i + step)] < sign * d[peak] - 3.0)
                    return f[(size_t) i];
            return f[peak];
        };
        const auto lo = edge (-1), hi = edge (1);
        return { d[peak], std::sqrt (lo * hi) / (hi - lo) };
    }
} // namespace

TEST_CASE ("Console: the card matches ngspice within 0.1 dB to 20 kHz at every rate", "[console][ngspice]")
{
    for (double fs : rates)
    {
        double worstDigital = 0.0, worstAnalog = 0.0;
        std::string where;
        for (const auto& s : ref::referenceSettings)
        {
            const auto card = cardFor (s.later, fs, potsOf (s));
            double peak = 0.0;
            for (int k = 0; k < ref::referenceFrequencies; ++k)
                peak = std::max (peak, std::abs (std::complex<double> { s.re[(size_t) k], s.im[(size_t) k] }));
            for (int k = 0; k < ref::referenceFrequencies; ++k)
            {
                const auto hz = ref::referenceHz[(size_t) k];
                const std::complex<double> h { s.re[(size_t) k], s.im[(size_t) k] };
                if (hz > 20000.0 * (1.0 + 1.0e-9) || std::abs (h) < 1.0e-4 * peak)
                    continue;   // a filter's stop band: no level to compare in dB
                const auto digital = std::abs (toDb (std::abs (card->response (hz))) - toDb (std::abs (h)));
                const auto analog = std::abs (toDb (std::abs (card->analogResponse (hz))) - toDb (std::abs (h)));
                if (digital > worstDigital)
                    worstDigital = digital, where = std::string (s.name) + " at " + std::to_string (hz) + " Hz";
                worstAnalog = std::max (worstAnalog, analog);
            }
        }
        INFO (fs << " Hz: realised " << worstDigital << " dB (" << where << "), analog " << worstAnalog << " dB");
        CHECK (worstDigital <= 0.1);
        CHECK (worstAnalog <= 0.001);
    }
}

TEST_CASE ("Console: curve and phase against ngspice, per rate", "[console][.report]")
{
    for (double fs : rates)
    {
        double worstDb = 0.0, worstPhase = 0.0;
        for (const auto& s : ref::referenceSettings)
        {
            const auto card = cardFor (s.later, fs, potsOf (s));
            for (int k = 0; k < ref::referenceFrequencies; ++k)
            {
                const auto hz = ref::referenceHz[(size_t) k];
                const std::complex<double> h { s.re[(size_t) k], s.im[(size_t) k] };
                if (hz > 20000.0 * (1.0 + 1.0e-9) || std::abs (h) < 1.0e-4)
                    continue;
                const auto r = card->response (hz);
                worstDb = std::max (worstDb, std::abs (toDb (std::abs (r)) - toDb (std::abs (h))));
                if (hz <= 10000.0 * (1.0 + 1.0e-9))
                    worstPhase = std::max (worstPhase, std::abs (std::arg (r / h)) * 180.0 / std::numbers::pi);
            }
        }
        std::printf ("%6.1f kHz: worst %.4f dB to 20 kHz, phase %.2f deg to 10 kHz (%zu settings)\n", fs / 1000.0, worstDb,
                     worstPhase, ref::referenceSettings.size());
    }
}

TEST_CASE ("Console: display = audio (impulse response DFT equals response())", "[console]")
{
    for (double fs : { 44100.0, 96000.0 })
        for (const auto* name : { "early: both filters, all bands boosted", "later: random 3" })
        {
            const auto it = std::find_if (ref::referenceSettings.begin(), ref::referenceSettings.end(),
                                          [&] (const auto& s) { return std::string (s.name) == name; });
            REQUIRE (it != ref::referenceSettings.end());
            const auto card = cardFor (it->later, fs, potsOf (*it));
            // Sub-audio poles (coupling capacitors, < 0.01 Hz) decay over minutes: compare where
            // a 20 s window resolves them.
            std::vector<double> ir ((size_t) (20.0 * fs), 0.0);
            ir[0] = 1.0;
            card->process (ir.data(), (int) ir.size());
            double worst = 0.0;
            for (double f = 100.0; f < 0.45 * fs; f *= 1.25)
            {
                std::complex<double> acc {}, phasor { 1.0, 0.0 };
                const auto step = std::polar (1.0, -twoPi * f / fs);
                for (size_t n = 0; n < ir.size(); ++n)
                {
                    acc += ir[n] * phasor;
                    phasor = (n & 1023) == 1023 ? std::polar (1.0, -twoPi * f / fs * (double) (n + 1)) : phasor * step;
                }
                worst = std::max (worst, std::abs (toDb (std::abs (acc)) - toDb (std::abs (card->response (f)))));
            }
            INFO (name << " at " << fs << " Hz: " << worst << " dB");
            CHECK (worst < 1.0e-4);
        }
}

TEST_CASE ("Console: the later card's high-pass joined to its bands settles like the circuit", "[console]")
{
    // One cascade of degree 22, the high-pass's zeros at DC and the coupling capacitors' poles near
    // them: zeros placed far from their poles amplified the rounding into a sub-audio swell (at
    // 192 kHz up to 0.4 of the impulse after 1 s, 108 dB over response() at 20 Hz).
    console::Pots p;
    p.later = true;
    p.hpfIn = true;
    p.hpf = 0.066;
    p.hf = { 0.5, 0.2, 0.5, false };
    p.hmf = { 0.5, 0.2, 0.88, true };
    p.lmf = { 0.5, 0.2, 0.89, true };
    p.lf = { 0.5, 0.22, 0.5, false };
    for (double fs : { 44100.0, 192000.0 })
        for (const double level : { 1.0, 1.0e-3 })
        {
            const auto card = cardFor (true, fs, p);
            std::vector<double> ir ((size_t) (8.0 * fs), 0.0);
            ir[0] = level;
            card->process (ir.data(), (int) ir.size());
            double tail = 0.0;
            for (auto i = (size_t) fs; i < ir.size(); ++i)
                tail = std::max (tail, std::abs (ir[i]) / level);
            std::complex<double> acc {};
            for (size_t n = 0; n < ir.size(); ++n)
                acc += ir[n] / level * std::polar (1.0, -twoPi * 20.0 / fs * (double) n);
            const auto at20 = std::abs (toDb (std::abs (acc)) - toDb (std::abs (card->response (20.0))));
            INFO (fs << " Hz, impulse " << level << ": tail after 1 s " << tail << ", 20 Hz " << at20 << " dB from response()");
            CHECK (tail < 1.0e-6);
            CHECK (at20 < 0.01);
        }
}

TEST_CASE ("Console: gain laws read out the circuit's deviation at the calibration points", "[console][laws]")
{
    for (const bool later : { false, true })
        for (const auto b : { console::Band::hf, console::Band::hmf, console::Band::lmf, console::Band::lf })
        {
            const auto range = later ? 18.0 : (b == console::Band::hf ? 16.1 : (b == console::Band::lf ? 15.2 : 15.0));
            const bool mid = b == console::Band::hmf || b == console::Band::lmf;
            double worst = 0.0;
            for (double g = -range; g <= range + 1.0e-9; g += range / 6.0)
            {
                const auto d = deviation (later, b, console::wiperFor (later, b, g), mid ? 1.0 : 0.5).first;
                worst = std::max (worst, std::abs (d - g));
            }
            INFO ((later ? "later " : "early ") << (int) b << ": worst " << worst << " dB");
            CHECK (worst < 0.02);
        }
}

TEST_CASE ("Console: Q laws read out the circuit's Q at the calibration points", "[console][laws]")
{
    struct Range
    {
        bool later;
        console::Band band;
        double lo, hi;
    };
    for (const auto& r : { Range { false, console::Band::hmf, 0.53, 2.78 }, Range { false, console::Band::lmf, 0.49, 2.89 },
                           Range { true, console::Band::hmf, 0.54, 4.06 }, Range { true, console::Band::lmf, 0.50, 4.26 } })
    {
        double worst = 0.0;
        for (int k = 0; k <= 6; ++k)
        {
            const auto q = r.lo * std::pow (r.hi / r.lo, k / 6.0);
            const auto measured = deviation (r.later, r.band, 1.0, console::gangFor (r.later, r.band, q)).second;
            worst = std::max (worst, std::abs (measured / q - 1.0));
        }
        INFO ((r.later ? "later " : "early ") << (int) r.band << ": worst " << 100.0 * worst << " %");
        CHECK (worst < 0.01);
    }
}

TEST_CASE ("Console: frequency laws invert the gangs' nominal frequency", "[console][laws]")
{
    for (const bool later : { false, true })
        for (const auto b : { console::Band::hf, console::Band::hmf, console::Band::lmf, console::Band::lf })
            for (double x : { 0.0, 0.1, 0.37, 0.5, 0.9, 1.0 })
                CHECK (std::abs (console::bandFraction (later, b, console::bandNominalHz (later, b, x)) - x) < 1.0e-9);
}

TEST_CASE ("Console: every pot sweeps without clicks (3rd-difference crest)", "[console][modulation]")
{
    // A 330 Hz sine while one pot glides 0 -> 1 -> 0 over 2 s, the others boosted so every band acts;
    // the shelves at the rates whose map differs least from the bells' (44.1 and 96 kHz).
    using Setter = void (*) (console::Pots&, double);
    struct Sweep
    {
        const char* name;
        Setter set;
    };
    const Sweep sweeps[] {
        { "hpf", [] (console::Pots& p, double v) { p.hpf = v; } },
        { "lpf", [] (console::Pots& p, double v) { p.lpf = v; } },
        { "hf gain", [] (console::Pots& p, double v) { p.hf.gain = v; } },
        { "hf freq", [] (console::Pots& p, double v) { p.hf.freq = v; } },
        { "hmf gain", [] (console::Pots& p, double v) { p.hmf.gain = v; } },
        { "hmf freq", [] (console::Pots& p, double v) { p.hmf.freq = v; } },
        { "hmf q", [] (console::Pots& p, double v) { p.hmf.q = v; } },
        { "lmf gain", [] (console::Pots& p, double v) { p.lmf.gain = v; } },
        { "lmf freq", [] (console::Pots& p, double v) { p.lmf.freq = v; } },
        { "lmf q", [] (console::Pots& p, double v) { p.lmf.q = v; } },
        { "lf gain", [] (console::Pots& p, double v) { p.lf.gain = v; } },
        { "lf freq", [] (console::Pots& p, double v) { p.lf.freq = v; } },
    };
    // 48 kHz puts the fitted map's band edge at 20 kHz; 176.4 and 192 kHz use a map of degree 2.
    for (const auto& [fs, withShelves] : { std::pair { 44100.0, true }, std::pair { 48000.0, false }, std::pair { 96000.0, true },
                                           std::pair { 192000.0, false } })
        for (const int variant : { 0, 1, 2 })     // early; later; later with the low-pass out
            for (const bool shelves : { false, true })
                for (const auto& sweep : sweeps)
                {
                    if (shelves && ! withShelves)
                        continue;
                    const bool later = variant > 0;
                    if (variant == 2 && std::string_view { sweep.name } == "lpf")
                        continue;
                    console::Pots p;
                    p.later = later;
                    p.hpfIn = true;
                    p.lpfIn = variant < 2;
                    p.hpf = 0.7;
                    p.lpf = 0.3;
                    p.hf = { 0.8, 0.4, 0.5, ! shelves };
                    p.hmf = { 0.25, 0.5, 0.8, true };
                    p.lmf = { 0.85, 0.6, 0.9, true };
                    p.lf = { 0.2, 0.5, 0.5, ! shelves };
                    sweep.set (p, 0.0);
                    const auto card = cardFor (later, fs, p);
                    const auto total = (size_t) (2.0 * fs);
                    std::vector<double> x (total);
                    for (size_t i = 0; i < total; ++i)
                        x[i] = 0.25 * std::sin (twoPi * 330.0 * (double) i / fs);
                    for (size_t i = 0; i < total; i += 32)
                    {
                        const auto t = (double) i / (double) total;
                        sweep.set (p, t < 0.5 ? 2.0 * t : 2.0 - 2.0 * t);
                        card->setPots (p);
                        card->process (x.data() + i, (int) std::min<size_t> (32, total - i));
                    }
                    const auto crest = clickCrestDb (x, (size_t) (0.05 * fs));
                    INFO ((variant == 0 ? "early " : (variant == 1 ? "later " : "later, low-pass out, ")) << (shelves ? "shelves, " : "bells, ") << sweep.name << " at " << fs
                                                        << " Hz: " << crest << " dB");
                    CHECK (crest <= -60.0);
                }
}

namespace
{
    /**
        The 3rd-difference crest at the tone's worst phase, from a sine on `l` and a cosine on `r`
        through the same networks (tests/support/ChainGates.h, worstPhaseCrestDb()).
    */
    double worstPhaseCrestDb (const std::vector<double>& l, const std::vector<double>& r, size_t start)
    {
        double peak = 0.0, ll = 0.0, rr = 0.0, lr = 0.0;
        for (size_t i = std::max<size_t> (start, 3); i < l.size(); ++i)
        {
            const auto dl = l[i] - 3.0 * l[i - 1] + 3.0 * l[i - 2] - l[i - 3];
            const auto dr = r[i] - 3.0 * r[i - 1] + 3.0 * r[i - 2] - r[i - 3];
            peak = std::max (peak, dl * dl + dr * dr);
        }
        for (size_t i = start; i < l.size(); ++i)
        {
            ll += l[i] * l[i];
            rr += r[i] * r[i];
            lr += l[i] * r[i];
        }
        const auto smallest = 0.5 * (ll + rr) - std::sqrt (0.25 * (ll - rr) * (ll - rr) + lr * lr);
        return toDb (std::sqrt (peak) / std::sqrt (std::max (smallest, 1.0e-300) / (double) (l.size() - start)));
    }

    /** A card's every pot jumping from `a` to `b` halfway through a 330 Hz tone at -40 dBFS, at 32-sample blocks. */
    double cardJumpCrestDb (const console::Pots& a, const console::Pots& b, double fs)
    {
        const auto card = cardFor (a.later, fs, a);
        card->align (0);
        const auto total = (size_t) fs;
        const auto fade = 0.01 * fs;
        std::vector<double> l (total), r (total);
        for (size_t i = 0; i < total; ++i)
        {
            const auto w = (double) i < fade ? 0.5 - 0.5 * std::cos (std::numbers::pi * (double) i / fade) : 1.0;
            l[i] = 0.01 * w * std::sin (twoPi * 330.0 * (double) i / fs);
            r[i] = 0.01 * w * std::cos (twoPi * 330.0 * (double) i / fs);
        }
        for (size_t i = 0; i < total; i += 32)
        {
            card->setPots (i < total / 2 ? a : b);
            double* ch[] { l.data() + i, r.data() + i };
            card->process (ch, 2, (int) std::min<size_t> (32, total - i));
        }
        return worstPhaseCrestDb (l, r, (size_t) (0.05 * fs));
    }

    console::Pots cardPots (bool later, bool hpfIn, bool lpfIn, double hpf, double lpf, console::BandPots hf, console::BandPots hmf,
                            console::BandPots lmf, console::BandPots lf)
    {
        console::Pots p;
        p.later = later;
        p.hpfIn = hpfIn;
        p.lpfIn = lpfIn;
        p.hpf = hpf;
        p.lpf = lpf;
        p.hf = hf;
        p.hmf = hmf;
        p.lmf = lmf;
        p.lf = lf;
        return p;
    }
} // namespace

TEST_CASE ("Console: every pot jumped at once where roots of different cascade stages meet, without a click", "[console][modulation]")
{
    // Traced worst cases of the chain's random every-knob jumps (PLAN.md Open 14; the last from
    // measureSeeds) as the cards' pots: two roots held in different stages meet in the glide
    // (early: a split pair meets a real pole, or two real poles of the back; later: two real poles
    // meet), and the later high-pass front has a root above 290 kHz under a leading coefficient
    // 1e-14 of the largest (console NOTES.md, "Measured").
    struct Jump
    {
        const char* name;
        console::Pots from, to;
    };
    const Jump jumps[] {
        { "early card, both filters Off (base 3, pair 45)",
          cardPots (false, false, false, 1.0, 0.0, { 0.48740355100805699, 0.0, 0.5, true },
                    { 0.018458026815902917, 0.046986445327728291, 0.43825153439501396, true },
                    { 0.33461500106390807, 0.5784777542390791, 0.88111161637318203, true }, { 0.18447241991433022, 0.0044753872370277715, 0.5, true }),
          cardPots (false, false, false, 1.0, 0.0, { 0.86971428818658592, 0.50151377266540109, 0.5, true },
                    { 0.92251511528369545, 0.69508902389071958, 0.34118307276348059, true },
                    { 2.7560282238684891e-05, 0.4300381846796863, 0.092053073880309921, true }, { 0.12820612346563262, 0.0, 0.5, true }) },
        { "later card, both filters Off (base 3, pair 11)",
          cardPots (true, false, false, 1.0, 0.0, { 0.00053533405858015813, 0.0029502184733272681, 0.5, true },
                    { 1.0, 0.030936241604631851, 0.52238333991229957, true },
                    { 0.22092395675389176, 0.5799776778191984, 0.91867703461354266, true }, { 0.60098690163806245, 0.0014321008953163074, 0.5, true }),
          cardPots (true, false, false, 1.0, 0.0, { 0.917449302060803, 0.0, 0.5, true }, { 0.65872547652354285, 1.0, 0.94845253439914279, true },
                    { 0.0081481659858000446, 0.69852907815381371, 0.99724155915947144, true }, { 1.0, 0.16011919886051482, 0.5, true }) },
        { "later card, high-pass in, low-pass Off (base 2, pair 44)",
          cardPots (true, true, false, 3.6220867326982384e-07, 0.0, { 0.015494252961185357, 0.81145158113688176, 0.5, false },
                    { 1.0, 0.026754692617723067, 0.0079384606603241439, true },
                    { 0.89915836805227722, 0.043199475135547255, 0.99992812484605398, true }, { 0.97786033977014997, 0.10309097078710518, 0.5, false }),
          cardPots (true, true, false, 0.14243569779217236, 0.0, { 0.9711931424392154, 0.0, 0.5, false },
                    { 0.13537844475375849, 0.0, 0.35732890302558823, true }, { 0.8956207094108164, 0.04459811966143401, 0.95808743328836576, true },
                    { 1.0, 0.5457627393022495, 0.5, false }) },
        { "early card, both filters Off (measure seeds, base 3, pair 719)",
          cardPots (false, false, false, 1.0, 0.0, { 0.43581716841732709, 0.99999991268415989, 0.5, true },
                    { 0.026999293842841859, 0.58162952660593337, 0.99998998617265011, true },
                    { 0.05978924776109483, 0.74942112694200624, 0.70467304641663864, true }, { 0.002757201930693651, 0.39492118330059495, 0.5, true }),
          cardPots (false, false, false, 1.0, 0.0, { 0.99931745190602983, 0.14733438281255198, 0.5, true },
                    { 0.17689480445640146, 0.035142396919680796, 0.51591047426413883, true },
                    { 0.63240824762802283, 0.074178377648907959, 0.69631855931561537, true }, { 0.00068986853813460638, 0.39457518413245729, 0.5, true }) },
    };
    for (const auto& j : jumps)
        for (const auto fs : rates)
        {
            const auto crest = cardJumpCrestDb (j.from, j.to, fs);
            INFO (j.name << ", " << fs << " Hz: " << crest << " dB");
            CHECK (crest <= -60.0);
        }
}

TEST_CASE ("Console: the amps' small-signal gain is exactly the model's report", "[console][drive]")
{
    // Drive goes into the input amp and the chain divides by Drive times this gain: at -80 dBFS
    // (far below the rails) the amps must give exactly what smallSignalGain() says.
    ConsoleModel model;
    const double fs = 48000.0;
    for (auto* s : model.stages())
        s->prepare (s->rate() == Stage::Rate::oversampled ? 2.0 * fs : fs, 2 * 4800, 1);
    ModelSettings settings;
    settings.eq.model = Model::console;
    settings.eqIn = false;      // the amps alone
    model.setSettings (settings);
    model.reset();

    for (double driveDb : { -12.0, 0.0, 12.0, 24.0 })
    {
        const auto drive = std::pow (10.0, driveDb / 20.0);
        const int n = 4800;
        std::vector<double> x ((size_t) n), up ((size_t) (2 * n));
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = 1.0e-4 * std::sin (twoPi * 1000.0 * i / fs) * drive;
        myzel::dsp::Oversampler2x a, b;
        StageContext host { fs }, twice { 2.0 * fs };
        auto oversampled = [&] (Stage& s, myzel::dsp::Oversampler2x& o)
        {
            for (int i = 0; i < n; ++i)
                o.upsample (x[(size_t) i], up[(size_t) (2 * i)], up[(size_t) (2 * i + 1)]);
            double* u[] { up.data() };
            s.process (u, 1, 2 * n, twice);
            for (int i = 0; i < n; ++i)
                x[(size_t) i] = o.downsample (up[(size_t) (2 * i)], up[(size_t) (2 * i + 1)]);
        };
        const auto stages = model.stages();
        oversampled (*stages[0], a);
        double* ch[] { x.data() };
        stages[1]->process (ch, 1, n, host);
        oversampled (*stages[2], b);

        double peak = 0.0;
        for (int i = n / 2; i < n; ++i)
            peak = std::max (peak, std::abs (x[(size_t) i]));
        const auto level = toDb (peak / (1.0e-4 * drive * model.smallSignalGain()));
        INFO ("Drive " << driveDb << " dB: " << level << " dB");
        CHECK (std::abs (level) < 0.01);
    }
}

TEST_CASE ("Console: flat is the circuit's flat", "[console][flat]")
{
    // PLAN's gate asks for 0.1 dB of the amps-only response; the drawn network departs from flat
    // by itself (C8 across the summing amps, NOTES.md "Flat"), so the gate here is the circuit's.
    for (const auto* name : { "early: flat", "later: flat" })
    {
        const auto it = std::find_if (ref::referenceSettings.begin(), ref::referenceSettings.end(),
                                      [&] (const auto& s) { return std::string (s.name) == name; });
        REQUIRE (it != ref::referenceSettings.end());
        const auto card = cardFor (it->later, 48000.0, potsOf (*it));
        double worst = 0.0, departure = 0.0;
        for (int k = 0; k < ref::referenceFrequencies; ++k)
        {
            const auto hz = ref::referenceHz[(size_t) k];
            if (hz < 20.0 || hz > 20000.0 * (1.0 + 1.0e-9))
                continue;
            const auto h = std::abs (std::complex<double> { it->re[(size_t) k], it->im[(size_t) k] });
            worst = std::max (worst, std::abs (toDb (std::abs (card->response (hz))) - toDb (h)));
            departure = std::max (departure, std::abs (toDb (h)));
        }
        INFO (name << ": " << worst << " dB from ngspice; the circuit departs from flat by " << departure << " dB");
        CHECK (worst <= 0.1);
    }
}

TEST_CASE ("Console: CPU of the card networks, at rest and moving", "[console][.perf]")
{
    for (const bool later : { false, true })
    {
        const double fs = 48000.0;
        console::Pots p;
        p.later = later;
        p.hpfIn = p.lpfIn = true;
        p.hpf = 0.5;
        p.lpf = 0.3;
        p.hf.gain = 0.8;
        p.lmf.gain = 0.3;
        std::array<std::unique_ptr<console::CardChannel>, 2> cards { cardFor (later, fs, p), cardFor (later, fs, p) };
        const int n = (int) (10.0 * fs);
        std::vector<double> l ((size_t) n, 0.1), r ((size_t) n, 0.1);
        for (const bool moving : { false, true })
        {
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < n; i += 32)
            {
                if (moving)
                {
                    p.hf.freq = p.hmf.freq = p.lmf.freq = p.lf.freq = 0.5 + 0.4 * std::sin ((double) i / n * 20.0);
                    for (auto& c : cards)
                        c->setPots (p);
                }
                const auto len = std::min (32, n - i);
                cards[0]->process (l.data() + i, len);
                cards[1]->process (r.data() + i, len);
            }
            const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            std::printf ("%s card, both filters in, 48 kHz stereo, %s: %.2f %% of one core\n", later ? "later" : "early",
                         moving ? "four frequency pots moving" : "at rest", 100.0 * seconds / 10.0);
        }
    }
}
