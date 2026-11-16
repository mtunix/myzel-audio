// Learn through the processor (PLAN.md, Learn; Shared infrastructure): the thresholds set as one
// host-visible gesture and one undo step, too few esses (also with Harsh on), Harsh and Allround,
// nothing stored, a host's automation during a capture and a host preparing again around one.

#include "detector/Signals.h"
#include "support/ProcessorHelpers.h"

#include "common/plugin/ParameterUndo.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace halite;
using namespace halite::testing;

namespace
{
    constexpr double rate = 48000.0;

    /** A voice with esses at spread levels, stereo, `seconds` long. */
    juce::AudioBuffer<float> voice (double seconds, bool esses = true, unsigned seed = 3)
    {
        const auto n = samplesFor (seconds, rate);
        auto x = harmonic ({ .f0 = 160.0, .topHz = 0.45 * rate, .tiltDbPerOctave = -9.0, .peakHz = 3200.0, .peakDb = 8.0, .rms = 0.1, .seed = seed }, n, rate);
        for (int k = 0; esses && 0.6 + 0.6 * k + 0.12 < seconds; ++k)
        {
            const auto from = samplesFor (0.6 + 0.6 * k, rate);
            const auto level = 0.02 * std::pow (10.0, (double) ((k * 5) % 12 - 6) * 1.2 / 20.0);
            x = sum (x, gated (bandNoise (4500.0, 12000.0, n, rate, seed + 10 + (unsigned) k, level), from, from + samplesFor (0.12, rate), rate));
        }
        juce::AudioBuffer<float> b (2, n);
        for (int ch = 0; ch < 2; ++ch)
            b.copyFrom (ch, 0, x.data(), n);
        return b;
    }

    /** Counts each parameter's gestures and changes while attached. */
    struct Watcher final : juce::AudioProcessorParameter::Listener
    {
        explicit Watcher (juce::AudioProcessor& p) : processor (p)
        {
            for (auto* param : processor.getParameters())
                param->addListener (this);
        }
        ~Watcher() override
        {
            for (auto* param : processor.getParameters())
                param->removeListener (this);
        }
        void parameterValueChanged (int index, float) override { ++changes[index]; }
        void parameterGestureChanged (int index, bool starting) override
        {
            ++(starting ? begins : ends)[index];
            if (starting)
                order.push_back (index);
        }

        juce::AudioProcessor& processor;
        std::map<int, int> changes, begins, ends;
        std::vector<int> order;
    };

    int indexOf (HaliteProcessor& p, const char* id)
    {
        return p.getState().getParameter (id)->getParameterIndex();
    }

    /** Learn pressed after `before` seconds of the buffer, the rest played, then the message thread's turn. */
    void learnOver (HaliteProcessor& p, const juce::AudioBuffer<float>& in, double before = 0.5, const std::function<void (int)>& eachBlock = {})
    {
        juce::MidiBuffer midi;
        constexpr int block = 512;
        auto pressed = false;
        for (int pos = 0, b = 0; pos < in.getNumSamples(); pos += block, ++b)
        {
            if (! pressed && pos >= samplesFor (before, rate))
            {
                p.getLearn().start();
                pressed = true;
            }
            if (eachBlock)
                eachBlock (b);
            const auto n = std::min (block, in.getNumSamples() - pos);
            juce::AudioBuffer<float> chunk (2, n);
            for (int ch = 0; ch < 2; ++ch)
                chunk.copyFrom (ch, 0, in, ch, pos, n);
            p.processBlock (chunk, midi);
        }
        p.getLearn().service();
    }
}

TEST_CASE ("Learn: sets Threshold as one gesture the host sees, one undo step", "[processor][learn]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    p.prepareToPlay (rate, 512);
    juce::UndoManager undo;
    myzel::plugin::ParameterUndoRecorder recorder (p, undo);
    p.getLearn().beforeApplying = [&undo] { undo.beginNewTransaction ("Learn"); };
    Watcher watcher (p);

    const auto before = plainValue (p, params::threshold);
    learnOver (p, voice (9.5));

    REQUIRE (p.getLearn().state() == LearnState::learned);
    const auto& r = p.getLearn().result();
    INFO ("esses " << r.esses << ", threshold " << r.essDb);
    CHECK (r.esses >= dsp::LearnTuning{}.minEsses);
    REQUIRE (r.essSet);
    CHECK_FALSE (r.harshSet);
    CHECK (plainValue (p, params::threshold) == Catch::Approx (r.essDb).margin (0.006));
    CHECK (std::abs (plainValue (p, params::threshold) - before) > 0.01f);

    // One gesture, one change, nothing else touched.
    const auto t = indexOf (p, params::threshold);
    CHECK (watcher.begins[t] == 1);
    CHECK (watcher.ends[t] == 1);
    CHECK (watcher.changes[t] == 1);
    CHECK (watcher.order.size() == 1);

    // One undo step brings the threshold back.
    REQUIRE (undo.canUndo());
    undo.undo();
    CHECK (plainValue (p, params::threshold) == Catch::Approx (before).margin (0.006));
    CHECK_FALSE (undo.canUndo());
}

TEST_CASE ("Learn: with Harsh on both thresholds move in one step", "[processor][learn]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    setParam (p, params::harshOn, 1.0f);
    p.prepareToPlay (rate, 512);
    juce::UndoManager undo;
    myzel::plugin::ParameterUndoRecorder recorder (p, undo);
    p.getLearn().beforeApplying = [&undo] { undo.beginNewTransaction ("Learn"); };
    Watcher watcher (p);

    const auto threshold = plainValue (p, params::threshold), harsh = plainValue (p, params::harshThreshold);
    learnOver (p, voice (9.5));

    REQUIRE (p.getLearn().state() == LearnState::learned);
    const auto& r = p.getLearn().result();
    REQUIRE (r.essSet);
    REQUIRE (r.harshSet);
    CHECK (plainValue (p, params::harshThreshold) == Catch::Approx (r.harshDb).margin (0.006));

    // Both gestures begin before either value moves.
    const auto t = indexOf (p, params::threshold), h = indexOf (p, params::harshThreshold);
    CHECK (watcher.begins[t] == 1);
    CHECK (watcher.begins[h] == 1);
    CHECK (watcher.order == std::vector<int> { t, h });

    undo.undo();
    CHECK (plainValue (p, params::threshold) == Catch::Approx (threshold).margin (0.006));
    CHECK (plainValue (p, params::harshThreshold) == Catch::Approx (harsh).margin (0.006));
    CHECK_FALSE (undo.canUndo());
}

TEST_CASE ("Learn: in Allround it sets the absolute thresholds", "[processor][learn]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    setParam (p, params::detection, 1.0f);
    setParam (p, params::harshOn, 1.0f);
    p.prepareToPlay (rate, 512);

    const auto relative = plainValue (p, params::threshold), harshRelative = plainValue (p, params::harshThreshold);
    learnOver (p, voice (9.5));
    REQUIRE (p.getLearn().state() == LearnState::learned);
    const auto& r = p.getLearn().result();
    REQUIRE (r.essSet);
    CHECK (plainValue (p, params::thresholdAbs) == Catch::Approx (r.essDb).margin (0.006));
    CHECK (r.essDb < 0.0f);
    if (r.harshSet)
        CHECK (plainValue (p, params::harshThresholdAbs) == Catch::Approx (r.harshDb).margin (0.006));
    CHECK (juce::exactlyEqual (plainValue (p, params::threshold), relative));
    CHECK (juce::exactlyEqual (plainValue (p, params::harshThreshold), harshRelative));
}

TEST_CASE ("Learn: too few esses keep the threshold", "[processor][learn]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    p.prepareToPlay (rate, 512);
    Watcher watcher (p);
    const auto before = plainValue (p, params::threshold);

    // A vowel, no esses; then silence.
    for (const auto silent : { false, true })
    {
        auto in = voice (9.5, false);
        if (silent)
            in.clear();
        learnOver (p, in);
        CHECK (p.getLearn().state() == LearnState::tooFew);
        CHECK (p.getLearn().result().esses < dsp::LearnTuning{}.minEsses);
        CHECK_FALSE (p.getLearn().result().essSet);
        CHECK (juce::exactlyEqual (plainValue (p, params::threshold), before));
        CHECK (watcher.order.empty());
    }

    // The display's dismissal returns to the live histogram.
    p.getLearn().dismiss();
    CHECK (p.getLearn().state() == LearnState::idle);
}

TEST_CASE ("Learn: never runs on its own, and nothing of it is stored", "[processor][learn][state]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    const auto in = voice (9.5);

    // Twenty seconds of voice without the button: nothing moves.
    HaliteProcessor idle;
    idle.prepareToPlay (rate, 512);
    const auto blank = stateOf (idle);
    run (idle, in, { 512 });
    run (idle, in, { 512 });
    idle.getLearn().service();
    CHECK (idle.getLearn().state() == LearnState::idle);
    CHECK (stateOf (idle) == blank);
    CHECK (idle.getChain().learn().live().total() > 0);

    // A session saved during a capture restores no capture, and holds nothing of it.
    HaliteProcessor p;
    p.prepareToPlay (rate, 512);
    p.getLearn().start();
    run (p, voice (3.0), { 512 });
    REQUIRE (p.getLearn().state() == LearnState::listening);
    REQUIRE (p.getChain().learn().progress() > 0.2f);
    const auto saved = stateOf (p);
    CHECK (saved == blank);

    HaliteProcessor q;
    q.prepareToPlay (rate, 512);
    restore (q, saved);
    run (q, in, { 512 });
    q.getLearn().service();
    CHECK (q.getLearn().state() == LearnState::idle);
    CHECK (q.getChain().learn().phase() == dsp::Learn::Phase::idle);
}

TEST_CASE ("Learn: a threshold the host automates during the capture changes nothing it learns", "[processor][learn]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    const auto in = voice (9.5);

    HaliteProcessor still;
    still.prepareToPlay (rate, 512);
    learnOver (still, in);

    HaliteProcessor automated;
    automated.prepareToPlay (rate, 512);
    std::mt19937 rng (5u);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);
    learnOver (automated, in, 0.5, [&] (int) { automated.getState().getParameter (params::threshold)->setValueNotifyingHost (unit (rng)); });

    REQUIRE (still.getLearn().result().essSet);
    REQUIRE (automated.getLearn().result().essSet);
    CHECK (juce::exactlyEqual (still.getLearn().result().essDb, automated.getLearn().result().essDb));
    CHECK (still.getLearn().result().esses == automated.getLearn().result().esses);
}

TEST_CASE ("Learn: pressed again during a capture, it starts over", "[processor][learn]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    p.prepareToPlay (rate, 512);
    p.getLearn().start();
    run (p, voice (5.0), { 512 });
    p.getLearn().service();
    REQUIRE (p.getLearn().state() == LearnState::listening);
    CHECK (p.getLearn().progress() > 0.5f);

    p.getLearn().start();
    run (p, voice (1.0), { 512 });
    p.getLearn().service();
    CHECK (p.getLearn().state() == LearnState::listening);
    CHECK (p.getLearn().progress() < 0.2f);

    run (p, voice (8.0), { 512 });
    p.getLearn().service();
    CHECK (p.getLearn().state() == LearnState::learned);
}

TEST_CASE ("Learn: a host preparing again during or right after a capture still learns", "[processor][learn]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    // The reference: the same capture, nothing in between.
    HaliteProcessor reference;
    reference.prepareToPlay (rate, 512);
    reference.getLearn().start();
    run (reference, voice (8.5), { 512 });
    reference.getLearn().service();
    REQUIRE (reference.getLearn().state() == LearnState::learned);

    SECTION ("during the capture, at the same rate: it runs on")
    {
        HaliteProcessor p;
        p.prepareToPlay (rate, 512);
        p.getLearn().start();
        run (p, voice (3.0), { 512 });
        const auto progress = p.getLearn().progress();
        p.prepareToPlay (rate, 256);
        p.getLearn().service();
        REQUIRE (p.getLearn().state() == LearnState::listening);
        CHECK (juce::exactlyEqual (p.getLearn().progress(), progress));
        run (p, voice (5.5, true, 4), { 256 });
        p.getLearn().service();
        CHECK (p.getLearn().state() == LearnState::learned);
    }
    SECTION ("during the capture, at a new rate: it starts over and finishes")
    {
        HaliteProcessor p;
        p.prepareToPlay (rate, 512);
        p.getLearn().start();
        run (p, voice (3.0), { 512 });
        p.prepareToPlay (44100.0, 512);
        run (p, voice (0.5), { 512 });
        p.getLearn().service();
        REQUIRE (p.getLearn().state() == LearnState::listening);
        CHECK (p.getLearn().progress() < 0.1f);
        run (p, voice (8.5), { 512 });
        p.getLearn().service();
        CHECK (p.getLearn().state() == LearnState::learned);
    }
    SECTION ("after the capture, before the message thread reads it: the same result")
    {
        for (const auto newRate : { rate, 96000.0 })
        {
            HaliteProcessor p;
            p.prepareToPlay (rate, 512);
            p.getLearn().start();
            run (p, voice (8.5), { 512 });
            REQUIRE (p.getChain().learn().phase() == dsp::Learn::Phase::captured);
            p.prepareToPlay (newRate, 1024);
            p.getLearn().service();
            INFO ("prepared again at " << newRate);
            REQUIRE (p.getLearn().state() == LearnState::learned);
            CHECK (p.getLearn().result().esses == reference.getLearn().result().esses);
            CHECK (juce::exactlyEqual (p.getLearn().result().essDb, reference.getLearn().result().essDb));
        }
    }
}

TEST_CASE ("Learn: with Harsh on, too few esses keep Threshold and say so while Harsh Threshold moves", "[processor][learn]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    setParam (p, params::harshOn, 1.0f);
    p.prepareToPlay (rate, 512);
    Watcher watcher (p);
    const auto threshold = plainValue (p, params::threshold), harsh = plainValue (p, params::harshThreshold);

    // A bright sung vowel: no esses, plenty of voiced frames.
    const auto n = samplesFor (9.5, rate);
    const auto x = harmonic ({ .f0 = 330.0, .topHz = 12000.0, .tiltDbPerOctave = -6.0, .vibratoCents = 30.0, .rms = 0.1 }, n, rate);
    juce::AudioBuffer<float> in (2, n);
    for (int ch = 0; ch < 2; ++ch)
        in.copyFrom (ch, 0, x.data(), n);
    learnOver (p, in);

    const auto& r = p.getLearn().result();
    INFO ("esses " << r.esses << ", voiced " << r.voicedSeconds << " s");
    CHECK (p.getLearn().state() == LearnState::tooFew);
    CHECK (r.esses < dsp::LearnTuning{}.minEsses);
    CHECK_FALSE (r.essSet);
    CHECK (juce::exactlyEqual (plainValue (p, params::threshold), threshold));
    REQUIRE (r.harshSet);
    CHECK (plainValue (p, params::harshThreshold) == Catch::Approx (r.harshDb).margin (0.006));
    CHECK (std::abs (plainValue (p, params::harshThreshold) - harsh) > 0.01f);
    CHECK (watcher.order == std::vector<int> { indexOf (p, params::harshThreshold) });
}

TEST_CASE ("Learn: one step in the processor's own undo history; its result stays until the next edit", "[processor][learn]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    HaliteProcessor p;
    p.prepareToPlay (rate, 512);
    auto& undo = p.getUndoManager();
    auto& learn = p.getLearn();

    const auto before = plainValue (p, params::threshold);
    learnOver (p, voice (9.5));
    REQUIRE (learn.state() == LearnState::learned);
    CHECK (std::abs (plainValue (p, params::threshold) - before) > 0.01f);

    // The processor opened the transaction itself (no editor here): one step, and the step dismisses the result.
    REQUIRE (undo.canUndo());
    CHECK (undo.getUndoDescription() == "Learn");
    undo.undo();
    CHECK (plainValue (p, params::threshold) == Catch::Approx (before).margin (0.006));
    CHECK_FALSE (undo.canUndo());
    CHECK (learn.state() == LearnState::idle);

    // Automation, Bypass and Listen are not edits; a gesture on a sound parameter is.
    learnOver (p, voice (9.5));
    REQUIRE (learn.state() == LearnState::learned);
    setParam (p, params::range, 12.0f);
    for (const auto* id : { params::bypass, params::listen })
    {
        auto* param = p.getState().getParameter (id);
        param->beginChangeGesture();
        param->setValueNotifyingHost (1.0f);
        param->endChangeGesture();
    }
    CHECK (learn.state() == LearnState::learned);

    auto* range = p.getState().getParameter (params::range);
    range->beginChangeGesture();
    range->setValueNotifyingHost (range->convertTo0to1 (6.0f));
    range->endChangeGesture();
    CHECK (learn.state() == LearnState::idle);

    // Loading a preset is an edit too.
    learnOver (p, voice (9.5));
    REQUIRE (learn.state() == LearnState::learned);
    p.getPresets().load (1);
    CHECK (learn.state() == LearnState::idle);
}
