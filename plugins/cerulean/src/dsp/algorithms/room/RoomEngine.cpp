#include "dsp/algorithms/room/RoomEngine.h"

#include "dsp/Mappings.h"

#include <numbers>

namespace cerulean::dsp
{

namespace
{
    //==============================================================================
    // Geometry at Size 1x (metres). Dimensions with no simple ratios (1 : 0.75 : 0.42), so the
    // image sources never line up into a regular (fluttering) train.
    struct Vec { float x, y, z; };

    constexpr Vec roomDims { 7.3f, 5.5f, 3.1f };

    // Facing -x, so the listener's left is -y. Two sources (the two inputs) on a small stage,
    // slightly asymmetric; two subcardioid mics 0.6 m apart, angled +-37 degrees.
    constexpr std::array<Vec, 2> sourcePos {{ { 1.80f, 2.05f, 1.35f }, { 1.95f, 3.60f, 1.40f } }};
    constexpr std::array<Vec, 2> micPos {{ { 4.90f, 2.55f, 1.55f }, { 4.90f, 3.15f, 1.55f } }};
    constexpr std::array<Vec, 2> micAxis {{ { -0.8f, -0.6f, 0.0f }, { -0.8f, 0.6f, 0.0f } }};

    constexpr float speedOfSound = 343.0f;
    constexpr int maxOrder = 2;
    constexpr float maxSizeScale = 2.0f;

    //==============================================================================
    // Late tank (ms at Size 1x): sixteen lines of 21-66 ms, mutually prime-ish, each with a short
    // allpass inside. Sixteen rather than eight: at RT60 3 s eight 19-42 ms lines left the modes
    // ~4 Hz apart and the tail rang (spectral fine structure 12-14 dB std vs 7 dB for Hall).
    constexpr std::array<float, 16> lineMs { 21.1f, 23.7f, 26.3f, 28.9f, 31.7f, 34.3f, 37.1f, 39.9f,
                                             42.7f, 45.7f, 48.9f, 52.1f, 55.3f, 58.9f, 62.3f, 66.1f };
    constexpr std::array<float, 16> apMs { 2.3f, 4.7f, 3.1f, 5.3f, 1.9f, 4.1f, 2.9f, 5.9f,
                                           3.7f, 2.1f, 4.9f, 3.3f, 5.5f, 2.7f, 4.3f, 3.9f };

    constexpr float maxModMs = 0.6f;
    constexpr float nominalModHz = 0.6f;
    constexpr float modRandomBlend = 0.6f;

    // Early/late balance: equal power between thetaMin and pi/2 - thetaMin (-14 dB at the ends).
    constexpr float thetaMin = 0.2f;

    // Calibration: equal early and late energy at A = 0.5, Decay 1 s, Size 1x ("Even"), and the
    // overall level so steady pink noise at Decay 2 s comes out near -3 dB.
    constexpr float earlyCal = 0.755f;
    constexpr float lateCal = 0.582f;

    float imageCoordinate (int n, float s, float length) noexcept
    {
        // n-th image along one axis: |n| reflections.
        return (float) n * length + ((n & 1) != 0 ? length - s : s);
    }

    float reflectionCoefficient (float walls) noexcept { return 0.5f + 0.42f * walls; }

    inline float allpass (DelayLine& line, float x, float d, float g, float gamma) noexcept
    {
        const auto delayed = gamma * line.readCubic (d);
        const auto w = x + g * delayed;
        line.write (flushTiny (w));
        return delayed - g * w;
    }

    /** The same at an integer delay (Size at rest): exact and cheaper. */
    inline float allpass (DelayLine& line, float x, int d, float g, float gamma) noexcept
    {
        const auto delayed = gamma * line.read (d);
        const auto w = x + g * delayed;
        line.write (flushTiny (w));
        return delayed - g * w;
    }
}

void RoomEngine::prepare (double sampleRate, int)
{
    fs = sampleRate;
    Rng rng (seed * 2246822519u + 3266489917u);

    // Image sources ---------------------------------------------------------------
    float directMin = 1.0e9f;
    for (const auto& s : sourcePos)
        for (const auto& m : micPos)
            directMin = std::min (directMin, std::hypot (s.x - m.x, s.y - m.y, s.z - m.z));

    float longestMs = 0.0f;
    for (int mic = 0; mic < 2; ++mic)
    {
        for (auto& byOrder : erGroups[(size_t) mic])
            for (auto& g : byOrder)
            {
                g.gain.clear();
                g.position.clear();
                g.delay.clear();
                g.length.clear();
            }
        const auto& m = micPos[(size_t) mic];
        const auto& axis = micAxis[(size_t) mic];

        for (int src = 0; src < 2; ++src)
        {
            const auto& s = sourcePos[(size_t) src];
            for (int nx = -maxOrder; nx <= maxOrder; ++nx)
                for (int ny = -maxOrder; ny <= maxOrder; ++ny)
                    for (int nz = -maxOrder; nz <= maxOrder; ++nz)
                    {
                        const auto order = std::abs (nx) + std::abs (ny) + std::abs (nz);
                        if (order == 0 || order > maxOrder)
                            continue;

                        const Vec img { imageCoordinate (nx, s.x, roomDims.x), imageCoordinate (ny, s.y, roomDims.y),
                                        imageCoordinate (nz, s.z, roomDims.z) };
                        const Vec d { img.x - m.x, img.y - m.y, img.z - m.z };
                        const auto dist = std::hypot (d.x, d.y, d.z);
                        const auto cosTheta = (d.x * axis.x + d.y * axis.y + d.z * axis.z) / dist;

                        // Scattering: a little time jitter and gain spread per image, more for
                        // higher orders; second-order polarity random (diffuse walls), so the
                        // cluster has no low-frequency pile-up and no regular comb.
                        const auto jitterMs = (float) order * 0.18f * rng.bipolar();
                        const auto spread = 0.8f + 0.4f * rng.uniform();
                        const auto sign = order == 1 ? 1.0f : (rng.uniform() < 0.5f ? -1.0f : 1.0f);

                        const auto ms = std::max (0.25f, (dist - directMin) / speedOfSound * 1000.0f + jitterMs);
                        longestMs = std::max (longestMs, ms);

                        auto& g = erGroups[(size_t) mic][(size_t) order - 1][(size_t) src];
                        g.gain.push_back (sign * spread * (directMin / dist) * (0.6f + 0.4f * cosTheta));
                        g.length.emplace_back();
                        g.length.back().prepare (sampleRate, msToSamples (ms, sampleRate), 2.0f);
                        g.delay.push_back ((int) g.length.back().end());
                        g.position.push_back (g.length.back().end());
                    }
        }

        // Normalise the cluster to unit energy per mic (at reflection coefficient 1).
        float energy[2] = { 0.0f, 0.0f };
        for (int order = 0; order < 2; ++order)
            for (const auto& g : erGroups[(size_t) mic][(size_t) order])
                for (const auto v : g.gain)
                    energy[order] += v * v;
        const auto norm = 1.0f / std::sqrt (energy[0] + energy[1]);
        for (auto& byOrder : erGroups[(size_t) mic])
            for (auto& g : byOrder)
                for (auto& v : g.gain)
                    v *= norm;
        erEnergy1[(size_t) mic] = energy[0] * norm * norm;
        erEnergy2[(size_t) mic] = energy[1] * norm * norm;
    }

    for (auto& line : erInput)
        line.setMaxDelay ((int) std::ceil (msToSamples (longestMs * maxSizeScale, sampleRate)) + 8);

    // Late tank ----------------------------------------------------------------------
    // Four reflection signals in on Hadamard rows 1, 2, 4, 8 (scaled 1 / sqrt (lines)), two
    // outputs on rows 3 and 5: all mutually orthogonal.
    for (int i = 0; i < lines; ++i)
    {
        const std::array<int, 4> inRows { 1, 2, 4, 8 };
        for (size_t k = 0; k < 4; ++k)
            inSign[k][(size_t) i] = hadamardSign (inRows[k], i) / std::sqrt ((float) lines);
        outSign[0][(size_t) i] = hadamardSign (3, i);
        outSign[1][(size_t) i] = hadamardSign (5, i);
    }

    const auto maxMod = msToSamples (maxModMs, sampleRate);
    for (int i = 0; i < lines; ++i)
    {
        tankLine[(size_t) i].setMaxDelay ((int) std::ceil (msToSamples (lineMs[(size_t) i] * maxSizeScale, sampleRate) + maxMod) + 8);
        tankAp[(size_t) i].setMaxDelay ((int) std::ceil (msToSamples (apMs[(size_t) i] * maxSizeScale, sampleRate)) + 8);
        lineLen[(size_t) i].prepare (sampleRate, msToSamples (lineMs[(size_t) i], sampleRate), 8.0f);
        apLen[(size_t) i].prepare (sampleRate, msToSamples (apMs[(size_t) i], sampleRate), 2.0f);
        sine[(size_t) i].setStartPhase ((float) i / (float) lines);
        random[(size_t) i].setSeed (seed * 7919u + (uint32_t) i * 104729u + 11u);
    }

    reset();
}

void RoomEngine::reset()
{
    for (auto& l : erInput) l.reset();
    for (auto& byMic : erGroups)
        for (auto& byOrder : byMic)
            for (auto& g : byOrder)
                for (size_t k = 0; k < g.length.size(); ++k)
                {
                    g.length[k].reset();
                    g.delay[k] = (int) g.length[k].end();
                    g.position[k] = g.length[k].end();
                }
    for (auto& f : bounce1) f.reset();
    for (auto& f : bounce2) f.reset();
    bandwidthL.reset();
    bandwidthR.reset();

    for (int i = 0; i < lines; ++i)
    {
        tankLine[(size_t) i].reset();
        tankAp[(size_t) i].reset();
        lineLen[(size_t) i].reset();
        apLen[(size_t) i].reset();
        lineGamma[(size_t) i].reset();
        apGamma[(size_t) i].reset();
        modOffset[(size_t) i].reset();
        shelves[(size_t) i].reset();
        sine[(size_t) i].reset();
        random[(size_t) i].reset();
    }

    for (auto* r : { &bandwidthG, &bounceG, &reflection, &erNorm, &earlyGain, &lateGain, &apCoefficient, &lowCrossG, &highCrossG })
        r->reset();

    snapNext = true;
}

void RoomEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                          int numSamples, const EngineParams& p) noexcept
{
    const auto n = std::max (numSamples, 1);
    const auto snap = snapNext;
    snapNext = false;

    // Control rate ---------------------------------------------------------------
    const auto scale = std::clamp (p.sizeScale, 0.25f, maxSizeScale);
    const auto walls = maps::clamp01 (p.paramB);
    const auto wallTilt = (0.5f - walls) * 2.0f;     // +1 soft .. -1 hard

    erStatic = true;
    for (auto& byMic : erGroups)
        for (auto& byOrder : byMic)
            for (auto& g : byOrder)
                for (size_t k = 0; k < g.length.size(); ++k)
                {
                    g.length[k].update (scale, n, snap);
                    erStatic = erStatic && g.length[k].isStatic();
                    g.delay[k] = (int) g.length[k].end();
                }
    // Below 1x the reflections carry the size; the tank shrinks only as sqrt (Size), so a booth
    // with a long Decay keeps enough modal density not to ring (0.3x: 0.55x tank).
    const auto tankScale = scale < 1.0f ? std::sqrt (scale) : scale;
    apStatic = true;
    for (int i = 0; i < lines; ++i)
    {
        lineLen[(size_t) i].update (tankScale, n, snap);
        apLen[(size_t) i].update (tankScale, n, snap);
        apStatic = apStatic && apLen[(size_t) i].isStatic();
        apDelay[(size_t) i] = (int) apLen[(size_t) i].end();
    }

    // Walls: per-bounce reflection coefficient and absorption low-pass. The reflection
    // coefficient's energy loss is compensated, so soft walls sound softer (darker, fewer audible
    // second-order reflections), not just quieter: pink-noise level stays within ~0.3 dB.
    const auto r = reflectionCoefficient (walls);
    reflection.set (r, n);
    bounceG.set (OnePole::coefficientFor (2400.0f * std::pow (18000.0f / 2400.0f, walls), fs), n);
    {
        const auto rRef = reflectionCoefficient (0.5f);
        const auto e = 0.5f * (erEnergy1[0] + erEnergy1[1]) * r * r + 0.5f * (erEnergy2[0] + erEnergy2[1]) * r * r * r * r;
        const auto eRef = 0.5f * (erEnergy1[0] + erEnergy1[1]) * rRef * rRef + 0.5f * (erEnergy2[0] + erEnergy2[1]) * rRef * rRef * rRef * rRef;
        erNorm.set (std::sqrt (eRef / std::max (e, 1.0e-6f)), n);
    }

    const auto bandwidthHz = std::clamp (12000.0f * std::pow (2.0f, 0.9f * p.tone - 0.7f * wallTilt), 3000.0f, 20000.0f);
    bandwidthG.set (OnePole::coefficientFor (bandwidthHz, fs), n);

    const auto diffusionAmount = std::min (std::sqrt (maps::clamp01 (p.diffusion) / 0.75f), 1.1f);
    apCoefficient.set (std::min (0.7f, (0.42f - 0.12f * wallTilt) * diffusionAmount), n);

    // The late field's energy at a given RT60 scales with 1 / (line length); sqrt (tank scale)
    // keeps it level against the reflections, so Even stays even from the booth to the live room.
    const auto theta = thetaMin + (0.5f * std::numbers::pi_v<float> - 2.0f * thetaMin) * maps::clamp01 (p.paramA);
    const auto glidedScale = lineLen[0].end() / msToSamples (lineMs[0], fs);
    earlyGain.set (earlyCal * std::cos (theta), n);
    lateGain.set (lateCal * std::sin (theta) * std::sqrt (glidedScale), n);

    // Tank decay: gamma^D on every delay element (exact exponential window), shelves per line
    // for the low/high band ratios. Freeze scales every decay rate by (1 - freeze).
    const auto freeze = std::clamp (p.freeze, 0.0f, 1.0f);
    const auto live = freeze >= 1.0f ? 0.0f : 1.0f - freeze;
    const auto rtMid = std::max (p.decaySeconds, 1.0e-3f);
    const auto high = maps::toneToHighDecay (p.tone);
    const auto highRatio = std::pow (high.ratio, std::pow (2.0f, 1.2f * wallTilt));
    // A room's air path per second is a hall's, but its surfaces dominate the highs: the band
    // starts a little higher (x1.25), so the 2 kHz octave of the mid band stays on the Decay.
    const auto highCross = std::clamp (high.crossoverHz * 1.25f * std::pow (2.0f, -0.8f * wallTilt), 1000.0f, 16000.0f);
    lowCrossG.set (OnePole::coefficientFor (maps::lowCrossoverHz, fs), n);
    highCrossG.set (OnePole::coefficientFor (highCross, fs), n);

    auto gammaFor = [&] (float length)
    {
        return live > 0.0f ? std::pow (10.0f, -3.0f * live * length / (rtMid * (float) fs)) : 1.0f;
    };
    auto ratioFor = [&] (float length, float rtBand)
    {
        const auto db = -60.0f * live * length / (float) fs * (1.0f / std::max (rtBand, 1.0e-3f) - 1.0f / rtMid);
        return live > 0.0f ? std::pow (10.0f, db / 20.0f) : 1.0f;
    };

    const auto depth = msToSamples (maxModMs * maps::clamp01 (p.mod), fs) * live;
    const auto rateHz = nominalModHz * p.modRate;

    for (int i = 0; i < lines; ++i)
    {
        const auto lineLength = lineLen[(size_t) i].end();
        const auto apLength = apLen[(size_t) i].end();
        lineGamma[(size_t) i].set (gammaFor (lineLength), n);
        apGamma[(size_t) i].set (gammaFor (apLength), n);
        shelves[(size_t) i].setRatios (ratioFor (lineLength + apLength, rtMid * p.lowEnd),
                                       ratioFor (lineLength + apLength, rtMid * highRatio), n);

        const auto lineRate = rateHz * (0.75f + 0.5f * (float) i / (float) lines);
        sine[(size_t) i].setRate (lineRate, fs);
        random[(size_t) i].setRate (lineRate * 1.7f, fs);
        const auto m = (1.0f - modRandomBlend) * sine[(size_t) i].advance (n) + modRandomBlend * random[(size_t) i].advance (n);
        modOffset[(size_t) i].set (live > 0.0f ? depth * m : 0.0f, n);
    }

    // Audio rate -----------------------------------------------------------------
    for (int s = 0; s < numSamples; ++s)
    {
        const auto bw = bandwidthG.next();
        bandwidthL.setCoefficient (bw);
        bandwidthR.setCoefficient (bw);
        erInput[0].write (bandwidthL.lowpass (inL[s]));
        erInput[1].write (bandwidthR.lowpass (inR[s]));

        // Early reflections, grouped by order: order k passes k absorption low-passes and k
        // reflection coefficients. Kept apart per (mic, source) until the output, so the tank is
        // fed four differently coloured reflection patterns (their ripples average out in the
        // late field instead of being imprinted on it).
        const auto bg = bounceG.next(), refl = reflection.next(), norm = erNorm.next();
        float part[2][2];   // [mic][source]
        for (int mic = 0; mic < 2; ++mic)
            for (int src = 0; src < 2; ++src)
            {
                float sums[2] = { 0.0f, 0.0f };
                for (int order = 0; order < 2; ++order)
                {
                    auto& g = erGroups[(size_t) mic][(size_t) order][(size_t) src];
                    const auto count = g.gain.size();
                    if (erStatic)
                    {
                        // At rest every tap sits on an integer delay: plain reads are exact.
                        sums[order] = erInput[(size_t) src].sum (g.delay.data(), g.gain.data(), count);
                    }
                    else
                    {
                        for (size_t k = 0; k < count; ++k)
                            g.position[k] = g.length[k].next();
                        sums[order] = erInput[(size_t) src].sumCubic (g.position.data(), g.gain.data(), count);
                    }
                }
                auto& b1 = bounce1[(size_t) (mic * 2 + src)];
                auto& b2 = bounce2[(size_t) (mic * 2 + src)];
                b1.setCoefficient (bg);
                b2.setCoefficient (bg);
                const auto inner = refl * b2.lowpass (sums[1]);
                part[mic][src] = norm * refl * b1.lowpass (sums[0] + inner);
            }
        const float er[2] = { part[0][0] + part[0][1], part[1][0] + part[1][1] };

        // Late tank, fed by the reflections.
        const auto apc = apCoefficient.next();
        const auto lowG = lowCrossG.next(), highG = highCrossG.next();
        std::array<float, (size_t) lines> y;
        for (int i = 0; i < lines; ++i)
        {
            const auto d = lineLen[(size_t) i].next() + modOffset[(size_t) i].next();
            auto v = lineGamma[(size_t) i].next() * tankLine[(size_t) i].readCubic (d);
            v = apStatic ? allpass (tankAp[(size_t) i], v, apDelay[(size_t) i], apc, apGamma[(size_t) i].next())
                         : allpass (tankAp[(size_t) i], v, apLen[(size_t) i].next(), apc, apGamma[(size_t) i].next());
            y[(size_t) i] = shelves[(size_t) i].process (v, lowG, highG);
        }

        float lateL = 0.0f, lateR = 0.0f;
        for (int i = 0; i < lines; ++i)
        {
            lateL += outSign[0][(size_t) i] * y[(size_t) i];
            lateR += outSign[1][(size_t) i] * y[(size_t) i];
        }

        hadamardInPlace<lines> (y.data());
        for (int i = 0; i < lines; ++i)
            tankLine[(size_t) i].write (flushTiny (y[(size_t) i] + inSign[0][(size_t) i] * part[0][0] + inSign[1][(size_t) i] * part[0][1]
                                                                 + inSign[2][(size_t) i] * part[1][0] + inSign[3][(size_t) i] * part[1][1]));

        const auto eg = earlyGain.next(), lg = lateGain.next();
        outL[s] = eg * er[0] + lg * lateL;
        outR[s] = eg * er[1] + lg * lateR;
    }
}

} // namespace cerulean::dsp
