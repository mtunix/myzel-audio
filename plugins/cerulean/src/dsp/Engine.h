#pragma once

#include <cstdint>
#include <memory>

namespace cerulean::dsp
{

/**
    Everything an algorithm needs to know, already mapped to physical units (see Mappings.h).

    The chain smooths every continuous value before it gets here and calls Engine::process() in
    sub-blocks of at most Engine::maxSubBlock samples, so an engine may treat these values as
    constant for one call and simply recompute its coefficients each time.
*/
struct EngineParams
{
    float decaySeconds = 3.0f;  // mid-band RT60, 0.1 .. 60 s
    float sizeScale    = 1.0f;  // delay-length scale, 0.3 .. 2 (maps::sizeToScale); 1 = nominal
    float mod          = 0.35f; // modulation depth, 0 .. 1
    float modRate      = 1.0f;  // multiplier of the algorithm's nominal rate, 0.25 .. 4
    float tone         = 0.0f;  // -1 dark .. +1 bright (maps::toneToHighDecay for the loop)
    float lowEnd       = 1.0f;  // RT60(low) / RT60(mid), 0.5 .. 2 (crossover maps::lowCrossoverHz)
    float diffusion    = 0.75f; // input diffusion amount, 0 .. 1
    float paramA       = 0.5f;  // character control A, 0 .. 1 (meaning per algorithm)
    float paramB       = 0.5f;  // character control B, 0 .. 1
    float echoTimeMs   = 300.0f;// resolved echo time (free or tempo-synced); only Echo uses it
    float freeze       = 0.0f;  // 0 = normal, 1 = frozen; the chain ramps it over ~50 ms
};

/**
    One reverb algorithm: stereo in, stereo wet out.

    Contract (checked for every algorithm by tests/support/EngineGates.h):

    - prepare() may allocate. Everything else must not: no allocation, no locks, no I/O.
    - Output is wet only, at a level comparable across algorithms: steady pink noise at Decay 2 s,
      nominal Size and default controls comes out at -3 dB +- 4 dB relative to the input.
    - Decay is the RT60 of the mid band (500 Hz - 2 kHz) and must be honoured within +-10 %
      at the nominal settings (algorithms whose tail is shaped on purpose document deviations).
    - Low End and Tone shape the low/high-band decay inside the loop.
    - Freeze (when 1): loop gain exactly 1, damping and modulation off, so the held sound neither
      grows nor decays (energy within +-1 dB over 60 s). The chain already cuts the input.
      Every value between 0 and 1 must be stable, too.
    - With freeze at 0, the tail always decays, for every parameter combination.
    - Deterministic: all randomness from a seeded PRNG; reset() restores the initial state
      exactly, so offline renders are bit-identical.
    - Size changes glide (slew-limited delay lengths); no clicks under fast automation.
    - Never produces NaN/Inf, even fed 0 dBFS square waves at any setting.
    - Process cost within the CPU budget in PLAN.md.
*/
class Engine
{
public:
    static constexpr int maxSubBlock = 32;

    virtual ~Engine() = default;

    /** Allocates. maxBlockSize is the largest numSamples process() will see (<= maxSubBlock). */
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    /** Clears every tail and reseeds every random generator. No allocation. */
    virtual void reset() = 0;

    /** inL/inR and outL/outR are distinct buffers of numSamples (<= maxSubBlock) samples. */
    virtual void process (const float* inL, const float* inR, float* outL, float* outR,
                          int numSamples, const EngineParams& params) noexcept = 0;

    /**
        Optional: a per-sample gain the chain applies to the *dry* signal, for the samples of the
        last process() call (e.g. Rise swelling the dry note in as well). nullptr = leave the dry
        alone. Only the active engine's dry gain is used; the chain smooths changes of engine.
    */
    virtual const float* getDryGain() const noexcept { return nullptr; }

    /**
        Optional: the chain calls this right before process() with the player's signal for the
        same samples, taken *before* pre-delay and freeze (after the input filters). Engines that
        react to the playing (Rise's onset detector) use it so their timing ignores pre-delay and
        they still hear the player while frozen. The pointers are valid for the next process()
        call only. Not called by tests or tools that drive an engine directly.
    */
    virtual void setDetectorInput (const float* left, const float* right) noexcept
    {
        (void) left;
        (void) right;
    }
};

} // namespace cerulean::dsp
