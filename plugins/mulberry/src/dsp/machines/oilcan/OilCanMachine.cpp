#include "dsp/machines/oilcan/OilCanMachine.h"

#include "dsp/MachineInfo.h"

namespace mulberry::dsp
{

Transport::Mechanics OilCanMachine::mechanics() const noexcept
{
    // A small motor driving the drum through a rubber belt: soft, slow, a little elastic.
    return { .naturalHz = 0.6, .damping = 0.6, .minSpeed = pickupArc / 8.0 };
}

TapeMachine::Setup OilCanMachine::setup (const MachineSettings& s) const noexcept
{
    Setup st;
    const auto brushes = (double) character::brushes (s.paramA);
    const auto oil = (double) character::oil (s.paramB);

    for (size_t l = 0; l < 2; ++l)
    {
        const auto ms = std::clamp ((double) s.timeMs[l], (double) maps::minTimeMs, (double) maps::maxTimeMs);
        st.speed[l] = pickupArc / (ms * 0.001);
        st.distance[l] = pickupArc;
    }

    // The brush contact is the aperture; there is no spacing or depth loss on a charged surface,
    // and no head bump.
    st.geometry = { .gap = (25.0 + 45.0 * oil) * 1.0e-6, .spacing = 1.0e-12, .thickness = 1.0e-12, .bumpWavelength = 1.0e3 };
    st.lossSpeedMin = 0.0;

    st.record.magnetic = false;
    st.record.emphasisMicroseconds = 0.0;
    st.record.ampHeadroom = 1.6;        // a tube stage: soft, early, asymmetric
    st.record.ampBias = 0.2;
    st.record.couplingHz = 40.0;

    // Drum rotation (v / circumference) with its 2nd harmonic (an eccentric drum on a worn
    // bearing), the belt slipping once per turn, and slow random drift: the warble is the sound.
    const auto slip = 1.0 + 1.5 * oil;
    st.wobble = { .capstanDepth = 0.0026 * slip, .capstanDiameter = circumference / std::numbers::pi,
                  .pinchDepth = 0.0011 * slip, .pinchDiameter = 0.5 * circumference / std::numbers::pi,
                  .spliceDepth = 0.0031 * slip, .loopLength = circumference,
                  .driftDepth = 0.0037 * slip, .driftHz = 0.9,
                  .scrapeDepth = 0.0, .scrapeHz = 1000.0, .nominalSpeed = 0.75 };
    st.wobbleRandom = 0.45;
    st.hissDb = -66.0;

    // Brushes: up to five pickups over 15 % of the arc, from one dominant to an even cluster.
    st.brushes = 1 + (int) std::lround (4.0 * brushes);
    st.brushSpread = 0.15 * brushes;
    st.brushTilt = 1.2 * (1.0 - brushes);
    st.leakSeconds = 30.0 / (1.0 + 3.0 * oil);
    return st;
}

} // namespace mulberry::dsp
