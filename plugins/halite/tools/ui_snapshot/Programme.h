#pragma once

#include "plugin/PluginProcessor.h"

#include <array>
#include <cmath>

/**
    A synthetic voice for the snapshot tool, played through the real processor, so the display feed,
    the events and Learn hold real data (src/dsp/feed/NOTES.md): syllables of harmonic vowels with a
    gliding pitch, some with a strong upper-mid formant (the Harsh band's business), esses of graded
    strength (from well under the default threshold to well over it, their noise centred in the
    sibilance band), /ʃ/-like esses lower in it,
    and pauses. Deterministic: the same audio on every run. play() continues where it stopped.
    With a transport it is also the processor's play head, playing from a song position.
*/
class Programme final : private juce::AudioPlayHead
{
public:
    explicit Programme (double sampleRate = 48000.0) : rate (sampleRate)
    {
        // Scattered harmonic phases: in phase they sum to a sawtooth, far peakier than a recorded vowel,
        // and display columns narrower than its period hatch the waveform.
        juce::Random phases { 0x9a5e };
        for (auto& p : harmonicPhase)
            p = juce::MathConstants<double>::twoPi * phases.nextDouble();
    }

    ~Programme() override
    {
        if (processor != nullptr)
            processor->setPlayHead (nullptr);
    }

    /** From here on the host plays, from `songSeconds` on (the events then carry song positions). */
    void playTransport (halite::HaliteProcessor& p, double songSeconds)
    {
        processor = &p;
        songStart = (std::int64_t) std::llround (songSeconds * rate) - played;
        p.setPlayHead (this);
    }

    /** Off: vowels and pauses only, as a sung passage (Learn then finds too few esses). */
    void setEsses (bool on) { esses = on; }

    void prepare (halite::HaliteProcessor& p, int blockSize = 512)
    {
        block = blockSize;
        p.setPlayConfigDetails (2, 2, rate, block);
        p.prepareToPlay (rate, block);
        buffer.setSize (2, block);
    }

    /** Plays whole blocks until `seconds` more have gone through (or a little over: the clock keeps the rest). */
    void play (halite::HaliteProcessor& p, double seconds)
    {
        juce::MidiBuffer midi;
        target += seconds * rate;
        while ((double) played < target)
        {
            for (int i = 0; i < block; ++i)
            {
                const auto s = next();
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            p.processBlock (buffer, midi);
            played += block;
        }
    }

private:
    enum class Kind { vowel, ess, sh, pause };

    struct Segment
    {
        Kind kind = Kind::pause;
        int length = 0;
        float level = 0.0f;
        double pitch = 180.0;
        bool harsh = false;
    };

    // A two-pole band-pass (RBJ, constant peak gain), for the /ʃ/ noise.
    struct BandPass
    {
        void set (double hz, double q, double fs)
        {
            const auto w = juce::MathConstants<double>::twoPi * hz / fs, alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
            b0 = alpha / a0;
            a1 = -2.0 * std::cos (w) / a0;
            a2 = (1.0 - alpha) / a0;
        }

        float process (float x)
        {
            const auto y = b0 * x - b0 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return (float) y;
        }

        double b0 = 0.0, a1 = 0.0, a2 = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    };

    Segment plan()
    {
        Segment s;
        const auto ms = [this] (double v) { return (int) (v * 0.001 * rate); };
        const auto previous = current.kind;

        if (previous == Kind::vowel && random.nextFloat() < (esses ? 0.55f : 0.0f))
        {
            // Ess strength spread evenly in dB over 24 dB, so about a third stay under the default threshold.
            s.kind = random.nextFloat() < 0.75f ? Kind::ess : Kind::sh;
            s.length = ms (60.0 + 80.0 * random.nextDouble());
            s.level = (s.kind == Kind::sh ? 0.12f : 0.32f) * std::pow (10.0f, (-28.0f + 24.0f * random.nextFloat()) / 20.0f);
        }
        else if (previous != Kind::pause && random.nextFloat() < 0.18f)
        {
            s.kind = Kind::pause;
            s.length = ms (120.0 + 300.0 * random.nextDouble());
        }
        else
        {
            s.kind = Kind::vowel;
            s.length = ms (120.0 + 220.0 * random.nextDouble());
            s.level = 0.11f + 0.11f * random.nextFloat();
            s.pitch = 150.0 + 90.0 * random.nextDouble();
            s.harsh = random.nextFloat() < 0.3f;
        }
        return s;
    }

    float next()
    {
        if (position >= current.length)
        {
            current = plan();
            position = 0;
            if (current.kind == Kind::sh)
                shNoise.set (3600.0 + 800.0 * random.nextDouble(), 1.4, rate);
            else if (current.kind == Kind::ess)
                essNoise.set (6500.0 + 2000.0 * random.nextDouble(), 0.8, rate);
        }

        const auto t = (double) position++ / std::max (1, current.length);
        const auto white = random.nextFloat() * 2.0f - 1.0f;
        auto out = 1.0e-4f * white;   // a faint room

        switch (current.kind)
        {
            case Kind::vowel:
            {
                const auto envelope = std::sin (juce::MathConstants<double>::pi * std::pow (t, 0.7));
                phase += juce::MathConstants<double>::twoPi * current.pitch * (1.0 + 0.04 * std::sin (t * 3.0)) / rate;
                auto v = 0.0;
                for (int h = 1; h * current.pitch < 9000.0; ++h)
                {
                    const auto hz = h * current.pitch;
                    auto gain = 1.0 / h * (hz > 500.0 && hz < 1200.0 ? 1.6 : 1.0);
                    if (current.harsh && hz > 2400.0 && hz < 4200.0)
                        gain *= 9.0;
                    v += gain * std::sin (phase * h + harmonicPhase[(size_t) h % harmonicPhase.size()]);
                }
                out += (float) (current.level * envelope * v);
                break;
            }
            case Kind::ess:
            case Kind::sh:
            {
                const auto envelope = std::sin (juce::MathConstants<double>::pi * t);
                const auto noise = current.kind == Kind::ess ? 2.4f * essNoise.process (white) : 2.2f * shNoise.process (white);
                out += current.level * (float) envelope * noise;
                break;
            }
            case Kind::pause:
                break;
        }
        return out;
    }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setTimeInSamples (songStart + played);
        info.setTimeInSeconds ((double) (songStart + played) / rate);
        info.setIsPlaying (true);
        return info;
    }

    double rate, target = 0.0;
    std::int64_t played = 0, songStart = 0;
    halite::HaliteProcessor* processor = nullptr;
    bool esses = true;
    int block = 512;
    juce::AudioBuffer<float> buffer;
    juce::Random random { 0x4a11e };
    Segment current;
    int position = 0;
    double phase = 0.0;
    std::array<double, 64> harmonicPhase {};
    BandPass essNoise, shNoise;
};

/** Prepares the processor and plays the first `seconds` of the programme (the self-test's short form). */
inline void playProgramme (halite::HaliteProcessor& p, double seconds, double rate = 48000.0)
{
    Programme programme (rate);
    programme.prepare (p);
    programme.play (p, seconds);
}
