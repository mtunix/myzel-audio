#include "dsp/chain/BasaltChain.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace basalt::dsp
{

using myzel::dsp::isExactly;

namespace
{
    int samplesFor (double seconds, double rate) noexcept { return std::max (1, (int) std::lround (seconds * rate)); }

    bool leftRight (StereoMode m) noexcept { return m == StereoMode::linked || m == StereoMode::unlinked; }

    size_t lineOf (const LaneConfig& c) noexcept { return c.key == KeySource::external ? 1 : 0; }

    /** A change only a second lane can make: what the detectors hear, not which engine hears it. */
    bool sameKey (const LaneConfig& a, const LaneConfig& b) noexcept
    {
        return a.stereo == b.stereo && a.key == b.key && a.numChannels == b.numChannels;
    }

    /** Multiplies by a smoothed gain, leaving the samples untouched while it is exactly 1. */
    void applyGain (GainSmoother& g, float* const* out, int numChannels, int n) noexcept
    {
        if (g.moving())
        {
            for (int i = 0; i < n; ++i)
                if (const auto v = g.next(); ! isExactly (v, 1.0))
                    for (int c = 0; c < numChannels; ++c)
                        out[c][i] *= v;
            return;
        }

        if (const auto v = g.current(); ! isExactly (v, 1.0))
            for (int c = 0; c < numChannels; ++c)
                juce::FloatVectorOperations::multiply (out[c], v, n);
    }

    float listened (Listen l, float output, float dry, float cell, float key) noexcept
    {
        switch (l)
        {
            case Listen::output: return output;
            case Listen::key:    return key;
            case Listen::delta:  return dry - cell;
        }
        return output;
    }

    float peakOf (const float* x, int n) noexcept
    {
        const auto r = juce::FloatVectorOperations::findMinAndMax (x, n);
        return std::max (std::abs (r.getStart()), std::abs (r.getEnd()));
    }
}

BasaltChain::BasaltChain()
{
    for (auto& lane : lanes)
        for (int e = 0; e < numEngines; ++e)
            lane.install (e, makeEngine (static_cast<Engine> (e)));
}

int BasaltChain::lookaheadDelaySamples (LatencyMode mode, double rate) noexcept
{
    return mode == LatencyMode::live || ! (rate > 0.0) ? 0 : (int) std::lround (dynamics::maxLookaheadMs * 0.001 * rate);
}

int BasaltChain::characterDelaySamples (LatencyMode, double) noexcept
{
    return 0;
}

int BasaltChain::latencySamples (LatencyMode mode, double rate) noexcept
{
    return lookaheadDelaySamples (mode, rate) + characterDelaySamples (mode, rate);
}

int BasaltChain::latencyFor (LatencyMode mode) const noexcept
{
    return lookaheadDelaySamples (mode, sampleRate) + (mode == LatencyMode::normal ? characterDelay : 0);
}

float BasaltChain::gainFor (float db) noexcept
{
    return isExactly (db, 0.0) ? 1.0f : std::pow (10.0f, db / 20.0f);
}

void BasaltChain::prepare (double rate, int maximumBlockSize, int numChannels, LatencyMode mode)
{
    prepare (rate, maximumBlockSize, numChannels, mode, characterDelaySamples (LatencyMode::normal, rate));
}

void BasaltChain::prepare (double rate, int /*maximumBlockSize*/, int /*numChannels*/, LatencyMode mode, int characterDelaySamplesNormal)
{
    sampleRate = rate;
    characterDelay = std::max (0, characterDelaySamplesNormal);

    for (auto& line : keyLines)
    {
        line.eq.prepare (rate);
        line.delay.prepare (2, latencyFor (LatencyMode::normal) + 1);
    }
    for (auto& line : dryDelay)
        line.prepare (latencyFor (LatencyMode::normal), chunk);
    for (auto& lane : lanes)
        lane.prepare (rate);

    lookahead.prepare (dynamics::lookaheadGlideMs, rate);
    laneFade.setLength (samplesFor (fadeSeconds, rate));
    listenFade.setLength (samplesFor (fadeSeconds, rate));
    bypassFade.setLength (samplesFor (bypassSeconds, rate));
    latencyGain.setLength (samplesFor (latencyFadeSeconds, rate));
    for (auto* g : { &makeupGain, &mixAmount, &outputGain })
        g->setTime (outputMs, rate);

    latencyMode = mode;
    reset();
}

void BasaltChain::reset() noexcept
{
    primed = false;
    for (auto& line : keyLines)
    {
        line.eq.reset();
        line.delay.reset();
        line.running = false;
    }
    for (auto& line : dryDelay)
        line.reset();
}

LaneConfig BasaltChain::configFor (const ChainSettings& s, int numChannels, bool keyAvailable) const noexcept
{
    LaneConfig c;
    c.engine = static_cast<int> (engineFor (s.model, s.vcaDetector));
    c.numChannels = numChannels;
    c.stereo = numChannels == 2 ? s.stereo : StereoMode::linked;
    c.key = s.key == KeySource::external && keyAvailable ? KeySource::external : KeySource::internal;
    return c;
}

dynamics::ModelParams BasaltChain::paramsFor (const ChainSettings& s) const noexcept
{
    dynamics::ModelParams p;
    p.curve = { s.thresholdDb, s.inverseRatio, s.kneeDb, s.rangeDb };
    p.attackMs = s.attackMs;
    p.releaseMs = s.releaseMs;
    p.autoRelease = s.autoRelease;
    p.floorDb = s.floorDb;
    p.vcaRms = s.vcaDetector == VcaDetector::rms;
    p.link = std::clamp (s.link, 0.0, 1.0);
    return p;
}

double BasaltChain::lookaheadTarget (const ChainSettings& s) const noexcept
{
    // Live has no look-ahead; Normal's comes out of the constant delay, so it never moves the latency.
    // Whole samples: a settled tap between two samples would read the key through a linear
    // interpolator, a low-pass that nulls a Nyquist-rate key at half a sample (dynamics/NOTES.md, Look-ahead).
    return std::clamp (std::round (s.lookaheadMs * 0.001 * sampleRate), 0.0, (double) lookaheadDelaySamples (latencyMode, sampleRate));
}

float BasaltChain::makeupFor (const ChainSettings& s, const dynamics::ModelParams& p) noexcept
{
    const auto autoDb = s.autoGain ? dynamics::autoGainDb (heardLane().targetModel(), p) : 0.0;
    autoGainDb = (float) autoDb;
    const auto db = (double) s.gainDb + autoDb;
    return isExactly (db, 0.0) ? 1.0f : (float) std::pow (10.0, db / 20.0);
}

void BasaltChain::prime (const ChainSettings& s, int numChannels, bool keyAvailable) noexcept
{
    latencyMode = s.latency;
    latency = latencyFor (latencyMode);
    latencyGain.jumpTo (true);
    lookahead.jumpTo (lookaheadTarget (s));

    activeLane = 0;
    changing = false;
    laneFade.jumpTo (false);
    const std::array<double, 2> released {};
    lanes[0].start (configFor (s, numChannels, keyAvailable));
    lanes[0].seed (released.data());
    keyLines[lineOf (lanes[0].config())].start();

    listen = listenBefore = s.listen;
    listenFade.jumpTo (true);
    bypassFade.jumpTo (s.bypass);
    outputGain.jumpTo (gainFor (s.outputDb));
    mixAmount.jumpTo ((float) std::clamp (s.mix, 0.0, 1.0));
    makeupGain.jumpTo (makeupFor (s, paramsFor (s)));
    primed = true;
}

void BasaltChain::process (float* const* channels, int numChannels, int numSamples, const ChainSettings& s,
                           const float* const* key, int numKeyChannels) noexcept
{
    if (numSamples <= 0 || numChannels <= 0 || channels == nullptr)
        return;

    numChannels = std::min (numChannels, 2);
    numKeyChannels = key != nullptr ? std::clamp (numKeyChannels, 0, 2) : 0;

    if (! primed)
        prime (s, numChannels, numKeyChannels > 0);

    for (auto& line : keyLines)
        line.eq.set (s.keyEq);

    for (int offset = 0; offset < numSamples;)
        offset += processChunk (channels, offset, numSamples - offset, numChannels, s, key, numKeyChannels);

    const auto& heard = lanes[(size_t) (laneFade.moving() ? 1 - activeLane : activeLane)];
    const auto engine = static_cast<Engine> (heard.targetEngine());
    chainStatus.model.store (static_cast<int> (modelOf (engine)), std::memory_order_relaxed);
    chainStatus.vcaDetector.store (static_cast<int> (detectorOf (engine)), std::memory_order_relaxed);
    chainStatus.requestedModel.store (static_cast<int> (s.model), std::memory_order_relaxed);
    chainStatus.latencySamples.store (latency, std::memory_order_relaxed);
    chainStatus.autoGainDb.store (autoGainDb, std::memory_order_relaxed);
}

void BasaltChain::tapKey (const DynamicsLane& lane, double delay, std::array<double, 2>& out) const noexcept
{
    const auto& c = lane.config();
    const auto& line = keyLines[c.key == KeySource::external ? 1 : 0].delay;

    if (c.numChannels == 1)
    {
        out[0] = line.read (0, delay);
        return;
    }

    dynamics::encodeKey (c.stereo, line.read (0, delay), line.read (1, delay), out.data());
}

int BasaltChain::processChunk (float* const* channels, int offset, int available, int numChannels, const ChainSettings& s,
                               const float* const* key, int numKeyChannels) noexcept
{
    // A new latency fades out, switches while silent and fades back in. Until the switch the key
    // tap keeps the look-ahead of the latency the audio has, so the reductions stay lined up.
    if (s.latency != latencyMode)
    {
        if (latencyGain.atZero())
        {
            latencyMode = s.latency;
            latency = latencyFor (latencyMode);
            lookahead.jumpTo (lookaheadTarget (s));
        }
        else
        {
            latencyGain.moveTo (false);
        }
    }
    if (s.latency == latencyMode)
        latencyGain.moveTo (true);
    lookahead.setTarget (lookaheadTarget (s));

    if (s.listen != listen && ! listenFade.moving())
    {
        listenBefore = listen;
        listen = s.listen;
        listenFade.jumpTo (false);
        listenFade.moveTo (true);
    }

    // A new engine fades in inside its lane; a new key or stereo mode in the other lane, which
    // first follows the key unheard until its level states have settled. The bus changing width
    // starts afresh at once.
    const auto keyAvailable = numKeyChannels > 0;
    const auto target = configFor (s, numChannels, keyAvailable);
    const auto params = paramsFor (s);
    {
        auto& active = lanes[(size_t) activeLane];
        auto& other = lanes[(size_t) (1 - activeLane)];

        if (target.numChannels != active.config().numChannels)
        {
            const std::array<double, 2> released {};
            active.start (target);
            active.seed (released.data());
            changing = false;
            laneFade.jumpTo (false);
            if (auto& line = keyLines[lineOf (target)]; ! line.running)
                line.start();
        }
        else if (! laneFade.moving())
        {
            if (sameKey (target, active.config()))
            {
                changing = false;
            }
            else if (! changing || ! (target == other.config()))
            {
                changing = ! active.switching();
                if (changing)
                {
                    other.start (target);
                    warmUpLeft = warmUpSamples (other.targetModel(), params, lookahead.current(), sampleRate);

                    // A key line that was not running starts from rest: the lane follows it once
                    // the latency has filled it.
                    if (auto& line = keyLines[lineOf (target)]; ! line.running)
                    {
                        line.start();
                        warmUpLeft += latency;
                    }
                }
            }
        }

        // The outgoing lane keeps its engine while the other comes in.
        active.request (sameKey (target, active.config()) ? target.engine : active.targetEngine(), params, lookahead.current());
    }

    auto& active = lanes[(size_t) activeLane];
    auto& other = lanes[(size_t) (1 - activeLane)];
    const auto lanesFading = laneFade.moving();
    const auto otherRuns = changing || lanesFading;
    const auto& heard = lanesFading ? other : active;
    const auto keyListened = listen == Listen::key || (listenFade.moving() && listenBefore == Listen::key);
    const auto deltaListened = listen == Listen::delta || (listenFade.moving() && listenBefore == Listen::delta);

    // Every decision above is taken at a chunk's start, so a chunk ends where the next one is due:
    // where a warm-up or a fade ends. The decisions then fall on the same samples whatever the
    // host's blocks, and so does the output.
    auto n = std::min ({ available, chunk, active.samplesToDecision() });
    for (const auto* f : { &latencyGain, &listenFade, &laneFade })
        if (f->moving())
            n = std::min (n, f->remaining());
    if (changing)
        n = std::min (n, warmUpLeft);
    n = std::max (n, 1);

    // The key lines the lanes hear run; the others stop and start from rest when heard again.
    for (size_t l = 0; l < keyLines.size(); ++l)
        if (lineOf (active.config()) != l && ! (otherRuns && lineOf (other.config()) == l))
            keyLines[l].running = false;

    // The detectors take the key sane (saneKey); the audio and the dry path pass whatever comes in.
    for (size_t l = 0; l < keyLines.size(); ++l)
    {
        auto& line = keyLines[l];
        if (! line.running)
            continue;

        // A lane may still hear the side chain the host has just turned off: silence, until its switch.
        auto& block = line.block;
        if (l == 1 && ! keyAvailable)
        {
            for (auto& ch : block)
                std::fill (ch.begin(), ch.begin() + n, 0.0);
        }
        else if (l == 0)
        {
            for (int c = 0; c < numChannels; ++c)
                for (int i = 0; i < n; ++i)
                    block[(size_t) c][(size_t) i] = dynamics::saneKey (channels[c][offset + i]);
        }
        else if (numChannels == 2)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    block[(size_t) c][(size_t) i] = dynamics::saneKey (key[std::min (c, numKeyChannels - 1)][offset + i]);
        }
        else
        {
            for (int i = 0; i < n; ++i)
                block[0][(size_t) i] = 0.5 * (dynamics::saneKey (key[0][offset + i]) + dynamics::saneKey (key[numKeyChannels - 1][offset + i]));
        }

        if (! line.eq.isFlat())
        {
            std::array<double*, 2> b { block[0].data(), block[1].data() };
            line.eq.process (b.data(), numChannels, n);
        }
    }

    for (int i = 0; i < n; ++i)
    {
        for (auto& line : keyLines)
            if (line.running)
            {
                const std::array<double, 2> frame { line.block[0][(size_t) i], line.block[1][(size_t) i] };
                line.delay.push (frame.data());
            }

        // The audio is delayed by the whole latency, Character's share included, so the key is
        // read at that delay less the look-ahead.
        const auto la = lookahead.next();
        tap[(size_t) i] = la;
        const auto delay = (double) latency - la;

        std::array<double, 2> d {};
        tapKey (active, delay, d);
        for (int c = 0; c < active.detectorChannels(); ++c)
            active.key (c)[i] = d[(size_t) c];

        if (otherRuns)
        {
            tapKey (other, delay, d);
            for (int c = 0; c < other.detectorChannels(); ++c)
                other.key (c)[i] = d[(size_t) c];
        }

        if (keyListened)
        {
            // Listen › Key: the key after the EQ, lined up with the audio; Mid, Side and Mid/Side as M and S.
            const auto& line = keyLines[heard.config().key == KeySource::external ? 1 : 0].delay;
            const auto l = line.read (0, latency);
            const auto r = numChannels == 2 ? line.read (1, latency) : l;
            const auto ms = numChannels == 2 && ! leftRight (heard.config().stereo);
            keyHeard[0][(size_t) i] = (float) (ms ? 0.5 * (l + r) : l);
            keyHeard[1][(size_t) i] = (float) (ms ? 0.5 * (l - r) : r);
        }
    }

    active.process (params, tap.data(), n);

    if (lanesFading)
    {
        other.process (params, tap.data(), n, other.config().engine);
    }
    else if (changing)
    {
        other.follow (params, tap.data(), n);
        if ((warmUpLeft -= n) <= 0)
        {
            const auto from = active.config(), to = other.config();
            const auto* r = active.lastApplied();
            std::array<double, 2> seed {};
            if (active.detectorChannels() == other.detectorChannels() && (from.stereo == to.stereo || (leftRight (from.stereo) && leftRight (to.stereo))))
                seed = { r[0], r[1] };
            else
                seed.fill (active.detectorChannels() == 2 ? std::max (r[0], r[1]) : r[0]);

            // A linked pair starts linked: at Link 100 % both channels carry the louder one's
            // reduction, so their gains are identical from the first sample (Settled 32).
            if (to.layout().linked)
                dynamics::linkReductions (seed.data(), params.link);

            other.seed (seed.data());
            changing = false;
            laneFade.jumpTo (false);
            laneFade.moveTo (true);
        }
    }

    for (size_t c = 0; c < (size_t) numChannels; ++c)
        dryDelay[c].process (channels[c] + offset, dry[c].data(), n, latency);

    std::array<float*, 2> out { channels[0] + offset, channels[numChannels - 1] + offset };
    const auto gainsOf = [] (const DynamicsLane& lane)
    {
        return std::array<const float*, 2> { lane.gains (0), lane.gains (lane.detectorChannels() - 1) };
    };

    // The gain cell, on the latency-aligned audio; across two lanes, the audio crossfades.
    for (int c = 0; c < numChannels; ++c)
        juce::FloatVectorOperations::copy (out[(size_t) c], dry[(size_t) c].data(), n);
    auto g = gainsOf (active);
    dynamics::applyGains (active.config().stereo, out.data(), numChannels, g.data(), n);

    if (lanesFading)
    {
        std::array<float*, 2> wet { wetOther[0].data(), wetOther[1].data() };
        for (int c = 0; c < numChannels; ++c)
            juce::FloatVectorOperations::copy (wet[(size_t) c], dry[(size_t) c].data(), n);
        g = gainsOf (other);
        dynamics::applyGains (other.config().stereo, wet.data(), numChannels, g.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const auto w = laneFade.next();
            for (size_t c = 0; c < (size_t) numChannels; ++c)
                if (const auto a = out[c][i], b = wet[c][i]; ! isExactly (a, b))
                    out[c][i] = a + w * (b - a);
        }
    }

    if (deltaListened)
        for (int c = 0; c < numChannels; ++c)
            juce::FloatVectorOperations::copy (cell[(size_t) c].data(), out[(size_t) c], n);

    makeupGain.setTarget (makeupFor (s, params));
    applyGain (makeupGain, out.data(), numChannels, n);

    // Mix against the latency-aligned dry path; at exactly 100 % the wet samples stay as they are.
    mixAmount.setTarget ((float) std::clamp (s.mix, 0.0, 1.0));
    if (mixAmount.moving() || ! isExactly (mixAmount.current(), 1.0))
    {
        const auto moving = mixAmount.moving();
        const auto steady = mixAmount.current();
        if (! moving && isExactly (steady, 0.0))
        {
            for (int c = 0; c < numChannels; ++c)
                juce::FloatVectorOperations::copy (out[(size_t) c], dry[(size_t) c].data(), n);
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                const auto m = moving ? mixAmount.next() : steady;
                if (isExactly (m, 1.0))
                    continue;
                for (size_t c = 0; c < (size_t) numChannels; ++c)
                    if (const auto o = out[c][i], d = dry[c][(size_t) i]; ! isExactly (o, d))
                        out[c][i] = d + m * (o - d);
            }
        }
    }

    if (listen != Listen::output || listenFade.moving())
    {
        for (int i = 0; i < n; ++i)
        {
            const auto w = listenFade.next();
            for (size_t c = 0; c < (size_t) numChannels; ++c)
            {
                const auto o = out[c][i], d = dry[c][(size_t) i], k = keyHeard[c][(size_t) i], x = cell[c][(size_t) i];
                auto v = listened (listen, o, d, x, k);
                if (w < 1.0f)
                {
                    const auto before = listened (listenBefore, o, d, x, k);
                    v = before + w * (v - before);
                }
                out[c][i] = v;
            }
        }
    }

    outputGain.setTarget (gainFor (s.outputDb));
    applyGain (outputGain, out.data(), numChannels, n);

    bypassFade.moveTo (s.bypass);
    if (bypassFade.moving())
    {
        for (int i = 0; i < n; ++i)
        {
            const auto b = bypassFade.next();
            for (size_t c = 0; c < (size_t) numChannels; ++c)
            {
                const auto d = dry[c][(size_t) i];
                out[c][i] = b >= 1.0f ? d : (b <= 0.0f ? out[c][i] : out[c][i] + b * (d - out[c][i]));
            }
        }
    }
    else if (bypassFade.atOne())
    {
        for (int c = 0; c < numChannels; ++c)
            juce::FloatVectorOperations::copy (out[(size_t) c], dry[(size_t) c].data(), n);
    }

    if (! latencyGain.atOne())
        for (int i = 0; i < n; ++i)
            if (const auto level = latencyGain.next(); level < 1.0f)
                for (size_t c = 0; c < (size_t) numChannels; ++c)
                    out[c][i] *= level;

    // During a lane fade the meters follow the lane coming in.
    const auto& metered = heard;
    for (int c = 0; c < numChannels; ++c)
    {
        chainMeters.input[(size_t) c].note (peakOf (dry[(size_t) c].data(), n));
        chainMeters.output[(size_t) c].note (peakOf (out[(size_t) c], n));
    }
    for (int c = 0; c < metered.detectorChannels(); ++c)
    {
        const auto* r = metered.reductions (c);
        const auto [lo, hi] = std::minmax_element (r, r + n);
        chainMeters.reductionDb[(size_t) c].note ((float) std::max (*hi, 0.0));
        chainMeters.liftDb[(size_t) c].note ((float) std::max (-*lo, 0.0));
    }
    chainMeters.detectorChannels.store (metered.detectorChannels(), std::memory_order_relaxed);
    chainMeters.stereo.store (static_cast<int> (metered.config().stereo), std::memory_order_relaxed);

    if (lanesFading && laneFade.atOne())
    {
        activeLane = 1 - activeLane;
        laneFade.jumpTo (false);
    }
    return n;
}

} // namespace basalt::dsp
