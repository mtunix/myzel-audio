#pragma once

namespace umber::dsp
{

/**
    One channel of an analog saturation circuit, running at the oversampled rate.

    Every model MUST honour these conventions — the chain, Auto gain and the UI rely on them:

    Level / calibration
      - Input sample 1.0 == 0 dBFS. The chain has already applied Drive (and Slam) gain, so the
        model sees: hostSignal * driveGain. Each model maps samples to circuit volts with its own
        internal constant.
      - Small-signal gain: at -60 dBFS, 1 kHz, |output| == |input| within ±0.5 dB, NON-inverted
        polarity. (Triode/common-emitter stages invert: flip the sign.) The model provides its own
        make-up gain so the chain can treat every model as "unity until it saturates".
      - Saturation onset (1 kHz sine, peak amplitude A at the model input) — target THD:
            A = 0.063 (-24 dBFS)  -> clean-ish:  < 0.3 %   (Fuzz may be dirtier)
            A = 0.25  (-12 dBFS)  -> warm:       ~0.3 - 3 %
            A = 1.0   (  0 dBFS)  -> driven:     ~3 - 15 %
            A = 8     (+18 dBFS)  -> heavy:      ~15 - 40 %
            A = 80    (+38 dBFS)  -> destroyed, but still musical and bounded.
        Each model shapes *how* it gets there (harmonic balance, frequency dependence, dynamics).
      - Output must stay finite and bounded (|y| < ~8) for ANY finite input up to |x| <= 1000.

    Real-time rules
      - prepare() is called off the audio thread and may allocate. Sample rate is the OVERSAMPLED
        rate, anywhere from 88.2 kHz to 800 kHz.
      - reset() and process() are called on the audio thread: no allocation, locks, I/O, or
        exceptions. Bounded work per sample (cap Newton-Raphson iterations).
      - Deterministic: after reset(), identical input gives bit-identical output.
      - Large lookup tables must be shared between instances (static, built once) — the chain
        keeps several instances of every model alive.
*/
class CircuitModel
{
public:
    virtual ~CircuitModel() = default;

    virtual void prepare (double oversampledRate) = 0;
    virtual void reset() noexcept = 0;

    /** Processes one channel in place. */
    virtual void process (float* samples, int numSamples) noexcept = 0;

    /**
        Moves the circuit's operating point away from its calibrated value (the panel's Bias
        control). amount is -1 (cold, towards cutoff) .. 0 (nominal) .. +1 (hot). Called on the
        audio thread, at most once per 32 oversampled samples while the control moves, already
        smoothed by the chain: real-time safe and cheap. At 0 the model must be bit-identical
        to its nominal behaviour. Each model maps the amount to a real component (a bias
        supply or trim) and documents it in its NOTES.md. Default: no bias control.
    */
    virtual void setBias (double amount) noexcept { (void) amount; }
};

} // namespace umber::dsp
