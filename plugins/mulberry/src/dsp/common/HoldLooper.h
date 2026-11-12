#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace mulberry::dsp
{

/**
    Hold: an exact sample loop of the last loop cycle of the wet signal.

    The wet signal is recorded continuously. Hold freezes the recording and plays the last cycle
    (per channel) over and over, with a 20 ms seam into the material just before the cycle, so
    the loop never recirculates through an interpolator or filter: no loss, no pitch drift,
    however long it is held. The seam keeps the power: its law follows the measured correlation
    of the two sides (equal gain for correlated material, equal power for uncorrelated), eased so
    it starts and ends without a kink. Releasing Hold lets the loop decay by the feedback gain per
    cycle while the machine takes over again; holding again while it decays fades the old loop
    out under the new one.
*/
class HoldLooper
{
public:
    static constexpr float seamMs = 20.0f;
    static constexpr float fadeMs = 20.0f;
    static constexpr float silence = 1.0e-4f;   // -80 dB: a released loop is over

    void prepare (double sampleRate, double maxCycleSeconds)
    {
        fs = sampleRate;
        seam = std::max (1, (int) std::lround (seamMs * 0.001 * fs));
        size = (int) std::ceil ((maxCycleSeconds + 1.0) * fs) + 2 * seam;
        for (auto& b : buffers)
            b.assign ((size_t) size, 0.0f);
        fadeStep = 1.0f / (float) std::max (1.0, fadeMs * 0.001 * fs);
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);
        writeIndex = 0;
        recorded = 0;
        holding = false;
        voice = old = {};
        liveFade = 1.0f;
    }

    bool isHolding() const noexcept { return holding; }

    /** 0 .. 1: how far the held loop has taken over from the live signal (meters, UI). */
    float holdAmount() const noexcept { return holding ? 1.0f - liveFade : 0.0f; }

    /**
        One sample. lengths: the loop cycle per channel in samples (read when Hold engages).
        decayPerCycle: the feedback gain, applied once per cycle after release.
    */
    void process (float& left, float& right, bool hold, std::array<double, 2> lengths, float decayPerCycle) noexcept
    {
        if (hold && ! holding)
            engage (lengths);
        else if (! hold && holding)
            release (decayPerCycle);

        const std::array<float, 2> live { left, right };

        if (! holding && ! voice.playing && ! old.playing)
        {
            record (live);
            return;
        }

        liveFade = std::clamp (liveFade + (holding ? -fadeStep : fadeStep), 0.0f, 1.0f);
        const auto x = eased (liveFade) * 0.5f * std::numbers::pi_v<float>;
        const auto liveGain = std::sin (x);

        std::array<float, 2> out { liveGain * live[0], liveGain * live[1] };
        play (voice, out);
        play (old, out);
        left = out[0];
        right = out[1];

        if (! holding)
        {
            record (live);
            ++sinceRelease;
        }
    }

private:
    struct Channel
    {
        int start = 0, length = 1, position = 0, seamLength = 0;
        float correlation = 0.0f;
    };

    struct Voice
    {
        std::array<Channel, 2> channels {};
        bool playing = false;
        float fadeIn = 0.0f;        // eased, equal power against the live signal
        float gain = 1.0f;          // released: decays per cycle
        float decay = 1.0f;         // per sample
        float fadeOut = 1.0f;       // a voice replaced by a new Hold fades out
        bool replaced = false;
    };

    static float eased (float t) noexcept { return t * t * (3.0f - 2.0f * t); }

    void record (const std::array<float, 2>& x) noexcept
    {
        buffers[0][(size_t) writeIndex] = x[0];
        buffers[1][(size_t) writeIndex] = x[1];
        if (++writeIndex == size)
            writeIndex = 0;
        recorded = std::min (recorded + 1, (long long) size);
    }

    int wrap (int i) const noexcept
    {
        i %= size;
        return i < 0 ? i + size : i;
    }

    void engage (std::array<double, 2> lengths) noexcept
    {
        if (voice.playing)
        {
            old = voice;
            old.replaced = true;
            old.fadeOut = 1.0f;
        }

        voice = {};
        for (size_t c = 0; c < 2; ++c)
        {
            auto& ch = voice.channels[c];
            const auto available = (int) std::max<long long> (0, recorded - seam - 1);
            ch.length = std::clamp ((int) std::lround (lengths[c]), 1, std::max (1, std::min (available, size - 2 * seam - 1)));
            ch.start = wrap (writeIndex - ch.length);
            ch.position = 0;
            ch.seamLength = std::min (seam, ch.length / 2);

            // Correlation of the cycle's end with what came just before its start.
            const auto& b = buffers[c];
            double ab = 0.0, aa = 0.0, bb = 0.0;
            for (int i = 0; i < ch.seamLength; ++i)
            {
                const auto x = (double) b[(size_t) wrap (ch.start + ch.length - ch.seamLength + i)];
                const auto y = (double) b[(size_t) wrap (ch.start - ch.seamLength + i)];
                ab += x * y;
                aa += x * x;
                bb += y * y;
            }
            ch.correlation = (float) std::clamp (ab / std::sqrt (aa * bb + 1.0e-30), 0.0, 1.0);
        }

        voice.playing = true;
        holding = true;
    }

    void release (float decayPerCycle) noexcept
    {
        holding = false;
        sinceRelease = 0;
        const auto cycle = (double) std::max (voice.channels[0].length, voice.channels[1].length);
        voice.decay = (float) std::pow ((double) std::clamp (decayPerCycle, 0.0f, 0.999f), 1.0 / cycle);
    }

    /** After release the recorder moves on and would reach a held cycle: fade it out before that. */
    float guard (const Voice& v) const noexcept
    {
        if (holding && &v == &voice)
            return 1.0f;
        const auto longest = std::max (v.channels[0].length, v.channels[1].length) + seam;
        const auto room = (long long) size - longest - (long long) seam;
        const auto remaining = room - sinceRelease;
        if (remaining <= 0)
            return 0.0f;
        return std::min (1.0f, (float) remaining / (float) seam);
    }

    void play (Voice& v, std::array<float, 2>& out) noexcept
    {
        if (! v.playing)
            return;

        v.fadeIn = std::min (1.0f, v.fadeIn + fadeStep);
        const auto inGain = std::sin (eased (v.fadeIn) * 0.5f * std::numbers::pi_v<float>);

        if (v.replaced)
            v.fadeOut = std::max (0.0f, v.fadeOut - fadeStep);
        else if (&v == &voice && ! holding)
            v.gain *= v.decay;

        const auto g = inGain * v.gain * eased (v.fadeOut) * guard (v);

        for (size_t c = 0; c < 2; ++c)
            out[c] += g * next (v.channels[c], buffers[c]);

        const auto held = holding && &v == &voice;
        if ((v.replaced && v.fadeOut <= 0.0f) || (! held && (v.gain < silence || guard (v) <= 0.0f)))
            v.playing = false;
    }

    float next (Channel& ch, const std::vector<float>& b) const noexcept
    {
        const auto p = ch.position;
        auto y = b[(size_t) wrap (ch.start + p)];
        const auto fadeFrom = ch.length - ch.seamLength;

        if (ch.seamLength > 0 && p >= fadeFrom)
        {
            // Into the material just before the cycle, which leads into its start.
            const auto t = ((float) (p - fadeFrom) + 0.5f) / (float) ch.seamLength;
            const auto w = eased (t);
            const auto a = 1.0f - w;
            const auto rho = ch.correlation;
            const auto norm = 1.0f / std::sqrt (a * a + w * w + 2.0f * rho * a * w);
            y = norm * (a * y + w * b[(size_t) wrap (ch.start + p - ch.length)]);
        }

        if (++ch.position >= ch.length)
            ch.position = 0;
        return y;
    }

    double fs = 48000.0;
    int size = 1, seam = 1;
    std::array<std::vector<float>, 2> buffers;
    int writeIndex = 0;
    long long recorded = 0;
    bool holding = false;
    Voice voice, old;
    float liveFade = 1.0f, fadeStep = 0.01f;
    long long sinceRelease = 0;
};

} // namespace mulberry::dsp
