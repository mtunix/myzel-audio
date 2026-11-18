#pragma once

// Set-ups shared by the VCA's unit tests: the parameters, a prepared model per detector, levels
// on each detector's own reading, and a block runner whose blocks break where the settings change.

#include "Reference.h"
#include "dynamics/Harness.h"

#include "dsp/models/vca/VcaModel.h"

#include <functional>

namespace basalt::testing::vca
{

using Detector = models::VcaModel::Detector;

inline constexpr double rate = 48000.0;

inline const char* nameOf (Detector d) { return d == Detector::peak ? "Peak" : "RMS"; }

inline dynamics::ModelParams params (double t, double inverseRatio, double knee, double range, double attackMs, double releaseMs,
                                     bool autoRelease = false)
{
    dynamics::ModelParams p;
    p.curve = { t, inverseRatio, knee, range };
    p.attackMs = attackMs;
    p.releaseMs = releaseMs;
    p.autoRelease = autoRelease;
    return p;
}

inline models::VcaModel prepared (Detector d)
{
    models::VcaModel m (d);
    m.prepare (rate);
    return m;
}

/** The constant-magnitude carrier's amplitude for a level on the detector's own reading (RMS reads it 3.01 dB high). */
inline double carrierAmplitude (Detector d, double levelDb)
{
    return amplitudeFor (d == Detector::rms ? levelDb - reference::vca::carrierRmsOffsetDb : levelDb);
}

/**
    One channel of key through the model, the parameters for each block from `at` (the block's
    first sample). Blocks are `block` long, and also break wherever `changes` lists a sample, so
    the settings change on the same samples whatever the block size.
*/
inline Trace runScheduled (dynamics::DynamicsModel& m, const Signal& key, int block, const std::vector<int>& changes,
                           const std::function<dynamics::ModelParams (int)>& at, const Signal* right = nullptr,
                           Trace* rightOut = nullptr)
{
    Trace r (key.size()), r2 (right != nullptr ? key.size() : 0);
    size_t next = 0;

    for (int start = 0; start < (int) key.size();)
    {
        auto end = std::min (start + block, (int) key.size());
        while (next < changes.size() && changes[next] <= start)
            ++next;
        if (next < changes.size() && changes[next] < end)
            end = changes[next];

        dynamics::KeyBlock k;
        k.numChannels = right != nullptr ? 2 : 1;
        k.numSamples = end - start;
        k.channels[0] = key.data() + start;
        if (right != nullptr)
            k.channels[1] = right->data() + start;
        double* o[] { r.data() + start, right != nullptr ? r2.data() + start : nullptr };
        m.process (at (start), k, o);
        start = end;
    }

    if (rightOut != nullptr)
        *rightOut = r2;
    return r;
}

/** The largest |first| and |second difference| of a trace from `from`, dB per sample (squared). */
inline std::pair<double, double> roughness (const Trace& r, size_t from, size_t to = SIZE_MAX)
{
    auto step = 0.0, corner = 0.0;
    for (size_t i = std::max<size_t> (from, 2); i < std::min (to, r.size()); ++i)
    {
        step = std::max (step, std::abs (r[i] - r[i - 1]));
        corner = std::max (corner, std::abs (r[i] - 2.0 * r[i - 1] + r[i - 2]));
    }
    return { step, corner };
}

} // namespace basalt::testing::vca
