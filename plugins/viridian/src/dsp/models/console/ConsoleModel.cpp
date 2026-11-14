#include "dsp/models/console/ConsoleModel.h"

#include "dsp/common/HissAndHum.h"
#include "dsp/models/console/ConsoleCard.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace viridian::dsp
{

namespace
{
    double dbuToVolts (double dbu) noexcept { return std::sqrt (0.6) * std::pow (10.0, dbu / 20.0); }

    /** The switch positions and modes a network set is built for: changing one crossfades. */
    struct Key
    {
        bool later = false, hpfIn = false, lpfIn = false, hfBell = true, lfBell = true, eqIn = true;
        bool linked = true;     // both channels through channel 0's networks (no Tolerance spread)
        StereoMode stereo = StereoMode::stereo;

        bool operator== (const Key&) const = default;
    };

    constexpr int numSpread = 12;
} // namespace

//==============================================================================
/**
    An NE5534 stage into its rails (OpAmpClip), at the chain's 2x; the clip runs at 8x inside it
    (NOTES.md, "Amplifier"). Stereo as two lanes in one loop, the same to the bit as one stage per
    channel.
*/
class ConsoleModel::AmpStage final : public Stage
{
public:
    explicit AmpStage (const myzel::dsp::LineAmpParams& p) : params (p) {}

    Rate rate() const noexcept override { return Rate::oversampled; }

    void prepare (double stageRate, int, int) override
    {
        mono.setParams (params);
        mono.prepare (stageRate);
        pair.setParams (params);
        pair.prepare (stageRate);
        scale = voltsPerUnit();
    }

    void reset() noexcept override
    {
        mono.reset();
        pair.reset();
    }

    void process (double* const* channels, int numChannels, int numSamples, StageContext&) noexcept override
    {
        if (numChannels >= 2)
        {
            double* l = channels[0];
            double* r = channels[1];
            for (int i = 0; i < numSamples; ++i)
            {
                const double x[2] { l[i] * scale, r[i] * scale };
                double y[2];
                pair.processFrame (x, y);
                l[i] = y[0] / scale;
                r[i] = y[1] / scale;
            }
            pair.flushDenormals();
        }
        else if (numChannels == 1)
        {
            for (int i = 0; i < numSamples; ++i)
                channels[0][i] = mono.process (channels[0][i] * scale) / scale;
            mono.flushDenormals();
        }
    }

    double smallSignalGain() const noexcept override { return params.gain; }

    double latencySamples() const noexcept override { return 0.5 * myzel::dsp::OpAmpClip::latencySamples; }

private:
    myzel::dsp::LineAmpParams params;
    myzel::dsp::OpAmpClip mono;
    myzel::dsp::OpAmpClipLanes<2> pair;
    double scale = 1.0;
};

//==============================================================================
/** The card's network: noise in, M/S around it, two voices for switch crossfades. */
class ConsoleModel::NetworkStage final : public Stage
{
public:
    NetworkStage()
    {
        // The tolerance spread: fixed per channel and pot, so every render is the same.
        std::uint64_t s = 0x9E3779B97F4A7C15ULL;
        for (auto& channel : spread)
            for (auto& u : channel)
            {
                s ^= s >> 12;
                s ^= s << 25;
                s ^= s >> 27;
                u = (double) ((s * 0x2545F4914F6CDD1DULL) >> 11) / 4503599627370496.0 - 1.0;
            }
    }

    Rate rate() const noexcept override { return Rate::host; }

    void prepare (double stageRate, int maxSamples, int numChannelsIn) override
    {
        numChannels = std::clamp (numChannelsIn, 1, 2);
        for (auto& v : voices)
            for (int c = 0; c < 2; ++c)
            {
                v.early[(size_t) c] = console::makeEarlyCard();
                v.later[(size_t) c] = console::makeLaterCard();
                v.early[(size_t) c]->setChannel (c);
                v.later[(size_t) c]->setChannel (c);
                v.early[(size_t) c]->prepare (stageRate);
                v.later[(size_t) c]->prepare (stageRate);
            }
        for (auto& b : previous)
            b.assign ((size_t) maxSamples, 0.0);
        mid.assign ((size_t) maxSamples, 0.0);
        side.assign ((size_t) maxSamples, 0.0);

        const auto volts = voltsPerUnit();
        const HissAndHum::Spec spec { dbuToVolts (hissDbu) / volts, humHz, dbuToVolts (humDbu) * std::sqrt (2.0) / volts,
                                      dbuToVolts (thirdDbu) * std::sqrt (2.0) / volts };
        noiseSources[0].prepare (stageRate, spec, 0x51ED2701ULL);
        noiseSources[1].prepare (stageRate, spec, 0x6A09E667ULL);
        fadeLength = std::max (1, (int) std::lround (switchSeconds * stageRate));
        warmTicks = std::max (1, (int) std::ceil (switchSeconds * stageRate / ConsoleModel::stepSamples));
    }

    void settings (const ModelSettings& s) noexcept
    {
        pots = console::potsFor (s.eq);
        key = { pots.later, pots.hpfIn, pots.lpfIn, pots.hf.bell, pots.lf.bell, s.eqIn, s.tolerance <= 0.0,
                numChannels == 2 ? s.stereo : StereoMode::stereo };
        tolerance = s.tolerance;
        noise = s.noise;
    }

    /** Designs the networks for the latest settings at once and clears every state. Not real-time safe. */
    void build() noexcept
    {
        beginBuild();
        while (! ready)
            tick();
    }

    /** build(), its designs spread over the following blocks; the stage passes the signal until ready. */
    void beginBuild() noexcept
    {
        clock = 0;
        fadeRemaining = 0;
        building = false;
        warming = 0;
        ready = false;
        active = 0;
        gridPhase = 0;
        start (0);
        for (auto& n : noiseSources)
            n.reset();
    }

    bool isReady() const noexcept { return ready; }

    /** Clears every state and keeps the designs; a running fade completes, a change in progress restarts. */
    void reset() noexcept override
    {
        for (auto& v : voices)
            for (int c = 0; c < 2; ++c)
            {
                if (v.early[(size_t) c] != nullptr)
                    v.early[(size_t) c]->clear();
                if (v.later[(size_t) c] != nullptr)
                    v.later[(size_t) c]->clear();
            }
        fadeRemaining = 0;
        building = false;
        warming = 0;
        gridPhase = 0;
        clock = 0;
        if (ready)
            align (voices[(size_t) active]);
        for (auto& n : noiseSources)
            n.reset();
    }

    void process (double* const* channels, int numChannelsIn, int n, StageContext&) noexcept override
    {
        const int numCh = std::min (numChannelsIn, numChannels);
        if (noise > 0.0 && numCh == 2)
        {
            // Both channels' generators in one loop: two independent chains the core overlaps.
            for (int i = 0; i < n; ++i)
            {
                const auto l = noiseSources[0].next(), r = noiseSources[1].next();
                channels[0][i] += noise * l;
                channels[1][i] += noise * r;
            }
        }
        else if (noise > 0.0)
            for (int i = 0; i < n; ++i)
                channels[0][i] += noise * noiseSources[0].next();

        // A set being built or warmed keeps the pots of its own design steps and glides on from
        // them when it fades in: only the active set re-designs as the knobs move.
        if (ready)
            setPots (voices[(size_t) active]);

        // Switch changes and design steps on a grid of their own, so the output does not depend
        // on the host's block sizes.
        for (int done = 0; done < n;)
        {
            if (gridPhase == 0)
                tick();
            const int length = std::min (ConsoleModel::stepSamples - gridPhase, n - done);
            std::array<double*, 2> at { channels[0] + done, numCh > 1 ? channels[1] + done : nullptr };
            render (at.data(), numCh, length);
            gridPhase = (gridPhase + length) % ConsoleModel::stepSamples;
            clock += length;
            done += length;
        }
    }

    std::complex<double> response (double hz, bool analog) const noexcept
    {
        const auto& v = voices[(size_t) active];
        if (! ready || ! v.key.eqIn)
            return 1.0;
        const auto& card = v.key.later ? *v.later[0] : *v.early[0];
        return analog ? card.analogResponse (hz) : card.response (hz);
    }

    bool switching() const noexcept { return fadeRemaining > 0 || building || warming > 0; }

private:
    struct Voice
    {
        Key key;
        double tolerance = 0.0;
        std::array<std::unique_ptr<console::CardChannel>, 2> early, later;

        console::CardChannel& card (int c) noexcept { return key.later ? *later[(size_t) c] : *early[(size_t) c]; }
    };

    console::Pots channelPots (int c, double amount) const noexcept
    {
        if (amount <= 0.0)
            return pots;
        const auto& u = spread[(size_t) c];
        const auto t = maxSpread * amount;
        auto p = pots;
        int k = 0;
        auto scaled = [&] (double& x) { x = std::clamp (x * (1.0 + t * u[(size_t) k++]), 0.0, 1.0); };
        auto centred = [&] (double& x) { x = std::clamp (0.5 + (x - 0.5) * (1.0 + t * u[(size_t) k++]), 0.0, 1.0); };
        scaled (p.hpf);
        scaled (p.lpf);
        for (auto* b : { &p.hf, &p.hmf, &p.lmf, &p.lf })
        {
            centred (b->gain);
            scaled (b->freq);
        }
        scaled (p.hmf.q);
        scaled (p.lmf.q);
        return p;
    }

    /** The pots on the switches and the spread a voice is built for. */
    console::Pots voicePots (const Voice& v, int c) const noexcept
    {
        auto p = channelPots (c, v.tolerance);
        p.later = v.key.later;
        p.hpfIn = v.key.hpfIn;
        p.lpfIn = v.key.lpfIn;
        p.hf.bell = v.key.hfBell;
        p.lf.bell = v.key.lfBell;
        return p;
    }

    /** Starts building a voice for the current key, one bounded step per tick(). */
    void start (int index) noexcept
    {
        target = index;
        voices[(size_t) index].key = key;
        voices[(size_t) index].tolerance = tolerance;
        stepChannel = 0;
        cardStarted = false;
    }

    /** One bounded design step of the voice being built; true when the voice is complete. */
    bool advance() noexcept
    {
        auto& v = voices[(size_t) target];
        // A voice off EQ In runs no network; a linked one runs channel 0's for both.
        if (! v.key.eqIn)
            return true;
        auto& card = v.card (stepChannel);
        if (! cardStarted)
            card.beginReset (voicePots (v, stepChannel));
        cardStarted = ! card.resetStep();
        if (cardStarted)
            return false;
        return ++stepChannel >= (v.key.linked ? 1 : numChannels);
    }

    /** Every stepSamples: builds the first voice, or takes a switch change one step further. */
    void tick() noexcept
    {
        if (! ready)
        {
            if (advance())
            {
                ready = true;
                align (voices[(size_t) active]);
                setPots (voices[(size_t) active]);
            }
            return;
        }
        if (building || warming > 0)
        {
            if (key == voices[(size_t) active].key)
            {
                building = false;       // changed back before the new voice was heard
                warming = 0;
                return;
            }
            if (! (key == voices[(size_t) target].key))
            {
                start (target);
                building = true;
                warming = 0;
            }
            if (building)
            {
                if (advance())
                    warmUp();
                return;
            }
            if (--warming == 0)
                fadeIn();
            return;
        }
        // A switch change waits for a running crossfade to finish.
        if (fadeRemaining == 0 && ! (key == voices[(size_t) active].key))
        {
            start (1 - active);
            building = true;
            if (advance())
                warmUp();
        }
    }

    /** The new networks start from rest: they run unheard on the input first, so the transient of
        starting (the bands ringing up) has died away before they are heard. */
    void warmUp() noexcept
    {
        building = false;
        warming = warmTicks;
        align (voices[(size_t) target]);
    }

    /** A voice's networks onto the stage's clock, as they start to run (Network::align()). */
    void align (Voice& v) noexcept
    {
        for (int c = 0; c < 2; ++c)
            v.card (c).align (clock);
    }

    void fadeIn() noexcept
    {
        building = false;
        warming = 0;
        active = target;
        fadeRemaining = fadeLength;
        setPots (voices[(size_t) active]);
    }

    void render (double* const* channels, int numCh, int n) noexcept
    {
        if (! ready)
            return;
        auto& now = voices[(size_t) active];
        std::array<double*, 2> copy { previous[0].data(), previous[1].data() };
        if (warming > 0)
        {
            for (int c = 0; c < numCh; ++c)
                std::copy (channels[c], channels[c] + n, copy[(size_t) c]);
            run (voices[(size_t) target], copy.data(), numCh, n);
        }
        if (fadeRemaining > 0)
        {
            // The fading set keeps the pots it had: after Alt the current ones are the other card's.
            auto& old = voices[(size_t) (1 - active)];
            for (int c = 0; c < numCh; ++c)
                std::copy (channels[c], channels[c] + n, copy[(size_t) c]);
            run (old, copy.data(), numCh, n);
            run (now, channels, numCh, n);
            for (int i = 0; i < n; ++i)
            {
                // A raised cosine: no slope where the fade starts or ends.
                const auto t = 1.0 - (double) std::max (0, fadeRemaining - i) / (double) fadeLength;
                const auto p = 0.5 - 0.5 * std::cos (std::numbers::pi * t);
                for (int c = 0; c < numCh; ++c)
                    channels[c][i] = copy[(size_t) c][i] + p * (channels[c][i] - copy[(size_t) c][i]);
            }
            fadeRemaining = std::max (0, fadeRemaining - n);
            return;
        }
        run (now, channels, numCh, n);
    }

    /**
        The current pots, on the switches the voice was built for. Pots for the other card leave it
        as it is, and so does a Tolerance on the other side of 0 (the voice keeps its spread while
        it fades out).
    */
    void setPots (Voice& v) noexcept
    {
        if (! v.key.eqIn || v.key.later != pots.later || v.key.linked != (tolerance <= 0.0))
            return;
        v.tolerance = tolerance;
        for (int c = 0; c < (v.key.linked ? 1 : numChannels); ++c)
            v.card (c).setPots (voicePots (v, c));
    }

    void run (Voice& v, double* const* ch, int numCh, int n) noexcept
    {
        if (! v.key.eqIn)
            return;
        if (numCh == 1 || v.key.stereo == StereoMode::stereo)
        {
            if (v.key.linked)
            {
                v.card (0).process (ch, numCh, n);
                return;
            }
            if (numCh == 2)
                v.card (0).processPair (v.card (1), ch[0], ch[1], n);
            else
                v.card (0).process (ch[0], n);
            return;
        }
        // Mid or Side: only that half goes through the network; the amps stay L/R.
        for (int i = 0; i < n; ++i)
        {
            mid[(size_t) i] = 0.5 * (ch[0][i] + ch[1][i]);
            side[(size_t) i] = 0.5 * (ch[0][i] - ch[1][i]);
        }
        v.card (0).process (v.key.stereo == StereoMode::mid ? mid.data() : side.data(), n);
        for (int i = 0; i < n; ++i)
        {
            ch[0][i] = mid[(size_t) i] + side[(size_t) i];
            ch[1][i] = mid[(size_t) i] - side[(size_t) i];
        }
    }

    std::array<Voice, 2> voices;
    int active = 0, numChannels = 2, fadeLength = 1, fadeRemaining = 0, warmTicks = 1, warming = 0;
    int target = 0, stepChannel = 0, gridPhase = 0;
    std::int64_t clock = 0;     // samples since the first build or the last reset
    bool ready = false, building = false, cardStarted = false;
    console::Pots pots;
    Key key;
    double tolerance = 0.0, noise = 0.0;
    std::array<std::array<double, numSpread>, 2> spread {};
    std::array<HissAndHum, 2> noiseSources;
    std::array<std::vector<double>, 2> previous;
    std::vector<double> mid, side;
};

//==============================================================================
ConsoleModel::ConsoleModel()
    : input (std::make_unique<AmpStage> (inputAmpParams())), output (std::make_unique<AmpStage> (summingAmpParams())),
      network (std::make_unique<NetworkStage>())
{
    list = { input.get(), network.get(), output.get() };
}

ConsoleModel::~ConsoleModel() = default;

double ConsoleModel::voltsPerUnit() noexcept
{
    return dbuToVolts (18.0) * std::sqrt (2.0);
}

myzel::dsp::LineAmpParams ConsoleModel::inputAmpParams() noexcept
{
    return { 1.0, openLoopGain, swing, 1.0, slewRate };
}

myzel::dsp::LineAmpParams ConsoleModel::summingAmpParams() noexcept
{
    // Inverting, four 4K75 at its virtual earth: the loop sees a quarter of the output.
    return { 1.0, 0.25 * openLoopGain, swing, 1.0, slewRate };
}

void ConsoleModel::prepare (double, int, int) {}

void ConsoleModel::build()
{
    input->reset();
    output->reset();
    network->build();
}

void ConsoleModel::beginBuild() noexcept
{
    input->reset();
    output->reset();
    network->beginBuild();
}

void ConsoleModel::reset() noexcept
{
    for (auto* s : list)
        s->reset();
}

bool ConsoleModel::ready() const noexcept { return network->isReady(); }

void ConsoleModel::setSettings (const ModelSettings& s) noexcept
{
    network->settings (s);
}

std::complex<double> ConsoleModel::networkResponse (double hz) const noexcept { return network->response (hz, false); }

std::complex<double> ConsoleModel::networkAnalogResponse (double hz) const noexcept { return network->response (hz, true); }

bool ConsoleModel::isSwitching() const noexcept { return network->switching(); }

} // namespace viridian::dsp
