#pragma once

#include "dsp/common/CaptureBuffer.h"
#include "dsp/modules/Module.h"
#include "dsp/modules/bits/BitsModule.h"

#include "common/dsp/Oversampler2x.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace inkcap::dsp
{

/**
    The dry that Mix blends with the wet (PLAN.md, Settled 53). The wet passes Crush's 2x pair,
    which is minimum phase (3.07 samples of delay at low frequencies, 4 and more near the band's
    top), and Bits' converter (its filters, its hold and one host sample); an integer-delayed dry
    combs against both as soon as Mix is below 100 %. Here the dry takes the same paths with
    nothing in them:

    - Crush: the module's clean ring, or an identical 2x pair with no stage between its halves,
      blended by the module's own share of each sample. The pair's phase is matched exactly; a
      tape head's delay (its group delay at 2 kHz) follows as a fractional delay.
    - Bits: a fractional delay (windowed sinc) of the converter's delay, blended by its share.
    - Warble's rest and every other module: an integer delay.

    Where Crush and Bits are at rest (Off, Damage 0) this is the integer-delayed dry to the bit,
    so the Damage-0 null holds at every Mix. While Mix rests at 100 % nobody listens: only the
    delay lines run (the pair starts afresh, under Mix's glide, when it is needed again). No
    allocation after prepare().
*/
class MatchedDry
{
public:
    /** `pairLatency`: Crush's clean ring; `latency`: the chain's whole latency. */
    void prepare (double sampleRate, int pairLatency, int latency)
    {
        pairDelay = std::clamp (pairLatency, 1, (int) channels[0].ring.size());
        exactDelay = std::max (latency - pairDelay, 0);
        maxRead = (double) exactDelay + std::ceil ((BitsModule::maxConverterDelaySeconds + maxHeadDelaySeconds) * sampleRate);
        capture.prepare (maxChannels, (int) maxRead + 4);
        reset();
    }

    /** Mix at rest on 100 %: keeps the delay lines' history; `dry` is left as it is. */
    void skip (const float* const* dry, int numChannels, int numSamples) noexcept
    {
        const auto nc = std::min (numChannels, maxChannels);
        if (pairLive)
            for (auto& c : channels)
                c.pair.reset();
        pairLive = false;

        for (int k = 0; k < numSamples; ++k)
        {
            for (int ch = 0; ch < nc; ++ch)
            {
                auto& c = channels[(size_t) ch];
                capture.write (ch, c.ring[(size_t) ringIndex]);
                c.ring[(size_t) ringIndex] = dry[ch][k];
            }
            if (++ringIndex == pairDelay)
                ringIndex = 0;
        }
    }

    void reset() noexcept
    {
        for (auto& c : channels)
        {
            c.pair.reset();
            c.ring.fill (0.0f);
        }
        ringIndex = 0;
        pairLive = false;
        capture.restart();
    }

    /**
        In place: dry[c][0 .. n) becomes the matched dry, delayed by the chain's latency. The shares
        are the modules' records of the same samples (-1: at rest).
    */
    void process (float* const* dry, int numChannels, int numSamples, const float* crushShares, const float* crushDelays,
                  const float* bitsShares, const float* bitsDelays) noexcept
    {
        const auto nc = std::min (numChannels, maxChannels);
        for (int k = 0; k < numSamples; ++k)
        {
            const auto crush = crushShares[k];
            if (crush < 0.0f && pairLive)
                for (auto& c : channels)
                    c.pair.reset();
            pairLive = crush >= 0.0f;

            // The fractional part: each module's delay by its share, blended in by the larger share
            // that has one, so it fades to the exact integer read as they come to rest.
            const auto crushWeight = crush > 0.0f && crushDelays[k] > 0.0f ? crush : 0.0f;
            const auto bitsWeight = std::max (bitsShares[k], 0.0f);
            const auto weight = std::max (crushWeight, bitsWeight);
            const auto extra = weight > 0.0f ? ((double) crushWeight * crushDelays[k] + (double) bitsWeight * bitsDelays[k]) / weight : 0.0;
            const auto read = std::min ((double) exactDelay + extra, maxRead);

            for (int ch = 0; ch < nc; ++ch)
            {
                auto& c = channels[(size_t) ch];
                const auto x = dry[ch][k];
                auto v = c.ring[(size_t) ringIndex];
                c.ring[(size_t) ringIndex] = x;

                if (pairLive)
                {
                    double a, b;
                    c.pair.upsample ((double) x, a, b);
                    v = OnOffFade::blend (v, (float) c.pair.downsample (a, b), crush);
                }

                capture.write (ch, v);
                const auto plain = capture.at (ch, exactDelay);
                dry[ch][k] = weight > 0.0f ? OnOffFade::blend (plain, capture.read (ch, read), weight) : plain;
            }

            if (++ringIndex == pairDelay)
                ringIndex = 0;
        }
    }

private:
    static constexpr int maxChannels = CaptureBuffer::maxChannels;
    static constexpr double maxHeadDelaySeconds = 0.001;

    struct Channel
    {
        myzel::dsp::Oversampler2x pair;
        std::array<float, 8> ring {};
    };

    std::array<Channel, (size_t) maxChannels> channels;
    CaptureBuffer capture;
    int pairDelay = 3, exactDelay = 6, ringIndex = 0;
    double maxRead = 64.0;
    bool pairLive = false;
};

} // namespace inkcap::dsp
