#pragma once

#include "dsp/MachineInfo.h"
#include "dsp/common/HoldLooper.h"
#include "dsp/machines/Machine.h"

#include <array>
#include <memory>

namespace mulberry::dsp
{

/** Everything the chain needs for one block, in physical units (the processor resolves sync). */
struct ChainSettings
{
    mulberry::Machine machine = mulberry::Machine::reel;
    std::array<float, 2> timeMs { 375.0f, 375.0f };     // per line, resolved and folded
    float feedback = 0.35f;         // 0 .. 1.2
    float driveDb = 0.0f;
    float wobble = 0.5f;
    float wobbleRate = 1.0f;
    float wobbleShape = 0.5f;
    float noise = 0.0f;
    float diffuse = 0.0f;           // 0 .. 1
    float lowCutHz = 20.0f;         // <= 20 Hz means off
    float highCutHz = 20000.0f;     // >= 20 kHz means off
    float paramA = 0.5f;
    float paramB = 0.5f;
    TapPlanInput plan;              // the heads are filled in by the chain (setMachineHeads)
    float mix = 0.35f;
    bool wetOnly = false;
    float outputDb = 0.0f;
    bool duck = false;
    float duckAmountDb = 12.0f;
    float duckThresholdDb = -40.0f;
    float duckReleaseMs = 300.0f;
    bool hold = false;
    bool bypass = false;
    bool trails = true;
    uint32_t presetGeneration = 0;  // bumped on preset load: a fresh slot, the old echoes ring out
};

/** Per-block levels for the UI (linear). */
struct ChainMeters
{
    float inputPeak = 0.0f, inputRms = 0.0f;
    float wetPeak = 0.0f, wetRms = 0.0f;
    float outputPeak = 0.0f, outputRms = 0.0f;
    float duckGain = 1.0f;
    float hold = 0.0f;
    std::array<float, 2> timeMs {};     // the live (gliding) time of each line
    int busySlots = 0;
};

/**
    The whole signal path around the machines:

    in -> drive -> duck detector (dry) -> TapPlan input routing -> [ slot: machine + loop ] -> Hold
       -> make-up (1 / drive) -> duck -> pedal-law mix -> output -> bypass (trails) -> out

    Two slots, allocated in prepare(), each with its delay memory and one instance of every
    machine. Changing machine or loading a preset moves the input to the other slot; the old one
    rings out with its settings, its feedback capped at 95 % (trails on), or fades in 30 ms.
    Routing changes (mode, steps, groove, feel, heads) crossfade between the old and the new tap
    plan in 20 ms; weight-only changes (width, cross-feed, step levels) ramp.

    Latency is zero. Stereo in / stereo out; the processor duplicates a mono input.
*/
class EchoChain
{
public:
    static constexpr int numSlots = 2;
    static constexpr float planFadeMs = 20.0f;

    EchoChain();
    ~EchoChain();

    void prepare (double sampleRate, int maximumBlockSize);
    void reset();

    void process (float* left, float* right, int numSamples, const ChainSettings& settings,
                  ChainMeters& meters, bool monoInput = false) noexcept;

    int getBusySlotCount() const noexcept;

    /** The live machine (tests, tools). */
    const EchoMachine* activeMachine() const noexcept;

private:
    enum class SlotState { idle, active, ringing, fading, stealing };

    struct Slot
    {
        SlotMemory memory;
        std::array<std::unique_ptr<EchoMachine>, numMachines> machines;
        EchoMachine* machine = nullptr;
        mulberry::Machine kind = mulberry::Machine::reel;
        LoopContext loop;
        SlotState state = SlotState::idle;
        ChainSettings settings;         // frozen while ringing
        TapPlanInput planInput;
        TapPlan plan, previousPlan;
        float planFade = 1.0f;          // 1 = no crossfade running
        bool weightRamp = false;        // the running fade only ramps weights
        std::array<ReadList, 2> reads;
        float feedbackNow = 0.0f;
        float diffusionNow = 0.0f;
        float lowHzNow = 20.0f, highHzNow = 20000.0f;
        float inputGain = 1.0f;
        float gain = 1.0f, gainStep = 0.0f;
        long long quietSamples = 0;
        bool dormant = false;
        bool fresh = true;
        uint64_t ringOrder = 0;
        std::array<float, LoopContext::maxSubBlock> outL {}, outR {};
    };

    void startSlot (Slot& slot, const ChainSettings& s) noexcept;
    void switchSlot (const ChainSettings& s) noexcept;
    void retire (Slot& slot, bool trails) noexcept;
    void setIdle (Slot& slot) noexcept;
    void runSlot (Slot& slot, bool isActive, int n) noexcept;
    void buildReads (Slot& slot, int n) noexcept;
    void processSubBlock (float* left, float* right, int n, const ChainSettings& s, bool monoInput) noexcept;
    TapPlanInput planInputFor (const ChainSettings& s, bool monoInput) const noexcept;
    MachineSettings machineSettingsFor (const ChainSettings& s, const TapPlan& plan, const LoopContext& loop) const noexcept;
    double holdLengthSamples (const Slot& slot, int channel) const noexcept;

    double fs = 48000.0;
    bool prepared = false;
    std::array<Slot, numSlots> slots;
    int activeSlot = -1;
    mulberry::Machine currentMachine = mulberry::Machine::reel;
    uint32_t currentGeneration = 0;
    uint64_t ringCounter = 0;

    HoldLooper holdLooper;
    myzel::dsp::EnvelopeFollower duckDetector;
    float duckReductionDb = 0.0f;
    myzel::dsp::Smoother driveGain, makeUpGain, dryGain, wetGain, outGain, duckEnable;
    float bypassRamp = 1.0f, bypassStep = 0.0f;
    bool bypassResetDone = false;

    std::array<float, LoopContext::maxSubBlock> inL {}, inR {}, routed0 {}, routed1 {}, zeros {}, wetL {}, wetR {};

    ChainMeters blockMeters;
    double inputSquares = 0.0, wetSquares = 0.0, outputSquares = 0.0;
};

} // namespace mulberry::dsp
