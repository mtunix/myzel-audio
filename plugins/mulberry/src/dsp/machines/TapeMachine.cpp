#include "dsp/machines/TapeMachine.h"

#include "dsp/MachineInfo.h"

namespace mulberry::dsp
{

void TapeMachine::prepare (double sampleRate, SlotMemory& memory)
{
    fs = sampleRate;

    for (size_t l = 0; l < 2; ++l)
    {
        auto& line = lines[l];
        line.track.attach (memory.audio[l].data(), memory.positionOffset[l].data(), memory.positionBase[l].data(), memory.capacity);
        line.record.prepare (fs);
        line.distance.prepare (fs, 250.0, 60.0, 1.0);
        line.hiss.setSeed (randomSeed * 40503u + 101u * (uint32_t) (l + 1));
        transports[l].prepare (fs, randomSeed * 7u + (uint32_t) l + 1u, mechanics());
    }

    // The loop's wear: a fixed map of bad spots along the tape, the same on every pass.
    myzel::dsp::Rng rng (randomSeed * 69069u + 1u);
    for (auto& cell : wearMap)
    {
        const auto u = rng.uniform();
        cell = u > 0.92f ? 0.3f + 0.7f * (u - 0.92f) / 0.08f : 0.0f;
    }

    reset();
}

void TapeMachine::reset() noexcept
{
    for (auto& line : lines)
    {
        line.track.restart();
        for (auto& h : line.heads)
            h.fill ({});
        line.ghost = {};
        line.record.reset();
        line.positions.fill (0.0);
        line.historyIndex = 0;
        line.hiss.reset();
    }
    for (auto& t : transports)
        t.reset();
    positions = { 0.0, 0.0 };
    dualMix = 0.0;
    lossSpeed = 0.0;
    lossCountdown = 0;
    snapNext = true;
}

float TapeMachine::dropout (double position) const noexcept
{
    if (current.wear <= 0.0 || current.loopLength <= 0.0)
        return 1.0f;

    // The fraction of a turn without a library floor (a read can sit before the start of the tape).
    const auto turns = position * inverseLoopLength;
    auto whole = (double) (long long) turns;
    if (whole > turns)
        whole -= 1.0;
    const auto x = (turns - whole) * (double) wearMap.size();
    const auto i = std::min ((size_t) x, wearMap.size() - 1);
    const auto j = i + 1 == wearMap.size() ? 0 : i + 1;
    const auto t = (float) (x - (double) i);
    // Smoothstep between cells: the spots fade in and out without a corner.
    const auto w = t * t * (3.0f - 2.0f * t);
    const auto depth = wearMap[i] + w * (wearMap[j] - wearMap[i]);
    return 1.0f - (float) current.wear * depth;
}

void TapeMachine::updateLosses (const Setup& s) noexcept
{
    for (size_t l = 0; l < 2; ++l)
    {
        const auto v = std::clamp (transports[l].getMechanicalSpeed(), s.lossSpeedMin, s.lossSpeedMax);
        lines[l].record.setLosses (TapeLosses::design (s.geometry, v, 2.0 * fs));
    }
}

void TapeMachine::process (LoopContext& loop, const std::array<ReadList, 2>& reads,
                           const MachineSettings& s, int n) noexcept
{
    current = setup (s);
    const auto& st = current;
    inverseLoopLength = st.loopLength > 0.0 ? 1.0 / st.loopLength : 0.0;

    // Transport: targets, wobble (one transport for both lines unless Dual).
    auto wobble = st.wobble;
    const auto scale = (double) maps::wobbleScale (s.wobble);
    wobble.capstanDepth *= scale;
    wobble.pinchDepth *= scale;
    wobble.spliceDepth *= scale;
    wobble.driftDepth *= scale;
    wobble.scrapeDepth *= scale;
    const auto shape = shapeGains (s.wobbleShape, (float) st.wobbleRandom);

    const auto dualStep = (double) n / (0.1 * fs);
    dualMix = snapNext ? (s.twoTransports ? 1.0 : 0.0) : std::clamp (dualMix + (s.twoTransports ? dualStep : -dualStep), 0.0, 1.0);

    for (size_t l = 0; l < 2; ++l)
    {
        auto& line = lines[l];
        transports[l].setWobble (wobble, shape, s.wobbleRate);
        line.record.setVoicing (st.record);

        if (snapNext)
        {
            transports[l].snap (st.speed[l]);
            line.distance.snap (st.distance[l]);
        }
        else
        {
            transports[l].setTarget (st.speed[l]);
            if (headsSlide())
            {
                // The head slides at up to half the tape speed: at most an octave down while it moves.
                line.distance.prepare (fs, 250.0, 60.0, 0.5 * st.speed[l] / fs);
                line.distance.setTarget (st.distance[l]);
            }
            else
            {
                line.distance.snap (st.distance[l]);
            }
        }
    }

    if (snapNext)
        updateLosses (st);

    const auto hissLevel = noiseGain (s.noise) * dbToGain ((float) st.hissDb);
    const auto feedbackSeconds = s.feedbackOffsetSamples / fs;
    const auto latency = lines[0].record.latencySamples();
    const auto ghostShare = 0.03f * (float) st.wear;
    const auto useDropouts = st.wear > 0.0 && st.loopLength > 0.0;
    const auto leak = st.leakSeconds > 0.0 ? 1.0 / st.leakSeconds : 0.0;

    // Brushes: pickups spread over an arc before the head's position, weights summing to 1.
    {
        float sum = 0.0f;
        for (int b = 0; b < st.brushes; ++b)
        {
            brushOffset[(size_t) b] = st.brushes > 1 ? (double) b / (st.brushes - 1) : 0.0;
            brushWeight[(size_t) b] = std::exp (-(float) st.brushTilt * (float) b);
            sum += brushWeight[(size_t) b];
        }
        for (int b = 0; b < st.brushes; ++b)
            brushWeight[(size_t) b] /= sum;
    }

    // Leakage over each pickup's dwell time, per block (dwell changes slowly).
    if (leak > 0.0)
        for (size_t l = 0; l < 2; ++l)
            for (int i = 0; i < reads[l].count; ++i)
            {
                const auto& r = reads[l].entries[(size_t) i].read;
                const auto dwell = (double) r.fraction * lines[l].distance.get() / transports[l].getMechanicalSpeed() + (double) r.offsetMs * 0.001;
                for (int b = 0; b < st.brushes; ++b)
                    leakGain[l][(size_t) i][(size_t) b] = (float) std::exp (-std::max (dwell, 0.0) * (1.0 - st.brushSpread * brushOffset[(size_t) b]) * leak);
            }

    if (snapNext)
        for (auto& line : lines)
            line.positions.fill (0.0);
    snapNext = false;

    for (int k = 0; k < n; ++k)
    {
        // The losses follow the speed: redesigned within 8 samples once it has moved by 0.03 %
        // (a glide), so their corners never step audibly.
        if ((k & 7) == 0)
        {
            const auto v = transports[0].getMechanicalSpeed();
            if (std::abs (v - lossSpeed) > 3.0e-4 * lossSpeed || --lossCountdown <= 0)
            {
                updateLosses (st);
                lossSpeed = v;
                lossCountdown = 64;
            }
        }

        // Move the tape. The right line rides the left transport unless Dual (blended in 100 ms).
        const auto v0 = transports[0].advance();
        if (dualMix < 1.0)
            transports[1].follow (transports[0]);
        const auto own = transports[1].advance();
        const std::array<double, 2> speed { v0, v0 + dualMix * (own - v0) };

        std::array<std::array<float, numBuses>, 2> bus {};

        for (size_t l = 0; l < 2; ++l)
        {
            auto& line = lines[l];
            positions[l] += speed[l] / fs;
            const auto position = positions[l];
            const auto base = line.distance.next();
            // Time offsets (Feel, the loop's latencies) become tape at the speed the tape runs now,
            // so a speed change moves them smoothly.
            const auto metresPerSecond = transports[l].getMechanicalSpeed();
            const auto& list = reads[l];
            const auto readable = line.track.span();

            for (int i = 0; i < list.count; ++i)
            {
                const auto& e = list.entries[(size_t) i];
                const auto gain = planGain (e, k);
                if (gain <= 0.0f)
                    continue;

                auto distance = (double) e.read.fraction * base + (double) e.read.offsetMs * 0.001 * metresPerSecond;
                if (e.feedback)
                    distance += feedbackSeconds * metresPerSecond;
                distance = std::max (distance, 0.0);

                // Many output heads (Pattern) read the main pickup only; feedback keeps the cluster.
                const auto pickups = (e.feedback || list.count <= 8) ? st.brushes : 1;
                const auto fast = ! e.feedback && list.count > 8;
                float v = 0.0f;
                if (pickups == 1)
                {
                    const auto target = position - distance;
                    v = line.track.read (line.heads[(size_t) i][0], target, fast, readable);
                    if (useDropouts)
                    {
                        // One transport (not Dual): the right line's read sits on the same spot as the left's.
                        auto& memo = dropoutMemo[(size_t) i];
                        if (l == 0 || ! (! (memo.first < target) && ! (memo.first > target)))
                            memo = { target, dropout (target) };
                        v *= memo.second;
                    }
                    if (leak > 0.0)
                        v *= leakGain[l][(size_t) i][0];
                }
                else
                {
                    for (int b = 0; b < pickups; ++b)
                    {
                        const auto d = distance * (1.0 - st.brushSpread * brushOffset[(size_t) b]);
                        const auto target = position - d;
                        auto x = line.track.read (line.heads[(size_t) i][(size_t) b], target, fast, readable)
                               * (pickups > 1 ? brushWeight[(size_t) b] : 1.0f);
                        if (useDropouts)
                            x *= dropout (target);
                        if (leak > 0.0)
                            x *= leakGain[l][(size_t) i][(size_t) b];
                        v += x;
                    }
                }
                v *= gain;

                const auto weights = weightsAt (e, k);
                if (e.feedback)
                {
                    for (size_t b = 0; b < (size_t) numBuses; ++b)
                        bus[l][b] += v * weights[b];
                }
                else
                {
                    // An output read feeds nothing back (TapPlan): only the two output buses.
                    bus[l][outLeft] += v * weights[outLeft];
                    bus[l][outRight] += v * weights[outRight];
                }
            }
        }

        std::array<float, numBuses> sum {};
        for (size_t l = 0; l < 2; ++l)
            for (size_t b = 0; b < (size_t) numBuses; ++b)
                sum[b] += bus[l][b];

        float rec0, rec1;
        loop.step (k, sum[feedback0], sum[feedback1], sum[outLeft], sum[outRight], rec0, rec1);
        const std::array<float, 2> rec { rec0, rec1 };

        std::array<float, 2> x = rec;
        for (size_t l = 0; l < 2; ++l)
        {
            // A weak erase head leaves a ghost of the previous pass under the new recording.
            auto& line = lines[l];
            if (ghostShare > 0.0f && st.loopLength > 0.0)
                x[l] = (1.0f - ghostShare) * x[l] + ghostShare * line.track.read (line.ghost, positions[l] - st.loopLength);
        }

        std::array<float, 2> y;
        TapeRecordPath::processPair (lines[0].record, lines[1].record, x, hissLevel, lines[0].hiss, lines[1].hiss, y);

        for (size_t l = 0; l < 2; ++l)
        {
            auto& line = lines[l];

            // The record path delays by its latency: the sample belongs where the tape was then.
            line.positions[(size_t) line.historyIndex] = positions[l];
            const auto back = std::clamp (latency, 0.0, (double) historyLength - 2.0);
            const auto whole = (int) back;
            const auto frac = back - whole;
            const auto at = [&line] (int ago) { return line.positions[(size_t) ((line.historyIndex - ago + historyLength) % historyLength)]; };
            const auto written = line.track.samplesWritten() > historyLength ? at (whole) + frac * (at (whole + 1) - at (whole))
                                                                             : positions[l] - back * speed[l] / fs;
            line.historyIndex = (line.historyIndex + 1) % historyLength;
            line.track.write (y[l], written);
        }
    }

    for (auto& line : lines)
        line.record.flush();
}

double TapeMachine::predictedPassGain (double hz, const MachineSettings& settings) const noexcept
{
    const auto st = setup (settings);
    const auto v = std::clamp (st.speed[0], st.lossSpeedMin, st.lossSpeedMax);
    const auto losses = TapeLosses::design (st.geometry, v, 2.0 * fs);
    TapeRecordPath path;
    path.prepare (fs);
    path.setVoicing (st.record);
    const auto leak = st.leakSeconds > 0.0 ? std::exp (-(st.distance[0] / std::max (st.speed[0], 1.0e-9)) / st.leakSeconds) : 1.0;
    return std::abs (TapeLosses::response (losses, hz, 2.0 * fs)) * std::abs (path.oversamplerResponse (hz / fs))
         * std::abs (path.linearResponse (hz)) * leak
         / (st.record.magnetic ? std::max (st.record.coreExpansion, 1.0) : 1.0);
}

float TapeMachine::currentTimeMs (int line) const noexcept
{
    const auto l = (size_t) std::clamp (line, 0, 1);
    const auto speed = std::max (transports[l].getMechanicalSpeed(), 1.0e-6);
    return (float) (lines[l].distance.get() / speed * 1000.0);
}

} // namespace mulberry::dsp
