#pragma once

#include "dsp/models/EqModel.h"

#include <array>

namespace viridian::dsp
{

/**
    Stands in for a model that is not built yet (PLAN.md, milestones): no amps, no network, so the
    signal passes through flat and Drive cancels out. It never imitates the model it holds the
    place of; isPlaceholder() says so to the tests and, later, the UI. Its path has the plan's two
    oversampled runs (input stage, make-up stage) around the host-rate network, empty: the chain's
    2x pairs give it the same phase as a built model's path, so a model crossfade does not comb.
*/
class PlaceholderModel final : public EqModel
{
public:
    void prepare (double, int, int) override {}
    void build() override {}
    void beginBuild() noexcept override {}
    void reset() noexcept override {}
    void setSettings (const ModelSettings&) noexcept override {}
    std::span<Stage* const> stages() noexcept override { return list; }
    bool isPlaceholder() const noexcept override { return true; }

private:
    /** A stage that passes its signal at its rate. */
    class Empty final : public Stage
    {
    public:
        explicit Empty (Rate r) : stageRate (r) {}
        Rate rate() const noexcept override { return stageRate; }
        void prepare (double, int, int) override {}
        void reset() noexcept override {}
        void process (double* const*, int, int, StageContext&) noexcept override {}

    private:
        Rate stageRate;
    };

    Empty input { Stage::Rate::oversampled }, network { Stage::Rate::host }, output { Stage::Rate::oversampled };
    std::array<Stage*, 3> list { &input, &network, &output };
};

} // namespace viridian::dsp
