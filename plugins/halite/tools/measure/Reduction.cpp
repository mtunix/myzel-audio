#include "Reduction.h"

#include "support/Gates.h"

#include "dsp/chain/HaliteChain.h"
#include "dsp/common/RealFft.h"
#include "dsp/detector/DetectorProbe.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <numbers>
#include <thread>

namespace halite::reduction
{
namespace
{
    constexpr float reducedDb = gates::reducedDb;     // a hop counts as cut above this
    constexpr double accuracyGateDb = 0.5;            // PLAN.md: the ess at the threshold ± 0.5 dB
    constexpr double collateralGateDb = 0.1;          // PLAN.md: outside the band ≤ 0.1 dB during events
    constexpr double harshOverlapDb = 8.7e-6;         // a depth over the chain's 10⁻⁶ snap: Ess reduction it applies
    constexpr double harshGuardMs = 60.0;             // the spectra's half frame (21 ms) and Spectral's reach past its hops (about 20 ms)
    constexpr double thirdOctave = 1.2599210498948732; // 2^(1/3): the readings start a third of an octave past each edge
    constexpr double reachMarginDb = 0.5;             // Spectral's reach: asked at least this far under what an in-band cut can take

    const char* nameOf (Shape s) { return s == Shape::split ? "Split" : (s == Shape::spectral ? "Spectral" : "Wide"); }

    /** One ess (or one Harsh episode), read on the input and on the output. */
    struct Reading
    {
        std::string where;          // recording and local time
        double askedDb = 0.0;       // what the detector's shaped reduction takes off the band's energy over the span
        double lostDb = 0.0;        // what the band's energy lost
        double landingDb = 0.0;     // over the decided hops, the energy kept against the energy cut exactly by the target
        bool landed = false;        // there was a decided hop
        double belowDb = 0.0, aboveDb = 0.0;    // the third octave outside the band that changed most, each side
        double belowLevelDb = 0.0, aboveLevelDb = 0.0;  // its power against the span's, dB
        bool hasBelow = false, hasAbove = false;
        double ownLostDb = 0.0;     // what the band's own bins lost (weighted by the band filter), and what they lose under
        double ownAskedDb = 0.0;    // the shaped reduction applied as asked, hop by hop: Settled 39's and 40's reading
        bool hasOwn = false;
        double outsideShare = 0.0;  // of the band filter's energy, the share in bins outside the band (no in-band cut reaches it)
        double reachDb = 0.0;       // the most a cut inside the band, at most Range per bin, can take off that energy
        double essDistanceMs = -1.0; // Harsh episodes: how far the nearest Ess reduction is (−1: none)
    };

    struct Null
    {
        long samples = 0, inexact = 0;
        double largest = 0.0;       // the largest |out + removed − dry|
        double peak = 0.0;          // the largest |dry|
    };

    struct ShapeResult
    {
        std::vector<Reading> ess, harsh;
        std::vector<float> tokenLostDb;     // each labelled /s/ /z/ token: what B's energy over its label lost (Harsh off)
        long harshLeftOut = 0;      // Harsh episodes left out for Ess reduction nearby
        Null null;
        double seconds = 0.0;
    };

    using Results = std::map<std::string, std::map<Shape, ShapeResult>>;

    /** The aligned output of the chain over `in` (mono), in 512-sample blocks, under flush-to-zero. */
    std::vector<float> render (const std::vector<float>& in, double rate, const dsp::ChainSettings& s)
    {
        const juce::ScopedNoDenormals noDenormals;
        auto chain = std::make_unique<dsp::HaliteChain>();
        chain->prepare (rate, 512, 1, s.detector.latency);
        const auto latency = dsp::HaliteChain::latencySamples (s.detector.latency, rate);

        std::vector<float> buffer (in.size() + (size_t) latency, 0.0f);
        std::copy (in.begin(), in.end(), buffer.begin());
        for (size_t pos = 0; pos < buffer.size(); pos += 512)
        {
            float* channel = buffer.data() + pos;
            chain->process (&channel, 1, (int) std::min<size_t> (512, buffer.size() - pos), s);
        }
        return { buffer.begin() + latency, buffer.end() };
    }

    /** Mean power of x through the detector's band filter, per 32-sample hop. */
    std::vector<double> bandPower (const std::vector<float>& x, double lowHz, double highHz, double rate)
    {
        dsp::BandFilter filter;
        filter.setBand (lowHz, highHz, rate);
        const auto hop = dsp::Detector::hopSize;
        std::vector<double> power ((x.size() + (size_t) hop - 1) / (size_t) hop, 0.0);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const auto y = filter.process ((double) x[i]);
            power[i / (size_t) hop] += y * y / hop;
        }
        return power;
    }

    /** Per hop of a probe, path 0 and reduction 0 (the measurement is mono), and the events. */
    struct Hops
    {
        std::vector<float> essTarget, harshTarget, ess, harsh;
        std::vector<std::pair<int, int>> events;    // input samples
    };

    Hops hopsOf (const std::vector<float>& x, double rate, const dsp::DetectorSettings& s)
    {
        const float* channel = x.data();
        const auto r = dsp::probe::run (&channel, 1, (int) x.size(), rate, s);
        Hops h;
        for (const auto& f : r.hops)
        {
            h.essTarget.push_back (f.decisions[0].essTargetDb);
            h.harshTarget.push_back (f.decisions[0].harshTargetDb);
            h.ess.push_back (f.reductions[0].essDb);
            h.harsh.push_back (f.reductions[0].harshDb);
        }
        for (const auto& e : r.events)
            h.events.emplace_back ((int) e.start, (int) std::min<std::int64_t> (e.end, (std::int64_t) x.size()));
        return h;
    }

    /**
        The third octave outside a band that changed most from `in` to `out` over [from, to), each
        side: Hann-windowed power spectra (frames of about 43 ms, a quarter apart), third octaves
        from 200 Hz up to a third of an octave under the band and from a third over it up to 0.45 fs.
        A third octave holding less than 10⁻¹⁰ of the span's power is not read.
    */
    class Spectra
    {
    public:
        explicit Spectra (double sampleRate) : rate (sampleRate)
        {
            auto order = 11;
            while ((1 << order) < 2048.0 * rate / 48000.0 - 1.0)
                ++order;
            fft.prepare (order);
            n = fft.size();
            frame.resize ((size_t) n);
            re.resize ((size_t) n / 2 + 1);
            im.resize ((size_t) n / 2 + 1);
        }

        /** `exact`: the same input under the shaped reduction applied as asked, for the own band's reading. */
        void read (const std::vector<float>& in, const std::vector<float>& out, const std::vector<float>* exact, int from, int to, double lowHz,
                   double highHz, double rangeDb, Reading& r)
        {
            std::vector<double> pin ((size_t) n / 2 + 1, 0.0), pout ((size_t) n / 2 + 1, 0.0), pexact ((size_t) n / 2 + 1, 0.0);
            const auto centre = (from + to) / 2;
            auto first = to - from <= n ? centre - n / 2 : from;
            const auto last = to - from <= n ? first : to - n;
            for (auto start = first; start <= last; start += n / 4)
            {
                accumulate (in, start, pin);
                accumulate (out, start, pout);
                if (exact != nullptr)
                    accumulate (*exact, start, pexact);
            }

            auto total = 0.0;
            for (const auto p : pin)
                total += p;

            // The detector's band filter is bilinear, so its power response at f is the analogue one at the prewarped frequency.
            const auto warp = [this] (double hz) { return std::tan (std::numbers::pi * std::min (hz, 0.49 * rate) / rate); };
            auto inside = 0.0, outside = 0.0, insideOut = 0.0, insideExact = 0.0;
            for (int k = 1; k <= n / 2; ++k)
            {
                const auto hz = k * rate / n;
                const auto w = 1.0 / (1.0 + std::pow (warp (lowHz) / warp (hz), 4.0)) / (1.0 + std::pow (warp (hz) / warp (highHz), 4.0));
                const auto inBand = hz >= lowHz && hz <= highHz;
                (inBand ? inside : outside) += w * pin[(size_t) k];
                insideOut += inBand ? w * pout[(size_t) k] : 0.0;
                insideExact += inBand ? w * pexact[(size_t) k] : 0.0;
            }
            r.outsideShare = outside / std::max (inside + outside, 1.0e-300);
            r.hasOwn = exact != nullptr && inside > 0.0;
            if (r.hasOwn)
            {
                r.ownLostDb = 10.0 * std::log10 (inside / std::max (insideOut, 1.0e-300));
                r.ownAskedDb = 10.0 * std::log10 (inside / std::max (insideExact, 1.0e-300));
            }
            r.reachDb = -10.0 * std::log10 (r.outsideShare + (1.0 - r.outsideShare) * std::pow (10.0, -rangeDb / 10.0));
            const auto worst = [&] (double fromHz, double toHz, double& db, double& levelDb, bool& has)
            {
                for (auto c = 200.0 * std::sqrt (thirdOctave); c * std::sqrt (thirdOctave) <= toHz; c *= thirdOctave)
                {
                    if (c / std::sqrt (thirdOctave) < fromHz)
                        continue;
                    auto a = 0.0, b = 0.0;
                    for (int k = 1; k <= n / 2; ++k)
                    {
                        const auto hz = k * rate / n;
                        if (hz >= c / std::sqrt (thirdOctave) && hz < c * std::sqrt (thirdOctave))
                        {
                            a += pin[(size_t) k];
                            b += pout[(size_t) k];
                        }
                    }
                    if (a <= 1.0e-10 * total || a <= 0.0)
                        continue;
                    const auto change = 10.0 * std::log10 (std::max (b, 1.0e-300) / a);
                    if (! has || std::abs (change) > std::abs (db))
                    {
                        db = change;
                        levelDb = 10.0 * std::log10 (a / total);
                    }
                    has = true;
                }
            };
            worst (0.0, lowHz / thirdOctave, r.belowDb, r.belowLevelDb, r.hasBelow);
            worst (highHz * thirdOctave, 0.45 * rate, r.aboveDb, r.aboveLevelDb, r.hasAbove);
        }

    private:
        void accumulate (const std::vector<float>& x, int start, std::vector<double>& power)
        {
            for (int j = 0; j < n; ++j)
            {
                const auto i = start + j;
                const auto w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * j / n);
                frame[(size_t) j] = i >= 0 && i < (int) x.size() ? (float) (w * x[(size_t) i]) : 0.0f;
            }
            fft.forward (frame.data(), re.data(), im.data());
            for (size_t k = 0; k < power.size(); ++k)
                power[k] += (double) re[k] * re[k] + (double) im[k] * im[k];
        }

        double rate;
        dsp::RealFft fft;
        int n = 0;
        std::vector<float> frame, re, im;
    };

    /**
        Over hops [first, last) of one band's energy: asked (what the shaped reduction takes off), lost,
        and landing: over the hops whose target is over reducedDb, the energy the band kept against what
        it would keep cut by exactly its target, hop by hop (0: the ess at the threshold, Range permitting).
    */
    void readBand (Reading& r, int first, int last, const std::vector<double>& powerIn, const std::vector<double>& powerOut,
                   const std::vector<float>& reductionDb, const std::vector<float>& targetDb)
    {
        auto in = 0.0, kept = 0.0, out = 0.0, decidedOut = 0.0, decidedTarget = 0.0;
        for (auto h = std::max (0, first); h < last && h < (int) powerIn.size(); ++h)
        {
            in += powerIn[(size_t) h];
            kept += powerIn[(size_t) h] * std::pow (10.0, -reductionDb[(size_t) h] / 10.0);
            out += powerOut[(size_t) h];
            if (targetDb[(size_t) h] > reducedDb)
            {
                decidedOut += powerOut[(size_t) h];
                decidedTarget += powerIn[(size_t) h] * std::pow (10.0, -targetDb[(size_t) h] / 10.0);
            }
        }
        r.askedDb = 10.0 * std::log10 (std::max (in, 1.0e-300) / std::max (kept, 1.0e-300));
        r.lostDb = 10.0 * std::log10 (std::max (in, 1.0e-300) / std::max (out, 1.0e-300));
        r.landed = decidedTarget > 0.0;
        r.landingDb = r.landed ? 10.0 * std::log10 (std::max (decidedOut, 1.0e-300) / decidedTarget) : 0.0;
    }

    std::string whereOf (const corpus::Recording& rec, int sample)
    {
        const auto [id, at] = corpus::locate (rec, sample / rec.sampleRate);
        return juce::String::formatted ("%s %.2f s", id.c_str(), at).toStdString();
    }

    /** Every shape over one recording (or stream). */
    std::map<Shape, ShapeResult> measureOne (const corpus::Recording& rec, const Options& options)
    {
        const auto rate = rec.sampleRate;
        auto x = rec.audio;
        const auto gain = std::pow (10.0f, options.gainDb / 20.0f);
        for (auto& v : x)
            v *= gain;

        auto settings = options.settings;
        if (const auto t = options.thresholds.find (rec.entry.corpus + "/" + rec.entry.speaker); t != options.thresholds.end())
        {
            const auto voice = settings.detection == Detection::voice;
            if (std::isfinite (t->second.first))
                (voice ? settings.thresholdDb : settings.thresholdAbsDb) = t->second.first;
            if (std::isfinite (t->second.second))
                (voice ? settings.harshThresholdDb : settings.harshThresholdAbsDb) = t->second.second;
        }
        auto essSettings = settings;
        essSettings.harshOn = false;
        auto harshSettings = settings;
        harshSettings.harshOn = true;

        const auto bands = dsp::Detector::bandsFor (harshSettings, rate);
        const auto hop = dsp::Detector::hopSize;
        const auto guardHops = (int) std::ceil (0.001 * harshGuardMs * rate / hop);
        const auto in = hopsOf (x, rate, harshSettings);     // Harsh does not change the Ess detector

        // What the band's own bins lose under the shaped reduction applied as asked: for the Ess band
        // exactly (Wide's gain is the reduction, sample by sample); for the Harsh band, which Wide cuts
        // with its bell too, hop by hop on the input hops it acts on (the reading `asked` makes of B).
        dsp::ChainSettings exactly;
        exactly.shape = Shape::wide;
        exactly.detector = essSettings;
        const auto essAsked = render (x, rate, exactly);
        const auto asked = [&] (const std::vector<float>& reductionDb)
        {
            std::vector<float> y (x.size());
            for (size_t i = 0; i < x.size(); ++i)
            {
                const auto h = std::min (i / (size_t) hop, reductionDb.size() - 1);
                y[i] = x[i] * std::pow (10.0f, -reductionDb[h] / 20.0f);
            }
            return y;
        };
        const auto harshAsked = asked (in.harsh);
        const auto essIn = bandPower (x, bands.essLow, bands.essHigh, rate);
        const auto harshIn = bandPower (x, bands.harshLow, bands.harshHigh, rate);
        Spectra spectra (rate);

        // Harsh episodes: runs of hops cut by more than reducedDb, with no Ess reduction within the guard of them.
        std::vector<std::pair<int, int>> episodes;
        std::vector<double> essDistanceMs;
        long leftOut = 0;
        for (int h = 0; h < (int) in.harsh.size();)
        {
            if (in.harsh[(size_t) h] <= reducedDb)
            {
                ++h;
                continue;
            }
            auto end = h;
            while (end < (int) in.harsh.size() && in.harsh[(size_t) end] > reducedDb)
                ++end;
            auto overlaps = false;
            for (auto k = std::max (0, h - guardHops); k < std::min ((int) in.ess.size(), end + guardHops); ++k)
                overlaps = overlaps || in.ess[(size_t) k] > harshOverlapDb;
            if (overlaps)
            {
                ++leftOut;
            }
            else
            {
                auto nearest = -1;
                for (int k = 0; k < (int) in.ess.size(); ++k)
                    if (in.ess[(size_t) k] > harshOverlapDb)
                    {
                        const auto d = k < h ? h - k : k - end + 1;
                        nearest = nearest < 0 ? d : std::min (nearest, d);
                    }
                episodes.emplace_back (h, end);
                essDistanceMs.push_back (nearest < 0 ? -1.0 : 1000.0 * nearest * hop / rate);
            }
            h = end;
        }

        std::map<Shape, ShapeResult> results;
        for (const auto shape : options.shapes)
        {
            auto& result = results[shape];
            result.seconds = (double) x.size() / rate;
            result.harshLeftOut = leftOut;

            dsp::ChainSettings s;
            s.shape = shape;
            s.detector = essSettings;
            const auto essOut = render (x, rate, s);
            const auto essOutPower = bandPower (essOut, bands.essLow, bands.essHigh, rate);
            for (const auto& seg : rec.labels)
            {
                if (corpus::classOf (seg.phone) != corpus::PhoneClass::sibilant || corpus::sibilantIndex (seg.phone) > 1)
                    continue;
                const auto first = std::clamp ((int) std::floor (seg.start * rate / hop), 0, (int) essIn.size());
                const auto last = std::clamp ((int) std::ceil (seg.end * rate / hop), first, (int) essIn.size());
                auto pin = 0.0, pout = 0.0;
                for (auto h = first; h < last; ++h)
                {
                    pin += essIn[(size_t) h];
                    pout += essOutPower[(size_t) h];
                }
                if (pin > 0.0)
                    result.tokenLostDb.push_back ((float) (10.0 * std::log10 (pin / std::max (pout, 1.0e-300))));
            }
            for (const auto& [from, to] : in.events)
            {
                Reading r;
                r.where = whereOf (rec, from);
                readBand (r, from / hop, (to + hop - 1) / hop, essIn, essOutPower, in.ess, in.essTarget);
                spectra.read (x, essOut, &essAsked, from, to, bands.essLow, bands.essHigh, essSettings.rangeDb, r);
                result.ess.push_back (std::move (r));
            }

            s.detector = harshSettings;
            const auto both = render (x, rate, s);
            auto listening = s;
            listening.listen = Listen::removed;
            const auto removed = render (x, rate, listening);
            for (size_t i = 0; i < x.size(); ++i)
            {
                const auto e = std::abs ((double) both[i] + (double) removed[i] - (double) x[i]);
                result.null.largest = std::max (result.null.largest, e);
                result.null.peak = std::max (result.null.peak, (double) std::abs (x[i]));
                result.null.inexact += juce::exactlyEqual (both[i] + removed[i], x[i]) ? 0 : 1;
            }
            result.null.samples += (long) x.size();

            const auto harshOutPower = bandPower (both, bands.harshLow, bands.harshHigh, rate);
            for (size_t e = 0; e < episodes.size(); ++e)
            {
                const auto [first, last] = episodes[e];
                Reading r;
                r.where = whereOf (rec, first * hop);
                r.essDistanceMs = essDistanceMs[e];
                readBand (r, first, last, harshIn, harshOutPower, in.harsh, in.harshTarget);
                spectra.read (x, both, &harshAsked, first * hop, std::min ((int) x.size(), last * hop), bands.harshLow, bands.harshHigh,
                              harshSettings.harshRangeDb, r);
                result.harsh.push_back (std::move (r));
            }
        }
        return results;
    }

    //==============================================================================
    struct Spread
    {
        double median = 0.0, p5 = 0.0, p95 = 0.0, worst = 0.0;     // worst: the largest magnitude, signed
        double within = 0.0;                                        // percent within the gate
        long count = 0;
    };

    Spread spreadOf (const std::vector<Reading>& readings, const std::function<bool (const Reading&)>& has,
                     const std::function<double (const Reading&)>& value, double gate)
    {
        std::vector<float> v;
        Spread s;
        long inside = 0;
        for (const auto& r : readings)
            if (has (r))
            {
                const auto x = value (r);
                v.push_back ((float) x);
                inside += std::abs (x) <= gate ? 1 : 0;
                if (std::abs (x) > std::abs (s.worst))
                    s.worst = x;
            }
        s.count = (long) v.size();
        if (v.empty())
            return s;
        s.within = 100.0 * (double) inside / (double) v.size();
        s.median = gates::percentile (v, 50.0);
        s.p5 = gates::percentile (v, 5.0);
        s.p95 = gates::percentile (v, 95.0);
        return s;
    }

    std::string cell (const Spread& s, int decimals = 2)
    {
        if (s.count == 0)
            return "–";
        return juce::String::formatted ("%+.*f [%+.*f, %+.*f], worst %+.*f; %.1f %% within", decimals, s.median, decimals, s.p5, decimals, s.p95,
                                        decimals, s.worst, s.within)
            .toStdString();
    }

    const char* verdict (const Spread& s) { return s.count == 0 ? "–" : (s.within >= 100.0 ? "pass" : "**fail**"); }

    const auto always = [] (const Reading&) { return true; };
    const auto fidelity = [] (const Reading& r) { return r.lostDb - r.askedDb; };
    const auto landed = [] (const Reading& r) { return r.landed; };
    const auto hasOwn = [] (const Reading& r) { return r.hasOwn; };
    const auto ownFidelity = [] (const Reading& r) { return r.ownLostDb - r.ownAskedDb; };
    const auto landing = [] (const Reading& r) { return r.landingDb; };
    const auto hasBelow = [] (const Reading& r) { return r.hasBelow; };
    const auto below = [] (const Reading& r) { return r.belowDb; };
    const auto hasAbove = [] (const Reading& r) { return r.hasAbove; };
    const auto above = [] (const Reading& r) { return r.aboveDb; };

    void row (std::FILE* out, const std::string& corpusName, const char* name, Shape shape, const std::vector<Reading>& readings, long leftOut, bool harsh)
    {
        std::vector<float> asked;
        for (const auto& x : readings)
            asked.push_back ((float) x.askedDb);
        const auto f = spreadOf (readings, always, fidelity, accuracyGateDb);
        const auto o = spreadOf (readings, hasOwn, ownFidelity, accuracyGateDb);
        const auto l = spreadOf (readings, landed, landing, accuracyGateDb);
        // Settled 38–40: Split and Spectral on the band's own power, Wide on the landing; the Harsh band on its own power.
        const auto& gated = ! harsh && shape == Shape::wide ? l : o;
        const auto b = spreadOf (readings, hasBelow, below, collateralGateDb);
        const auto a = spreadOf (readings, hasAbove, above, collateralGateDb);
        const auto collateralPass = (b.count == 0 || b.within >= 100.0) && (a.count == 0 || a.within >= 100.0);
        const auto collateralGate = ! harsh && shape == Shape::wide ? "– (Wide cuts everything)"
                                                                    : (b.count + a.count == 0 ? "–" : (collateralPass ? "pass" : "**fail**"));
        const auto count = harsh ? juce::String::formatted ("%zu (%ld left out)", readings.size(), leftOut) : juce::String ((int) readings.size());
        std::fprintf (out, "| %s | %s | %s | %.2f dB | %s | %s | %s | %s | %s | %s | %s |\n", corpusName.c_str(), name, count.toRawUTF8(),
                      asked.empty() ? 0.0 : gates::percentile (asked, 50.0), cell (o).c_str(), cell (f).c_str(), cell (l).c_str(), verdict (gated),
                      cell (b, 3).c_str(), cell (a, 3).c_str(), collateralGate);
    }

    void table (std::FILE* out, const char* title, const char* intro, const Results& results, bool harsh)
    {
        std::fprintf (out, "\n## %s\n\n%s\n\n| Corpus | Shape | %s | Asked (median) | Own band: lost − asked, median [p5, p95] | Lost − asked: median [p5, p95] "
                           "| Landing: median [p5, p95] | Accuracy gate | Below the band | Above the band | Collateral gate |\n|---|---|---|---|---|---|---|---|---|---|---|\n",
                      title, intro, harsh ? "Episodes" : "Events");
        for (const auto& [corpusName, shapes] : results)
            for (const auto& [shape, r] : shapes)
            {
                const auto& readings = harsh ? r.harsh : r.ess;
                row (out, corpusName, nameOf (shape), shape, readings, r.harshLeftOut, harsh);
                if (shape == Shape::wide)
                    continue;
                std::vector<Reading> reachable;
                for (const auto& x : readings)
                    if (x.askedDb <= x.reachDb - reachMarginDb)
                        reachable.push_back (x);
                row (out, corpusName, shape == Shape::split ? "Split, within its reach" : "Spectral, within its reach", shape, reachable, r.harshLeftOut, harsh);
            }

        std::fprintf (out, "\nOutside the band (every shape alike): the share of the band filter's energy in bins outside the band, and the most an "
                           "in-band cut (at most Range per bin) can take off the band's energy. *Within its reach*: the readings that ask at "
                           "least %.1f dB less than that.\n\n| Corpus | %s | Share outside: median, p90, largest | Reach: median, p10, smallest | "
                           "Asking more than the reach |\n|---|---|---|---|---|\n",
                      reachMarginDb, harsh ? "Episodes" : "Events");
        for (const auto& [corpusName, shapes] : results)
        {
            const auto& readings = harsh ? shapes.begin()->second.harsh : shapes.begin()->second.ess;
            std::vector<float> share, reach;
            long beyond = 0;
            for (const auto& x : readings)
            {
                share.push_back ((float) (100.0 * x.outsideShare));
                reach.push_back ((float) x.reachDb);
                beyond += x.askedDb > x.reachDb - reachMarginDb ? 1 : 0;
            }
            if (readings.empty())
                continue;
            std::fprintf (out, "| %s | %zu | %.1f, %.1f, %.1f %% | %.1f, %.1f, %.1f dB | %ld (%.1f %%) |\n", corpusName.c_str(), readings.size(),
                          gates::percentile (share, 50.0), gates::percentile (share, 90.0), gates::percentile (share, 100.0),
                          gates::percentile (reach, 50.0), gates::percentile (reach, 10.0), gates::percentile (reach, 0.0), beyond,
                          100.0 * (double) beyond / (double) readings.size());
        }
    }

    void worstList (std::FILE* out, const Results& results, int count)
    {
        if (count <= 0)
            return;
        std::fprintf (out, "\n## The worst readings\n");
        for (const auto& [corpusName, shapes] : results)
            for (const auto& [shape, r] : shapes)
                for (const auto harsh : { false, true })
                {
                    auto readings = harsh ? r.harsh : r.ess;
                    const auto list = [&] (const char* what, const std::function<bool (const Reading&)>& has, const std::function<double (const Reading&)>& value,
                                           const std::function<double (const Reading&)>& level = {})
                    {
                        std::vector<const Reading*> v;
                        for (const auto& x : readings)
                            if (has (x))
                                v.push_back (&x);
                        std::sort (v.begin(), v.end(), [&] (auto* a, auto* b) { return std::abs (value (*a)) > std::abs (value (*b)); });
                        std::fprintf (out, "\n**%s, %s, %s, %s**:", corpusName.c_str(), nameOf (shape), harsh ? "Harsh" : "Ess", what);
                        for (size_t i = 0; i < std::min (v.size(), (size_t) count); ++i)
                        {
                            std::fprintf (out, "%s %s %+.2f dB (asked %.2f, reach %.1f", i == 0 ? "" : ";", v[i]->where.c_str(), value (*v[i]),
                                          v[i]->askedDb, v[i]->reachDb);
                            if (level)
                                std::fprintf (out, ", a third octave %.0f dB under the span's power", -level (*v[i]));
                            if (harsh)
                                std::fprintf (out, v[i]->essDistanceMs < 0.0 ? ", no ess" : ", ess %.0f ms away", v[i]->essDistanceMs);
                            std::fprintf (out, ")");
                        }
                        std::fprintf (out, "\n");
                    };
                    list ("lost − asked", always, fidelity);
                    list ("own band, lost − asked", hasOwn, ownFidelity);
                    list ("landing", landed, landing);
                    list ("below the band", hasBelow, below, [] (const Reading& x) { return x.belowLevelDb; });
                    list ("above the band", hasAbove, above, [] (const Reading& x) { return x.aboveLevelDb; });
                }
    }

    /** Units of work: one recording each, or one speaker's recordings (a stream). */
    std::vector<std::vector<corpus::Entry>> unitsOf (const std::vector<corpus::Entry>& entries, bool stream)
    {
        std::vector<std::vector<corpus::Entry>> units;
        if (! stream)
        {
            for (const auto& e : entries)
                units.push_back ({ e });
            return units;
        }
        std::map<std::string, size_t> bySpeaker;
        for (const auto& e : entries)
        {
            const auto key = e.corpus + "/" + e.speaker;
            if (! bySpeaker.contains (key))
            {
                bySpeaker[key] = units.size();
                units.emplace_back();
            }
            units[bySpeaker[key]].push_back (e);
        }
        return units;
    }
}

bool measure (const std::vector<CorpusEntries>& data, const Options& options, std::FILE* out)
{
    Results results;
    std::mutex mutex;
    std::atomic<bool> failed { false };
    std::map<std::string, std::pair<int, double>> counted;     // recordings and seconds per corpus

    for (const auto& c : data)
    {
        const auto units = unitsOf (c.entries, options.stream);
        std::atomic<size_t> next { 0 };
        std::vector<std::thread> threads;
        for (int j = 0; j < std::max (1, options.jobs); ++j)
            threads.emplace_back ([&] {
                for (auto u = next++; u < units.size(); u = next++)
                {
                    std::vector<corpus::Recording> recordings (units[u].size());
                    for (size_t i = 0; i < units[u].size(); ++i)
                    {
                        juce::String error;
                        if (! corpus::load (units[u][i], recordings[i], error))
                        {
                            std::fprintf (stderr, "%s\n", error.toRawUTF8());
                            failed = true;
                            return;
                        }
                    }
                    if (options.stream)
                        recordings = corpus::streams (std::move (recordings));
                    for (const auto& rec : recordings)
                    {
                        auto measured = measureOne (rec, options);
                        const std::scoped_lock lock (mutex);
                        counted[c.name].first += (int) std::max<size_t> (1, rec.parts.size());
                        counted[c.name].second += (double) rec.audio.size() / rec.sampleRate;
                        for (auto& [shape, r] : measured)
                        {
                            auto& into = results[c.name][shape];
                            into.ess.insert (into.ess.end(), r.ess.begin(), r.ess.end());
                            into.harsh.insert (into.harsh.end(), r.harsh.begin(), r.harsh.end());
                            into.tokenLostDb.insert (into.tokenLostDb.end(), r.tokenLostDb.begin(), r.tokenLostDb.end());
                            into.harshLeftOut += r.harshLeftOut;
                            into.null.samples += r.null.samples;
                            into.null.inexact += r.null.inexact;
                            into.null.largest = std::max (into.null.largest, r.null.largest);
                            into.null.peak = std::max (into.null.peak, r.null.peak);
                            into.seconds += r.seconds;
                        }
                    }
                }
            });
        for (auto& t : threads)
            t.join();
        if (failed)
            return false;
    }

    const auto& s = options.settings;
    std::fprintf (out, "# Halite reduction gates (real material)\n\n%s, threshold %+.1f dB%s, Range %.1f dB, Detect %.0f–%.0f Hz, Focus %.0f %%, "
                       "look-ahead %.1f ms (%s), Speed %s; Harsh threshold %+.1f dB, Range %.1f dB, %.0f Hz, %.2f oct; input gain %+.1f dB%s.\n\n",
                  s.detection == Detection::voice ? "Voice" : "Allround", s.detection == Detection::voice ? s.thresholdDb : s.thresholdAbsDb,
                  s.detection == Detection::voice ? "" : "FS", s.rangeDb, s.detectLowHz, s.detectHighHz, 100.0f * s.focus, s.lookaheadMs,
                  s.latency == LatencyMode::normal ? "Normal" : "Live", s.speedAuto ? "Auto" : juce::String::formatted ("%.0f ms", s.releaseMs).toRawUTF8(),
                  s.harshThresholdDb, s.harshRangeDb, s.harshFreqHz, s.harshWidthOct, options.gainDb,
                  options.stream ? ", each speaker's recordings back to back" : ", each recording from a cold start");
    for (const auto& [name, n] : counted)
        std::fprintf (out, "- **%s**: %d recordings, %.1f s\n", name.c_str(), n.first, n.second);

    table (out, "Ess band (Harsh off)",
           "Per event (the detector's, on the input): *asked* is what its shaped reduction takes off the energy of the Detect band (its own band "
           "filter, B) over the event, *lost* what that energy lost: the chain's fidelity. *Own band* (Settled 39): what the band's own bins "
           "(inside Detect Low … High, weighted by the band filter) lost, against what they lose with the shaped reduction applied exactly "
           "(Wide's render), Hann spectra over the event. *Landing* is where the ess lands on B: over the event's hops whose target is over "
           "0.5 dB, B's energy in the output against its energy cut hop by hop by exactly the target (dB); 0 puts the ess at the threshold "
           "(Range permitting), negative under it, and it includes the detector's ballistics (the shaped reduction against the target). "
           "The accuracy gate (every event within ±0.5 dB) reads the own band for Split and Spectral and the landing for Wide. *Below* and "
           "*above*: of the third octaves from 200 Hz to a third of an octave under the band, and from a third over it to 0.45 fs, the one that "
           "changed most during the event (signed dB); the gate wants every one within 0.1 dB.",
           results, false);
    table (out, "Harsh band (Harsh on)",
           "Per episode (hops where the detector cuts the Harsh band by more than 0.5 dB), the same readings on the Harsh band's energy; the "
           "accuracy gate reads its own band (Settled 40: the Harsh band's power; against its reduction applied hop by hop, since Wide "
           "cuts the Harsh band with its bell), the landing (H) is reported. "
           "Episodes with any Ess reduction the chain applies (its release tail too, until the depth's 1e-6 snap) within 60 ms are left "
           "out: both bands act there (the Harsh band holds during an ess by design; in Spectral even a tail of 0.01 dB cuts the Ess band's "
           "loudest bins), Spectral's Ess cut reaches about 20 ms past its hops and the spectra 21 ms past the episode.",
           results, true);

    std::fprintf (out, "\n## Labelled /s/ /z/ tokens (Harsh off)\n\nWhat the Ess band filter's energy over each token's label lost through the shape, "
                       "every token (those the detector leaves alone lose 0): the rule Learn applies asks the median ess for 4 dB at its peak and the "
                       "quietest quarter for none; a token's mean over its label loses less than its peak.\n\n"
                       "| Corpus | Shape | Tokens | p25 | Median | p75 | p90 | Losing over 0.5 dB |\n|---|---|---|---|---|---|---|---|\n");
    for (const auto& [corpusName, shapes] : results)
        for (const auto& [shape, r] : shapes)
        {
            if (r.tokenLostDb.empty())
                continue;
            auto v = r.tokenLostDb;
            const auto over = std::count_if (v.begin(), v.end(), [] (float x) { return x > reducedDb; });
            std::fprintf (out, "| %s | %s | %zu | %.2f dB | %.2f dB | %.2f dB | %.2f dB | %.1f %% |\n", corpusName.c_str(), nameOf (shape), v.size(),
                          gates::percentile (v, 25.0), gates::percentile (v, 50.0), gates::percentile (v, 75.0), gates::percentile (v, 90.0),
                          100.0 * (double) over / (double) v.size());
        }

    std::fprintf (out, "\n## Listen Removed (Harsh on): out + removed = dry\n\nTwo renders, Listen Output and Listen Removed, against the aligned input.\n\n"
                       "| Corpus | Shape | Samples | Not exact | Largest error | Peak of the input | Gate |\n|---|---|---|---|---|---|---|\n");
    for (const auto& [corpusName, shapes] : results)
        for (const auto& [shape, r] : shapes)
        {
            // out = dry − removed is rounded once: the sum is dry within half an ulp of the larger part.
            const auto bound = std::ldexp (std::max (r.null.peak, 1.0e-30), -23);
            std::fprintf (out, "| %s | %s | %ld | %.3f %% | %.3g (%.1f dBFS) | %.3f | %s |\n", corpusName.c_str(), nameOf (shape), r.null.samples,
                          100.0 * (double) r.null.inexact / (double) std::max (1L, r.null.samples), r.null.largest,
                          20.0 * std::log10 (std::max (r.null.largest, 1.0e-30)), r.null.peak, r.null.largest <= bound ? "pass" : "**fail**");
        }

    worstList (out, results, options.worst);
    return true;
}

} // namespace halite::reduction
