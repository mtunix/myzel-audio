#include "dsp/algorithms/choir/ChoirEngine.h"

namespace cerulean::dsp
{

namespace
{
    // Tank lengths (ms at Size 1x): 31-127 ms, mean ~70 ms, no small common factors. A medium
    // hall: the voices need a dense, smooth tail to sing from, not a huge wash.
    constexpr std::array<float, 16> tankMs {
        31.13f, 35.69f, 39.89f, 44.71f, 48.97f, 53.59f, 58.43f, 63.71f,
        69.31f, 75.07f, 81.29f, 87.83f, 95.27f, 103.31f, 114.59f, 127.21f
    };

    // Input diffusers (ms at Size 1x): six short stages, slightly different per side, so the
    // late field is dense before the voices start ringing on it.
    constexpr std::array<float, ChoirEngine::diffuserStages> diffuseLMs { 2.71f, 3.97f, 5.81f, 8.23f, 11.93f, 16.07f };
    constexpr std::array<float, ChoirEngine::diffuserStages> diffuseRMs { 3.13f, 4.43f, 6.37f, 9.07f, 12.79f, 17.53f };

    constexpr float maxSizeScale = 2.0f;

    // Per voice: vocal-tract scale of the formants (tenor, alto, bass, soprano-ish), base delay,
    // vibrato rate, pan and stereo width. Voice 1 sits in the middle and fairly narrow (a solo
    // voice); each added voice takes its own place, so more voices = a wider, thicker choir.
    struct VoiceSetup
    {
        float scale, delayMs, vibratoHz, pan, width;
    };
    constexpr std::array<VoiceSetup, ChoirEngine::maxVoices> voiceSetup {{
        { 1.00f,  4.1f, 5.1f,  0.00f, 0.35f },
        { 1.08f, 11.3f, 4.7f, -0.55f, 0.70f },
        { 0.93f, 17.9f, 5.6f,  0.55f, 0.70f },
        { 1.15f, 25.1f, 4.4f,  0.00f, 1.00f },
    }};

    constexpr float vibratoMs = 0.35f;     // peak, at Mod 1 (~ +-20 cents at 5 Hz)
    constexpr float detuneMs = 1.6f;       // peak slow drift, at Mod 1 (ensemble detune)
    constexpr float maxVoiceDelayMs = 40.0f;

    constexpr float vowelWanderDepth = 1.2f;   // vowel positions, at Mod 1 (whole choir)
    constexpr float vowelDriftDepth = 0.25f;   // vowel positions, at Mod 1 (per voice)

    // Wet blend (power-complementary): mostly voices, enough plain tail that it stays a reverb.
    constexpr float plainMix = 0.45f;
    constexpr float voiceMix = 0.89f;
    constexpr float outputGain = 2.2f;     // sets the wet level (steady pink noise, -3 dB)

    /** Reflects a drifting vowel position back into 0..4, so drift never sticks at an end. */
    float reflectVowel (float x) noexcept
    {
        if (x < 0.0f)
            x = -x;
        if (x > 4.0f)
            x = 8.0f - x;
        return std::clamp (x, 0.0f, 4.0f);
    }
}

void ChoirEngine::prepare (double sampleRate, int)
{
    fs = sampleRate;

    diffuserL.prepare (sampleRate, diffuseLMs, maxSizeScale);
    diffuserR.prepare (sampleRate, diffuseRMs, maxSizeScale);

    tank.prepare (sampleRate, { .delayMs = tankMs, .maxScale = maxSizeScale, .maxModMs = 2.0f,
                                .nominalModHz = 0.45f, .seed = seed * 13u + 5u,
                                .mixing = Fdn<lines>::Mixing::hadamard });

    const auto maxDelay = (int) std::ceil (msToSamples (maxVoiceDelayMs, sampleRate)) + 8;
    for (int k = 0; k < maxVoices; ++k)
    {
        auto& v = voices[(size_t) k];
        v.lineL.setMaxDelay (maxDelay);
        v.lineR.setMaxDelay (maxDelay);
        v.bank.prepare (sampleRate);
        v.vibrato.setStartPhase (0.27f * (float) k);
        v.detune.setSeed (seed * 4099u + (uint32_t) k * 613u + 1u);
        v.vowelDrift.setSeed (seed * 6151u + (uint32_t) k * 331u + 7u);
    }
    vowelWander.setSeed (seed * 1031u + 3u);

    reset();
}

void ChoirEngine::reset()
{
    bandwidthL.reset();
    bandwidthR.reset();
    diffuserL.reset();
    diffuserR.reset();
    tank.reset();
    for (auto& v : voices)
    {
        v.lineL.reset();
        v.lineR.reset();
        v.bank.reset();
        v.vibrato.reset();
        v.detune.reset();
        v.vowelDrift.reset();
        v.level.reset();
        v.delayStep = 0.0f;
    }
    vowelWander.reset();
    diffusionGain.reset();
    snapNext = true;
}

void ChoirEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                           int numSamples, const EngineParams& p) noexcept
{
    // Control rate ---------------------------------------------------------------
    const auto n = std::max (numSamples, 1);
    const auto freeze = std::clamp (p.freeze, 0.0f, 1.0f);
    const auto mod = maps::clamp01 (p.mod);

    const auto bandwidthHz = std::clamp (9500.0f * std::pow (2.0f, 0.9f * p.tone), 4000.0f, 18000.0f);
    bandwidthL.setCutoff (bandwidthHz, fs);
    bandwidthR.setCutoff (bandwidthHz, fs);

    diffuserL.setScale (p.sizeScale);
    diffuserR.setScale (p.sizeScale);
    diffuserL.advance (numSamples);
    diffuserR.advance (numSamples);
    diffusionGain.set (0.74f * std::sqrt (maps::clamp01 (p.diffusion)), numSamples);

    tank.update (numSamples, { .scale = p.sizeScale, .rt60 = p.decaySeconds, .lowEnd = p.lowEnd,
                               .high = maps::toneToHighDecay (p.tone), .freeze = freeze,
                               .modDepthMs = 1.4f * mod, .modRate = p.modRate,
                               .randomBlend = 0.65f });

    // Voices. Their modulators slow to a halt with Freeze (they hold where they are), so the
    // frozen cloud is static: no vowel drift, no vibrato.
    const auto motion = 1.0f - freeze;
    vowelWander.setRate (0.17f * p.modRate * motion, fs);
    const auto wander = vowelWander.advance (n);
    const auto vowel = maps::vowelPosition (p.paramA);
    const auto count = maps::voiceCount (p.paramB);

    float power = 0.0f;
    for (int k = 0; k < maxVoices; ++k)
        power += voiceLevel (k, count) * voiceLevel (k, count);
    const auto norm = 1.0f / std::sqrt (power);

    for (int k = 0; k < maxVoices; ++k)
    {
        auto& v = voices[(size_t) k];
        const auto& s = voiceSetup[(size_t) k];

        v.vibrato.setRate (s.vibratoHz * motion, fs);
        v.detune.setRate (0.35f * (1.0f + 0.13f * (float) k) * p.modRate * motion, fs);
        v.vowelDrift.setRate (0.23f * (1.0f + 0.17f * (float) k) * p.modRate * motion, fs);
        const auto vib = v.vibrato.advance (n);
        const auto det = v.detune.advance (n);
        const auto drift = v.vowelDrift.advance (n);

        const auto end = msToSamples (s.delayMs * std::sqrt (p.sizeScale) + mod * (vibratoMs * vib + detuneMs * det), fs);
        if (snapNext)
            v.delay = end;
        v.delayStep = (end - v.delay) / (float) n;

        const auto position = reflectVowel (vowel + mod * (vowelWanderDepth * wander + vowelDriftDepth * drift));
        v.bank.setTarget (choir::vowelAt (position), s.scale, n);
        v.level.set (voiceLevel (k, count) * norm, n);
    }
    snapNext = false;

    // Audio rate -----------------------------------------------------------------
    constexpr float inject = 1.0f / 4.0f;   // 1 / sqrt (lines)
    constexpr float tapScale = 1.0f / 4.0f;
    std::array<float, lines> in, y;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = diffusionGain.next();
        const auto dl = diffuserL.process (bandwidthL.lowpass (inL[i]), g) * inject;
        const auto dr = diffuserR.process (bandwidthR.lowpass (inR[i]), g) * inject;

        for (int l = 0; l < lines; ++l)
            in[(size_t) l] = hadamardSign (1, l) * dl + hadamardSign (2, l) * dr;

        tank.process (in.data(), y.data());

        float plainL = 0.0f, plainR = 0.0f;
        for (int l = 0; l < lines; ++l)
        {
            plainL += hadamardSign (3, l) * y[(size_t) l];
            plainR += hadamardSign (5, l) * y[(size_t) l];
        }

        float choirL = 0.0f, choirR = 0.0f;
        for (int k = 0; k < maxVoices; ++k)
        {
            auto& v = voices[(size_t) k];
            const auto& s = voiceSetup[(size_t) k];

            float tapL = 0.0f, tapR = 0.0f;
            for (int l = 0; l < lines; ++l)
            {
                tapL += hadamardSign (6 + 2 * k, l) * y[(size_t) l];
                tapR += hadamardSign (7 + 2 * k, l) * y[(size_t) l];
            }
            v.lineL.write (tapL * tapScale);
            v.lineR.write (tapR * tapScale);

            v.delay += v.delayStep;
            const auto level = v.level.next();
            if (level <= 0.0f)
                continue;   // silent voice: its delay line keeps running, its filters rest

            float vl, vr;
            v.bank.process (v.lineL.readCubic (v.delay), v.lineR.readCubic (v.delay), vl, vr);

            const auto mid = 0.5f * (vl + vr), side = 0.5f * (vl - vr) * s.width;
            choirL += level * (mid * std::sqrt (1.0f - s.pan) + side);
            choirR += level * (mid * std::sqrt (1.0f + s.pan) - side);
        }

        outL[i] = outputGain * (plainMix * tapScale * plainL + voiceMix * choirL);
        outR[i] = outputGain * (plainMix * tapScale * plainR + voiceMix * choirR);
    }
}

} // namespace cerulean::dsp
