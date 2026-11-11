#pragma once

#include "dsp/Algorithms.h"
#include "dsp/Engine.h"
#include "dsp/common/ReverbBlocks.h"

#include <array>
#include <memory>
#include <vector>

namespace cerulean::dsp
{

/** Everything the chain needs for one block, in physical units (the processor resolves sync). */
struct ChainSettings
{
    Algorithm algorithm = Algorithm::hall;
    EngineParams engine;            // targets; engine.freeze is ignored (see freeze)
    bool freeze = false;
    bool freezeLayer = false;       // while frozen, the playing adds into the held tail (see below)
    float preDelayMs = 20.0f;       // resolved (free or tempo-synced), 0 .. maxPreDelayMs
    float mix = 0.35f;              // 0 .. 1, pedal law (see process())
    bool wetOnly = false;
    float outputDb = 0.0f;
    bool duck = false;
    float duckAmountDb = 12.0f;
    float duckThresholdDb = -40.0f;
    float duckReleaseMs = 300.0f;
    float width = 1.0f;             // 0 .. 1.5
    float inLowCutHz = 20.0f;       // <= 20 Hz means off
    float inHighCutHz = 20000.0f;   // >= 20 kHz means off
    bool bypass = false;
    bool trails = true;
    uint32_t presetGeneration = 0;  // bumped by the preset manager: starts a fresh engine (trails)
};

/** Per-block levels for the UI (linear). */
struct ChainMeters
{
    float inputPeak = 0.0f, inputRms = 0.0f;
    float wetPeak = 0.0f, wetRms = 0.0f;
    float outputPeak = 0.0f, outputRms = 0.0f;
    float duckGain = 1.0f;          // current duck gain on the wet (1 = none)
    float freeze = 0.0f;            // freeze ramp 0..1
    int busyEngines = 0;            // active + ringing + fading
};

/**
    The whole signal path except the engines' insides:

    in -> input Low/High Cut -> pre-delay -> [ engine slots ] -> wet tilt -> width -> duck
       -> pedal-law mix with the dry -> output gain -> bypass (with trails) -> out

    Engine slots: two pre-allocated instances of every algorithm. Changing algorithm (or loading a
    preset, see ChainSettings::presetGeneration) moves the input to a fresh instance; the old one
    keeps ringing with its old settings until silent (trails on) or fades out over 30 ms (trails
    off). At most two old tails ring at once; older ones fade quickly. Nothing allocates after
    prepare().

    Freeze layering: normally Freeze cuts the input, so the held tail never changes. With
    freezeLayer on, the playing keeps going into the frozen engine (at layerFeedDb) and builds up
    on top of what is held. Loop gain stays exactly 1, so the layers hold just like a plain freeze,
    and a level ceiling keeps them from piling up forever: the ceiling is the louder of the tail
    when Freeze was pressed and the loudest playing since (minus layerHeadroomDb). Above it the
    feed closes and the loop leaks a little (engine freeze slightly below 1) until the pad is back
    under, so old layers make room for new ones and the level stays put.

    Latency is zero. Stereo in / stereo out; the processor duplicates a mono input.
*/
class ReverbChain
{
public:
    static constexpr float maxPreDelayMs = 2000.0f;
    static constexpr int maxRinging = 2;
    static constexpr float layerFeedDb = -3.0f;      // input into a frozen, layering engine
    static constexpr float layerHeadroomDb = -6.0f;  // ceiling relative to the playing level

    ReverbChain();
    ~ReverbChain();

    void prepare (double sampleRate, int maximumBlockSize);
    void reset();

    void process (float* left, float* right, int numSamples, const ChainSettings& settings,
                  ChainMeters& meters) noexcept;

    /** For tests: the slot states (number of engines currently processing). */
    int getBusyEngineCount() const noexcept;

private:
    enum class SlotState { idle, active, ringing, fading, stealing };

    struct Slot
    {
        std::unique_ptr<Engine> engine;
        Algorithm algorithm = Algorithm::hall;
        SlotState state = SlotState::idle;
        EngineParams params;
        float gain = 1.0f, gainStep = 0.0f;
        int quietSamples = 0;
        bool dormant = false;
        uint64_t ringOrder = 0;
        LinearRamp sizeGain;   // level compensation for Size (see sizeLevelExponent)
    };

    void switchEngine (const ChainSettings& s);
    void retire (int slotIndex, bool trails);
    void enforceRingingLimit();
    void smoothActiveParams (const ChainSettings& s, bool snap);
    void runEngines (int numSamples, bool feedActive) noexcept;
    void setSlotIdle (Slot& slot) noexcept;
    void processSubBlock (float* left, float* right, int numSamples, const ChainSettings& s) noexcept;
    void updateLayering (int numSamples, const ChainSettings& s, double feedSquares) noexcept;

    double fs = 48000.0;
    bool prepared = false;

    std::vector<Slot> slots;    // numAlgorithms * 2
    int activeSlot = -1;
    Algorithm currentAlgorithm = Algorithm::hall;
    uint32_t currentPresetGeneration = 0;
    uint64_t ringCounter = 0;
    EngineParams lastTargets;

    // Input stage.
    Svf lowCutL, lowCutR, highCutL, highCutR;
    Smoother lowCutHz, highCutHz;
    DelayLine preDelayL, preDelayR;
    GlideValue preDelaySamples;
    EnvelopeFollower duckDetector;
    float duckReductionDb = 0.0f;

    // Ramps and smoothers (per sample unless noted).
    Smoother dryGain, wetGain, outGain, widthAmount, duckEnable, toneSmooth, engineDryGainSmooth;
    float freezeRamp = 0.0f, freezeStep = 0.0f;   // linear, 50 ms
    float activeRamp = 1.0f, activeStep = 0.0f;   // linear, 30 ms (bypass)
    bool bypassResetDone = false;

    // Freeze layering (control rate, see updateLayering()).
    bool layering = false;
    double padSquares = 0.0;                        // active engine output, current sub-block
    float padLevel = 0.0f, playLevel = 0.0f;        // smoothed RMS of pad and playing (linear)
    float layerCeiling = 0.0f;
    int layerCaptureSamples = 0;                    // the first moments of a freeze set the ceiling
    float layerLeak = 0.0f;                         // 0..maxLeak: engine freeze = ramp * (1 - leak)
    float layerFeed = 0.0f, layerFeedTarget = 0.0f; // input gain into the frozen engine

    // Wet stage.
    OnePole tiltSplitL, tiltSplitR;

    // Scratch (one sub-block).
    std::array<float, Engine::maxSubBlock> engineInL {}, engineInR {}, engineOutL {}, engineOutR {},
                                           wetL {}, wetR {}, zeros {}, detectorL {}, detectorR {};

    ChainMeters blockMeters;
    double inputSquares = 0.0, wetSquares = 0.0, outputSquares = 0.0;
};

} // namespace cerulean::dsp
