#include "dsp/algorithms/echo/EchoEngine.h"

namespace cerulean::dsp
{

namespace
{
    // Plate tank (ms at Size 1x): short, dense lines (9-47 ms, mean ~24 ms) for the instant,
    // bright build-up of a plate rather than the slow arrival of a room.
    constexpr std::array<float, 16> plateMs {
        9.13f, 10.37f, 11.71f, 13.19f, 14.83f, 16.57f, 18.43f, 20.47f,
        22.69f, 25.13f, 27.73f, 30.61f, 33.79f, 37.21f, 41.03f, 45.29f
    };
    // Short, strongly coupled input diffusers: the plate is diffuse from the first millisecond.
    constexpr std::array<float, 4> diffuseLMs { 1.43f, 2.17f, 3.31f, 4.79f };
    constexpr std::array<float, 4> diffuseRMs { 1.61f, 2.39f, 3.59f, 5.23f };

    constexpr float maxSizeScale = 2.0f;

    // Tape
    constexpr float minEchoMs = 10.0f;
    constexpr float maxWowMs = 12.0f;
    constexpr float glideTauMs = 200.0f;      // echo time follows like a tape motor
    constexpr float glideMaxSlew = 0.5f;      // samples per sample: at most an octave down / a fifth up while gliding
    constexpr float glideAccelMs = 30.0f;     // the motor's inertia: the glide's pitch bends in, never steps
    constexpr float wowHz = 0.55f;            // x Mod Rate
    constexpr float flutterHz = 6.3f;         // x sqrt (Mod Rate)
    constexpr float wowMaxDeviation = 0.008f; // peak pitch deviation at Mod 1 (14 cents)
    constexpr float flutterDeviation = 0.0012f;

    constexpr std::array<float, 3> headGain { 0.30f, 0.42f, 1.0f };
    constexpr std::array<float, 3> headPanL { 0.92f, 0.39f, 0.71f };   // T/3 left, 2T/3 right, T centre
    constexpr std::array<float, 3> headPanR { 0.39f, 0.92f, 0.71f };

    constexpr float drive = 0.6f;             // tape saturation starts around +-1.5 at the record head
    constexpr float echoOutputGain = 0.85f;   // fixed echo level: the main repeat sits ~4 dB under the note
    constexpr float dryToPlate = 0.6f;        // the plate hears the note itself, too
    constexpr float plateOutputGain = 0.3f;   // pink noise at Decay 2 s, default Repeats: ~-3 dB wet

    /** Plate amount follows Decay: a short Decay is a little ambience behind the echoes, a long one a wash. */
    float plateAmount (float decaySeconds) noexcept
    {
        const auto x = std::clamp ((std::log (std::max (decaySeconds, 0.1f)) - std::log (0.3f))
                                       / (std::log (5.0f) - std::log (0.3f)), 0.0f, 1.0f);
        return 0.45f + 0.55f * x * x * (3.0f - 2.0f * x);
    }
}

void EchoEngine::prepare (double sampleRate, int)
{
    fs = sampleRate;

    tape.setMaxDelay ((int) std::ceil (msToSamples (maps::echoMaxMs + maxWowMs + 2.0f, fs)) + 8);
    echoTime.prepare (fs, glideTauMs, glideAccelMs, glideMaxSlew);

    wowSine.setStartPhase (0.0f);
    flutterSine.setStartPhase (0.25f);
    wowRandom.setSeed (seed * 7919u + 11u);
    flutterRandom.setSeed (seed * 104729u + 23u);

    recordLowCut.setCutoff (40.0f, fs);
    loopLowLoss.setCutoff (110.0f, fs);

    diffuserL.prepare (sampleRate, diffuseLMs, maxSizeScale);
    diffuserR.prepare (sampleRate, diffuseRMs, maxSizeScale);
    tank.prepare (sampleRate, { .delayMs = plateMs, .maxScale = maxSizeScale, .maxModMs = 1.0f,
                                .nominalModHz = 0.9f, .seed = seed,
                                .mixing = Fdn<lines>::Mixing::hadamard });
    reset();
}

void EchoEngine::reset()
{
    tape.reset();
    echoTime.snap (msToSamples (300.0f, fs));
    timeNow = echoTime.value;
    modNow = modStep = 0.0f;
    snapNext = true;

    wowSine.reset();
    flutterSine.reset();
    wowRandom.reset();
    flutterRandom.reset();

    for (auto* f : { &recordLowCut, &recordHighCut, &loopHighLoss, &loopLowLoss, &playbackL, &playbackR,
                     &bandwidthL, &bandwidthR })
        f->reset();

    feedback.reset();
    echoLevel.reset();
    sendLevel.reset();
    reverbLevel.reset();
    diffusionGain.reset();

    diffuserL.reset();
    diffuserR.reset();
    tank.reset();
}

void EchoEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                          int numSamples, const EngineParams& p) noexcept
{
    const auto n = std::max (numSamples, 1);
    const auto freeze = std::clamp (p.freeze, 0.0f, 1.0f);
    const auto run = 1.0f - freeze;

    // Tape: time, wow & flutter ------------------------------------------------------
    const auto targetTime = msToSamples (std::clamp (p.echoTimeMs, minEchoMs, maps::echoMaxMs), fs);
    if (snapNext)
        echoTime.snap (targetTime);
    else
        echoTime.target = targetTime;

    const auto mod = maps::clamp01 (p.mod);
    const auto rate = std::clamp (p.modRate, 0.1f, 8.0f);
    const auto wowRate = wowHz * rate;
    const auto flutterRate = flutterHz * std::sqrt (rate);
    wowSine.setRate (wowRate, fs);
    wowRandom.setRate (wowRate * 1.3f, fs);
    flutterSine.setRate (flutterRate, fs);
    flutterRandom.setRate (flutterRate * 1.7f, fs);

    // Depth (samples) for a given peak pitch deviation: deviation = 2 pi f depth.
    const auto wowDepth = std::min (msToSamples (maxWowMs, fs) * 0.5f,
                                    wowMaxDeviation * mod * (float) fs / (twoPi * wowRate));
    const auto flutterDepth = flutterDeviation * (0.25f + 0.75f * mod) * (float) fs / (twoPi * flutterRate);
    const auto modEnd = wowDepth * (0.6f * wowSine.advance (n) + 0.4f * wowRandom.advance (n))
                      + flutterDepth * (0.7f * flutterSine.advance (n) + 0.3f * flutterRandom.advance (n));
    if (snapNext)
        modNow = modEnd;
    modStep = (modEnd - modNow) / (float) n;
    snapNext = false;

    // Tape tone: Tone moves the head/tape loss; the record and playback paths stay warm.
    const auto loopLossHz = 3600.0f * std::pow (2.0f, 0.75f * std::clamp (p.tone, -1.0f, 1.0f));
    loopHighLoss.setCutoff (loopLossHz, fs);
    recordHighCut.setCutoff (9000.0f, fs);
    const auto playbackHz = std::min (6500.0f * std::pow (2.0f, 0.6f * std::clamp (p.tone, -1.0f, 1.0f)), 0.45f * (float) fs);
    playbackL.setCutoff (playbackHz, fs);
    playbackR.setCutoff (playbackHz, fs);

    // Freeze: the echoes stop (no feedback, no output, no send), the plate holds.
    feedback.set (feedbackFor (p.paramB, std::max (targetTime, echoTime.value) * 1000.0f / (float) fs) * run, n);
    echoLevel.set (echoOutputGain * run, n);
    sendLevel.set (run, n);
    reverbLevel.set (plateOutputGain * plateAmount (p.decaySeconds), n);

    // Plate ------------------------------------------------------------------------
    const auto bandwidthHz = std::clamp (11000.0f * std::pow (2.0f, 0.8f * p.tone), 4000.0f, 18000.0f);
    bandwidthL.setCutoff (bandwidthHz, fs);
    bandwidthR.setCutoff (bandwidthHz, fs);
    diffuserL.setScale (p.sizeScale);
    diffuserR.setScale (p.sizeScale);
    diffuserL.advance (n);
    diffuserR.advance (n);
    diffusionGain.set (std::min (0.8f, 0.72f * std::sqrt (maps::clamp01 (p.diffusion)) + 0.08f), n);

    tank.update (n, { .scale = p.sizeScale, .rt60 = p.decaySeconds, .lowEnd = p.lowEnd,
                      .high = maps::toneToHighDecay (p.tone), .freeze = p.freeze,
                      .modDepthMs = 0.6f * mod, .modRate = p.modRate, .randomBlend = 0.5f });

    // Audio rate -------------------------------------------------------------------
    constexpr float inject = 1.0f / 4.0f;
    const auto minRead = 4.0f;

    for (int k = 0; k < numSamples; ++k)
    {
        timeNow = echoTime.next();
        modNow += modStep;

        // Heads (read before writing: the loop sees the main head of the previous pass).
        std::array<float, 3> head;
        for (size_t h = 0; h < 3; ++h)
            head[h] = tape.readCubic (std::max (minRead, headRatio[h] * timeNow + modNow));

        // Record path: input + darkened, thinned main head -> soft saturation -> tape.
        const auto x = 0.5f * (inL[k] + inR[k]);
        const auto rec = recordHighCut.lowpass (recordLowCut.highpass (x));
        const auto back = loopLowLoss.highpass (loopHighLoss.lowpass (head[2])) * feedback.next();
        tape.write (flushTiny (saturate (drive * (rec + back)) / drive));

        // Echo output: heads panned, playback head loss.
        float eL = 0.0f, eR = 0.0f;
        for (size_t h = 0; h < 3; ++h)
        {
            eL += headGain[h] * headPanL[h] * head[h];
            eR += headGain[h] * headPanR[h] * head[h];
        }
        eL = playbackL.lowpass (eL);
        eR = playbackR.lowpass (eR);

        // Plate: the echoes and some of the note, in series.
        const auto send = sendLevel.next();
        const auto pl = bandwidthL.lowpass (dryToPlate * inL[k] + send * eL);
        const auto pr = bandwidthR.lowpass (dryToPlate * inR[k] + send * eR);
        const auto g = diffusionGain.next();
        const auto dl = diffuserL.process (pl, g) * inject;
        const auto dr = diffuserR.process (pr, g) * inject;

        std::array<float, lines> in, y;
        for (int i = 0; i < lines; ++i)
            in[(size_t) i] = hadamardSign (1, i) * dl + hadamardSign (2, i) * dr;
        tank.process (in.data(), y.data());

        float sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < lines; ++i)
        {
            sumL += hadamardSign (3, i) * y[(size_t) i];
            sumR += hadamardSign (5, i) * y[(size_t) i];
        }

        const auto el = echoLevel.next(), rl = reverbLevel.next();
        outL[k] = el * eL + rl * sumL;
        outR[k] = el * eR + rl * sumR;
    }
}

} // namespace cerulean::dsp
