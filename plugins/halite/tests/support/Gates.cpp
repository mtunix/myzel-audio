#include "support/Gates.h"

#include "detector/Signals.h"

#include <algorithm>
#include <tuple>
#include <cmath>
#include <limits>

namespace halite::gates
{

using corpus::PhoneClass;

namespace
{
    bool overlapsEvent (const dsp::probe::Result& r, double fromSeconds, double toSeconds)
    {
        const auto from = fromSeconds * r.sampleRate, to = toSeconds * r.sampleRate;
        return std::any_of (r.events.begin(), r.events.end(),
                            [from, to] (const dsp::EssEvent& ev) { return (double) ev.start < to && (double) ev.end > from; });
    }
}

dsp::probe::Result run (const corpus::Recording& rec, const dsp::DetectorSettings& settings, const dsp::DetectorTuning& tuning, float gainDb)
{
    const auto gain = std::pow (10.0f, gainDb / 20.0f);
    std::vector<float> scaled;
    const float* input = rec.audio.data();
    if (std::abs (gainDb) > 0.0f)
    {
        scaled.resize (rec.audio.size());
        std::transform (rec.audio.begin(), rec.audio.end(), scaled.begin(), [gain] (float x) { return x * gain; });
        input = scaled.data();
    }
    return dsp::probe::run (&input, 1, (int) rec.audio.size(), rec.sampleRate, settings, nullptr, 0, { 512 }, tuning);
}

RecordingResult analyse (const corpus::Recording& rec, const dsp::probe::Result& r, float thresholdDb)
{
    RecordingResult out;
    const auto& e = rec.entry;
    out.id = e.id;
    out.corpus = e.corpus;
    out.speaker = e.speaker;
    out.sex = e.sex;
    out.split = e.split;
    out.role = e.role;
    out.sung = e.sung;
    out.seconds = (double) rec.audio.size() / rec.sampleRate;

    const auto numHops = (long) r.hops.size();
    const auto hopSeconds = r.hopSize / r.sampleRate;
    const auto bright = e.sung;

    // Labels are contiguous and in order, so the sibilants are sorted by both ends.
    std::vector<std::pair<double, double>> sibilants;
    for (const auto& s : rec.labels)
        if (corpus::classOf (s.phone) == PhoneClass::sibilant)
            sibilants.emplace_back (s.start, s.end);

    const auto distanceToSibilant = [&sibilants] (double t)
    {
        auto best = std::numeric_limits<double>::max();
        const auto next = std::lower_bound (sibilants.begin(), sibilants.end(), t, [] (const auto& s, double x) { return s.second < x; });
        if (next != sibilants.end())
            best = t < next->first ? next->first - t : 0.0;
        if (next != sibilants.begin())
            best = std::min (best, t - std::prev (next)->second);
        return best;
    };

    for (size_t k = 0; k < rec.labels.size(); ++k)
    {
        const auto& seg = rec.labels[k];
        const auto cls = corpus::classOf (seg.phone);
        const auto before = k > 0 ? corpus::classOf (rec.labels[k - 1].phone) : PhoneClass::silence;
        const auto first = std::clamp ((long) std::floor (seg.start / hopSeconds), 0L, numHops);
        const auto last = std::clamp ((long) std::ceil (seg.end / hopSeconds), first, numHops);

        if (cls == PhoneClass::sibilant)
        {
            Token t;
            t.sibilant = corpus::sibilantIndex (seg.phone);
            t.start = seg.start;
            t.end = seg.end;
            t.afterVowel = before == PhoneClass::vowel;

            // Added: Harsh reduction over what the hop before the token carried (the vowel's own cut).
            const auto carried = first > 0 ? r.hops[(size_t) first - 1].reductions[0].harshDb : 0.0f;
            auto harshSum = 0.0, heldSum = 0.0, addedSum = 0.0;
            for (auto i = first; i < last; ++i)
            {
                const auto& h = r.hops[(size_t) i];
                t.peakSibilanceDb = std::max (t.peakSibilanceDb, h.paths[0].sibilanceDb);
                t.peakReductionDb = std::max (t.peakReductionDb, h.reductions[0].essDb);
                t.harshPeakDb = std::max (t.harshPeakDb, h.reductions[0].harshDb);
                harshSum += h.reductions[0].harshDb;
                heldSum += h.decisions[0].harshHeld ? h.reductions[0].harshDb : 0.0f;
                addedSum += std::max (0.0f, h.reductions[0].harshDb - carried);

                const auto centre = ((double) i + 0.5) * hopSeconds;
                if (centre < seg.start || centre >= seg.end)
                    continue;
                t.reducedAtStart = t.reducedAtStart || (t.hops == 0 && h.reductions[0].essDb > reducedDb);
                ++t.hops;
                if (h.reductions[0].essDb > reducedDb && t.reducedHops++ == 0)
                    t.firstReducedMs = (float) (1000.0 * (centre - seg.start));
                t.reductionSumDb += h.reductions[0].essDb;
                t.onsetHeld = t.onsetHeld || h.paths[0].voiceOnset || h.paths[0].jumpHeld;
                if (h.decisions[0].essTargetDb > reducedDb && t.decidedHops++ == 0)
                    t.firstDecisionMs = (float) (1000.0 * (centre - seg.start));
            }
            t.harshMeanDb = last > first ? (float) (harshSum / (double) (last - first)) : 0.0f;
            t.harshAddedDb = last > first ? (float) (addedSum / (double) (last - first)) : 0.0f;
            out.harshSibilantSum += harshSum;
            out.harshSibilantHeldSum += heldSum;
            out.harshSibilantAddedSum += addedSum;
            out.harshSibilantHops += last - first;

            t.detected = overlapsEvent (r, seg.start, seg.end);
            out.tokens.push_back (t);
        }
        else if (cls == PhoneClass::vowel)
        {
            if (seg.end - seg.start >= pumpVowelSeconds && last > first && r.hops[(size_t) first].paths[0].harshDb > dsp::silentDb)
            {
                std::vector<float> harsh;
                for (auto i = first; i < last; ++i)
                    harsh.push_back (r.hops[(size_t) i].reductions[0].harshDb);
                if (*std::max_element (harsh.begin(), harsh.end()) > reducedDb)
                {
                    // A dip: down by more than pumpDipDb from the highest since the last one, then up as much from its lowest.
                    auto high = harsh.front(), low = harsh.front();
                    auto falling = false;
                    for (const auto v : harsh)
                    {
                        if (! falling)
                        {
                            high = std::max (high, v);
                            if (v < high - pumpDipDb)
                            {
                                falling = true;
                                low = v;
                            }
                        }
                        else
                        {
                            low = std::min (low, v);
                            if (v > low + pumpDipDb)
                            {
                                falling = false;
                                high = v;
                                ++out.harshDips;
                            }
                        }
                    }
                    out.harshActiveSeconds += (double) harsh.size() * hopSeconds;
                    const auto depth = percentile (harsh, 95.0) - percentile (harsh, 5.0);
                    out.harshDepthDb.push_back ((float) depth);
                }
            }

            auto anyAfterEss = false, countedAfterEss = false;
            for (auto i = first; i < last; ++i)
            {
                const auto centre = ((double) i + 0.5) * hopSeconds;
                if (centre < seg.start || centre >= seg.end)
                    continue;

                const auto& h = r.hops[(size_t) i];
                const auto reduction = h.reductions[0].essDb;
                const auto hit = reduction > reducedDb;

                if (h.paths[0].harshDb > dsp::silentDb)
                {
                    out.harshVowelSum += h.reductions[0].harshDb;
                    out.harshVowel.add (h.reductions[0].harshDb > reducedDb);
                    out.harshRelativeDb.push_back (h.paths[0].harshRelativeDb);
                }

                if (distanceToSibilant (centre) <= vowelGuardSeconds)
                    continue;

                if (bright)
                {
                    out.bright.add (hit);
                    if (reduction > out.brightMaxDb)
                    {
                        out.brightMaxDb = reduction;
                        out.brightMaxTime = centre;
                    }
                }

                out.vowel.add (hit);
                const auto triggered = h.decisions[0].essTargetDb > reducedDb;
                out.vowelTriggered.add (triggered);
                if (std::isfinite (thresholdDb))
                    out.vowelOverThreshold.add (h.decisions[0].sibilanceDb > thresholdDb);
                out.vowelTail.add (hit && ! triggered);
                out.vowelCarried.add (reduction > carriedDb);
                if (triggered)
                {
                    static const std::array<const char*, 4> afterNames { "after silence", "after a vowel", "after a sibilant", "after a consonant" };
                    ++out.triggers[h.paths[0].noise >= 0.5f ? "noise-like" : "harmonic"];
                    ++out.triggers[corpus::isFrontVowel (seg.phone) ? "front vowel" : "other vowel"];
                    ++out.triggers[centre - seg.start < 0.05 ? "first 50 ms" : "later"];
                    ++out.triggers[afterNames[(size_t) before]];
                    if (k > 0 && before == PhoneClass::consonant)
                        ++out.triggers["after " + rec.labels[k - 1].phone];
                }
                out.vowelSibilanceDb.push_back (h.paths[0].sibilanceDb);
                if (before == PhoneClass::sibilant)
                {
                    out.vowelAfterEss.add (hit);
                    countedAfterEss = true;
                    anyAfterEss = anyAfterEss || hit;
                }
                else if (before == PhoneClass::silence)
                {
                    out.vowelAfterSilence.add (hit);
                }
                if (hit)
                    out.vowelHits.push_back ({ centre, reduction, before == PhoneClass::sibilant });
            }
            if (countedAfterEss)
                out.afterEssVowels.add (anyAfterEss);
        }
    }

    for (const auto& ev : r.events)
    {
        ++out.events;
        const auto from = (double) ev.start / r.sampleRate, to = (double) ev.end / r.sampleRate;
        auto onSibilant = false;
        auto most = 0.0;
        std::string phone = "sil";
        for (const auto& seg : rec.labels)
        {
            const auto overlap = std::min (to, seg.end) - std::max (from, seg.start);
            if (overlap <= 0.0)
                continue;
            onSibilant = onSibilant || corpus::classOf (seg.phone) == PhoneClass::sibilant;
            if (overlap > most)
            {
                most = overlap;
                phone = seg.phone;
            }
        }
        if (onSibilant)
            ++out.eventsOnSibilants;
        else
            ++out.eventsElsewhere[phone];
    }

    return out;
}

void Summary::add (const RecordingResult& r)
{
    ++recordings;
    seconds += r.seconds;
    for (const auto& t : r.tokens)
    {
        detected[(size_t) t.sibilant].add (t.detected);
        auto& c = coverage[(size_t) t.sibilant];
        c.decided.total += t.hops;
        c.decided.hits += t.decidedHops;
        c.reduced.total += t.hops;
        c.reduced.hits += t.reducedHops;
        c.reductionSumDb += t.reductionSumDb;
        c.withDecision.add (t.decidedHops > 0);
        if (t.decidedHops > 0)
        {
            c.late.add (t.firstDecisionMs > lateMs);
            c.firstDecisionMs.push_back (t.firstDecisionMs);
            c.firstReducedMs.push_back (t.firstReducedMs);
            c.reducedFromStart.add (t.reducedAtStart);
            c.tokenReducedShare.push_back (t.hops > 0 ? 100.0f * (float) t.reducedHops / (float) t.hops : 0.0f);
        }
        if (t.detected)
            c.onsetHeld.add (t.onsetHeld);
        if (t.afterVowel)
            detectedAfterVowel[(size_t) t.sibilant].add (t.detected);
        harshTokens.add (t.harshMeanDb > reducedDb);
        harshTokensPeak.add (t.harshPeakDb > reducedDb);
        harshTokensAdded.add (t.harshAddedDb > reducedDb);
    }
    vowel.add (r.vowel);
    vowelTriggered.add (r.vowelTriggered);
    vowelOverThreshold.add (r.vowelOverThreshold);
    vowelTail.add (r.vowelTail);
    vowelCarried.add (r.vowelCarried);
    events += r.events;
    eventsOnSibilants += r.eventsOnSibilants;
    for (const auto& [phone, n] : r.eventsElsewhere)
        eventsElsewhere[phone] += n;
    for (const auto& [kind, n] : r.triggers)
        triggers[kind] += n;
    vowelAfterEss.add (r.vowelAfterEss);
    vowelAfterSilence.add (r.vowelAfterSilence);
    afterEssVowels.add (r.afterEssVowels);
    if (r.sung)
    {
        bright.add (r.bright);
        brightMaxDb = std::max (brightMaxDb, r.brightMaxDb);
        ++brightRecordings;
        brightRecordingsReduced += r.brightMaxDb > reducedDb ? 1 : 0;
        for (const auto db : r.vowelSibilanceDb)
            brightLargestSibilanceDb = std::max (brightLargestSibilanceDb, db);
    }
    harshSibilantSum += r.harshSibilantSum;
    harshSibilantHeldSum += r.harshSibilantHeldSum;
    harshSibilantAddedSum += r.harshSibilantAddedSum;
    harshSibilantHops += r.harshSibilantHops;
    harshVowelSum += r.harshVowelSum;
    harshVowel.add (r.harshVowel);
    harshDepthDb.insert (harshDepthDb.end(), r.harshDepthDb.begin(), r.harshDepthDb.end());
    harshDips += r.harshDips;
    harshActiveSeconds += r.harshActiveSeconds;
}

Counts Summary::sz() const noexcept
{
    auto c = detected[0];
    c.add (detected[1]);
    return c;
}

void LevelResult::add (const LevelResult& o)
{
    if (perGain.size() < o.perGain.size())
        perGain.resize (o.perGain.size());
    for (size_t g = 0; g < o.perGain.size(); ++g)
    {
        auto& p = perGain[g];
        const auto& q = o.perGain[g];
        p.gainDb = q.gainDb;
        p.events += q.events;
        p.inRange += q.inRange;
        p.compared += q.compared;
        p.listChanges += q.listChanges;
        p.worstDifferenceDb = std::max (p.worstDifferenceDb, q.worstDifferenceDb);
    }
    events += o.events;
    compared += o.compared;
    listChanges += o.listChanges;
    excluded += o.excluded;
    allCompared += o.allCompared;
    allListChanges += o.allListChanges;
    allWorstDifferenceDb = std::max (allWorstDifferenceDb, o.allWorstDifferenceDb);
    if (o.worstDifferenceDb > worstDifferenceDb)
    {
        worstDifferenceDb = o.worstDifferenceDb;
        worstId = o.worstId;
        worstTime = o.worstTime;
        worstGainDb = o.worstGainDb;
    }
    changes.insert (changes.end(), o.changes.begin(), o.changes.end());
}

LevelResult levelIndependence (const corpus::Recording& rec, const dsp::DetectorSettings& settings, const dsp::DetectorTuning& tuning,
                               const std::vector<float>& gainsDb, float bodyDb)
{
    LevelResult out;
    if (gainsDb.size() < 2)
        return out;

    auto offsetDb = 0.0f;
    if (std::isfinite (bodyDb))
    {
        const auto first = run (rec, settings, tuning, gainsDb[0]);
        const auto hopSeconds = first.hopSize / first.sampleRate;
        std::vector<float> vowelBody;
        for (const auto& seg : rec.labels)
            if (corpus::classOf (seg.phone) == PhoneClass::vowel)
                for (auto i = (size_t) std::ceil (seg.start / hopSeconds - 0.5); i < first.hops.size() && ((double) i + 0.5) * hopSeconds < seg.end; ++i)
                    vowelBody.push_back (first.hops[i].paths[0].bodyDb);
        if (! vowelBody.empty())
            offsetDb = bodyDb - (float) percentile (vowelBody, 50.0);
    }

    std::vector<dsp::probe::Result> runs;
    for (const auto g : gainsDb)
        runs.push_back (run (rec, settings, tuning, g + offsetDb));

    // In the working range: in a render the body V that S is measured against is at least the
    // margin above the floor on every hop of the event and of the release before it, where an
    // earlier event's tail can still lift the reduction.
    const auto quiet = (size_t) std::distance (gainsDb.begin(), std::min_element (gainsDb.begin(), gainsDb.end()));
    const auto hopSize = (std::int64_t) runs[0].hopSize;
    const auto lookback = (std::int64_t) std::lround (floorLookbackSeconds * runs[0].sampleRate);
    const auto inRange = [&] (const dsp::EssEvent& e, size_t render)
    {
        const auto& hops = runs[render].hops;
        const auto from = (size_t) std::clamp<std::int64_t> ((e.start - lookback) / hopSize, 0, (std::int64_t) hops.size());
        const auto to = (size_t) std::clamp<std::int64_t> ((e.end + hopSize - 1) / hopSize, (std::int64_t) from, (std::int64_t) hops.size());
        for (auto i = from; i < to; ++i)
            if (hops[i].paths[0].bodyDb < (float) tuning.bodyFloorDb + levelMarginDb)
                return false;
        return true;
    };
    const auto overlaps = [] (const dsp::EssEvent& a, const dsp::EssEvent& b) { return a.start < b.end && b.start < a.end; };
    const auto seconds = [&runs] (std::int64_t sample) { return (double) sample / runs[0].sampleRate; };

    const auto& reference = runs[0].events;
    for (const auto& e : reference)
        (inRange (e, quiet) ? out.events : out.excluded) += 1;

    // In the working range at the quietest gain (the gate), in each render's own range (reported
    // per gain), then every event: the absolute floor breaks invariance on purpose.
    enum class Range { quietest, own, every };
    const auto compare = [&] (Range range, int& compared, int& listChanges, float& worst, bool record)
    {
        for (size_t g = 1; g < runs.size(); ++g)
        {
            const auto counts = [&] (const dsp::EssEvent& e)
            {
                return range == Range::every || inRange (e, range == Range::quietest ? quiet : g);
            };
            auto& perGain = out.perGain[g - 1];
            const auto before = std::make_pair (compared, listChanges);
            const auto worstBefore = worst;
            if (range == Range::own)
                worst = 0.0f;

            const auto& other = runs[g].events;
            for (const auto& e : reference)
            {
                if (range == Range::own)
                {
                    ++perGain.events;
                    perGain.inRange += counts (e) ? 1 : 0;
                }
                if (! counts (e))
                    continue;
                auto matched = false;
                for (const auto& o : other)
                {
                    if (! overlaps (e, o))
                        continue;
                    matched = true;
                    ++compared;
                    const auto diff = std::abs (o.peakReductionDb - e.peakReductionDb);
                    if (diff > worst)
                    {
                        worst = diff;
                        if (record)
                        {
                            std::tie (out.worstId, out.worstTime) = corpus::locate (rec, seconds (e.start));
                            out.worstGainDb = gainsDb[g];
                        }
                    }
                }
                if (! matched)
                {
                    ++listChanges;
                    if (record)
                        out.changes.emplace_back (seconds (e.start), gainsDb[g]);
                }
            }
            for (const auto& o : other)
                if (counts (o) && std::none_of (reference.begin(), reference.end(), [&] (const dsp::EssEvent& e) { return overlaps (e, o); }))
                {
                    ++listChanges;
                    if (record)
                        out.changes.emplace_back (seconds (o.start), gainsDb[g]);
                }

            if (range == Range::own)
            {
                perGain.compared = compared - before.first;
                perGain.listChanges = listChanges - before.second;
                perGain.worstDifferenceDb = worst;
                worst = std::max (worst, worstBefore);
            }
        }
    };

    out.perGain.resize (gainsDb.size() - 1);
    for (size_t g = 1; g < gainsDb.size(); ++g)
        out.perGain[g - 1].gainDb = gainsDb[g];

    compare (Range::quietest, out.compared, out.listChanges, out.worstDifferenceDb, true);
    auto ownCompared = 0, ownChanges = 0;
    auto ownWorst = 0.0f;
    compare (Range::own, ownCompared, ownChanges, ownWorst, false);
    compare (Range::every, out.allCompared, out.allListChanges, out.allWorstDifferenceDb, false);
    return out;
}

std::vector<corpus::Recording> syntheticBrightVowels (double sampleRate, bool holdout)
{
    constexpr double silence = 0.3, length = 2.0, fade = 0.02;
    std::vector<corpus::Recording> out;
    const auto n = testing::samplesFor (length, sampleRate);
    const auto start = testing::samplesFor (silence, sampleRate), fadeLength = testing::samplesFor (fade, sampleRate);
    unsigned seed = holdout ? 1001 : 1;

    const auto pitches = holdout ? std::vector<double> { 98.0, 147.0, 196.0, 294.0, 392.0, 587.0, 784.0 }
                                 : std::vector<double> { 110.0, 165.0, 220.0, 330.0, 440.0, 660.0, 880.0 };
    const auto vibratoDepth = holdout ? 35.0 : 50.0, vibratoRate = holdout ? 6.2 : 5.5;

    for (const auto tilt : { 0.0, -3.0, -6.0, -12.0 })
        for (const auto vibrato : { 0.0, vibratoDepth })
            for (const auto f0 : pitches)
            {
                auto x = testing::harmonic ({ .f0 = f0, .topHz = 12000.0, .tiltDbPerOctave = tilt, .vibratoCents = vibrato,
                                              .vibratoHz = vibratoRate, .rms = 0.1, .seed = seed++ },
                                            n, sampleRate);
                for (int i = 0; i < n; ++i)
                {
                    const auto d = std::min (i - start, n - 1 - i);
                    const auto g = d < 0 ? 0.0 : (d >= fadeLength ? 1.0 : 0.5 - 0.5 * std::cos (std::numbers::pi * d / fadeLength));
                    x[(size_t) i] = (float) (x[(size_t) i] * g);
                }

                corpus::Recording r;
                r.entry.id = juce::String::formatted ("f0 %.0f Hz, tilt %+.0f dB/oct, vibrato %.0f c", f0, tilt, vibrato).toStdString();
                r.entry.corpus = "synthetic";
                r.entry.speaker = juce::String::formatted ("tilt %+.0f dB/oct", tilt).toStdString();
                r.entry.split = holdout ? "holdout" : "tune";
                r.entry.role = "bright";
                r.entry.sung = true;
                r.sampleRate = sampleRate;
                r.audio = std::move (x);
                r.labels = { { 0.0, silence, "sil" }, { silence, length, "IY" } };
                out.push_back (std::move (r));
            }
    return out;
}

float thresholdForRecall (const std::vector<corpus::Recording>& recordings, const dsp::DetectorSettings& settings, const dsp::DetectorTuning& tuning,
                          int sibilant, double recallPercent, const ForEach& forEach)
{
    // The Harsh band does not change the Ess detector; the range is the Threshold parameter's.
    constexpr float lowest = -36.0f, highest = 24.0f, resolution = 0.02f;
    auto s = settings;
    s.detection = Detection::voice;
    s.harshOn = false;

    const auto recall = [&] (float thresholdDb)
    {
        s.thresholdDb = thresholdDb;
        std::vector<Counts> counts (recordings.size());
        const auto one = [&] (size_t i)
        {
            const auto& rec = recordings[i];
            if (rec.entry.sung)
                return;
            const auto r = run (rec, s, tuning);
            for (const auto& seg : rec.labels)
                if (corpus::sibilantIndex (seg.phone) == sibilant)
                    counts[i].add (overlapsEvent (r, seg.start, seg.end));
        };
        if (forEach)
            forEach (recordings.size(), one);
        else
            for (size_t i = 0; i < recordings.size(); ++i)
                one (i);
        Counts all;
        for (const auto& c : counts)
            all.add (c);
        return all.percent();
    };

    auto lo = lowest, hi = highest;
    if (recall (lo) < recallPercent)
        return lo;
    if (recall (hi) >= recallPercent)
        return hi;
    while (hi - lo > resolution)
    {
        const auto mid = 0.5f * (lo + hi);
        (recall (mid) >= recallPercent ? lo : hi) = mid;
    }
    return std::floor (lo * 100.0f) / 100.0f;
}

BandLevelCheck bandLevel (double sampleRate, const dsp::DetectorTuning& tuning)
{
    BandLevelCheck out;
    dsp::DetectorSettings s;
    s.thresholdDb = 100.0f;

    const auto n = testing::samplesFor (4.0, sampleRate);
    const auto noise = testing::bandNoise (4500.0, 12000.0, n, sampleRate, 7, 0.1);
    const auto* in = noise.data();
    const auto r = dsp::probe::run (&in, 1, n, sampleRate, s, nullptr, 0, { 512 }, tuning);

    auto sum = 0.0, sumSquares = 0.0;
    auto count = 0;
    for (auto i = testing::hopAt (0.5, sampleRate); i < (int) r.hops.size(); ++i)
    {
        const auto db = (double) r.hops[(size_t) i].paths[0].bandDb;
        sum += db;
        sumSquares += db * db;
        ++count;
    }
    const auto mean = sum / count;
    out.rippleDb = std::sqrt (std::max (0.0, sumSquares / count - mean * mean));
    out.offsetDb = mean + 20.0;

    // A tone at the band's centre, switched on at 0.5 s.
    std::vector<float> tone ((size_t) n, 0.0f);
    const auto onset = testing::samplesFor (0.5, sampleRate);
    const auto hz = std::sqrt (4500.0 * 12000.0);
    for (int i = onset; i < n; ++i)
        tone[(size_t) i] = (float) (0.1 * std::sin (2.0 * std::numbers::pi * hz * (i - onset) / sampleRate));
    const auto* t = tone.data();
    const auto rt = dsp::probe::run (&t, 1, n, sampleRate, s, nullptr, 0, { 512 }, tuning);
    const auto final = rt.hops.back().paths[0].bandDb;
    const auto hopMs = 1000.0 * dsp::Detector::hopSize / sampleRate;
    const auto onsetHop = testing::hopAt (onset);
    out.riseTo3DbMs = out.riseTo1DbMs = -1.0;
    for (auto i = onsetHop; i < (int) rt.hops.size(); ++i)
    {
        const auto below = final - rt.hops[(size_t) i].paths[0].bandDb;
        const auto ms = (i + 1 - onsetHop) * hopMs;
        if (out.riseTo3DbMs < 0.0 && below <= 3.0f)
            out.riseTo3DbMs = ms;
        if (below <= 1.0f)
        {
            out.riseTo1DbMs = ms;
            break;
        }
    }
    return out;
}

double percentile (std::vector<float>& values, double p)
{
    if (values.empty())
        return std::numeric_limits<double>::quiet_NaN();
    std::sort (values.begin(), values.end());
    const auto x = std::clamp (p, 0.0, 100.0) / 100.0 * (double) (values.size() - 1);
    const auto i = (size_t) std::floor (x);
    const auto j = std::min (i + 1, values.size() - 1);
    return values[i] + (x - (double) i) * (values[j] - values[i]);
}

} // namespace halite::gates
