#include "dsp/machines/reel/ReelMachine.h"

#include "dsp/MachineInfo.h"

namespace mulberry::dsp
{

ReelMachine::ReelMachine (uint32_t seed) : TapeMachine (seed)
{
    for (int i = 0; i < expansionPoints; ++i)
        expansionTable[(size_t) i] = TapeRecordPath::expansion (coreFor (-1.0 + 2.0 * i / (expansionPoints - 1)));
}

myzel::dsp::TapeHysteresis::Parameters ReelMachine::coreFor (double bias) noexcept
{
    // Nominal is Umber's bias-linearised set (c 0.9, k 0.5); under-biased towards the wide
    // unbiased loop (more hysteresis, Rayleigh expansion), over-biased narrower.
    myzel::dsp::TapeHysteresis::Parameters p;
    p.alpha = 0.01;
    p.c = bias < 0.0 ? 0.9 + 0.35 * bias : 0.9 + 0.07 * bias;
    p.k = bias < 0.0 ? 0.5 - 0.5 * bias : 0.5 - 0.15 * bias;
    return p;
}

Transport::Mechanics ReelMachine::mechanics() const noexcept
{
    // Heavy reels and a servo capstan: a speed change (the Speed switch) takes a second or so.
    return { .naturalHz = 0.8, .damping = 0.9, .minSpeed = 0.05 };
}

TapeMachine::Setup ReelMachine::setup (const MachineSettings& s) const noexcept
{
    Setup st;
    const auto ips = (double) character::speedIps (s.paramA);
    const auto speed = ips * metresPerInch;
    const auto bias = (double) character::bias (s.paramB);

    for (size_t l = 0; l < 2; ++l)
    {
        const auto ms = std::clamp ((double) s.timeMs[l], (double) maps::minTimeMs, (double) maps::maxTimeMs);
        st.speed[l] = speed;
        st.distance[l] = speed * ms * 0.001;
    }

    // A studio repro head. Over-bias erases the shortest wavelengths as they are written (like
    // extra spacing) and records deeper; under-bias records shallower, so it is brighter.
    st.geometry = { .gap = 2.5e-6,
                    .spacing = (0.15 + 0.6 * std::max (bias, 0.0)) * 1.0e-6,
                    .thickness = 1.0e-6 * (1.0 + 0.4 * bias),
                    .bumpWavelength = 5.4e-3 };

    // IEC record EQ: 70 us at 7.5 ips, 35 us at 15 ips, 17.5 us at 30 ips.
    st.record.emphasisMicroseconds = 70.0 * 7.5 / ips;
    st.record.fieldScale = 1.2;
    st.record.ampHeadroom = 2.5;
    st.record.ampBias = 0.15;

    st.record.core = coreFor (bias);
    const auto x = (bias + 1.0) * 0.5 * (expansionPoints - 1);
    const auto i = std::clamp ((int) x, 0, expansionPoints - 2);
    st.record.coreExpansion = expansionTable[(size_t) i] + (x - i) * (expansionTable[(size_t) i + 1] - expansionTable[(size_t) i]);

    // Transport at nominal Wobble (AES6 weighted ~0.05 %): low flutter, slow reel wow.
    st.wobble = { .capstanDepth = 0.0003, .capstanDiameter = 0.02,
                  .pinchDepth = 0.000128, .pinchDiameter = 0.03,
                  .spliceDepth = 0.0, .loopLength = 0.0,
                  .driftDepth = 0.000535, .driftHz = 0.35,
                  .scrapeDepth = 0.0001, .scrapeHz = 3000.0, .nominalSpeed = 0.381 };
    st.wobbleRandom = 0.4;

    // Hiss: 3 dB more per halving of the speed.
    st.hissDb = -72.0 + 3.0 * std::log2 (15.0 / ips);
    return st;
}

} // namespace mulberry::dsp
