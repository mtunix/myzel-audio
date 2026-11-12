#include "dsp/machines/loop/LoopMachine.h"

#include "dsp/MachineInfo.h"

namespace mulberry::dsp
{

Transport::Mechanics LoopMachine::mechanics() const noexcept
{
    // A small capstan motor and flywheel: a speed change takes about half a second, overshooting
    // a little, so the echoes bend in and out.
    return { .naturalHz = 1.2, .damping = 0.75, .minSpeed = headSpacing / 6.0 };
}

TapeMachine::Setup LoopMachine::setup (const MachineSettings& s) const noexcept
{
    Setup st;
    const auto wear = (double) character::wear (s.paramB);

    for (size_t l = 0; l < 2; ++l)
    {
        const auto ms = std::clamp ((double) s.timeMs[l], (double) maps::minTimeMs, (double) maps::maxTimeMs);
        st.speed[l] = headSpacing / (ms * 0.001);
        st.distance[l] = headSpacing;
    }

    // Echo-unit heads on a worn loop: a wide-ish gap, some lift, thin effective record depth.
    st.geometry = { .gap = 1.2e-6, .spacing = (0.4 + 1.0 * wear) * 1.0e-6, .thickness = 1.0e-6, .bumpWavelength = 5.4e-3 };
    st.lossSpeedMin = headSpacing / (physicalMaxMs * 0.001);
    st.lossSpeedMax = headSpacing / (physicalMinMs * 0.001);

    st.record.emphasisMicroseconds = 70.0;
    st.record.fieldScale = 2.0;
    st.record.ampBandwidthHz = 12000.0;
    st.record.ampHeadroom = 1.8;
    st.record.ampBias = 0.2;
    st.record.core = { .alpha = 0.01, .k = 0.55, .c = 0.88 };
    static const auto expansion = TapeRecordPath::expansion (st.record.core);
    st.record.coreExpansion = expansion;

    // Transport at nominal Wobble (AES6 weighted ~0.25 %): a small capstan and pinch roller, the
    // splice once per loop, motor hunting, scrape. Wear adds wow.
    const auto worn = 1.0 + wear;
    st.wobble = { .capstanDepth = 0.00056 * worn, .capstanDiameter = 0.006,
                  .pinchDepth = 0.000315 * worn, .pinchDiameter = 0.014,
                  .spliceDepth = 0.0014, .loopLength = loopLength,
                  .driftDepth = 0.00077 * worn, .driftHz = 0.7,
                  .scrapeDepth = 0.000105, .scrapeHz = 1800.0, .nominalSpeed = 0.1 };
    st.wobbleRandom = 0.45;
    st.hissDb = -62.0;
    st.wear = wear;
    st.loopLength = loopLength;
    return st;
}

} // namespace mulberry::dsp
