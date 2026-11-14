#pragma once

#include "dsp/EqParams.h"
#include "dsp/chain/Stage.h"

#include <span>

namespace viridian::dsp
{

/** What a model reads once per host block: the knobs resolved to its physical settings. */
struct ModelSettings
{
    EqParams eq;
    bool eqIn = true;
    StereoMode stereo = StereoMode::stereo;
    double tolerance = 0.0;     // 0 ... 1
    double noise = 0.0;         // 0 ... 1
};

/**
    One EQ model: its stages in signal order (input amp, network, make-up amp, transformer), each
    model's own. Every model exists from prepare(); EqChain crossfades between them. Drive is the
    chain's: a model sees the driven signal and reports its stages' small-signal gain, so the chain
    can undo both.
*/
class EqModel
{
public:
    virtual ~EqModel() = default;

    /** Not real-time safe. */
    virtual void prepare (double hostRate, int maxBlock, int numChannels) = 0;

    /** Designs everything for the latest settings at once and clears every state. Not real-time safe. */
    virtual void build() = 0;

    /**
        build() in bounded steps spread over the following process() calls (a model that fades in
        must not stall a callback); until ready() the output is not yet the model's. ready()
        depends on the number of samples processed since, not on how they were split into calls.
    */
    virtual void beginBuild() noexcept = 0;
    virtual bool ready() const noexcept { return true; }

    /** Clears every state and keeps the designs: a host's reset, cheap on the audio thread. */
    virtual void reset() noexcept = 0;

    virtual void setSettings (const ModelSettings&) noexcept = 0;

    virtual std::span<Stage* const> stages() noexcept = 0;

    /** Product of the stages' small-signal gains. */
    double smallSignalGain() noexcept
    {
        double g = 1.0;
        for (auto* s : stages())
            g *= s->smallSignalGain();
        return g;
    }

    /** True for a model that is not built yet: it passes the signal through unchanged. */
    virtual bool isPlaceholder() const noexcept { return false; }
};

} // namespace viridian::dsp
