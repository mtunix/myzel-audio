#pragma once

#include "plugin/Parameters.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace umber::dsp
{

/** Plain-value snapshot of every parameter the chain needs, taken once per block. */
struct ChainSettings
{
    float driveKnob   = 3.0f;           // 0..10
    bool  slam        = false;
    Model model       = Model::classA;
    float tone        = 0.0f;           // -1 (dark) .. +1 (bright)
    float lowCutHz    = params::lowCutMinHz;
    bool  weight      = false;
    float highCutHz   = params::highCutMaxHz;
    bool  autoGain    = true;
    float mix         = 1.0f;           // 0..1
    float outputDb    = 0.0f;
    int   realtimeFactor = 4;           // 2, 4, 8
    int   renderFactor   = 16;          // 0 = same as realtime, else 8 or 16
    bool  bypass      = false;
    bool  delta       = false;          // listen to what the circuit adds (wet - dry)
    StereoMode stereoMode = StereoMode::stereo;
    float emphasis    = 0.0f;           // -1 (lows into the circuit) .. +1 (highs)
    float bias        = 0.0f;           // -1 (cold) .. +1 (hot), see CircuitModel::setBias
};

/** Levels measured over the last processed block (linear amplitude). */
struct ChainMeters
{
    float inputRms = 0.0f, inputPeak = 0.0f;
    float driveRms = 0.0f;              // level hitting the circuit (after Drive/Slam)
    float outputRms = 0.0f, outputPeak = 0.0f;
    int   oversamplingFactor = 4;
};

/**
    The complete Umber signal path:

      in ─► ↑OS ─► M/S encode ─┬─► Low Cut (+Weight) ─► Drive ─► Pre-emphasis ─► Circuit (Bias) ─► De-emphasis ─┐
                               │   ┌──────────────────────────────────────────────────────────────────────────────┘
                               │   └─► DC block ─► High Cut ─► Tone ─► Auto ─┐
                               └──────────────── dry ────────────── Mix/Delta ◄┘
                                                                        │
      out ◄─ Bypass xfade ◄─ Output gain ◄─ latency pad ◄─ ↓OS ◄─ M/S decode

    Dry/wet mixing happens inside the oversampled domain, so dry and wet pass through the
    identical resampling filters and blend without phase smearing.

    Delta replaces "dry + mix * (wet - dry)" by "mix * (wet - dry)": only what the circuit adds.
    Stereo mode Mid/Side encodes to M/S inside the oversampled domain and runs only one of the two
    through the wet path (the other stays dry); a mode change dips the wet path to zero, swaps
    the domain at the silent point and fades back in, so it never clicks.

    Each oversampling factor owns a complete, pre-allocated engine. Switching quality (or going
    into offline render) crossfades between engines — no allocation, no clicks. Reported latency
    is constant across all factors.
*/
class SaturatorChain
{
public:
    SaturatorChain();
    ~SaturatorChain();

    /** Allocates everything. Not real-time safe. */
    void prepare (double sampleRate, int maximumBlockSize, int numChannels);
    void reset() noexcept;

    void process (juce::AudioBuffer<float>& buffer, const ChainSettings& settings,
                  bool isNonRealtime, ChainMeters& meters) noexcept;

    /** Constant for a given sample rate. */
    int getLatencySamples() const noexcept { return reportedLatency; }

    static float driveDb (float driveKnob, bool slam) noexcept;

    /** Effective factor for a request at this host rate (caps the oversampled rate). */
    static int effectiveFactor (int requestedFactor, double hostRate) noexcept;

    static constexpr double maxOversampledRate = 800000.0;
    static constexpr float maxInputLevel = 1000.0f;   // +60 dBFS, the circuit models' specified limit
    static constexpr double transitionSeconds = 0.02;

private:
    class Engine;
    static constexpr int numEngines = 4; // 2x, 4x, 8x, 16x

    void processChunk (float* const* channels, int numCh, int numSamples, const ChainSettings& settings,
                       bool isNonRealtime, ChainMeters& meters) noexcept;

    Engine* engineForFactor (int factor) noexcept;
    void recoverIfNonFinite (Engine& engine, float* const* wet, int numCh, int numSamples,
                             const ChainSettings& settings) noexcept;

    std::array<std::unique_ptr<Engine>, numEngines> engines;
    Engine* activeEngine = nullptr;
    Engine* fadingEngine = nullptr;     // previous engine during a quality crossfade
    int engineFadeRemaining = 0, engineFadeLength = 1;

    double sampleRate = 44100.0;
    int maxBlock = 0, numChannels = 0;
    int reportedLatency = 0;

    // bypass: dry delayed by the reported latency
    std::vector<std::vector<float>> dryDelay;
    int dryDelayWrite = 0;
    juce::SmoothedValue<float> bypassAmount, outputGain;

    juce::AudioBuffer<float> wetA, wetB;
};

} // namespace umber::dsp
