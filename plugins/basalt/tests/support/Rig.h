#pragma once

// The measurement rig: what the gates set (Settings), what they run it on (a Device: the real
// processor through its APVTS, or the double-precision reference of the NOTES), and the gain
// trace they read back. Shared by tests/GateTests.cpp and tools/measure.

#include "support/Signals.h"

#include "dsp/Modes.h"

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace basalt::measure
{

inline constexpr double infiniteRatio = std::numeric_limits<double>::infinity();

/** Every control in plain units (PLAN.md, Controls); the defaults are the plugin's. */
struct Settings
{
    Model model = Model::clean;
    VcaDetector vcaDetector = VcaDetector::peak;
    double thresholdDb = -18.0;
    double ratio = 4.0;                 // infiniteRatio for the ∞ step
    double kneeDb = 6.0;
    double attackMs = 10.0;
    double releaseMs = 150.0;
    bool autoRelease = true;
    double rangeDb = 40.0;
    double floorDb = -70.0;
    double gainDb = 0.0;
    bool autoGain = false;
    double mixPercent = 100.0;
    CharacterMode character = CharacterMode::off;
    double driveDb = 6.0;
    bool charPost = true;
    bool driveLink = false;
    double targetDb = 3.0;
    double lookaheadMs = 0.0;
    StereoMode stereo = StereoMode::linked;
    double linkPercent = 100.0;
    KeySource key = KeySource::internal;
    double keyHpfHz = 20.0;             // the lowest position is Off
    double keyLpfHz = 20000.0;          // the highest position is Off
    double keyBellHz = 1000.0;
    double keyBellDb = 0.0;
    double keyBellQ = 1.0;
    Listen listen = Listen::output;
    double outputDb = 0.0;
    bool bypass = false;
    LatencyMode latency = LatencyMode::normal;  // a session setting, not a parameter

    bool operator== (const Settings&) const = default;
};

/** Sets one control by its parameter ID ("latency" for the session setting); false if the key or value is not understood. */
bool setSetting (Settings&, const std::string& key, const std::string& value);

const char* modelKey (Model);              // "clean", "vca", "fet", "opto", "vari-mu", "upward", "diode-bridge", "pwm", "pumping"
std::optional<Model> modelFromKey (const std::string&);
const char* stereoName (StereoMode);
const char* latencyName (LatencyMode);
std::string ratioText (double ratio);

/**
    Something that runs Basalt's signal path: the plugin's processor, or a reference built from
    the NOTES. The main bus is processed in place; the key (the side-chain bus) is read only.
*/
class Device
{
public:
    virtual ~Device() = default;

    /** Not real-time safe. keyChannels 0 leaves the side-chain bus off. */
    virtual void prepare (const Settings&, double rate, int mainChannels, int keyChannels, int maximumBlockSize) = 0;

    /** Between blocks, as a host's automation. */
    virtual void set (const Settings&) = 0;

    virtual void process (float* const* main, int mainChannels, const float* const* key, int keyChannels, int numSamples) = 0;

    /** The latency the device reports now, in samples. */
    virtual int latency() const = 0;

    /** The settings as the device holds them (parameters are stored normalised: a value may round). */
    virtual Settings applied() const = 0;
};

enum class DeviceKind
{
    processor,  // BasaltProcessor, parameters set through its APVTS
    reference,  // the double-precision transcription of the NOTES (support/Reference.h)
    onePole     // a branching one-pole on a hard-knee peak curve: a known answer for the ballistics method
};

const char* deviceName (DeviceKind);
std::unique_ptr<Device> makeDevice (DeviceKind);

//==============================================================================
struct Input
{
    Channels main;
    Channels key;   // empty: the side-chain bus is off
};

using Automation = std::function<void (int position, Settings&)>;

struct RenderOptions
{
    double rate = 48000.0;
    std::vector<int> blocks { 512 };    // cycled
    Automation automate;                // called before every block with its first sample
    std::string dump;                   // when set: <dump>.f32 (per sample: main 0 in, main 0 out, key 0) and <dump>.txt
};

struct Rendered
{
    Channels main;
    int latency = 0;    // at the start
    Settings applied;   // at the start
};

/** Prepares the device afresh, then runs the whole input through it. */
Rendered render (Device&, const Settings&, const Input&, const RenderOptions&);

/** Pseudo-random block sizes in [1, 4096], the same for a seed. */
std::vector<int> variableBlocks (unsigned seed, int count = 97);

//==============================================================================
/**
    The gain the engine applied, read from a render whose main bus carried `in`: g[n] =
    out[n + latency] / in[n], per input sample (NaN where the input is 0). With a power-of-two
    probe amplitude the quotient is the float gain itself, bit for bit.
*/
struct Trace
{
    std::vector<float> gain;
    std::vector<double> reductionDb;    // −20·log10(gain)
};

Trace traceOf (const Samples& in, const Samples& out, int latency);

/**
    How a gate reads the engine's gain for a key signal. External (the default): the test signal
    goes to the side-chain bus and the main bus carries a constant-magnitude probe at 0.25, so the
    gain is known at every sample, digital silence on the key included. Internal: the test signal
    is the main input; its digital silence is replaced by a carrier at −100 dBFS (under every
    Threshold − Knee/2) so the gain stays visible.
*/
enum class Probe
{
    external,
    internal
};

inline constexpr float probeAmplitude = 0.25f;
inline constexpr float internalSilenceProbe = 1.0e-5f;

struct KeyRender
{
    std::vector<Trace> traces;  // per main channel
    int latency = 0;
    Settings applied;
};

/** Renders `key` (one or two channels) and returns the gain trace of each main channel (two). */
KeyRender renderKey (DeviceKind, Settings, const Channels& key, Probe, const RenderOptions&);

} // namespace basalt::measure
