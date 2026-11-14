#pragma once

#include "dsp/EqParams.h"
#include "dsp/models/EqModel.h"

#include "common/dsp/Filters.h"
#include "common/dsp/Oversampler2x.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <complex>
#include <memory>
#include <vector>

namespace viridian::dsp
{

/**
    The chain's 2x pair: Oversampler2x's half-band with half its transition (pass band to 0.48 fs,
    stop band from 0.52 fs, below -100 dB), so a clipping stage behind the host-rate network sees
    no image of the network's top octave: Oversampler2x leaves the image of 21 kHz at 44.1 kHz
    43 dB down, and a stage that clips mixes it back into the band (console NOTES.md, "Amplifier").
*/
class ChainOversampler final : public myzel::dsp::HalfBandPair<10>
{
public:
    ChainOversampler() : HalfBandPair<10> (0.02) {}
};

/**
    In front of a model's first 2x pair and behind each, at the host rate: an elliptic low-pass of
    order 10, flat to 0.4535 fs (0.001 dB ripple) and below -100 dB from 0.4702 fs, in normalised
    frequency (one set of coefficients for every rate). The pairs pass half of what lies near
    Nyquist (their transition band is 0.48 ... 0.52 fs): in front, nothing is left there for the
    interpolator to image into a stage that clips; behind, a clipping stage's products that fold
    into the transition band land above 0.48 fs and go (console NOTES.md, "Amplifier"; PLAN.md
    Deviations 10). Both channels in one loop.
*/
class ChainBandLimit
{
public:
    /** The group delay at DC, in host samples. */
    static constexpr double delaySamples = 0.42622176744608134;

    void reset() noexcept;
    void process (double* const* channels, int numChannels, int numSamples) noexcept;

    /** The filter's response at f / fs (tests). */
    static std::complex<double> response (double normalised) noexcept;

private:
    static constexpr int sections = 5;

    // scipy.signal.ellip (10, 0.001, 100, 0.907, output = "sos"): b0, b1, b2, a1, a2 (a0 = 1).
    static constexpr double sos[sections][5] {
        { 0.42779566346901515, 0.855116897897889, 0.42779566346901504, 1.2597578372637253, 0.4092048655037618 },
        { 1.0, 1.9911201741262312, 0.9999999999999999, 1.5123245548111683, 0.6281517672199772 },
        { 1.0, 1.9800918469436117, 1.0000000000000004, 1.7164065639668085, 0.8058968497427289 },
        { 1.0, 1.9706887001958715, 1.0, 1.8325037527130494, 0.9087642019778848 },
        { 1.0, 1.965570830885293, 1.0, 1.9013648333084892, 0.973011363497438 } };

    double state[2][sections][2] {};     // transposed direct form II, per channel and section
};

/** Plain-value snapshot of every parameter the chain needs, taken once per block. */
struct ChainSettings
{
    Model model = Model::inductor;
    bool alt = false;
    SlotPositions positions = defaultPositions();
    float driveDb = 0.0f;
    bool eqIn = true;
    float outputDb = 0.0f;
    StereoMode stereo = StereoMode::stereo;
    float tolerance = 0.0f;         // 0 .. 1
    float noise = 0.0f;             // 0 .. 1
    bool bypass = false;
};

/**
    The Viridian signal path (PLAN.md, Signal flow):

        in -> Drive -> [model: its stages, each oversampled run inside one 2x pair, a band-limit
           in front of the first pair and behind each] -> inverse small-signal gain -> model
           crossfade -> DC block -> Output -> Bypass -> out

    Every model exists from prepare() and owns its stages (EqModel); the chain runs them in order,
    so a later dynamics stage is one more entry in a model's list. A model change first runs the
    incoming model beside the active one, its output unheard, until it is ready and has run
    warmUpSeconds (so it enters settled, and its designs are spread over many callbacks), then
    crossfades the two over transitionSeconds along a raised cosine, amplitude-complementary (both
    carry the same signal: equal power would swell by 3 dB). Until the fade
    starts the change follows the Model knob: back to the active model drops it unheard, another
    model restarts it. Each step of a change falls on the chain's own sample count, so the output
    does not depend on the host's block sizes. Each model's path is padded to the slowest model's
    delay, so the reported latency is constant; bypass delays the dry signal by the same amount.
*/
class EqChain
{
public:
    EqChain();
    ~EqChain();

    /** Designs the active model for `current` (the settings of the first callback). Not real-time safe. */
    void prepare (double sampleRate, int maximumBlockSize, int numChannels, const ChainSettings& current);

    /** Clears every state and keeps the designs (PLAN.md Settled 19): cheap on the audio thread. */
    void reset() noexcept;

    /**
        Designs the model `current` selects for those settings at once and clears every state, as
        prepare() does: a fresh start for an offline render or a measurement. Not real-time safe.
    */
    void rebuild (const ChainSettings& current);

    /** Mono (one channel) or stereo (two), any block length. */
    void process (float* const* channels, int numChannels, int numSamples, const ChainSettings&) noexcept;

    int getLatencySamples() const noexcept { return latencySamples; }

    /** The model a chain runs (tests). */
    EqModel& model (Model m) noexcept { return *runners[(size_t) m].model; }

    /** True while a model change runs two models: the incoming one unheard, or both in the fade. */
    bool isChangingModel() const noexcept { return incomingModel >= 0 || fadeRemaining > 0; }

    static constexpr double bypassSeconds = 0.02;
    static constexpr double outputSeconds = 0.01;     // each of Output's two glide stages
    static constexpr double driveSeconds = 0.01;      // each of Drive's two glide stages
    static constexpr double transitionSeconds = 0.02;
    static constexpr double dcBlockHz = 1.0;
    static constexpr double warmUpSeconds = 0.02;     // input a ready model runs on before it fades in
    static constexpr int readyCheckSamples = 32;      // an incoming model's ready() is read this often from the change

private:
    static constexpr int maxChannels = 2;

    /** One model, its oversamplers (one pair per oversampled run of stages) and its delay pad. */
    struct Runner
    {
        std::unique_ptr<EqModel> model;
        std::vector<std::array<ChainOversampler, maxChannels>> oversamplers;
        std::vector<ChainBandLimit> bandLimits;     // one in front of the first pair, one behind each
        double latency = 0.0;           // host samples
        int pad = 0;
        std::array<std::vector<double>, maxChannels> delay;
        int delayWrite = 0;
        std::vector<double> driveDelay;     // Drive as it went in, `latency` samples ago
        int driveWrite = 0;
    };

    void runModel (Runner&, const ModelSettings&, const double* const* in, int numChannels, int numSamples,
                   double* const* out) noexcept;
    void compensate (Runner&, int numChannels, int numSamples, double* const* io, const double* drives) noexcept;
    void restart (Runner&, double driveGain) noexcept;
    void processChunk (float* const* channels, int numChannels, int numSamples, const ChainSettings&) noexcept;
    void followModelKnob (int modelIndex, const ChainSettings&, double driveGain) noexcept;
    void runModels (int numChannels, int from, int length, const ChainSettings&) noexcept;
    ModelSettings settingsFor (Model, const ChainSettings&) const noexcept;

    double sampleRate = 44100.0;
    int maxBlock = 0, numChannelsPrepared = 0, latencySamples = 0;
    std::array<Runner, numModels> runners;

    int activeModel = -1, fadingModel = -1, incomingModel = -1;
    int fadeLength = 1, fadeRemaining = 0, warmLength = 1, warmRemaining = 0, readyCheckRemaining = 0;
    bool incomingReady = false;

    /**
        Drive's and Output's glide: two one-pole stages in dB, so the gain has no corner when it
        starts or stops moving (a linear ramp's corners click under fast automation).
    */
    struct GainGlide
    {
        void prepare (double sampleRate, double seconds) noexcept;
        void jump (double gain) noexcept;
        double next (double targetGain) noexcept;

        double coefficient = 1.0, first = 0.0, second = 0.0, target = 1.0, targetLog = 0.0, value = 1.0;
        bool moving = false;
    };

    GainGlide drive, output;
    juce::SmoothedValue<double> bypass { 0.0 };

    std::array<std::vector<double>, maxChannels> driven, wetA, wetB, oversampled;
    std::vector<double> drives, lagged;
    std::array<std::vector<double>, maxChannels> dryDelay;
    int dryWrite = 0;
    std::array<myzel::dsp::DcBlocker, maxChannels> dc;
};

} // namespace viridian::dsp
