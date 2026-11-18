#include "dsp/chain/DynamicsLane.h"

#include "common/dsp/FloatCompare.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace basalt::dsp
{

using namespace dynamics;

int warmUpSamples (const DynamicsModel& model, const ModelParams& p, double lookahead, double sampleRate) noexcept
{
    return (int) std::ceil (model.settleMs (p) * 0.001 * sampleRate) + (int) std::ceil (lookahead) + 1;
}

void DynamicsLane::install (int engine, std::unique_ptr<DynamicsModel> model)
{
    models[(size_t) engine] = std::move (model);
}

void DynamicsLane::prepare (double sampleRate)
{
    rate = sampleRate;
    for (auto& m : models)
        if (m != nullptr)
            m->prepare (sampleRate);

    fade.setLength (std::max (1, (int) std::lround (fadeSeconds * sampleRate)));
    start (lane);
}

void DynamicsLane::start (const LaneConfig& c) noexcept
{
    lane = c;
    stereoLayout = c.layout();
    numDetectors = stereoLayout.detectorChannels;
    heard = models[(size_t) c.engine] != nullptr ? c.engine : 0;
    incoming = warming = -1;
    warmUpLeft = 0;
    fade.jumpTo (false);

    for (auto& m : models)
        if (m != nullptr)
            m->reset();

    last = {};
}

void DynamicsLane::seed (const double* reductionDb) noexcept
{
    models[(size_t) heard]->seed (reductionDb, numDetectors);
    for (int c = 0; c < numDetectors; ++c)
        last[(size_t) c] = reductionDb[c];
}

ModelParams DynamicsLane::paramsFor (const ModelParams& p) const noexcept
{
    auto q = p;
    q.linked = stereoLayout.linked;
    return q;
}

KeyBlock DynamicsLane::keyBlock (const double* lookahead, int n) const noexcept
{
    KeyBlock k;
    for (int c = 0; c < maxDetectorChannels; ++c)
        k.channels[(size_t) c] = keys[(size_t) c].data();
    k.numChannels = numDetectors;
    k.numSamples = n;
    k.lookahead = lookahead;
    return k;
}

void DynamicsLane::follow (const ModelParams& params, const double* lookahead, int n) noexcept
{
    models[(size_t) heard]->follow (paramsFor (params), keyBlock (lookahead, n));
}

int DynamicsLane::samplesToDecision() const noexcept
{
    if (incoming >= 0)
        return std::max (1, fade.remaining());
    if (warming >= 0)
        return std::max (1, warmUpLeft);
    return INT_MAX;
}

void DynamicsLane::request (int engine, const ModelParams& params, double lookahead) noexcept
{
    if (incoming >= 0)
        return;

    const auto wanted = engine >= 0 && engine < maxEngines && models[(size_t) engine] != nullptr ? engine : heard;

    if (wanted == heard)
    {
        warming = -1;
    }
    else if (wanted != warming)
    {
        // Only the heard engine runs, so a new one starts from rest and follows the key unheard
        // until its key-only states read it (dynamics/NOTES.md, The chain).
        warming = wanted;
        models[(size_t) warming]->reset();
        warmUpLeft = warmUpSamples (*models[(size_t) warming], paramsFor (params), lookahead, rate);
    }
    else if (warmUpLeft <= 0)
    {
        incoming = warming;
        warming = -1;
        models[(size_t) incoming]->seed (last.data(), numDetectors);
        fade.jumpTo (false);
        fade.moveTo (true);
    }
}

void DynamicsLane::process (const ModelParams& params, const double* lookahead, int n, int engine) noexcept
{
    request (engine, params, lookahead != nullptr ? lookahead[0] : 0.0);
    process (params, lookahead, n);
}

void DynamicsLane::process (const ModelParams& params, const double* lookahead, int n) noexcept
{
    using myzel::dsp::isExactly;

    const auto p = paramsFor (params);
    const auto key = keyBlock (lookahead, n);

    std::array<double*, maxDetectorChannels> out { applied[0].data(), applied[1].data() };
    models[(size_t) heard]->process (p, key, out.data());

    if (incoming >= 0)
    {
        std::array<double*, maxDetectorChannels> in { incomingReductions[0].data(), incomingReductions[1].data() };
        models[(size_t) incoming]->process (p, key, in.data());

        for (int i = 0; i < n; ++i)
        {
            const auto w = (double) fade.next();
            for (int c = 0; c < numDetectors; ++c)
            {
                const auto a = out[(size_t) c][i], b = in[(size_t) c][i];
                if (! isExactly (a, b))
                    out[(size_t) c][i] = a + w * (b - a);
            }
        }

        if (fade.atOne())
        {
            heard = incoming;
            incoming = -1;
            fade.jumpTo (false);
        }
    }
    else if (warming >= 0)
    {
        models[(size_t) warming]->follow (p, key);
        warmUpLeft -= n;
    }

    for (int c = 0; c < numDetectors; ++c)
    {
        const auto* r = applied[(size_t) c].data();
        auto* g = gain[(size_t) c].data();
        for (int i = 0; i < n; ++i)
            g[i] = gainFor (r[i]);
        last[(size_t) c] = r[n - 1];
    }
}

} // namespace basalt::dsp
