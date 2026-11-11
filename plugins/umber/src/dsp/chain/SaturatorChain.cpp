#include "dsp/chain/SaturatorChain.h"

#include "dsp/chain/AutoGain.h"
#include "dsp/common/Filters.h"
#include "dsp/models/ModelFactory.h"

#include <juce_dsp/juce_dsp.h>

namespace umber::dsp
{

namespace
{
    constexpr int subBlock = 32;               // OS samples per filter-coefficient update
    constexpr double toneMaxDb = 5.0;          // tilt per side at the Tone extremes
    constexpr double tonePivotHz = 800.0;
    constexpr double lowCutQ = 0.7071;         // Butterworth
    constexpr double weightQ = 1.9;            // ≈ +5.5 dB resonant bump at the cutoff
    constexpr double highCutQ = 0.7071;
    constexpr double emphasisMaxDb = 6.0;      // per side at the Emphasis extremes (12 dB tilt)
    constexpr double emphasisPivotHz = 1000.0;

    float gainFromDb (float db) noexcept { return std::pow (10.0f, db / 20.0f); }
}

//==============================================================================
/** Everything that runs at one oversampling factor. */
class SaturatorChain::Engine
{
public:
    explicit Engine (int factorLog2In) : factorLog2 (factorLog2In), factor (1 << factorLog2In) {}

    int getFactor() const noexcept { return factor; }
    int getLatency() const noexcept { return latency; }
    bool isPrepared() const noexcept { return oversampler != nullptr; }

    void prepare (double hostRate, int maxBlockIn, int numChannelsIn)
    {
        osRate = hostRate * factor;
        maxOsBlock = maxBlockIn * factor;

        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) numChannelsIn, (size_t) factorLog2,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true,   // max quality
            true);  // integer latency
        oversampler->initProcessing ((size_t) maxBlockIn);
        latency = (int) std::lround (oversampler->getLatencyInSamples());

        channels.clear();
        channels.resize ((size_t) numChannelsIn);

        for (auto& c : channels)
        {
            for (int m = 0; m < numModels; ++m)
            {
                c.models[(size_t) m] = createModel (static_cast<Model> (m));
                c.models[(size_t) m]->prepare (osRate);
            }

            c.dc.prepare (osRate);
            c.dry.assign ((size_t) maxOsBlock, 0.0f);
            c.scratch.assign ((size_t) maxOsBlock, 0.0f);
        }

        const auto maxSubBlocks = (size_t) (maxOsBlock / subBlock + 2);
        driveRamp.assign ((size_t) maxOsBlock, 1.0f);
        autoRamp.assign ((size_t) maxOsBlock, 1.0f);
        mixRamp.assign ((size_t) maxOsBlock, 1.0f);
        fadeRamp.assign ((size_t) maxOsBlock, 1.0f);
        deltaRamp.assign ((size_t) maxOsBlock, 0.0f);
        modeRamp.assign ((size_t) maxOsBlock, 1.0f);

        for (auto* v : { &lcHz, &lcQ, &lcAmt, &hcHz, &hcAmt, &tone, &emph, &bias })
            v->assign (maxSubBlocks, 0.0);

        const auto t = SaturatorChain::transitionSeconds;
        driveGain.reset (osRate, 0.03);
        autoGain.reset (osRate, 0.05);
        mix.reset (osRate, 0.03);
        smLcHz.reset (osRate, 0.05);
        smHcHz.reset (osRate, 0.05);
        smLcQ.reset (osRate, 0.05);
        smLcAmt.reset (osRate, t);
        smHcAmt.reset (osRate, t);
        smTone.reset (osRate, 0.05);
        smEmph.reset (osRate, 0.05);
        smBias.reset (osRate, 0.08);
        smDelta.reset (osRate, t);

        modelFadeLength = std::max (1, (int) (t * osRate));
        modeStep = 1.0f / (float) std::max (1, (int) (t * osRate));
    }

    void setPad (int padSamples)
    {
        pad = padSamples;
        for (auto& c : channels)
            c.padBuffer.assign ((size_t) std::max (1, pad), 0.0f);
        padWrite = 0;
    }

    /** Snaps every smoother and filter to the given settings and clears all state. */
    void reset (const ChainSettings& s) noexcept
    {
        if (! isPrepared())
            return;

        oversampler->reset();
        activeModel = previousModel = static_cast<int> (s.model);
        modelFadeRemaining = 0;

        const auto t = targetsFor (s);
        driveGain.setCurrentAndTargetValue (t.drive);
        autoGain.setCurrentAndTargetValue (t.autoGain);
        mix.setCurrentAndTargetValue (t.mix);
        smLcHz.setCurrentAndTargetValue (t.lcHz);
        smHcHz.setCurrentAndTargetValue (t.hcHz);
        smLcQ.setCurrentAndTargetValue (t.lcQ);
        smLcAmt.setCurrentAndTargetValue (t.lcAmt);
        smHcAmt.setCurrentAndTargetValue (t.hcAmt);
        smTone.setCurrentAndTargetValue (t.tone);
        smEmph.setCurrentAndTargetValue (t.emphasis);
        smBias.setCurrentAndTargetValue (t.bias);
        smDelta.setCurrentAndTargetValue (t.delta);

        stereoMode = pendingStereoMode = channels.size() == 2 ? static_cast<int> (s.stereoMode) : 0;
        modeGain = 1.0f;

        for (auto& c : channels)
        {
            for (auto& m : c.models)
            {
                m->setBias (t.bias);
                m->reset();
            }

            resetChannelFilters (c);
            std::fill (c.padBuffer.begin(), c.padBuffer.end(), 0.0f);
        }

        padWrite = 0;
    }

    /** Processes base-rate audio in place. Output is delayed to the chain's reported latency. */
    void process (float* const* io, int numCh, int n, const ChainSettings& s,
                  double& driveSumSq, int& driveCount) noexcept
    {
        updateTargets (s);
        updateModelSelection (s.model, numCh);

        // Stereo mode: a change waits until the wet path has faded out (see below).
        pendingStereoMode = numCh == 2 ? static_cast<int> (s.stereoMode) : 0;

        juce::dsp::AudioBlock<float> block (io, (size_t) numCh, (size_t) n);
        auto os = oversampler->processSamplesUp (block);
        const int N = (int) os.getNumSamples();

        computeRamps (N);
        const bool modeFading = computeModeRamp (N);

        const bool msActive = numCh == 2 && stereoMode != 0;

        if (msActive)
            encodeMidSide (os.getChannelPointer (0), os.getChannelPointer (1), N);

        const bool fading = modelFadeRemaining > 0;
        const bool emphasisActive = smEmphActive;
        const bool biasMoving = smBias.isSmoothing() || biasSubBlocksPending;

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& c = channels[(size_t) ch];
            float* x = os.getChannelPointer ((size_t) ch);

            // Mid mode runs only channel 0 (M) through the circuit, Side only channel 1 (S).
            const bool wetEnabled = ! msActive || (stereoMode == 1 ? ch == 0 : ch == 1);

            if (! wetEnabled)
            {
                // Untouched, except that Delta (which plays only what the circuit adds) mutes it.
                if (deltaActive)
                    for (int i = 0; i < N; ++i)
                        x[i] *= 1.0f - deltaRamp[(size_t) i];
                continue;
            }

            std::copy (x, x + N, c.dry.begin());

            // Low Cut (+Weight), Drive and pre-emphasis
            for (int sb = 0, start = 0; start < N; ++sb, start += subBlock)
            {
                c.lowCut.setCoefficients (lcHz[(size_t) sb], lcQ[(size_t) sb], osRate);
                const auto amount = lcAmt[(size_t) sb];
                const int end = std::min (N, start + subBlock);

                if (emphasisActive)
                {
                    const auto hfGain = std::pow (10.0, emph[(size_t) sb] * emphasisMaxDb / 20.0);
                    c.preEmphasis.setCoefficients (emphasisPivotHz, hfGain, osRate);
                    c.deEmphasis.setCoefficients (emphasisPivotHz, 1.0 / hfGain, osRate);
                }

                for (int i = start; i < end; ++i)
                {
                    const double in = x[i];
                    const double hp = c.lowCut.processHighpass (in);
                    double v = (in + amount * (hp - in)) * driveRamp[(size_t) i];

                    if (emphasisActive)
                        v = c.preEmphasis.process (v);

                    driveSumSq += v * v;
                    x[i] = (float) v;
                }
            }

            driveCount += N;

            // Circuit (with crossfade on model change). While Bias moves, the circuit runs in
            // sub-blocks so the operating point follows the smoothed control.
            auto runCircuit = [&] (CircuitModel& model, float* data)
            {
                if (! biasMoving)
                {
                    model.setBias (bias[0]);
                    model.process (data, N);
                    return;
                }

                for (int sb = 0, start = 0; start < N; ++sb, start += subBlock)
                {
                    model.setBias (bias[(size_t) sb]);
                    model.process (data + start, std::min (subBlock, N - start));
                }
            };

            if (fading)
            {
                std::copy (x, x + N, c.scratch.begin());
                runCircuit (*c.models[(size_t) previousModel], c.scratch.data());
            }

            runCircuit (*c.models[(size_t) activeModel], x);

            if (fading)
                for (int i = 0; i < N; ++i)
                    x[i] = c.scratch[(size_t) i] + fadeRamp[(size_t) i] * (x[i] - c.scratch[(size_t) i]);

            // De-emphasis, DC block, High Cut, Tone, Auto gain, Mix / Delta
            for (int sb = 0, start = 0; start < N; ++sb, start += subBlock)
            {
                c.highCut.setCoefficients (hcHz[(size_t) sb], highCutQ, osRate);
                c.tilt.setCoefficients (tonePivotHz, tone[(size_t) sb], toneMaxDb, osRate);
                const auto amount = hcAmt[(size_t) sb];
                const int end = std::min (N, start + subBlock);

                for (int i = start; i < end; ++i)
                {
                    double y = x[i];

                    if (emphasisActive)
                        y = c.deEmphasis.process (y);

                    y = c.dc.process (y);
                    const double lp = c.highCut.processLowpass (y);
                    y += amount * (lp - y);
                    y = c.tilt.process (y) * autoRamp[(size_t) i];

                    const double d = c.dry[(size_t) i];
                    const double wet = (double) mixRamp[(size_t) i] * (y - d);

                    if (deltaActive || modeFading)
                    {
                        const double keepDry = deltaActive ? 1.0 - deltaRamp[(size_t) i] : 1.0;
                        const double wetGain = modeFading ? modeRamp[(size_t) i] : modeGain;
                        x[i] = (float) (keepDry * d + wetGain * wet);
                    }
                    else
                        x[i] = (float) (d + wet);
                }
            }
        }

        if (fading)
            modelFadeRemaining = std::max (0, modelFadeRemaining - N);

        if (msActive)
            decodeMidSide (os.getChannelPointer (0), os.getChannelPointer (1), N);

        // The wet path is silent now: switch the stereo domain and start from clean state.
        if (pendingStereoMode != stereoMode && modeGain <= 0.0f)
        {
            stereoMode = pendingStereoMode;

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& c = channels[(size_t) ch];
                c.models[(size_t) activeModel]->reset();
                resetChannelFilters (c);
            }

            modelFadeRemaining = 0;
            previousModel = activeModel;
        }

        oversampler->processSamplesDown (block);
        applyPad (io, numCh, n);
    }

private:
    struct Targets
    {
        float drive, autoGain, mix, lcHz, hcHz, lcQ, lcAmt, hcAmt, tone, emphasis, bias, delta;
    };

    Targets targetsFor (const ChainSettings& s) const noexcept
    {
        const auto driveDb = SaturatorChain::driveDb (s.driveKnob, s.slam);
        const bool lcOff = params::isLowCutOff (s.lowCutHz);
        const bool hcOff = params::isHighCutOff (s.highCutHz);

        Targets t {};
        t.drive    = gainFromDb (driveDb);
        t.autoGain = s.autoGain ? gainFromDb (AutoGain::compensationDb (s.model, driveDb, juce::jlimit (-1.0f, 1.0f, s.bias), juce::jlimit (-1.0f, 1.0f, s.emphasis))) : 1.0f;
        t.mix      = juce::jlimit (0.0f, 1.0f, s.mix);
        // When a filter is switched off it fades out at its last frequency, so turning it
        // off/on never sweeps the cutoff audibly.
        t.lcHz     = lcOff ? smLcHz.getTargetValue() : s.lowCutHz;
        t.hcHz     = hcOff ? smHcHz.getTargetValue() : s.highCutHz;
        t.lcQ      = (float) (s.weight ? weightQ : lowCutQ);
        t.lcAmt    = lcOff ? 0.0f : 1.0f;
        t.hcAmt    = hcOff ? 0.0f : 1.0f;
        t.tone     = juce::jlimit (-1.0f, 1.0f, s.tone);
        t.emphasis = juce::jlimit (-1.0f, 1.0f, s.emphasis);
        t.bias     = juce::jlimit (-1.0f, 1.0f, s.bias);
        t.delta    = s.delta ? 1.0f : 0.0f;

        if (t.lcHz <= 0.0f) t.lcHz = params::lowCutMinHz;
        if (t.hcHz <= 0.0f) t.hcHz = params::highCutMaxHz;
        return t;
    }

    void updateTargets (const ChainSettings& s) noexcept
    {
        const auto t = targetsFor (s);
        driveGain.setTargetValue (t.drive);
        autoGain.setTargetValue (t.autoGain);
        mix.setTargetValue (t.mix);
        smLcHz.setTargetValue (t.lcHz);
        smHcHz.setTargetValue (t.hcHz);
        smLcQ.setTargetValue (t.lcQ);
        smLcAmt.setTargetValue (t.lcAmt);
        smHcAmt.setTargetValue (t.hcAmt);
        smTone.setTargetValue (t.tone);
        smEmph.setTargetValue (t.emphasis);
        smBias.setTargetValue (t.bias);
        smDelta.setTargetValue (t.delta);
    }

    void updateModelSelection (Model target, int numCh) noexcept
    {
        const auto targetIndex = static_cast<int> (target);

        // A new switch waits for a running crossfade to finish: dropping a half-faded model
        // would click.
        if (modelFadeRemaining > 0 || targetIndex == activeModel)
            return;

        previousModel = activeModel;
        activeModel = targetIndex;
        modelFadeRemaining = modelFadeLength;

        for (int ch = 0; ch < numCh; ++ch)
            channels[(size_t) ch].models[(size_t) activeModel]->reset();
    }

    static void encodeMidSide (float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto m = 0.5f * (l[i] + r[i]);
            const auto sd = 0.5f * (l[i] - r[i]);
            l[i] = m;
            r[i] = sd;
        }
    }

    static void decodeMidSide (float* m, float* sd, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto l = m[i] + sd[i];
            const auto r = m[i] - sd[i];
            m[i] = l;
            sd[i] = r;
        }
    }

    template <typename ChannelT>
    static void resetChannelFilters (ChannelT& c) noexcept
    {
        c.lowCut.reset();
        c.highCut.reset();
        c.tilt.reset();
        c.dc.reset();
        c.preEmphasis.reset();
        c.deEmphasis.reset();
    }

    /** Wet-path gain for stereo-mode changes: fades out, (switch), fades in. True while moving. */
    bool computeModeRamp (int N) noexcept
    {
        const float target = pendingStereoMode != stereoMode ? 0.0f : 1.0f;

        if (modeGain == target)
            return false;

        for (int i = 0; i < N; ++i)
        {
            modeGain = target > modeGain ? std::min (target, modeGain + modeStep)
                                         : std::max (target, modeGain - modeStep);
            modeRamp[(size_t) i] = modeGain;
        }

        return true;
    }

    void computeRamps (int N) noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            driveRamp[(size_t) i] = driveGain.getNextValue();
            autoRamp[(size_t) i]  = autoGain.getNextValue();
            mixRamp[(size_t) i]   = mix.getNextValue();
        }

        deltaActive = smDelta.isSmoothing() || smDelta.getTargetValue() > 0.0f;

        if (deltaActive)
            for (int i = 0; i < N; ++i)
                deltaRamp[(size_t) i] = smDelta.getNextValue();

        // Emphasis is skipped entirely while flat. When it is switched on from flat the filter
        // starts from rest, which is inaudible: at 0 dB both shelves are the identity.
        const bool wasActive = smEmphActive;
        smEmphActive = smEmph.isSmoothing() || smEmph.getTargetValue() != 0.0f;

        if (smEmphActive && ! wasActive)
            for (auto& c : channels)
            {
                c.preEmphasis.reset();
                c.deEmphasis.reset();
            }

        biasSubBlocksPending = smBias.isSmoothing();

        if (modelFadeRemaining > 0)
            for (int i = 0; i < N; ++i)
                fadeRamp[(size_t) i] = 1.0f - (float) std::max (0, modelFadeRemaining - i) / (float) modelFadeLength;

        for (int sb = 0, start = 0; start < N; ++sb, start += subBlock)
        {
            const int len = std::min (subBlock, N - start);
            lcHz[(size_t) sb]  = smLcHz.skip (len);
            hcHz[(size_t) sb]  = smHcHz.skip (len);
            lcQ[(size_t) sb]   = smLcQ.skip (len);
            lcAmt[(size_t) sb] = smLcAmt.skip (len);
            hcAmt[(size_t) sb] = smHcAmt.skip (len);
            tone[(size_t) sb]  = smTone.skip (len);
            emph[(size_t) sb]  = smEmph.skip (len);
            bias[(size_t) sb]  = smBias.skip (len);
        }
    }

    void applyPad (float* const* io, int numCh, int n) noexcept
    {
        if (pad <= 0)
            return;

        int w = padWrite;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& buf = channels[(size_t) ch].padBuffer;
            w = padWrite;
            for (int i = 0; i < n; ++i)
            {
                const auto delayed = buf[(size_t) w];
                buf[(size_t) w] = io[ch][i];
                io[ch][i] = delayed;
                if (++w == pad) w = 0;
            }
        }
        padWrite = w;
    }

    struct Channel
    {
        std::array<std::unique_ptr<CircuitModel>, numModels> models;
        TptSvf lowCut, highCut;
        TiltEq tilt;
        EmphasisShelf preEmphasis, deEmphasis;
        DcBlocker dc;
        std::vector<float> dry, scratch, padBuffer;
    };

    const int factorLog2, factor;
    double osRate = 0.0;
    int maxOsBlock = 0, latency = 0, pad = 0, padWrite = 0;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::vector<Channel> channels;

    int activeModel = 0, previousModel = 0;
    int modelFadeRemaining = 0, modelFadeLength = 1;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> driveGain, autoGain, smLcHz, smHcHz;
    juce::SmoothedValue<float> mix, smLcQ, smLcAmt, smHcAmt, smTone, smEmph, smBias, smDelta;

    std::vector<float> driveRamp, autoRamp, mixRamp, fadeRamp, deltaRamp, modeRamp;
    std::vector<double> lcHz, lcQ, lcAmt, hcHz, hcAmt, tone, emph, bias;

    bool deltaActive = false, smEmphActive = false, biasSubBlocksPending = false;

    // Stereo mode (0 stereo, 1 mid, 2 side) and the wet-path gain used to switch it.
    int stereoMode = 0, pendingStereoMode = 0;
    float modeGain = 1.0f, modeStep = 1.0f;
};

//==============================================================================
SaturatorChain::SaturatorChain()
{
    for (int i = 0; i < numEngines; ++i)
        engines[(size_t) i] = std::make_unique<Engine> (i + 1);
}

SaturatorChain::~SaturatorChain() = default;

float SaturatorChain::driveDb (float driveKnob, bool slam) noexcept
{
    return -6.0f + 4.2f * juce::jlimit (0.0f, params::driveMax, driveKnob) + (slam ? 20.0f : 0.0f);
}

int SaturatorChain::effectiveFactor (int requestedFactor, double hostRate) noexcept
{
    int f = juce::jlimit (2, 16, juce::nextPowerOfTwo (requestedFactor));
    while (f > 2 && hostRate * f > maxOversampledRate)
        f /= 2;
    return f;
}

SaturatorChain::Engine* SaturatorChain::engineForFactor (int factor) noexcept
{
    for (auto& e : engines)
        if (e->getFactor() == factor && e->isPrepared())
            return e.get();

    return engines[0].get(); // 2x is always prepared
}

void SaturatorChain::prepare (double newSampleRate, int maximumBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate;
    maxBlock = std::max (1, maximumBlockSize);
    numChannels = std::max (1, newNumChannels);

    // Fresh engines so nothing from a previous configuration survives.
    for (int i = 0; i < numEngines; ++i)
        engines[(size_t) i] = std::make_unique<Engine> (i + 1);

    reportedLatency = 0;
    for (auto& e : engines)
    {
        if (e->getFactor() == 2 || sampleRate * e->getFactor() <= maxOversampledRate)
        {
            e->prepare (sampleRate, maxBlock, numChannels);
            reportedLatency = std::max (reportedLatency, e->getLatency());
        }
    }

    for (auto& e : engines)
        if (e->isPrepared())
            e->setPad (reportedLatency - e->getLatency());

    dryDelay.assign ((size_t) numChannels, std::vector<float> ((size_t) std::max (1, reportedLatency), 0.0f));
    dryDelayWrite = 0;

    wetA.setSize (numChannels, maxBlock);
    wetB.setSize (numChannels, maxBlock);

    bypassAmount.reset (sampleRate, transitionSeconds);
    outputGain.reset (sampleRate, 0.05);

    activeEngine = nullptr;
    fadingEngine = nullptr;
    engineFadeRemaining = 0;
}

void SaturatorChain::reset() noexcept
{
    activeEngine = nullptr; // re-selected and reset on the next block
    fadingEngine = nullptr;
    engineFadeRemaining = 0;

    for (auto& d : dryDelay)
        std::fill (d.begin(), d.end(), 0.0f);
    dryDelayWrite = 0;
}

void SaturatorChain::process (juce::AudioBuffer<float>& buffer, const ChainSettings& settings,
                              bool isNonRealtime, ChainMeters& meters) noexcept
{
    const int numCh = std::min ({ buffer.getNumChannels(), numChannels, 8 });
    const int total = buffer.getNumSamples();

    if (numCh <= 0 || total <= 0 || maxBlock <= 0)
        return;

    meters = {};
    std::array<float*, 8> ptrs {};

    for (int offset = 0; offset < total; offset += maxBlock)
    {
        const int len = std::min (maxBlock, total - offset);
        for (int ch = 0; ch < numCh; ++ch)
            ptrs[(size_t) ch] = buffer.getWritePointer (ch, offset);

        processChunk (ptrs.data(), numCh, len, settings, isNonRealtime, meters);
    }

    const auto norm = 1.0f / (float) (total * numCh);
    meters.inputRms  = std::sqrt (meters.inputRms * norm);
    meters.outputRms = std::sqrt (meters.outputRms * norm);
}

void SaturatorChain::recoverIfNonFinite (Engine& engine, float* const* wet, int numCh, int n,
                                         const ChainSettings& s) noexcept
{
    // Last line of defence: if a circuit solver ever diverges, mute this block and start the
    // engine from a clean state instead of emitting NaN or staying silent forever.
    for (int ch = 0; ch < numCh; ++ch)
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (wet[ch][i]))
            {
                engine.reset (s);
                for (int c = 0; c < numCh; ++c)
                    std::fill (wet[c], wet[c] + n, 0.0f);
                return;
            }
}

void SaturatorChain::processChunk (float* const* io, int numCh, int n, const ChainSettings& s,
                                   bool isNonRealtime, ChainMeters& meters) noexcept
{
    // Sanitise and meter the input. A single NaN/Inf from the host would otherwise poison every
    // filter and circuit state for good; absurd levels are clamped to what the models are
    // specified for (|x| <= 1000, i.e. +60 dBFS). Finite, sane input passes bit-exact.
    for (int ch = 0; ch < numCh; ++ch)
        for (int i = 0; i < n; ++i)
        {
            auto v = io[ch][i];

            if (! std::isfinite (v))
                v = 0.0f;
            else if (std::abs (v) > maxInputLevel)
                v = std::copysign (maxInputLevel, v);

            io[ch][i] = v;
            meters.inputRms += v * v;
            meters.inputPeak = std::max (meters.inputPeak, std::abs (v));
        }

    // Engine selection
    const int requested = (isNonRealtime && s.renderFactor > 0) ? s.renderFactor : s.realtimeFactor;
    auto* target = engineForFactor (effectiveFactor (requested, sampleRate));

    if (activeEngine == nullptr)
    {
        activeEngine = target;
        activeEngine->reset (s);
        outputGain.setCurrentAndTargetValue (gainFromDb (s.outputDb));
        bypassAmount.setCurrentAndTargetValue (s.bypass ? 1.0f : 0.0f);
    }
    else if (target != activeEngine && fadingEngine == nullptr)
    {
        fadingEngine = activeEngine;
        activeEngine = target;
        activeEngine->reset (s);
        engineFadeLength = engineFadeRemaining = std::max (1, (int) (transitionSeconds * sampleRate));
    }

    meters.oversamplingFactor = activeEngine->getFactor();

    // Wet path(s)
    std::array<float*, 8> a {}, b {};
    for (int ch = 0; ch < numCh; ++ch)
    {
        a[(size_t) ch] = wetA.getWritePointer (ch);
        std::copy (io[ch], io[ch] + n, a[(size_t) ch]);

        if (fadingEngine != nullptr)
        {
            b[(size_t) ch] = wetB.getWritePointer (ch);
            std::copy (io[ch], io[ch] + n, b[(size_t) ch]);
        }
    }

    double driveSumSq = 0.0;
    int driveCount = 0;
    activeEngine->process (a.data(), numCh, n, s, driveSumSq, driveCount);
    recoverIfNonFinite (*activeEngine, a.data(), numCh, n, s);

    if (fadingEngine != nullptr)
    {
        double unusedSq = 0.0;
        int unusedCount = 0;
        fadingEngine->process (b.data(), numCh, n, s, unusedSq, unusedCount);
        recoverIfNonFinite (*fadingEngine, b.data(), numCh, n, s);

        for (int i = 0; i < n; ++i)
        {
            const float w = 1.0f - (float) std::max (0, engineFadeRemaining - i) / (float) engineFadeLength;
            for (int ch = 0; ch < numCh; ++ch)
                a[(size_t) ch][i] = b[(size_t) ch][i] + w * (a[(size_t) ch][i] - b[(size_t) ch][i]);
        }

        engineFadeRemaining -= n;
        if (engineFadeRemaining <= 0)
            fadingEngine = nullptr;
    }

    if (driveCount > 0)
        meters.driveRms = std::max (meters.driveRms, (float) std::sqrt (driveSumSq / driveCount));

    // Output gain, bypass crossfade against latency-aligned dry, output metering
    outputGain.setTargetValue (gainFromDb (s.outputDb));
    bypassAmount.setTargetValue (s.bypass ? 1.0f : 0.0f);

    const int delayLength = (int) dryDelay[0].size();

    for (int i = 0; i < n; ++i)
    {
        const float og = outputGain.getNextValue();
        const float bp = bypassAmount.getNextValue();

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float in = io[ch][i];
            float dry = in;

            if (reportedLatency > 0)
            {
                auto& d = dryDelay[(size_t) ch];
                dry = d[(size_t) dryDelayWrite];
                d[(size_t) dryDelayWrite] = in;
            }

            const float out = a[(size_t) ch][i] * og * (1.0f - bp) + dry * bp;
            io[ch][i] = out;

            meters.outputRms += out * out;
            meters.outputPeak = std::max (meters.outputPeak, std::abs (out));
        }

        if (reportedLatency > 0 && ++dryDelayWrite == delayLength)
            dryDelayWrite = 0;
    }
}

} // namespace umber::dsp
