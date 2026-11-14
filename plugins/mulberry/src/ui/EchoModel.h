#pragma once

#include "dsp/MachineInfo.h"
#include "dsp/TapPlan.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mulberry::ui
{

/**
    What the echo map draws, from the parameters alone (no audio): the repeats of a click, their
    times, levels and sides, through the tap plan. Pure functions, as Cerulean's TailModel.
*/
struct EchoShape
{
    Machine machine = Machine::reel;
    dsp::TapPlanInput plan;
    std::array<float, 2> timeMs { 375.0f, 375.0f };
    float feedback = 0.35f;
    bool synced = true;
    double bpm = 120.0;
    bool hold = false;
    bool bypassed = false;
    int foldedOctaves = 0;

    bool operator== (const EchoShape&) const = default;
};

struct EchoEvent
{
    float timeMs = 0.0f;
    float left = 0.0f, right = 0.0f;    // linear level on each side
};

/** A machine's loss per pass at 1 kHz (dB), for the picture: Feedback times this per repeat. */
inline float nominalPassDb (Machine m) noexcept
{
    switch (m)
    {
        case Machine::clean:  return 0.0f;
        case Machine::rack:   return -0.05f;
        case Machine::bucket: return -0.4f;
        case Machine::oilCan: return -0.5f;
        case Machine::loop:   return -0.6f;
        case Machine::reel:   return -0.4f;
        case Machine::drum:   return -0.5f;
    }
    return 0.0f;
}

/** The repeats of a click into both inputs, up to spanMs, strongest first never below -60 dB. */
inline std::vector<EchoEvent> echoEvents (const EchoShape& s, float spanMs, int maxEvents = 160)
{
    const auto plan = dsp::compileTapPlan (s.plan);
    const auto loop = std::min (s.feedback, 1.2f) * std::pow (10.0f, nominalPassDb (s.machine) / 20.0f);

    struct Pulse { int line; float t, amplitude; };
    std::vector<Pulse> pulses;
    std::vector<EchoEvent> events;

    for (int l = 0; l < 2; ++l)
    {
        const auto a = plan.lines[(size_t) l].inputFromLeft + plan.lines[(size_t) l].inputFromRight;
        if (a > 0.0f)
            pulses.push_back ({ l, 0.0f, a });
    }

    for (size_t next = 0; next < pulses.size() && (int) events.size() < maxEvents && pulses.size() < 600; ++next)
    {
        const auto p = pulses[next];
        const auto& line = plan.lines[(size_t) p.line];
        const auto time = s.timeMs[(size_t) (plan.twoTransports ? p.line : 0)];

        for (int i = 0; i < line.numReads; ++i)
        {
            const auto& r = line.reads[(size_t) i];
            const auto t = p.t + r.fraction * time + r.offsetMs;
            if (t > spanMs)
                continue;

            const auto l = p.amplitude * r.weights[dsp::outLeft], rr = p.amplitude * r.weights[dsp::outRight];
            if (l + rr > 0.001f)
                events.push_back ({ t, l, rr });

            for (int d = 0; d < 2; ++d)
            {
                const auto w = r.weights[(size_t) (d == 0 ? dsp::feedback0 : dsp::feedback1)];
                const auto a = p.amplitude * w * loop;
                if (w > 0.0f && a > 0.001f)
                    pulses.push_back ({ d, t, a });
            }
        }
    }

    std::sort (events.begin(), events.end(), [] (const EchoEvent& a, const EchoEvent& b) { return a.timeMs < b.timeMs; });
    return events;
}

/** A time span (ms) that shows the first few repeats: a whole number of beats when synced. */
inline float chooseSpan (const EchoShape& s)
{
    const auto longest = std::max (s.timeMs[0], s.plan.mode == Mode::dual ? s.timeMs[1] : s.timeMs[0]);
    auto span = std::clamp (longest * (s.plan.mode == Mode::pattern ? 2.2f : 4.4f), 200.0f, 12000.0f);
    if (s.synced && s.bpm > 0.0)
    {
        const auto beat = (float) (60000.0 / s.bpm);
        span = std::max (beat, std::ceil (span / beat) * beat);
    }
    return span;
}

} // namespace mulberry::ui
