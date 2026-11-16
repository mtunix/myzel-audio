#include "dsp/detector/Detector.h"

#include <cmath>
#include <limits>
#include <numbers>

namespace halite::dsp
{

namespace
{
    int hopsFor (double ms, double sampleRate) noexcept
    {
        return std::max (1, (int) std::ceil (ms * 0.001 * sampleRate / Detector::hopSize));
    }

    bool usesPeriodicity (NoiseCueKind k) noexcept { return k != NoiseCueKind::flatness; }
    bool usesFlatness (NoiseCueKind k) noexcept { return k != NoiseCueKind::periodicity; }

    constexpr auto notComputed = std::numeric_limits<float>::quiet_NaN();

    // One NaN or Inf would leave every filter and envelope NaN until the next reset, and a burst at
    // float max would lift V hundreds of dB for the gap hold to keep: the key is made sane first.
    double sane (float x) noexcept
    {
        return std::isfinite (x) ? std::clamp ((double) x, -Detector::keyLimit, Detector::keyLimit) : 0.0;
    }
}

//==============================================================================
void Detector::Path::prepare (double sampleRate, const DetectorTuning& t)
{
    bandLevel.setTimes (t.bandAveragingMs, attackMs, t.bandReleaseMs, sampleRate);
    bodyLevel.setTimes (bodyAveragingMs, attackMs, t.bodyReleaseMs, sampleRate);
    harshLevel.setTimes (harshAveragingMs, attackMs, harshReleaseMs, sampleRate);
    harshBodyLevel.setTimes (bodyAveragingMs, attackMs, t.bodyReleaseMs, sampleRate);
    slowBandLevel.setTimes (bodyAveragingMs, attackMs, t.bandReleaseMs, sampleRate);
    slowHarshLevel.setTimes (bodyAveragingMs, attackMs, harshReleaseMs, sampleRate);
    presenceLevel.setTimes (t.bandAveragingMs, 0.0, 0.0, sampleRate);
    periodicity.prepare (sampleRate, t.periodicLongestLagMs);
    harshPeriodicity.prepare (sampleRate);
    flatness.prepare (sampleRate);
    harshFlatness.prepare (sampleRate);
    mapping = { t.periodicNoise, t.periodicHarmonic, t.flatNoiseDb, t.flatHarmonicDb };
    riseDb = t.riseDb;
    jumpDb = t.jumpDb;
    floorPower = std::pow (10.0, t.bodyFloorDb / 10.0);
    presenceOn = t.presenceSlope > 0.0f;
    persistOn = t.persistMs > 0.0;
    heldRho.prepare (persistOn ? hopsFor (t.persistMs, sampleRate) : 1, 0.0f);
    lowestBand.prepare (persistOn ? hopsFor (t.persistMs, sampleRate) : 1, silentDb);
    persistRho = t.persistRho;
    persistJumpDb = t.persistJumpDb;
    gapRatio = std::pow (10.0, -t.bodyGapDb / 10.0);
    gapHoldHops = std::max (1, (int) std::ceil (t.bodyGapHoldMs * 0.001 * sampleRate / hopSize));
    reset();
}

void Detector::Path::reset() noexcept
{
    keyHpf.reset();
    band.reset();
    body.reset();
    bandLevel.reset();
    bodyLevel.reset();
    bodyLevel.setHeld (false);
    bodyGapHops = 0;
    slowBandLevel.reset();
    bodyRise.reset();
    bodyJump.reset();
    presenceLevel.reset();
    heldRho.reset();
    lowestBand.reset();
    resetHarsh();
    periodicity.reset();
    flatness.reset();
}

void Detector::Path::resetHarsh() noexcept
{
    harshBand.reset();
    harshBody.reset();
    harshLevel.reset();
    harshBodyLevel.reset();
    harshBodyLevel.setHeld (false);
    harshBodyGapHops = 0;
    slowHarshLevel.reset();
    harshBodyRise.reset();
    harshPeriodicity.reset();
    harshFlatness.reset();
}

void Detector::Path::resetCues() noexcept
{
    heldRho.reset();
    periodicity.reset();
    flatness.reset();
    harshPeriodicity.reset();
    harshFlatness.reset();
}

void Detector::Path::setBands (double essLow, double essHigh, double harshLow, double harshHigh, double keyHpfHz, double sampleRate,
                               const DetectorTuning& t) noexcept
{
    band.setBand (essLow, essHigh, sampleRate);
    body.setBand (bodyLowHz, std::min (t.bodyCapHz, essLow * t.bodyCapShare), sampleRate);
    harshBand.setBand (harshLow, harshHigh, sampleRate);
    harshBody.setBand (harshBodyLowHz, harshLow / std::numbers::sqrt2, sampleRate);
    flatness.setBand (essLow, essHigh);
    harshFlatness.setBand (harshLow, harshHigh);

    const auto hpfOn = keyHpfHz > 0.0;
    if (hpfOn && ! keyHpfOn)
        keyHpf.reset();
    keyHpfOn = hpfOn;
    if (hpfOn)
        keyHpf.setCoefficients (keyHpfHz, std::numbers::sqrt2 / 2.0, sampleRate);
}

void Detector::Path::setRiseSpan (int hops) noexcept
{
    bodyRise.setSpan (hops);
    bodyJump.setSpan (hops);
    harshBodyRise.setSpan (hops);
}

void Detector::Path::anchor (std::int64_t timelinePosition) noexcept
{
    periodicity.anchor (timelinePosition);
    harshPeriodicity.anchor (timelinePosition);
}

void Detector::Path::process (const double* key, int n, NoiseCueKind cue, bool harsh, float* essTap, float* harshTap) noexcept
{
    const auto bandScale = band.powerScale(), bodyScale = body.powerScale();
    const auto harshScale = harshBand.powerScale(), harshBodyScale = harshBody.powerScale();
    const auto periodic = usesPeriodicity (cue), flat = usesFlatness (cue);

    for (int i = 0; i < n; ++i)
    {
        const auto x = keyHpfOn ? keyHpf.processHighpass (key[i]) : key[i];

        const auto b = band.process (x);
        bandLevel.process (b * b * bandScale);
        slowBandLevel.process (b * b * bandScale);
        auto pr = 0.0;
        const auto v = body.process (x, pr);
        bodyLevel.process (v * v * bodyScale);
        presenceLevel.process (pr * pr);

        if (periodic)
            periodicity.push (b);
        if (flat)
            flatness.push ((float) b);
        if (essTap != nullptr)
            essTap[i] = (float) b;

        if (harsh)
        {
            const auto h = harshBand.process (x);
            harshLevel.process (h * h * harshScale);
            slowHarshLevel.process (h * h * harshScale);
            const auto hb = harshBody.process (x);
            harshBodyLevel.process (hb * hb * harshBodyScale);

            if (periodic)
                harshPeriodicity.push (h);
            if (flat)
                harshFlatness.push ((float) h);
            if (harshTap != nullptr)
                harshTap[i] = (float) h;
        }
        else if (harshTap != nullptr)
        {
            harshTap[i] = 0.0f;
        }
    }
}

double Detector::Path::persistent (double rho, float bandDb) noexcept
{
    if (! persistOn)
        return rho;

    const auto held = heldRho.get(), lowest = std::min (bandDb, lowestBand.get());
    heldRho.push ((float) rho);
    lowestBand.push (bandDb);

    // The cue dips for a few tens of ms inside sustained and gliding notes; an ess raises B.
    return bandDb - lowest < persistJumpDb && held > persistRho ? std::max (rho, (double) held) : rho;
}

void Detector::Path::read (DetectorFrame::Path& out, NoiseCueKind cue, bool harsh, bool analyseFlatness) noexcept
{
    const auto noiseOf = [this, cue] (PeriodicityCue& p, FlatnessCue& f, bool analyse, float& rawP, float& rawF, const float* persistOnBand)
    {
        auto n = 0.0;
        rawP = rawF = notComputed;
        if (usesPeriodicity (cue))
        {
            const auto rho = p.value();
            rawP = (float) rho;
            n += mapping.fromPeriodicity (persistOnBand != nullptr ? persistent (rho, *persistOnBand) : rho);
        }
        if (usesFlatness (cue))
        {
            if (analyse)
                f.analyse();
            rawF = (float) f.value();
            n += mapping.fromFlatness (f.value());
        }
        return (float) (cue == NoiseCueKind::blend ? 0.5 * n : n);
    };

    // The ratio never divides by less than the floor: before any voice has been heard V is 0, and
    // a first breath would otherwise read as a huge ess. While the body rises (a voice starting or
    // getting louder) B is the smaller of itself and B with the body's averaging: B's own, faster
    // one would run ahead of V and read the onset as sibilance, by up to 9 dB for a few ms. The
    // Harsh band does the same against its own body.
    const auto bandPower = bandLevel.get(), bodyPower = bodyLevel.get();
    out.bandDb = powerToDb (bandPower);
    out.bodyDb = powerToDb (bodyPower);
    out.bodyRising = bodyRise.push (out.bodyDb, riseDb);

    // V is held through closures and pauses, so its rise misses a voice coming back: the averaged body does not.
    out.bodyJumped = jumpDb > 0.0f && bodyJump.push (powerToDb (bodyLevel.present()), jumpDb);
    const auto comparedBand = out.bodyRising ? std::min (bandPower, slowBandLevel.get()) : bandPower;
    out.relativeDb = (float) (10.0 * std::log10 (std::max (comparedBand, 1.0e-30) / std::max (bodyPower, floorPower)));
    out.bodyHeld = bodyLevel.isHeld();
    out.bodyFloored = bodyPower < floorPower;
    out.noise = noiseOf (periodicity, flatness, analyseFlatness, out.periodicity, out.flatnessDb, &out.bandDb);
    // The shape cue: an ess rises towards the Detect band; vowels and a stop's aspiration fall.
    out.presenceDb = presenceOn ? (float) (10.0 * std::log10 (std::max (bandLevel.present(), 1.0e-30) / std::max (presenceLevel.present(), 1.0e-30)))
                                : 0.0f;

    if (harsh)
    {
        const auto harshPower = harshLevel.get(), harshBodyPower = harshBodyLevel.get();
        out.harshDb = powerToDb (harshPower);
        out.harshBodyDb = powerToDb (harshBodyPower);
        const auto comparedHarsh = harshBodyRise.push (out.harshBodyDb, riseDb) ? std::min (harshPower, slowHarshLevel.get()) : harshPower;
        out.harshRelativeDb = (float) (10.0 * std::log10 (std::max (comparedHarsh, 1.0e-30) / std::max (harshBodyPower, floorPower)));
        out.harshNoise = noiseOf (harshPeriodicity, harshFlatness, analyseFlatness, out.harshPeriodicity, out.harshFlatnessDb, nullptr);
    }
    else
    {
        out.harshDb = out.harshBodyDb = silentDb;
        out.harshRelativeDb = 0.0f;
        out.harshNoise = 1.0f;
        out.harshPeriodicity = out.harshFlatnessDb = notComputed;
    }
}

void Detector::Path::holdBody (bool candidateHold) noexcept
{
    // V holds while the body is far below it (a pause, a closure), relative to V so that it does
    // not depend on the input gain, and for a limited time, so V follows a voice that gets much
    // quieter. The Harsh body holds with V, or Harsh would read the end of every ess as harshness.
    const auto gapHold = [this] (PowerEnvelope& level, int& hops)
    {
        const auto far = level.present() < level.get() * gapRatio;
        hops = far ? hops + 1 : 0;
        return far && hops <= gapHoldHops;
    };
    bodyLevel.setHeld (gapHold (bodyLevel, bodyGapHops) || candidateHold);
    harshBodyLevel.setHeld (gapHold (harshBodyLevel, harshBodyGapHops) || candidateHold);
}

//==============================================================================
void Detector::prepare (double newSampleRate, const DetectorTuning& newTuning)
{
    sampleRate = newSampleRate;
    tuning = newTuning;

    for (auto& p : paths)
        p.prepare (sampleRate, tuning);

    maxLookaheadHops = lookaheadHopsLimit (sampleRate);
    for (int r = 0; r < 2; ++r)
    {
        essShapers[(size_t) r].prepare (maxLookaheadHops);
        harshShapers[(size_t) r].prepare (maxLookaheadHops);
        sibilanceDelays[(size_t) r].prepare (maxLookaheadHops, silentDb);
    }

    hangoverHops = std::max (1, (int) std::lround (eventHangoverMs * 0.001 * sampleRate / hopSize));
    bodyHoldHops = hopsFor (tuning.bodyHoldMs, sampleRate);
    onsetHoldHops = tuning.onsetHoldMs > 0.0 ? hopsFor (tuning.onsetHoldMs, sampleRate) : 0;
    harshWarmUpHops = hopsFor (harshWarmUpMs, sampleRate);

    flatnessEveryHops = std::max (1, paths[0].flatnessHop() / hopSize);
    for (auto& p : paths)
        p.setRiseSpan (hopsFor (riseMs, sampleRate));

    bands = {};
    reset();
}

void Detector::reset() noexcept
{
    for (auto& p : paths)
        p.reset();
    for (int r = 0; r < 2; ++r)
    {
        essShapers[(size_t) r].reset();
        harshShapers[(size_t) r].reset();
        sibilanceDelays[(size_t) r].reset();
    }
    events.reset();
    hopPhase = 0;
    position = 0;
    harshWasOn = false;
    harshWarmUp = 0;
    keyRoute = -1;
    candidateHops = {};
    onsetHops = {};
    jumpHops = {};
    essHoldHops = {};
    harshHeldTarget = {};
    frame = {};
}

void Detector::anchor (std::int64_t timelinePosition) noexcept
{
    hopPhase = (int) (((timelinePosition % hopSize) + hopSize) % hopSize);
    position = timelinePosition - hopPhase;
    for (auto& p : paths)
        p.anchor (timelinePosition);
}

Detector::Bands Detector::bandsFor (const DetectorSettings& s, double rate) noexcept
{
    // Detect Low above Detect High swaps them; a band narrower than a third of an octave widens
    // around its centre; nothing reaches past 0.45 fs.
    const auto top = 0.45 * rate;
    auto lo = (double) std::min (s.detectLowHz, s.detectHighHz);
    auto hi = std::min ((double) std::max (s.detectLowHz, s.detectHighHz), top);
    lo = std::clamp (lo, 20.0, top);

    if (hi < lo * std::exp2 (minBandOctaves))
    {
        const auto centre = std::sqrt (lo * std::max (hi, lo));
        lo = centre * std::exp2 (-minBandOctaves / 2.0);
        hi = std::min (centre * std::exp2 (minBandOctaves / 2.0), top);
        lo = std::min (lo, hi * std::exp2 (-minBandOctaves));
    }

    const auto halfWidth = std::exp2 (0.5 * std::max ((double) s.harshWidthOct, 0.1));
    const auto harshHigh = std::min ((double) s.harshFreqHz * halfWidth, top);
    const auto harshLow = std::min ((double) s.harshFreqHz / halfWidth, harshHigh * std::exp2 (-minBandOctaves));

    return { lo, hi, harshLow, harshHigh, s.keyHpfHz > 0.0f ? (double) s.keyHpfHz : 0.0 };
}

void Detector::process (const float* const* main, int numMain, const float* const* key, int numKey,
                        int numSamples, const DetectorSettings& s, DetectorObserver* observer, const BandTaps* taps) noexcept
{
    if (numSamples <= 0 || main == nullptr || numMain <= 0)
        return;

    const auto external = s.key == KeySource::external && key != nullptr && numKey > 0;
    const auto* const* source = external ? key : main;
    const auto numSource = std::min (external ? numKey : numMain, 2);

    enum class Route { first, both, mid, side };
    auto route = Route::first;
    auto newPaths = 1, newReductions = 1;
    auto newLinked = false;

    if (numSource == 2)
    {
        const auto mode = numMain == 1 ? StereoMode::linked : s.stereo;
        route = mode == StereoMode::mid ? Route::mid : (mode == StereoMode::side ? Route::side : Route::both);
        newPaths = route == Route::both ? 2 : 1;
        newLinked = mode == StereoMode::linked;
        newReductions = mode == StereoMode::unlinked ? 2 : 1;
    }

    // A path that is fed another signal (the other key, another stereo routing) starts clean, with
    // the Harsh warm-up, so that the holds do not keep the old signal's V; a reduction that comes
    // back starts clean too.
    if (const auto routeNow = 2 * (int) route + (external ? 1 : 0); routeNow != keyRoute)
    {
        keyRoute = routeNow;
        for (int p = 0; p < 2; ++p)
        {
            paths[(size_t) p].reset();
            paths[(size_t) p].anchor (position + hopPhase);
            candidateHops[(size_t) p] = onsetHops[(size_t) p] = jumpHops[(size_t) p] = 0;
        }
        harshWarmUp = harshWarmUpHops;
    }
    for (int r = numReductions; r < newReductions; ++r)
    {
        essShapers[(size_t) r].reset();
        harshShapers[(size_t) r].reset();
        sibilanceDelays[(size_t) r].reset();
        essHoldHops[(size_t) r] = 0;
        harshHeldTarget[(size_t) r] = 0.0f;
    }
    numPaths = newPaths;
    numReductions = newReductions;
    linked = newLinked;

    if (s.noiseCue != cue)
    {
        cue = s.noiseCue;
        for (auto& p : paths)
        {
            p.resetCues();
            p.anchor (position + hopPhase);
        }
    }

    if (s.harshOn && ! harshWasOn)
    {
        for (auto& p : paths)
        {
            p.resetHarsh();
            p.anchor (position + hopPhase);
        }
        harshWarmUp = harshWarmUpHops;
    }
    harshWasOn = s.harshOn;

    if (const auto wanted = bandsFor (s, sampleRate); ! (wanted == bands))
    {
        bands = wanted;
        for (auto& p : paths)
            p.setBands (bands.essLow, bands.essHigh, bands.harshLow, bands.harshHigh, bands.keyHpf, sampleRate, tuning);
    }

    lookaheadHops = s.latency == LatencyMode::live
                        ? 0
                        : std::clamp ((int) std::lround (s.lookaheadMs * 0.001 * sampleRate / hopSize), 0, maxLookaheadHops);

    for (int done = 0; done < numSamples;)
    {
        const auto n = std::min (numSamples - done, hopSize - hopPhase);
        auto& k0 = keyBuffer[0];
        auto& k1 = keyBuffer[1];
        const auto* a = source[0] + done;
        const auto* b = numSource == 2 ? source[1] + done : a;

        switch (route)
        {
            case Route::first: for (int i = 0; i < n; ++i) k0[(size_t) i] = sane (a[i]); break;
            case Route::both:  for (int i = 0; i < n; ++i) { k0[(size_t) i] = sane (a[i]); k1[(size_t) i] = sane (b[i]); } break;
            case Route::mid:   for (int i = 0; i < n; ++i) k0[(size_t) i] = 0.5 * (sane (a[i]) + sane (b[i])); break;
            case Route::side:  for (int i = 0; i < n; ++i) k0[(size_t) i] = 0.5 * (sane (a[i]) - sane (b[i])); break;
        }

        for (size_t p = 0; p < (size_t) numPaths; ++p)
        {
            auto* essTap = taps != nullptr && taps->ess[p] != nullptr ? taps->ess[p] + done : nullptr;
            auto* harshTap = taps != nullptr && taps->harsh[p] != nullptr ? taps->harsh[p] + done : nullptr;
            paths[p].process (keyBuffer[p].data(), n, cue, s.harshOn, essTap, harshTap);
        }

        hopPhase += n;
        done += n;

        if (hopPhase == hopSize)
        {
            hopPhase = 0;
            position += hopSize;
            endHop (s, observer);
        }
    }
}

void Detector::endHop (const DetectorSettings& s, DetectorObserver* observer) noexcept
{
    const auto voice = s.detection == Detection::voice;
    const auto focus = std::clamp (s.focus, 0.0f, 1.0f);
    const auto analyseFlatness = (position / hopSize) % flatnessEveryHops == 0;
    const auto hopSeconds = hopSize / sampleRate;

    frame.position = position;
    frame.numPaths = numPaths;
    frame.numReductions = numReductions;

    for (int p = 0; p < 2; ++p)
    {
        auto& fp = frame.paths[(size_t) p];
        if (p >= numPaths)
        {
            fp = {};
            continue;
        }

        paths[(size_t) p].read (fp, cue, s.harshOn, analyseFlatness);

        // A rising V is a voice starting, which an ess is not: for a while after it the band counts
        // as harmonic, because the noise cue needs about 20 ms to hear a new vowel's pitch. A jump
        // is a vowel coming back or a stop's burst before an ess alike, so its hold only asks for
        // more (NOTES.md, M1b, After the review). The shape cue lowers S where the spectrum falls;
        // no cue raises it.
        auto& onset = onsetHops[(size_t) p];
        auto& jump = jumpHops[(size_t) p];
        onset = fp.bodyRising ? onsetHoldHops : std::max (0, onset - 1);
        jump = fp.bodyJumped ? onsetHoldHops : std::max (0, jump - 1);
        fp.voiceOnset = onset > 0;
        fp.jumpHeld = jump > 0;
        const auto noise = fp.voiceOnset ? 0.0f : fp.noise;
        const auto harmonicDb = std::max (tuning.noiseRangeDb * (1.0f - noise), fp.jumpHeld ? tuning.jumpHoldDb : 0.0f);

        fp.sibilanceDb = voice ? fp.relativeDb - focus * harmonicDb
                                     - focus * std::clamp (tuning.presenceSlope * (tuning.presenceKneeDb - fp.presenceDb), 0.0f, tuning.presenceRangeDb)
                               : fp.bandDb;

        // The body holds while a candidate lasts, at most the hold time, so a sustained noise lets go.
        fp.candidate = fp.relativeDb > tuning.candidateDb;
        candidateHops[(size_t) p] = fp.candidate ? candidateHops[(size_t) p] + 1 : 0;
        paths[(size_t) p].holdBody (fp.candidate && candidateHops[(size_t) p] <= bodyHoldHops);
    }

    const ReleaseRule essRule { s.speedAuto, s.releaseMs, tuning.essAutoMinMs, tuning.essAutoMaxMs, tuning.essAutoShare };
    const ReleaseRule harshRule { s.harshSpeedAuto, s.harshReleaseMs, tuning.harshAutoMinMs, tuning.harshAutoMaxMs, tuning.harshAutoShare };
    const auto essThreshold = voice ? s.thresholdDb : s.thresholdAbsDb;
    const auto harshThreshold = voice ? s.harshThresholdDb : s.harshThresholdAbsDb;

    auto largestReduction = 0.0f, largestSibilance = silentDb;

    for (int r = 0; r < 2; ++r)
    {
        auto& d = frame.decisions[(size_t) r];
        auto& red = frame.reductions[(size_t) r];
        if (r >= numReductions)
        {
            d = {};
            red = {};
            continue;
        }

        // Linked: the louder channel's detector, per band.
        auto essPath = std::min (r, numPaths - 1), harshPath = essPath;
        if (linked && numPaths == 2)
        {
            essPath = frame.paths[0].bandDb >= frame.paths[1].bandDb ? 0 : 1;
            harshPath = frame.paths[0].harshDb >= frame.paths[1].harshDb ? 0 : 1;
        }

        d.sibilanceDb = frame.paths[(size_t) essPath].sibilanceDb;
        d.essTargetDb = reductionDb (d.sibilanceDb - essThreshold, s.rangeDb);

        if (s.harshOn)
        {
            const auto& hp = frame.paths[(size_t) harshPath];
            const auto level = voice ? hp.harshRelativeDb : hp.harshDb;
            const auto target = reductionDb (level - harshThreshold, s.harshRangeDb);

            // Harsh holds its reduction during an ess (the Ess band handles it) and, in Voice, on
            // noise-like energy in its own band: the /ʃ/ peak lies in the default Harsh band.
            essHoldHops[(size_t) r] = d.essTargetDb > 0.0f ? hangoverHops : std::max (0, essHoldHops[(size_t) r] - 1);
            d.harshHeld = essHoldHops[(size_t) r] > 0 || (voice && hp.harshNoise >= tuning.harshNoiseHold) || harshWarmUp > 0;
            if (! d.harshHeld)
                harshHeldTarget[(size_t) r] = target;
            d.harshTargetDb = harshHeldTarget[(size_t) r];
        }
        else
        {
            essHoldHops[(size_t) r] = 0;
            harshHeldTarget[(size_t) r] = 0.0f;
            d.harshTargetDb = 0.0f;
            d.harshHeld = false;
        }

        red.essDb = essShapers[(size_t) r].push (d.essTargetDb, lookaheadHops, essRule, hopSeconds);
        red.harshDb = harshShapers[(size_t) r].push (d.harshTargetDb, lookaheadHops, harshRule, hopSeconds);
        red.sibilanceDb = sibilanceDelays[(size_t) r].push (d.sibilanceDb, lookaheadHops);

        largestReduction = std::max (largestReduction, red.essDb);
        largestSibilance = std::max (largestSibilance, red.sibilanceDb);
    }

    if (s.harshOn && harshWarmUp > 0)
        --harshWarmUp;

    frame.reductionPosition = position - (std::int64_t) lookaheadHops * hopSize;

    EssEvent finished;
    if (events.push (largestReduction, largestSibilance, frame.reductionPosition, hopSize, hangoverHops, finished) && observer != nullptr)
        observer->event (finished);
    frame.eventActive = events.active();

    if (observer != nullptr)
        observer->hop (frame);
}

} // namespace halite::dsp
