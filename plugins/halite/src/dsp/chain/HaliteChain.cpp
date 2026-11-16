#include "dsp/chain/HaliteChain.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace halite::dsp
{

namespace
{
    int samplesFor (double seconds, double rate) noexcept { return std::max (1, (int) std::lround (seconds * rate)); }

    bool hearsTrigger (Listen l) noexcept { return l == Listen::essTrigger || l == Listen::harshTrigger; }

    /** One output sample of what Listen plays, before Output and Bypass. */
    float listened (Listen l, float dry, float ess, float harsh, float essTrigger, float harshTrigger) noexcept
    {
        switch (l)
        {
            case Listen::output:
            {
                // Untouched rather than less 0: under DAZ a subtraction would flush a denormal input.
                const auto removed = ess + harsh;
                return juce::exactlyEqual (removed, 0.0f) ? dry : dry - removed;
            }
            case Listen::removed:       return ess + harsh;
            case Listen::essRemoved:    return ess;
            case Listen::harshRemoved:  return harsh;
            case Listen::essTrigger:    return essTrigger;
            case Listen::harshTrigger:  return harshTrigger;
        }
        return dry;
    }
}

int HaliteChain::frameSize (double rate) noexcept
{
    if (! (rate > 0.0))
        return 512;

    const auto octaves = std::clamp ((int) std::lround (std::log2 (rate / 48000.0)), -4, 4);
    auto frame = octaves >= 0 ? 512 << octaves : 512 >> -octaves;
    // Normal lines the reductions up with the audio from inside the frame (NOTES.md, Timing).
    const auto needed = (Detector::lookaheadHopsLimit (rate) + 1) * Detector::hopSize;
    while (frame < needed)
        frame <<= 1;
    return frame;
}

int HaliteChain::latencySamples (LatencyMode mode, double rate) noexcept
{
    return mode == LatencyMode::live ? 0 : frameSize (rate);
}

void HaliteChain::prepare (double newSampleRate, int /*maximumBlockSize*/, int /*numChannels*/, LatencyMode mode)
{
    sampleRate = newSampleRate;
    detector.prepare (sampleRate);
    learner.prepare (sampleRate);
    publisher.ring = &eventRing;
    publisher.reductions = &reductions;
    publisher.learn = &learner;
    publisher.feed = &displayFeed;

    const auto longest = latencySamples (LatencyMode::normal, sampleRate);
    const auto lookahead = Detector::lookaheadHopsLimit (sampleRate) * Detector::hopSize;
    displayFeed.prepare (sampleRate, longest);

    // The tracks reach back over the latency and two frames of Spectral's reading and learning, and
    // ahead over the look-ahead and a chunk.
    reductions.prepare ((3 * longest + lookahead + chunk) / Detector::hopSize + 16);
    for (auto* lines : { &dryDelay, &essTriggerDelay, &harshTriggerDelay })
        for (auto& line : *lines)
            line.prepare (longest, chunk);
    for (auto& h : history)
        h.assign ((size_t) longest, 0.0f);
    for (auto& lane : lanes)
        lane.prepare (sampleRate, longest);

    laneFade.setLength (samplesFor (fadeSeconds, sampleRate));
    listenFade.setLength (samplesFor (fadeSeconds, sampleRate));
    bypassFade.setLength (samplesFor (bypassSeconds, sampleRate));
    latencyGain.setLength (samplesFor (latencyFadeSeconds, sampleRate));
    outputGain.setTime (outputMs, sampleRate);

    latencyMode = mode;
    reset();
}

void HaliteChain::reset() noexcept
{
    primed = false;
    samplePosition = 0;
    wasPlaying = false;
    expectedPosition = 0;
    detector.reset();
    learner.reset();
    reductions.reset();
    for (auto* lines : { &dryDelay, &essTriggerDelay, &harshTriggerDelay })
        for (auto& line : *lines)
            line.reset();
    publisher.essMax = {};
    publisher.harshMax = {};
    publisher.any = false;
}

void HaliteChain::Publisher::hop (const DetectorFrame& f) noexcept
{
    last = f;
    any = true;
    for (size_t r = 0; r < 2; ++r)
    {
        essMax[r] = std::max (essMax[r], f.reductions[r].essDb);
        harshMax[r] = std::max (harshMax[r], f.reductions[r].harshDb);
    }

    reductions->push (f, mode);
    learn->hop (f, *settings);
    feed->hop (f, feedOffset);
}

void HaliteChain::publish() noexcept
{
    auto& p = publisher;
    if (! p.any)
        return;

    // The path the first reduction follows: in Linked, the louder one.
    const auto& f = p.last;
    const auto path = f.numPaths == 2 && f.numReductions == 1 && f.paths[1].bandDb > f.paths[0].bandDb ? 1 : 0;
    const auto& level = f.paths[(size_t) path];
    auto& m = detectorMeters;

    m.bandDb.store (level.bandDb, std::memory_order_relaxed);
    m.bodyDb.store (level.bodyDb, std::memory_order_relaxed);
    m.sibilanceDb.store (f.decisions[0].sibilanceDb, std::memory_order_relaxed);
    m.noise.store (level.noise, std::memory_order_relaxed);
    m.harshDb.store (level.harshDb, std::memory_order_relaxed);
    m.harshBodyDb.store (level.harshBodyDb, std::memory_order_relaxed);
    for (size_t r = 0; r < 2; ++r)
    {
        m.essReductionDb[r].store (p.essMax[r], std::memory_order_relaxed);
        m.harshReductionDb[r].store (p.harshMax[r], std::memory_order_relaxed);
    }
    m.numReductions.store (f.numReductions, std::memory_order_relaxed);
    m.eventActive.store (f.eventActive, std::memory_order_release);

    p.essMax = {};
    p.harshMax = {};
    p.any = false;
}

LaneConfig HaliteChain::laneConfigFor (const ChainSettings& s, int numChannels) const noexcept
{
    // Settings that make no difference to a lane do not start a crossfade. Spectral needs the
    // Normal latency the audio has; in Live, Split runs.
    LaneConfig c;
    c.shape = s.shape == Shape::spectral && latencyMode == LatencyMode::live ? Shape::split : s.shape;
    c.numChannels = numChannels;
    c.stereo = numChannels == 2 ? s.detector.stereo : StereoMode::linked;
    const auto top = std::max (s.detector.detectLowHz, s.detector.detectHighHz);
    c.essKind = c.shape == Shape::split ? SplitFilter::kindFor (top, sampleRate) : SplitFilter::Kind::bell;
    return c;
}

void HaliteChain::startLane (int index, const LaneConfig& c, const ShapeLane* learned) noexcept
{
    auto& lane = lanes[(size_t) index];
    lane.start (c, bands, learned);
    laneReductions[(size_t) index].set (reductions, c.stereo);
    if (c.shape != Shape::spectral)
        return;

    // The frame of input before this chunk: the chunk itself goes into the delay after this.
    const auto frame = (int) history[0].size();
    for (size_t ch = 0; ch < (size_t) c.numChannels; ++ch)
        dryDelay[ch].copyRecent (history[ch].data(), frame, 0);
    const std::array<const float*, 2> past { history[0].data(), history[(size_t) c.numChannels - 1].data() };
    lane.fill (past.data());
}

void HaliteChain::prime (const ChainSettings& s, int numChannels, const Timeline& timeline) noexcept
{
    latencyMode = s.detector.latency;
    latency = latencySamples (latencyMode, sampleRate);
    latencyGain.jumpTo (true);

    // Every grid is anchored here: the detector's hops, Spectral's frames, its target's blocks.
    samplePosition = timeline.known ? timeline.position : 0;
    detector.anchor (samplePosition);
    wasPlaying = timeline.known && timeline.playing;
    expectedPosition = samplePosition;

    const auto b = Detector::bandsFor (s.detector, sampleRate);
    bands = { SplitFilter::Kind::bell, b.essLow, b.essHigh, b.harshLow, b.harshHigh };
    activeLane = 0;
    const auto c = laneConfigFor (s, numChannels);
    lanes[0].start (c, bands);
    laneReductions[0].set (reductions, c.stereo);
    changing = false;
    laneFade.jumpTo (false);

    listen = listenBefore = s.listen;
    listenFade.jumpTo (true);
    bypassFade.jumpTo (s.bypass);
    outputGain.jumpTo (juce::Decibels::decibelsToGain (s.outputDb));
    primed = true;
}

void HaliteChain::restart (std::int64_t timelinePosition) noexcept
{
    // The audio in the delay keeps the reductions it had; the detector and Spectral's analysis
    // start afresh on the grids of the new position, and the target from its warm start.
    const auto shift = samplePosition - timelinePosition;
    reductions.restart (timelinePosition, shift);
    samplePosition = timelinePosition;
    detector.reset();
    detector.anchor (samplePosition);

    // A lane still waiting to be heard starts again on the new timeline, from the next chunk.
    changing = false;
    lanes[(size_t) activeLane].restart (shift, timelinePosition);
    if (laneFade.moving())
        lanes[(size_t) (1 - activeLane)].restart (shift, timelinePosition);
}

void HaliteChain::process (float* const* channels, int numChannels, int numSamples, const ChainSettings& s,
                           const float* const* key, int numKeyChannels, const Timeline& timeline) noexcept
{
    if (numSamples <= 0 || numChannels <= 0 || channels == nullptr)
        return;

    numChannels = std::min (numChannels, 2);
    numKeyChannels = key != nullptr ? std::clamp (numKeyChannels, 0, 2) : 0;
    if (! primed)
        prime (s, numChannels, timeline);
    else if (timeline.known && timeline.playing && (! wasPlaying || std::abs (timeline.position - expectedPosition) > jumpTolerance))
        restart (timeline.position);

    if (timeline.known)
    {
        wasPlaying = timeline.playing;
        expectedPosition = timeline.position + numSamples;
    }
    publisher.playing = timeline.known && timeline.playing;
    displayFeed.noteTransport (displayFeed.clock() - samplePosition, publisher.playing);

    for (int offset = 0; offset < numSamples; offset += chunk)
        processChunk (channels, offset, std::min (chunk, numSamples - offset), numChannels, s, key, numKeyChannels);

    publish();

    const auto& running = runningLane().config();
    const auto fallback = s.shape == Shape::spectral && latencyMode == LatencyMode::live ? ShapeFallback::live : ShapeFallback::none;
    chainStatus.shape.store ((int) running.shape, std::memory_order_relaxed);
    chainStatus.fallback.store ((int) fallback, std::memory_order_relaxed);
    chainStatus.latencySamples.store (latency, std::memory_order_relaxed);
}

void HaliteChain::processChunk (float* const* channels, int offset, int n, int numChannels, const ChainSettings& s,
                                const float* const* key, int numKeyChannels) noexcept
{
    // A new latency fades out, switches while silent and fades back in once the lane is ready;
    // until the switch the detector keeps the look-ahead of the latency the audio has, so the
    // reductions stay lined up.
    auto switched = false;
    if (s.detector.latency != latencyMode)
    {
        if (latencyGain.atZero())
        {
            latencyMode = s.detector.latency;
            latency = latencySamples (latencyMode, sampleRate);
            switched = true;
        }
        else
        {
            latencyGain.moveTo (false);
        }
    }

    if (s.listen != listen && ! listenFade.moving())
    {
        // No fade is running, so the trigger delays have run only if Listen played a trigger.
        const auto wasHeard = hearsTrigger (listen);
        listenBefore = listen;
        listen = s.listen;
        listenFade.jumpTo (false);
        listenFade.moveTo (true);
        if (! wasHeard && hearsTrigger (listen))
            for (int c = 0; c < 2; ++c)
            {
                essTriggerDelay[(size_t) c].reset();
                harshTriggerDelay[(size_t) c].reset();
            }
    }
    const auto triggers = hearsTrigger (listen) || (listenFade.moving() && hearsTrigger (listenBefore));

    const auto b = Detector::bandsFor (s.detector, sampleRate);
    bands = { SplitFilter::Kind::bell, b.essLow, b.essHigh, b.harshLow, b.harshHigh };

    // A new lane starts unheard; it fades in once it is ready and reads its own stereo mode's
    // reductions (NOTES.md, Crossfades). A change of latency or of the bus switches at once while
    // the output is silent: Spectral's removed part would not line up with the other latency's audio.
    const auto target = laneConfigFor (s, numChannels);
    if (switched || target.numChannels != lanes[(size_t) activeLane].config().numChannels)
    {
        if (! (target == lanes[(size_t) activeLane].config()))
            startLane (activeLane, target, nullptr);
        changing = false;
        laneFade.jumpTo (false);
    }
    else if (! changing && ! laneFade.moving() && ! (target == lanes[(size_t) activeLane].config()))
    {
        // From one Spectral lane to another (a new Stereo mode) the target carries over.
        startLane (1 - activeLane, target, &lanes[(size_t) activeLane]);
        changing = true;
    }
    auto& current = lanes[(size_t) activeLane];
    auto& other = lanes[(size_t) (1 - activeLane)];
    const auto otherRuns = changing || laneFade.moving();

    // The detector works in the stereo mode of the lane the chain is heading to.
    auto detectorSettings = s.detector;
    detectorSettings.latency = latencyMode;
    detectorSettings.stereo = (otherRuns ? other : current).config().stereo;
    publisher.mode = detectorSettings.stereo;
    publisher.settings = &detectorSettings;
    publisher.feedOffset = displayFeed.clock() - samplePosition;

    const std::array<const float*, 2> in { channels[0] + offset, channels[numChannels - 1] + offset };
    std::array<const float*, 2> side {};
    for (int c = 0; c < numKeyChannels; ++c)
        side[(size_t) c] = key[c] + offset;

    BandTaps taps;
    if (triggers)
        taps = { { essTap[0].data(), essTap[1].data() }, { harshTap[0].data(), harshTap[1].data() } };

    detector.process (in.data(), numChannels, numKeyChannels > 0 ? side.data() : nullptr, numKeyChannels, n, detectorSettings,
                      &publisher, triggers ? &taps : nullptr);

    // The dry path: the input, delayed by the latency.
    std::array<const float*, 2> dryIn {};
    for (size_t c = 0; c < (size_t) numChannels; ++c)
    {
        dryDelay[c].process (in[c], dry[c].data(), n, latency);
        dryIn[c] = dry[c].data();
    }

    if (triggers)
    {
        // One key path is heard on every channel; two on a mono bus as their mean.
        if (detector.numKeyPaths() == 2 && numChannels == 1)
            for (int i = 0; i < n; ++i)
            {
                essTap[0][(size_t) i] = 0.5f * (essTap[0][(size_t) i] + essTap[1][(size_t) i]);
                harshTap[0][(size_t) i] = 0.5f * (harshTap[0][(size_t) i] + harshTap[1][(size_t) i]);
            }
        for (size_t c = 0; c < (size_t) numChannels; ++c)
        {
            const auto path = detector.numKeyPaths() == 2 ? c : 0;
            essTriggerDelay[c].process (essTap[path].data(), essTrigger[c].data(), n, latency);
            harshTriggerDelay[c].process (harshTap[path].data(), harshTrigger[c].data(), n, latency);
        }
    }

    // The reductions, on the audio's clock: Normal reads the input hop of each delayed sample, Live
    // (no delay) the hop before it, the newest one that is complete.
    const auto start = samplePosition - latency - (latencyMode == LatencyMode::live ? Detector::hopSize : 0);

    // A lane fades in once what it removes is complete and, after a Stereo change, once everything
    // it removes is made of the new mode's reductions: the depths here, Spectral's frames a frame earlier.
    if (changing && other.ready())
    {
        const auto mode = other.config().stereo;
        const auto from = other.config().shape == Shape::spectral ? start - latency : start;
        if (mode == current.config().stereo || reductions.readsOnly (mode, from))
        {
            changing = false;
            laneFade.moveTo (true);
        }
    }

    // Back in after a change of latency, once the lane plays what it should.
    if (latencyMode == s.detector.latency && current.ready())
        latencyGain.moveTo (true);

    // Split designs its cut once per segment, for the segment's last sample: the depths run on to the
    // end of the segment that holds this chunk's last sample. Segments sit inside the hops of the
    // track each sample reads, so every depth of one is known once its first sample is
    // (src/dsp/shapes/split/NOTES.md, Modulation).
    constexpr auto mask = SplitFilter::interval - 1;
    DesignGrid grid;
    grid.phase = (int) (reductions.gridTime (start) & mask);
    if (const auto boundary = reductions.restartBoundary(); start < boundary && boundary - start < n + SplitFilter::interval)
    {
        grid.restartAt = (int) (boundary - start);
        grid.phaseAfter = (int) (boundary & mask);
    }
    const auto rendered = grid.restartAt < n ? n + SplitFilter::lookahead (grid.phaseAfter, n - grid.restartAt)
                                             : n + SplitFilter::lookahead (grid.phase, n, grid.restartAt);
    auto harshActive = false;
    for (const auto index : { activeLane, 1 - activeLane })
    {
        if (index != activeLane && ! otherRuns)
            continue;
        const auto& reader = laneReductions[(size_t) index];
        auto& ess = essDepth[(size_t) index];
        auto& harsh = harshDepth[(size_t) index];
        for (int r = 0; r < 2; ++r)
        {
            reader.render (start, rendered, ReductionTrack::essTrack (r), ess[(size_t) r].data());
            reader.render (start, rendered, ReductionTrack::harshTrack (r), harsh[(size_t) r].data());
            for (int i = 0; i < n && ! harshActive; ++i)
                harshActive = ! juce::exactlyEqual (harsh[(size_t) r][(size_t) i], 0.0f);
        }
    }

    LaneInput laneInput;
    laneInput.input = in.data();
    laneInput.dry = dryIn.data();
    laneInput.harsh = s.detector.harshOn || harshActive;
    laneInput.bands = bands;
    laneInput.position = samplePosition;
    laneInput.grid = grid;
    laneInput.spectral = { s.detector.detection, s.detector.rangeDb, s.detector.harshRangeDb, s.detector.harshOn,
                           detector.reductionDelaySamples() };

    const auto run = [&] (int index)
    {
        laneInput.essDepth = { essDepth[(size_t) index][0].data(), essDepth[(size_t) index][1].data() };
        laneInput.harshDepth = { harshDepth[(size_t) index][0].data(), harshDepth[(size_t) index][1].data() };
        laneInput.reductions = &laneReductions[(size_t) index];
        std::array<float*, 2> e { laneEss[(size_t) index][0].data(), laneEss[(size_t) index][1].data() };
        std::array<float*, 2> h { laneHarsh[(size_t) index][0].data(), laneHarsh[(size_t) index][1].data() };
        lanes[(size_t) index].process (laneInput, e.data(), h.data(), n);
    };
    run (activeLane);
    if (otherRuns)
        run (1 - activeLane);

    const auto lanesFading = laneFade.moving();
    const auto& ess = laneEss[(size_t) activeLane];
    const auto& harshOut = laneHarsh[(size_t) activeLane];
    const auto& essOther = laneEss[(size_t) (1 - activeLane)];
    const auto& harshOther = laneHarsh[(size_t) (1 - activeLane)];

    outputGain.setTarget (juce::Decibels::decibelsToGain (s.outputDb));
    bypassFade.moveTo (s.bypass);

    const auto steady = ! lanesFading && ! listenFade.moving() && ! bypassFade.moving() && ! outputGain.moving()
                        && latencyGain.atOne() && listen == Listen::output;

    if (steady)
    {
        if (bypassFade.atOne())
        {
            for (size_t c = 0; c < (size_t) numChannels; ++c)
                std::copy (dry[c].begin(), dry[c].begin() + n, channels[c] + offset);
        }
        else
        {
            const auto gain = outputGain.current();
            const auto unity = juce::exactlyEqual (gain, 1.0f);
            for (size_t c = 0; c < (size_t) numChannels; ++c)
            {
                auto* out = channels[c] + offset;
                for (size_t i = 0; i < (size_t) n; ++i)
                {
                    const auto removed = ess[c][i] + harshOut[c][i];
                    const auto v = juce::exactlyEqual (removed, 0.0f) ? dry[c][i] : dry[c][i] - removed;
                    out[i] = unity ? v : v * gain;
                }
            }
        }
    }
    else
    {
        for (size_t i = 0; i < (size_t) n; ++i)
        {
            const auto toOther = laneFade.next();
            const auto toListen = listenFade.next();
            const auto bypassed = bypassFade.next();
            const auto gain = outputGain.next();
            const auto level = latencyGain.next();

            for (size_t c = 0; c < (size_t) numChannels; ++c)
            {
                auto e = ess[c][i], h = harshOut[c][i];
                if (lanesFading)
                {
                    e += toOther * (essOther[c][i] - e);
                    h += toOther * (harshOther[c][i] - h);
                    removedMix[c][i] = e + h;
                }

                const auto d = dry[c][i];
                auto v = listened (listen, d, e, h, essTrigger[c][i], harshTrigger[c][i]);
                if (toListen < 1.0f)
                {
                    const auto before = listened (listenBefore, d, e, h, essTrigger[c][i], harshTrigger[c][i]);
                    v = before + toListen * (v - before);
                }
                if (! juce::exactlyEqual (gain, 1.0f))
                    v *= gain;

                auto out = bypassed <= 0.0f ? v : (bypassed >= 1.0f ? d : v + bypassed * (d - v));
                if (level < 1.0f)
                    out *= level;
                channels[c][(size_t) offset + i] = out;
            }
        }

        if (laneFade.atOne())
        {
            activeLane = 1 - activeLane;
            laneFade.jumpTo (false);
        }
    }

    if (! lanesFading)
        for (size_t c = 0; c < (size_t) numChannels; ++c)
            for (size_t i = 0; i < (size_t) n; ++i)
                removedMix[c][i] = ess[c][i] + harshOut[c][i];

    const std::array<const float*, 2> removedOut { removedMix[0].data(), removedMix[1].data() };
    const std::array<const float*, 2> out { channels[0] + offset, channels[numChannels - 1] + offset };
    displayFeed.audio (dryIn.data(), removedOut.data(), out.data(), numChannels, n, latency);
    const auto& running = runningLane();
    displayFeed.spectral (running.config().shape == Shape::spectral ? &running.spectralShape() : nullptr, s.detector.detection, n);

    samplePosition += n;
}

} // namespace halite::dsp
