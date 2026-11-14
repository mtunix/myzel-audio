#pragma once

#include "dsp/common/Network.h"
#include "dsp/models/console/ConsoleControls.h"

#include <complex>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace viridian::dsp::console
{

/** One card for one channel: the front network into the back (an op-amp output: exact). */
class CardChannel
{
public:
    virtual ~CardChannel() = default;

    /** Not real-time safe. */
    virtual void prepare (double sampleRate) = 0;

    /** Which channel this card runs (before prepare()): its networks' turn in a control block (align()). */
    void setChannel (int c) noexcept { channel = c; }

    /** Continuous pots glide; the switches must equal those of the last reset(). */
    virtual void setPots (const Pots&) noexcept = 0;

    /**
        reset() in bounded steps (Network::resetStep()), so a caller can spread a switch change
        over several callbacks: beginReset(), then resetStep() until it returns true. Only the
        networks the switches use are designed; the others wait for a reset that selects them.
    */
    virtual void beginReset (const Pots&) noexcept = 0;
    virtual bool resetStep() noexcept = 0;

    /** Jumps to the pots, switches included, designing from scratch, and clears the states. */
    void reset (const Pots& p) noexcept
    {
        beginReset (p);
        while (! resetStep())
        {
        }
    }

    /** Clears the states; the designs stay. */
    virtual void clear() noexcept = 0;

    /**
        Places the networks' control blocks on the model's sample clock (Network::align()): above
        72 kHz each channel's front and back design in a quarter of a block of their own, so a
        callback of a quarter block (32 samples at 96 kHz) holds one network's design.
    */
    virtual void align (std::int64_t clock) noexcept = 0;

    /** One channel, or two through the same networks (the same pots on both). */
    virtual void process (double* const* channels, int numChannels, int numSamples) noexcept = 0;

    void process (double* samples, int numSamples) noexcept
    {
        double* ch[] { samples };
        process (ch, 1, numSamples);
    }

    /**
        This card on `a` and `other` (the same card type, the other channel's pots) on `b`, in one
        pass: the same output as processing each (Network::processPair()).
    */
    virtual void processPair (CardChannel& other, double* a, double* b, int numSamples) noexcept = 0;

    /** The realised digital response and the netlists' analog H (j 2 pi f) at the current pots. */
    virtual std::complex<double> response (double hz) const noexcept = 0;
    virtual std::complex<double> analogResponse (double hz) const noexcept = 0;

protected:
    /** The quarters of a control block this channel's front and back design in (setChannel()). */
    std::pair<int, int> staggerFor (double sampleRate) const noexcept
    {
        // Up to 72 kHz the networks design on one boundary, so a callback of half a block holds
        // every design or none; turns would only split the networks' loops.
        if (networkControlBlock (sampleRate) < maxControlBlock)
            return { 0, 0 };
        return channel == 1 ? std::pair { 1, 3 } : std::pair { 0, 2 };
    }

    int channel = 0;
};

std::unique_ptr<CardChannel> makeEarlyCard();
std::unique_ptr<CardChannel> makeLaterCard();

/** The index of a netlist control by name, at compile time. */
template <typename Topology>
constexpr int controlIndex (std::string_view name)
{
    for (int i = 0; i < Topology::numControls; ++i)
        if (std::string_view { Topology::controls[(size_t) i].name } == name)
            return i;
    return -1;
}

/** A card from its two generated networks. */
template <typename Front, typename Back>
class Card final : public CardChannel
{
public:
    void prepare (double sampleRate) override
    {
        const auto [frontPart, backPart] = staggerFor (sampleRate);
        front.setStagger (frontPart, 4);
        back.setStagger (backPart, 4);
        front.prepare (sampleRate);
        back.prepare (sampleRate);
    }

    void align (std::int64_t clock) noexcept override
    {
        front.align (clock);
        back.align (clock);
    }

    void setPots (const Pots& p) noexcept override
    {
        front.setTargets (frontControls (p));
        back.setTargets (backControls (p));
    }

    void beginReset (const Pots& p) noexcept override
    {
        front.setTargets (frontControls (p));
        back.setTargets (backControls (p));
        front.beginReset();
        building = 0;
    }

    bool resetStep() noexcept override
    {
        if (building == 1)
            return back.resetStep();
        if (front.resetStep())
        {
            back.beginReset();
            building = 1;
        }
        return false;
    }

    void clear() noexcept override
    {
        front.clear();
        back.clear();
    }

    void process (double* const* channels, int numChannels, int numSamples) noexcept override
    {
        front.process (channels, numChannels, numSamples);
        back.process (channels, numChannels, numSamples);
    }

    void processPair (CardChannel& otherCard, double* a, double* b, int numSamples) noexcept override
    {
        auto& other = static_cast<Card&> (otherCard);
        front.processPair (other.front, a, b, numSamples);
        back.processPair (other.back, a, b, numSamples);
    }

    std::complex<double> response (double hz) const noexcept override { return front.response (hz) * back.response (hz); }

    std::complex<double> analogResponse (double hz) const noexcept override
    {
        return front.analogResponse (hz) * back.analogResponse (hz);
    }

private:
    template <typename T>
    static void put (typename Network<T>::Controls& c, std::string_view name, double value) noexcept
    {
        const auto i = controlIndex<T> (name);
        if (i >= 0)
            c[(size_t) i] = value;
    }

    static typename Network<Front>::Controls frontControls (const Pots& p) noexcept
    {
        typename Network<Front>::Controls c {};
        put<Front> (c, "hpf_in", p.hpfIn ? 1.0 : 0.0);
        put<Front> (c, "hpf_freq", p.hpf);
        put<Front> (c, "lpf_in", p.lpfIn ? 1.0 : 0.0);
        put<Front> (c, "lpf_freq", p.lpf);
        put<Front> (c, "hf_gain", p.hf.gain);
        put<Front> (c, "hf_freq", p.hf.freq);
        put<Front> (c, "hf_bell", p.hf.bell ? 1.0 : 0.0);
        put<Front> (c, "hmf_gain", p.hmf.gain);
        put<Front> (c, "hmf_freq", p.hmf.freq);
        put<Front> (c, "hmf_q", p.hmf.q);
        return c;
    }

    static typename Network<Back>::Controls backControls (const Pots& p) noexcept
    {
        typename Network<Back>::Controls c {};
        put<Back> (c, "lmf_gain", p.lmf.gain);
        put<Back> (c, "lmf_freq", p.lmf.freq);
        put<Back> (c, "lmf_q", p.lmf.q);
        put<Back> (c, "lf_gain", p.lf.gain);
        put<Back> (c, "lf_freq", p.lf.freq);
        put<Back> (c, "lf_bell", p.lf.bell ? 1.0 : 0.0);
        return c;
    }

    static_assert (controlIndex<Front> ("hf_gain") >= 0 && controlIndex<Front> ("hmf_q") >= 0
                   && controlIndex<Front> ("hpf_in") >= 0 && controlIndex<Front> ("lpf_freq") >= 0);
    static_assert (controlIndex<Back> ("lmf_q") >= 0 && controlIndex<Back> ("lf_bell") >= 0);

    Network<Front> front;
    Network<Back> back;
    int building = 0;
};

} // namespace viridian::dsp::console
