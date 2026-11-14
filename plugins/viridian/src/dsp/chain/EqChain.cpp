#include "dsp/chain/EqChain.h"

#include "dsp/models/ModelFactory.h"

#include "common/dsp/FloatCompare.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace viridian::dsp
{

namespace
{
    /** A delay line of `length` samples (0: none) over a ring of length + 1. */
    double delayed (std::vector<double>& ring, int& write, int length, double x) noexcept
    {
        if (length <= 0)
            return x;
        const auto size = (int) ring.size();
        ring[(size_t) write] = x;
        auto read = write - length;
        if (read < 0)
            read += size;
        write = write + 1 == size ? 0 : write + 1;
        return ring[(size_t) read];
    }
} // namespace

void ChainBandLimit::reset() noexcept
{
    for (auto& channel : state)
        for (auto& s : channel)
            s[0] = s[1] = 0.0;
}

void ChainBandLimit::process (double* const* channels, int numChannels, int numSamples) noexcept
{
    if (numChannels >= 2)
    {
        double* l = channels[0];
        double* r = channels[1];
        for (int k = 0; k < numSamples; ++k)
        {
            double a = l[k], b = r[k];
            for (int i = 0; i < sections; ++i)
            {
                const auto& c = sos[i];
                auto& s = state[0][i];
                auto& t = state[1][i];
                const double ya = c[0] * a + s[0], yb = c[0] * b + t[0];
                s[0] = c[1] * a - c[3] * ya + s[1];
                t[0] = c[1] * b - c[3] * yb + t[1];
                s[1] = c[2] * a - c[4] * ya;
                t[1] = c[2] * b - c[4] * yb;
                a = ya;
                b = yb;
            }
            l[k] = a;
            r[k] = b;
        }
    }
    else if (numChannels == 1)
    {
        double* x = channels[0];
        for (int k = 0; k < numSamples; ++k)
        {
            double v = x[k];
            for (int i = 0; i < sections; ++i)
            {
                const auto& c = sos[i];
                auto& s = state[0][i];
                const double y = c[0] * v + s[0];
                s[0] = c[1] * v - c[3] * y + s[1];
                s[1] = c[2] * v - c[4] * y;
                v = y;
            }
            x[k] = v;
        }
    }
    for (auto& channel : state)
        for (auto& s : channel)
            for (auto& v : s)
                if (std::abs (v) < 1.0e-30)
                    v = 0.0;
}

std::complex<double> ChainBandLimit::response (double normalised) noexcept
{
    const auto z1 = std::polar (1.0, -2.0 * std::numbers::pi * normalised);
    const auto z2 = z1 * z1;
    std::complex<double> h { 1.0, 0.0 };
    for (const auto& c : sos)
        h *= (c[0] + c[1] * z1 + c[2] * z2) / (1.0 + c[3] * z1 + c[4] * z2);
    return h;
}

EqChain::EqChain()
{
    for (int m = 0; m < numModels; ++m)
        runners[(size_t) m].model = createModel (static_cast<Model> (m));
}

EqChain::~EqChain() = default;

void EqChain::prepare (double newSampleRate, int maximumBlockSize, int numChannels, const ChainSettings& current)
{
    sampleRate = newSampleRate;
    maxBlock = std::max (1, maximumBlockSize);
    numChannelsPrepared = std::clamp (numChannels, 1, maxChannels);

    // Each model: its stages prepared at their rates, one oversampler pair per oversampled run.
    double slowest = 0.0;
    for (auto& r : runners)
    {
        r.model->prepare (sampleRate, maxBlock, numChannelsPrepared);
        int runs = 0;
        double stageLatency = 0.0;
        auto previous = Stage::Rate::host;
        for (auto* stage : r.model->stages())
        {
            const auto oversampledStage = stage->rate() == Stage::Rate::oversampled;
            stage->prepare (oversampledStage ? 2.0 * sampleRate : sampleRate, oversampledStage ? 2 * maxBlock : maxBlock,
                            numChannelsPrepared);
            if (oversampledStage && previous == Stage::Rate::host)
                ++runs;
            previous = stage->rate();
            stageLatency += stage->latencySamples();
        }
        r.oversamplers.assign ((size_t) runs, {});
        r.bandLimits.assign (runs > 0 ? (size_t) runs + 1 : 0, {});
        r.latency = stageLatency + (runs > 0 ? runs * r.oversamplers.front()[0].latencySamples() + (runs + 1) * ChainBandLimit::delaySamples : 0.0);
        slowest = std::max (slowest, r.latency);
    }

    latencySamples = (int) std::lround (slowest);
    for (auto& r : runners)
    {
        r.pad = std::max (0, latencySamples - (int) std::lround (r.latency));
        for (auto& d : r.delay)
            d.assign ((size_t) r.pad + 1, 0.0);
        r.delayWrite = 0;
        r.driveDelay.assign ((size_t) std::lround (r.latency) + 1, 1.0);
        r.driveWrite = 0;
    }

    for (auto* buffers : { &driven, &wetA, &wetB })
        for (auto& b : *buffers)
            b.assign ((size_t) maxBlock, 0.0);
    drives.assign ((size_t) maxBlock, 1.0);
    lagged.assign ((size_t) maxBlock, 1.0);
    for (auto& b : oversampled)
        b.assign ((size_t) (2 * maxBlock), 0.0);
    for (auto& d : dryDelay)
        d.assign ((size_t) latencySamples + 1, 0.0);
    for (auto& f : dc)
        f.prepare (sampleRate, dcBlockHz);

    fadeLength = std::max (1, (int) std::lround (transitionSeconds * sampleRate));
    warmLength = std::max (1, (int) std::lround (warmUpSeconds * sampleRate));
    drive.prepare (sampleRate, driveSeconds);
    output.prepare (sampleRate, outputSeconds);
    bypass.reset (sampleRate, bypassSeconds);

    rebuild (current);
}

void EqChain::rebuild (const ChainSettings& current)
{
    // Only the model the settings select is designed; another one is built when it is selected.
    activeModel = std::clamp ((int) current.model, 0, numModels - 1);
    auto& active = runners[(size_t) activeModel];
    active.model->setSettings (settingsFor (static_cast<Model> (activeModel), current));
    active.model->build();
    drive.jump (std::pow (10.0, std::clamp ((double) current.driveDb, -12.0, 24.0) / 20.0));
    output.jump (std::pow (10.0, std::clamp ((double) current.outputDb, -24.0, 24.0) / 20.0));
    bypass.setCurrentAndTargetValue (current.bypass ? 1.0 : 0.0);
    reset();
}

void EqChain::reset() noexcept
{
    // Cheap enough for the audio thread: states only, no design. A change in progress restarts
    // from the next callback; a running fade completes.
    fadeRemaining = 0;
    fadingModel = -1;
    incomingModel = -1;
    drive.jump (drive.target);
    output.jump (output.target);
    bypass.setCurrentAndTargetValue (bypass.getTargetValue());
    for (auto& r : runners)
    {
        restart (r, drive.value);
        r.model->reset();
    }
    for (auto& d : dryDelay)
        std::fill (d.begin(), d.end(), 0.0);
    dryWrite = 0;
    for (auto& f : dc)
        f.reset();
}

ModelSettings EqChain::settingsFor (Model m, const ChainSettings& s) const noexcept
{
    ModelSettings ms;
    ms.eq = resolveEq (m, s.alt, s.positions);
    ms.eqIn = s.eqIn;
    ms.stereo = s.stereo;
    ms.tolerance = std::clamp ((double) s.tolerance, 0.0, 1.0);
    ms.noise = std::clamp ((double) s.noise, 0.0, 1.0);
    return ms;
}

void EqChain::runModel (Runner& r, const ModelSettings& settings, const double* const* in, int numChannels, int numSamples,
                        double* const* out) noexcept
{
    r.model->setSettings (settings);
    for (int c = 0; c < numChannels; ++c)
        std::copy (in[c], in[c] + numSamples, out[c]);

    StageContext host { sampleRate };
    StageContext twice { 2.0 * sampleRate };
    std::array<double*, maxChannels> up {};
    for (int c = 0; c < maxChannels; ++c)
        up[(size_t) c] = oversampled[(size_t) c].data();

    const auto stages = r.model->stages();
    size_t i = 0;
    int run = 0;
    while (i < stages.size())
    {
        if (stages[i]->rate() == Stage::Rate::host)
        {
            stages[i]->process (out, numChannels, numSamples, host);
            ++i;
            continue;
        }
        if (run == 0)
            r.bandLimits.front().process (out, numChannels, numSamples);
        auto& pair = r.oversamplers[(size_t) run++];
        for (int c = 0; c < numChannels; ++c)
            for (int k = 0; k < numSamples; ++k)
                pair[(size_t) c].upsample (out[c][k], up[(size_t) c][2 * k], up[(size_t) c][2 * k + 1]);
        for (; i < stages.size() && stages[i]->rate() == Stage::Rate::oversampled; ++i)
            stages[i]->process (up.data(), numChannels, 2 * numSamples, twice);
        for (int c = 0; c < numChannels; ++c)
            for (int k = 0; k < numSamples; ++k)
                out[c][k] = pair[(size_t) c].downsample (up[(size_t) c][2 * k], up[(size_t) c][2 * k + 1]);
        r.bandLimits[(size_t) run].process (out, numChannels, numSamples);
    }
}

void EqChain::compensate (Runner& r, int numChannels, int numSamples, double* const* io, const double* driveGains) noexcept
{
    // The inverse of the stages' small-signal gain and of the Drive that went in the model's own
    // delay earlier (a gliding Drive would otherwise wobble the level), then the pad to the
    // chain's latency.
    const auto inverse = 1.0 / r.model->smallSignalGain();
    const auto lag = (int) r.driveDelay.size() - 1;
    for (int i = 0; i < numSamples; ++i)
        lagged[(size_t) i] = inverse / delayed (r.driveDelay, r.driveWrite, lag, driveGains[i]);
    int write = r.delayWrite;
    for (int c = 0; c < numChannels; ++c)
    {
        write = r.delayWrite;
        for (int i = 0; i < numSamples; ++i)
            io[c][i] = delayed (r.delay[(size_t) c], write, r.pad, io[c][i] * lagged[(size_t) i]);
    }
    r.delayWrite = write;
}

void EqChain::GainGlide::prepare (double sampleRate, double seconds) noexcept
{
    coefficient = 1.0 - std::exp (-1.0 / (seconds * sampleRate));
}

void EqChain::GainGlide::jump (double gain) noexcept
{
    target = value = gain;
    targetLog = first = second = std::log (gain);
    moving = false;
}

double EqChain::GainGlide::next (double targetGain) noexcept
{
    if (! myzel::dsp::isExactly (targetGain, target))
    {
        target = targetGain;
        targetLog = std::log (targetGain);
        moving = true;
    }
    if (! moving)
        return value;
    first += (targetLog - first) * coefficient;
    second += (first - second) * coefficient;
    if (std::abs (targetLog - second) < 1.0e-9 && std::abs (targetLog - first) < 1.0e-9)
    {
        jump (target);
        return value;
    }
    value = std::exp (second);
    return value;
}

void EqChain::restart (Runner& r, double driveGain) noexcept
{
    for (auto& pair : r.oversamplers)
        for (auto& o : pair)
            o.reset();
    for (auto& b : r.bandLimits)
        b.reset();
    for (auto& d : r.delay)
        std::fill (d.begin(), d.end(), 0.0);
    r.delayWrite = 0;
    std::fill (r.driveDelay.begin(), r.driveDelay.end(), driveGain);
    r.driveWrite = 0;
}

void EqChain::process (float* const* channels, int numChannels, int numSamples, const ChainSettings& s) noexcept
{
    numChannels = std::min (numChannels, numChannelsPrepared);
    if (numSamples <= 0 || numChannels <= 0 || maxBlock == 0)
        return;
    for (int done = 0; done < numSamples; done += maxBlock)
    {
        std::array<float*, maxChannels> at {};
        for (int c = 0; c < numChannels; ++c)
            at[(size_t) c] = channels[c] + done;
        processChunk (at.data(), numChannels, std::min (maxBlock, numSamples - done), s);
    }
}

void EqChain::processChunk (float* const* channels, int numChannels, int n, const ChainSettings& s) noexcept
{
    const auto modelIndex = std::clamp ((int) s.model, 0, numModels - 1);
    const auto driveGain = std::pow (10.0, std::clamp ((double) s.driveDb, -12.0, 24.0) / 20.0);
    const auto outputGain = std::pow (10.0, std::clamp ((double) s.outputDb, -24.0, 24.0) / 20.0);
    bypass.setTargetValue (s.bypass ? 1.0 : 0.0);

    for (int i = 0; i < n; ++i)
    {
        const auto d = drive.next (driveGain);
        drives[(size_t) i] = d;
        for (int c = 0; c < numChannels; ++c)
            driven[(size_t) c][(size_t) i] = (double) channels[c][i] * d;
    }

    // A change's steps (its ready checks, the end of its warm-up and of its fade) split the block.
    for (int at = 0; at < n;)
    {
        followModelKnob (modelIndex, s, drives[(size_t) at]);
        int length = n - at;
        if (incomingModel >= 0)
            length = std::min (length, incomingReady ? warmRemaining : readyCheckRemaining);
        else if (fadeRemaining > 0)
            length = std::min (length, fadeRemaining);
        runModels (numChannels, at, length, s);
        at += length;
    }

    for (int i = 0; i < n; ++i)
    {
        const auto g = output.next (outputGain);
        const auto b = bypass.getNextValue();
        int write = dryWrite;
        for (int c = 0; c < numChannels; ++c)
        {
            write = dryWrite;
            const auto dry = delayed (dryDelay[(size_t) c], write, latencySamples, (double) channels[c][i]);
            const auto wet = dc[(size_t) c].process (wetA[(size_t) c][(size_t) i]) * g;
            channels[c][i] = (float) (wet + (dry - wet) * b);
        }
        dryWrite = write;
    }
}

void EqChain::followModelKnob (int modelIndex, const ChainSettings& s, double driveGain) noexcept
{
    // A running crossfade finishes first: dropping a half-faded model would click.
    if (fadeRemaining > 0 || modelIndex == incomingModel)
        return;
    if (modelIndex == activeModel)
    {
        incomingModel = -1;     // back before the fade: the incoming model was never heard
        return;
    }
    incomingModel = modelIndex;
    auto& r = runners[(size_t) incomingModel];
    r.model->setSettings (settingsFor (static_cast<Model> (incomingModel), s));
    r.model->beginBuild();
    restart (r, driveGain);
    incomingReady = r.model->ready();
    readyCheckRemaining = readyCheckSamples;
    warmRemaining = warmLength;
}

void EqChain::runModels (int numChannels, int from, int length, const ChainSettings& s) noexcept
{
    // Each running model: its stages, the inverse of Drive and of its stages' gain, its pad.
    auto runAndCompensate = [&] (int index, std::array<std::vector<double>, maxChannels>& wet)
    {
        auto& r = runners[(size_t) index];
        const std::array<const double*, maxChannels> in { driven[0].data() + from, driven[1].data() + from };
        const std::array<double*, maxChannels> out { wet[0].data() + from, wet[1].data() + from };
        runModel (r, settingsFor (static_cast<Model> (index), s), in.data(), numChannels, length, out.data());
        compensate (r, numChannels, length, out.data(), drives.data() + from);
    };

    runAndCompensate (activeModel, wetA);
    if (incomingModel >= 0)
    {
        // Unheard until it is ready and has run warmUpSeconds on the input since.
        runAndCompensate (incomingModel, wetB);
        if (! incomingReady)
        {
            readyCheckRemaining -= length;
            if (readyCheckRemaining == 0)
            {
                incomingReady = runners[(size_t) incomingModel].model->ready();
                readyCheckRemaining = readyCheckSamples;
            }
            return;
        }
        warmRemaining -= length;
        if (warmRemaining == 0)
        {
            fadingModel = activeModel;
            activeModel = incomingModel;
            incomingModel = -1;
            fadeRemaining = fadeLength;
        }
        return;
    }
    if (fadeRemaining == 0)
        return;

    runAndCompensate (fadingModel, wetB);
    for (int i = from; i < from + length; ++i)
    {
        // Amplitude-complementary (Settled 17: both models carry the same signal) along a raised
        // cosine: no slope at either end.
        const auto t = 1.0 - (double) (fadeRemaining - (i - from)) / (double) fadeLength;
        const auto p = 0.5 - 0.5 * std::cos (std::numbers::pi * t);
        for (int c = 0; c < numChannels; ++c)
            wetA[(size_t) c][(size_t) i] = wetB[(size_t) c][(size_t) i] + p * (wetA[(size_t) c][(size_t) i] - wetB[(size_t) c][(size_t) i]);
    }
    fadeRemaining -= length;
    if (fadeRemaining == 0)
        fadingModel = -1;
}

} // namespace viridian::dsp
