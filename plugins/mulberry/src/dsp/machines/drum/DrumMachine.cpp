#include "dsp/machines/drum/DrumMachine.h"

#include "dsp/MachineInfo.h"

namespace mulberry::dsp
{

Transport::Mechanics DrumMachine::mechanics() const noexcept
{
    // The drum is its own flywheel: a speed change takes a second or so and barely overshoots.
    return { .naturalHz = 0.7, .damping = 0.85, .minSpeed = headArc / 8.0 };
}

TapeMachine::Setup DrumMachine::setup (const MachineSettings& s) const noexcept
{
    Setup st;

    for (size_t l = 0; l < 2; ++l)
    {
        const auto ms = std::clamp ((double) s.timeMs[l], (double) maps::minTimeMs, (double) maps::maxTimeMs);
        st.speed[l] = headArc / (ms * 0.001);
        st.distance[l] = headArc;
    }

    // Contact-free heads: a few microns of air between head and oxide.
    st.geometry = { .gap = 4.0e-6, .spacing = 6.0e-6, .thickness = 2.0e-6, .bumpWavelength = 40.0e-3 };
    st.lossSpeedMin = headArc / (physicalMaxMs * 0.001);
    st.lossSpeedMax = headArc / (physicalMinMs * 0.001);

    st.record.emphasisMicroseconds = 50.0;
    st.record.fieldScale = 2.0;
    st.record.ampBandwidthHz = 16000.0;
    st.record.ampHeadroom = 1.6;        // a valve stage: soft, early, asymmetric
    st.record.ampBias = 0.2;
    st.record.core = { .alpha = 0.01, .k = 0.5, .c = 0.9 };
    static const auto expansion = TapeRecordPath::expansion (st.record.core);
    st.record.coreExpansion = expansion;

    // Drum eccentricity once per turn, the motor pulley four times per turn, slow drift of the
    // mains-driven motor, a little bearing noise.
    st.wobble = { .capstanDepth = 0.00081, .capstanDiameter = circumference / std::numbers::pi,
                  .pinchDepth = 0.00029, .pinchDiameter = 0.25 * circumference / std::numbers::pi,
                  .spliceDepth = 0.0, .loopLength = 0.0,
                  .driftDepth = 0.00059, .driftHz = 0.4,
                  .scrapeDepth = 0.00005, .scrapeHz = 1500.0, .nominalSpeed = 0.8 };
    st.wobbleRandom = 0.35;
    st.hissDb = -64.0;
    return st;
}

} // namespace mulberry::dsp
