#pragma once

namespace viridian::dsp
{

/**
    What a stage sees besides its audio. A dynamics stage (a later channel strip) reads a side-chain
    key here and leaves its gain reduction for the meters; v1 has neither, so both stay empty.
*/
struct StageContext
{
    double sampleRate = 0.0;                // the rate this stage runs at
    const double* const* key = nullptr;     // side-chain key at that rate, or none
    int keyChannels = 0;
    double gainReductionDb = 0.0;           // written by a dynamics stage once per block
};

/**
    One step of a model's signal path: the input amp, the EQ network, the make-up amp, the output
    transformer, later a compressor. EqChain runs a model's stages in order, each oversampled run of
    them inside one 2x up/down pair (PLAN.md, "Open for a channel strip").
*/
class Stage
{
public:
    enum class Rate
    {
        host,
        oversampled         // twice the host rate
    };

    virtual ~Stage() = default;

    virtual Rate rate() const noexcept = 0;

    /** stageRate: the host rate or twice it, as rate() says; maxSamples at that rate. Not real-time safe. */
    virtual void prepare (double stageRate, int maxSamples, int numChannels) = 0;
    virtual void reset() noexcept = 0;

    /** In place; one or two channels. */
    virtual void process (double* const* channels, int numChannels, int numSamples, StageContext&) noexcept = 0;

    /** Small-signal gain from input to output: the chain divides the model's output by the product. */
    virtual double smallSignalGain() const noexcept { return 1.0; }

    /** Delay the stage adds (look-ahead, its own resampling), in host samples; added to the chain's latency report. */
    virtual double latencySamples() const noexcept { return 0.0; }
};

} // namespace viridian::dsp
