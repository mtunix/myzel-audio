#include "dsp/modules/hiss/HissModule.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace inkcap::dsp
{

namespace
{
    // Lanes of hissNoise: one white source per type and channel, so a type crossfade mixes
    // unrelated noises (equal power holds).
    enum NoiseLane { vinylLateral, vinylVertical, tapeLeft, tapeRight, ampLeft, ampRight };

    constexpr int vinylBed = (int) HissType::vinyl, tapeBed = (int) HissType::tape, humBed = (int) HissType::hum;

    // Value-noise lattices (Hz): the faster, the higher the wander reaches.
    constexpr double rumbleLattice = 70.0;          // rumble: mostly below 30 Hz
    constexpr double tapeLattice = 7.0;             // tape's level breathing
    constexpr double humLevelLattice = 0.5;
    constexpr double humDriftLattice = 0.35;        // the mains frequency wandering by up to ~0.1 Hz
    constexpr double humBuzzLattice = 0.6;          // a load somewhere switching on: buzzier for a while
    constexpr double humBalanceLattice = 0.07;

    constexpr float verticalShare = 0.6f;           // surface noise: vertical (L - R) re lateral (L + R)

    template <typename Filter>
    double impulseEnergy (Filter filter, double sampleRate)
    {
        double energy = 0.0;
        for (int k = 0; k < (int) (0.5 * sampleRate); ++k)
        {
            const auto y = filter.process (k == 0 ? 1.0 : 0.0);
            energy += y * y;
        }
        return energy;
    }

    template <typename Excitation>
    double peakThrough (hiss::RiaaPlayback filter, double sampleRate, Excitation&& excitation)
    {
        filter.reset();
        double largest = 0.0;
        for (int k = 0; k < (int) (0.05 * sampleRate); ++k)
            largest = std::max (largest, std::abs (filter.process (excitation (k))));
        return largest;
    }

    struct LowpassProbe
    {
        myzel::dsp::TptOnePole filter;
        double process (double x) noexcept { return filter.processLowpass (x); }
    };
}

double HissModule::TapeShaping::process (double x) noexcept
{
    const auto body = lowCut.processHighpass (x);
    const auto lifted = body + 0.6 * presence.process (body).bp;     // the playback head's presence: +3.4 dB at 5.5 kHz
    return top.process (lifted).lp;
}

void HissModule::Wander::setSeed (uint32_t seed) noexcept
{
    const SeedRng rumble (seed, Stream::hissRumble), slow (seed, Stream::hissWander);
    rumbleLateral.setSource (rumble.lane (0));
    rumbleVertical.setSource (rumble.lane (1));
    tapeLevel.setSource (slow.lane (0));
    humLevel.setSource (slow.lane (1));
    humDrift.setSource (slow.lane (2));
    humBuzz.setSource (slow.lane (3));
    humBalance.setSource (slow.lane (4));
    swishPhase = slow.lane (5).uniform (0);
}

void HissModule::prepare (const ModuleSpec& spec)
{
    fs = spec.sampleRate;
    channels = std::clamp (spec.numChannels, 1, 2);
    onOff.prepare (fs);
    presence.setDuration (fs, presenceSeconds);

    const auto c = 1.0 - std::exp (-1.0 / (glideSeconds * fs));
    level.coefficient = tone.coefficient = follow.coefficient = chaos.coefficient = c;
    for (auto& f : typeFade)
        f.setDuration (fs, typeFadeSeconds);
    attack = (float) (1.0 - std::exp (-1.0 / (0.008 * fs)));
    release = (float) (1.0 - std::exp (-1.0 / (0.16 * fs)));

    crackle.prepare (fs);
    riaaLeft.prepare (fs);
    riaaRight.prepare (fs);
    hum.prepare (fs);
    for (auto& t : tape)
    {
        t.lowCut.setCutoff (300.0, fs);
        t.presence.setCoefficients (5500.0, 0.8, fs);
        t.top.setCoefficients (std::min (15000.0, 0.45 * fs), 0.7071, fs);
    }
    for (auto& a : amplifier)
        a.setCutoff (std::min (9000.0, 0.45 * fs), fs);

    // Each bed normalised to RMS 1 (white noise in), so the level mapping is the bed's RMS at any rate.
    hiss::RiaaPlayback riaaProbe;
    riaaProbe.prepare (fs);
    const auto surface = 1.0 + verticalShare * verticalShare;
    vinylTrim = (float) (1.0 / std::sqrt (impulseEnergy (riaaProbe, fs) * surface));

    // The clicks' levels are in the bed's RMS after the phono stage, measured through it.
    const auto popSamples = std::max (2.0, hiss::Crackle::middlePopMs * 0.001 * fs);
    const auto tickPeak = peakThrough (riaaProbe, fs, [] (int k) { return k == 0 ? 1.0 : k == 1 ? -0.5 : 0.0; });
    const auto popPeak = peakThrough (riaaProbe, fs, [popSamples] (int k)
                                      { return k < (int) std::ceil (popSamples) ? (double) hiss::Crackle::popShape (k, popSamples) : 0.0; });
    crackle.setScales ((float) (1.0 / (vinylTrim * tickPeak)), (float) std::sqrt (surface), (float) (1.0 / (vinylTrim * popPeak)));

    tapeTrim = (float) (1.0 / std::sqrt (impulseEnergy (tape[0], fs)));
    LowpassProbe ampProbe { amplifier[0] };
    amplifierTrim = (float) (1.0 / std::sqrt (impulseEnergy (ampProbe, fs)));

    reset();
}

void HissModule::reset() noexcept
{
    onOff.reset();
    primed = awake = false;
    timelineKnown = seedKnown = false;
    running = {};
    blend.setDuration (fs, jumpFadeSeconds);
    blend.jumpTo (1.0f);
    envelope = 0.0f;
    appliedTone = 2.0;
    lastIntensity = lastActivity = 0.0f;
}

hiss::Crackle::Wear HissModule::wearFor (float i, double chaosNow) const noexcept
{
    hiss::Crackle::Wear w;
    w.rate = i > 0.0f ? hiss::crackleRate (i) : 0.0;
    w.chaos = chaosNow;
    w.popShare = 0.03 + 0.06 * chaosNow;
    w.scratches = i >= 0.8f ? 2 : i >= 0.45f ? 1 : 0;
    return w;
}

void HissModule::startBlend (double seconds) noexcept
{
    blendWander = wander;
    blendOffset = offset;
    blend.setDuration (fs, seconds);
    blend.jumpTo (0.0f);
    blend.moveTo (1.0f);
}

void HissModule::followSeed (uint32_t newSeed) noexcept
{
    if (seedKnown && newSeed == seed)
        return;

    if (seedKnown && awake)
        startBlend (seedFadeSeconds);

    seed = newSeed;
    seedKnown = true;
    wander.setSeed (seed);
    crackle.setSeed (seed);
    const SeedRng white (seed, Stream::hissNoise);
    for (size_t l = 0; l < noise.size(); ++l)
        noise[l] = white.lane ((uint32_t) l);
}

void HissModule::followTimeline (const BlockContext& context, int64_t first, const hiss::Crackle::Wear& wear) noexcept
{
    if (timelineKnown && ! context.clock.jumpedAt (first))
        return;

    const auto newOffset = std::llround (context.clock.secondsAt (first) * fs) - first;
    if (timelineKnown && awake && newOffset != offset)
    {
        startBlend (jumpFadeSeconds);
        if (running[(size_t) vinylBed])
            crackle.restartAt (first + newOffset, wear);
    }
    offset = newOffset;
    timelineKnown = true;
}

void HissModule::setToneFilters (double t) noexcept
{
    const auto lowpassHz = std::min (hiss::toneLowpassHz (t), 0.45 * fs);
    const auto highpassHz = hiss::toneHighpassHz (t);
    for (auto& f : toneFilters)
    {
        f.lowpass.setCutoff (lowpassHz, fs);
        f.highpass.setCutoff (highpassHz, fs);
        f.shelf.setCutoff (hiss::toneShelfHz, fs);
        f.shelfGain = hiss::toneShelfGain (t);
    }
    appliedTone = t;
}

void HissModule::wake (int64_t) noexcept
{
    running = {};
    for (auto& f : toneFilters)
        f.reset();
    appliedTone = 2.0;
    envelope = 0.0f;
    awake = true;
}

HissModule::Smooth HissModule::smoothAt (int64_t t, Wander& w, double chaosNow, double buzz, std::array<bool, 3> on) noexcept
{
    Smooth s;
    const auto seconds = (double) t / fs;

    if (on[(size_t) vinylBed])
    {
        // Rumble is mostly vertical motion (the platter, the bearing): L - R.
        const auto lateral = w.rumbleLateral.at (seconds * rumbleLattice);
        const auto vertical = w.rumbleVertical.at (seconds * rumbleLattice);
        s.rumbleLeft = (float) (hiss::rumbleLevel * (0.45 * lateral + vertical));
        s.rumbleRight = (float) (hiss::rumbleLevel * (0.45 * lateral - vertical));

        // The surface noise swells once a turn (an off-centre record), on the crackle's own turn.
        const auto turn = (double) hiss::floorMod (t, crackle.samplesPerTurn()) / (double) crackle.samplesPerTurn();
        s.swish = (float) (1.0 + (0.1 + 0.1 * chaosNow) * std::cos (2.0 * std::numbers::pi * (turn + w.swishPhase)));
    }

    if (on[(size_t) tapeBed])
        s.tapeLevel = (float) (1.0 + 0.3 * chaosNow * w.tapeLevel.at (seconds * tapeLattice));

    if (on[(size_t) humBed])
    {
        const auto cycles = hum.cyclesAt (t) + 0.25 * chaosNow * w.humDrift.at (seconds * humDriftLattice);
        const auto p = hum.at (cycles);
        const auto share = std::clamp (buzz + 0.6 * chaosNow * std::max (0.0, w.humBuzz.at (seconds * humBuzzLattice) - 0.1), 0.0, 1.0);
        // Hum (sine phase) and buzz (cosine phase) are orthogonal: this keeps the RMS at 1 for any share.
        const auto mix = ((1.0 - share) * p.hum + share * p.buzz) / std::sqrt ((1.0 - share) * (1.0 - share) + share * share);
        const auto h = (1.0 + 0.25 * chaosNow * w.humLevel.at (seconds * humLevelLattice)) * mix;
        const auto balance = 1.0 + 0.12 * chaosNow * w.humBalance.at (seconds * humBalanceLattice);
        s.humLeft = (float) (h * balance);
        s.humRight = (float) (h / balance);
    }
    return s;
}

void HissModule::process (const AudioBlock& block, const HissSettings& s, const BlockContext& context) noexcept
{
    const auto i = moduleIntensity (Module::hiss, s.amount, context.damage);
    lastIntensity = s.on ? i : 0.0f;
    onOff.set (s.on);
    const auto chaosTarget = (double) chaosAmount (context.chaos);
    const auto levelTarget = hiss::bedGain (i);
    const auto wear = wearFor (i, chaosTarget);
    followSeed (context.seed);
    followTimeline (context, block.firstSample, wear);
    const auto type = std::clamp ((int) s.type, 0, 2);

    if (! primed)
    {
        level.jumpTo (levelTarget);
        presence.jumpTo (i > 0.0f ? 1.0f : 0.0f);
        tone.jumpTo (s.tone);
        follow.jumpTo (s.follow);
        chaos.jumpTo (chaosTarget);
        for (int t = 0; t < 3; ++t)
            typeFade[(size_t) t].jumpTo (t == type ? 1.0f : 0.0f);
        currentType = (HissType) type;
        primed = true;
    }
    else if ((int) currentType != type)
    {
        typeFade[(size_t) currentType].moveTo (0.0f);
        typeFade[(size_t) type].moveTo (1.0f);
        currentType = (HissType) type;
    }
    presence.moveTo (i > 0.0f ? 1.0f : 0.0f);

    if (onOff.isIdle() || presence.isAt (0.0f))
    {
        // Nothing to add: the input passes untouched. The glides wait at their targets.
        for (int k = 0; k < block.numSamples; ++k)
        {
            onOff.next();
            presence.next();
            for (auto& f : typeFade)
                f.next();
        }
        level.jumpTo (levelTarget);
        tone.jumpTo (s.tone);
        follow.jumpTo (s.follow);
        chaos.jumpTo (chaosTarget);
        awake = false;
        lastActivity = 0.0f;
        return;
    }

    if (! awake)
        wake (block.firstSample + offset);

    const auto stereo = block.numChannels > 1 && channels > 1;
    float* left = block.channels[0];
    float* right = stereo ? block.channels[1] : nullptr;
    float loudest = 0.0f;

    for (int k = 0; k < block.numSamples; ++k)
    {
        const auto t = block.firstSample + k + offset;
        const auto gain = (float) level.next (levelTarget) * presence.next() * onOff.next();
        const auto toneNow = tone.next (s.tone);
        if (! myzel::dsp::isExactly (toneNow, appliedTone))
            setToneFilters (toneNow);
        const auto followNow = follow.next (s.follow);
        const auto chaosNow = chaos.next (chaosTarget);

        std::array<float, 3> weight {};
        for (int m = 0; m < 3; ++m)
        {
            const auto v = typeFade[(size_t) m].next();
            weight[(size_t) m] = v <= 0.0f ? 0.0f : v >= 1.0f ? 1.0f : (float) std::sin (0.5 * std::numbers::pi * (double) v);
            const auto wanted = v > 0.0f;
            if (wanted && ! running[(size_t) m])
            {
                // A type starts from rest: its filters from zero, the crackle with what is under way here.
                if (m == vinylBed)
                {
                    riaaLeft.reset();
                    riaaRight.reset();
                    crackle.restartAt (t, wear);
                }
                else if (m == tapeBed)
                {
                    for (auto& f : tape)
                        f.reset();
                }
                else
                {
                    for (auto& f : amplifier)
                        f.reset();
                }
            }
            running[(size_t) m] = wanted;
        }

        const auto inLeft = left[k], inRight = stereo ? right[k] : inLeft;
        const auto peak = std::max (std::abs (inLeft), std::abs (inRight));
        envelope += (peak - envelope) * (peak > envelope ? attack : release);

        auto followGain = 1.0f;
        if (! myzel::dsp::isExactly (followNow, 0.0))
        {
            const auto db = 20.0 * std::log10 (std::max ((double) envelope, 1.0e-9));
            const auto x = std::clamp ((db + 54.0) / 36.0, 0.0, 1.0);
            const auto riding = x * x * (3.0 - 2.0 * x);       // 0 below -54 dBFS, 1 above -18 dBFS
            followGain = (float) (followNow > 0.0 ? 1.0 - followNow + followNow * riding : 1.0 + 0.9 * followNow * riding);
        }

        const auto buzz = hiss::buzzShare (toneNow);
        auto smooth = smoothAt (t, wander, chaosNow, buzz, running);
        if (blend.isMoving())
        {
            const auto h = blend.next();
            const auto old = smoothAt (block.firstSample + k + blendOffset, blendWander, chaosNow, buzz, running);
            auto mix = [h] (float a, float b) { return a + h * (b - a); };
            smooth.rumbleLeft = mix (old.rumbleLeft, smooth.rumbleLeft);
            smooth.rumbleRight = mix (old.rumbleRight, smooth.rumbleRight);
            smooth.swish = mix (old.swish, smooth.swish);
            smooth.tapeLevel = mix (old.tapeLevel, smooth.tapeLevel);
            smooth.humLeft = mix (old.humLeft, smooth.humLeft);
            smooth.humRight = mix (old.humRight, smooth.humRight);
        }

        float bedLeft = 0.0f, bedRight = 0.0f;
        if (running[(size_t) vinylBed])
        {
            const auto lateral = hiss::gaussian (noise[vinylLateral].bits (t));
            const auto vertical = verticalShare * hiss::gaussian (noise[vinylVertical].bits (t));
            float clickLeft = 0.0f, clickRight = 0.0f;
            crackle.next (t, wear, clickLeft, clickRight);

            const auto w = weight[(size_t) vinylBed];
            bedLeft += w * ((float) riaaLeft.process ((lateral + vertical) * smooth.swish + clickLeft) * vinylTrim + smooth.rumbleLeft);
            if (stereo)
                bedRight += w * ((float) riaaRight.process ((lateral - vertical) * smooth.swish + clickRight) * vinylTrim + smooth.rumbleRight);
        }
        if (running[(size_t) tapeBed])
        {
            const auto w = weight[(size_t) tapeBed] * tapeTrim * smooth.tapeLevel;
            bedLeft += w * (float) tape[0].process (hiss::gaussian (noise[tapeLeft].bits (t)));
            if (stereo)
                bedRight += w * (float) tape[1].process (hiss::gaussian (noise[tapeRight].bits (t)));
        }
        if (running[(size_t) humBed])
        {
            const auto w = weight[(size_t) humBed];
            const auto floor = (float) hiss::humFloor * amplifierTrim;
            bedLeft += w * (smooth.humLeft + floor * (float) amplifier[0].processLowpass (hiss::gaussian (noise[ampLeft].bits (t))));
            if (stereo)
                bedRight += w * (smooth.humRight + floor * (float) amplifier[1].processLowpass (hiss::gaussian (noise[ampRight].bits (t))));
        }

        const auto g = gain * followGain;
        const auto addLeft = g * toneFilters[0].process (bedLeft);
        left[k] = inLeft + addLeft;
        loudest = std::max (loudest, std::abs (addLeft));
        if (stereo)
        {
            const auto addRight = g * toneFilters[1].process (bedRight);
            right[k] = inRight + addRight;
            loudest = std::max (loudest, std::abs (addRight));
        }
    }

    // The display's dot: the noise's peak, -72 .. -12 dBFS.
    const auto db = 20.0 * std::log10 (std::max ((double) loudest, 1.0e-9));
    lastActivity = (float) std::clamp ((db + 72.0) / 60.0, 0.0, 1.0);
}

} // namespace inkcap::dsp
