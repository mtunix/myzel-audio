// The audio thread allocates nothing: every operator new on the calling thread is counted while
// processBlock runs (CLAUDE.md, "Audio thread").

#include "support/ProcessorHelpers.h"

#include "dsp/feed/CutCurves.h"
#include "dsp/feed/SpectrumAnalyser.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <new>

namespace
{
    thread_local bool counting = false;
    thread_local long allocations = 0;

    void* allocate (std::size_t size, bool mayThrow)
    {
        if (counting)
            ++allocations;
        if (void* p = std::malloc (size > 0 ? size : 1))
            return p;
        if (mayThrow)
            throw std::bad_alloc();
        return nullptr;
    }
} // namespace

// The unaligned forms, replaced as a matched set; the aligned ones keep their defaults.
void* operator new (std::size_t size) { return allocate (size, true); }
void* operator new[] (std::size_t size) { return allocate (size, true); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept { return allocate (size, false); }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { return allocate (size, false); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }

using namespace halite;
using namespace halite::testing;

TEST_CASE ("Allocation: processBlock allocates nothing while every parameter is automated", "[processor][realtime]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto& key : { juce::AudioChannelSet::disabled(), juce::AudioChannelSet::stereo() })
    {
        HaliteProcessor p;
        REQUIRE (p.setBusesLayout (layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo(), key)));
        p.prepareToPlay (48000.0, 512);

        auto buffer = noise (2 + key.size(), 512, 5u);
        juce::MidiBuffer midi;
        std::mt19937 rng (17u);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        const auto& parameters = p.getParameters();

        dsp::SpectralView shown;
        shown.reserve();
        long counted = 0;
        for (int block = 0; block < 400; ++block)
        {
            // Parameter changes come from the host's thread, outside the count; so does the latency
            // setting, which the chain then fades over to.
            for (int k = 0; k < 3; ++k)
                parameters[(int) (unit (rng) * (float) parameters.size()) % parameters.size()]->setValueNotifyingHost (unit (rng));
            if (block % 37 == 0)
                p.setLatencyMode (block % 74 == 0 ? LatencyMode::live : LatencyMode::normal);
            // Learn pressed, and the message thread's turn, outside the count too; captures run and end inside it.
            if (block % 97 == 0)
                p.getLearn().start();
            p.getLearn().service();
            // The display reads and acknowledges Spectral's cut now and then, so both of the feed's publishing paths run.
            if (block % 3 == 0 && p.getChain().feed().spectralSnapshot().read (shown))
                p.getChain().feed().spectralSnapshot().acknowledge (shown.sequence);

            const auto n = 1 + (int) (unit (rng) * 511.0f);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), 0, n);

            allocations = 0;
            counting = true;
            p.processBlock (view, midi);
            counting = false;
            counted += allocations;

            // Keep the signal bounded while Output wanders.
            buffer = noise (2 + key.size(), 512, (unsigned) block);
        }

        INFO ("key " << key.getDescription());
        CHECK (counted == 0);
    }
}

TEST_CASE ("Allocation: the chain allocates nothing, with every noise cue, shape, Listen and latency", "[chain][realtime]")
{
    // The noise cue is not a parameter, so the processor test above runs only the default one; here
    // every setting changes block by block, block lengths up to 4096.
    for (const auto rate : { 44100.0, 96000.0 })
        for (const auto cue : { dsp::NoiseCueKind::periodicity, dsp::NoiseCueKind::flatness, dsp::NoiseCueKind::blend })
        {
            dsp::HaliteChain chain;
            chain.prepare (rate, 4096, 2, LatencyMode::normal);
            auto buffer = noise (4, 4096, 9u);
            std::mt19937 rng (23u);
            std::uniform_int_distribution<int> length (1, 4096);

            long counted = 0;
            for (int block = 0; block < 200; ++block)
            {
                dsp::ChainSettings s;
                s.detector.noiseCue = cue;
                s.detector.harshOn = block % 3 != 0;
                s.detector.stereo = static_cast<StereoMode> (block % numStereoModes);
                s.detector.key = block % 5 == 0 ? KeySource::external : KeySource::internal;
                s.detector.detectLowHz = block % 7 == 0 ? 3000.0f : 4500.0f;
                s.detector.latency = block % 11 == 0 ? LatencyMode::live : LatencyMode::normal;
                s.detector.detectHighHz = block % 13 == 0 ? 19000.0f : 12000.0f;
                s.shape = static_cast<Shape> (block % numShapes);
                s.listen = static_cast<Listen> ((block / 3) % numListenModes);
                s.bypass = block % 17 == 0;
                s.outputDb = (float) (block % 9) - 4.0f;
                const auto n = length (rng);

                if (block % 29 == 0)
                    chain.learn().request();

                // The transport plays, stops, starts again and jumps.
                const dsp::Timeline timeline { block % 23 == 0 ? (std::int64_t) block * 100000 : (std::int64_t) block * 4096, true, block % 19 != 0 };

                allocations = 0;
                counting = true;
                chain.process (buffer.getArrayOfWritePointers(), 2, n, s, buffer.getArrayOfReadPointers() + 2, 2, timeline);
                counting = false;
                counted += allocations;
            }

            INFO ("rate " << rate << ", cue " << (int) cue);
            CHECK (counted == 0);
        }
}

TEST_CASE ("Allocation: a Learn capture, from its request to its end, allocates nothing", "[chain][realtime][learn]")
{
    // The tests above restart captures faster than they end; this one lets one finish, Harsh on.
    dsp::HaliteChain chain;
    chain.prepare (48000.0, 512, 2, LatencyMode::normal);
    auto buffer = noise (2, 512, 13u);
    dsp::ChainSettings s;
    s.detector.harshOn = true;
    chain.learn().request();

    long counted = 0;
    for (int block = 0; block < 1000; ++block)
    {
        allocations = 0;
        counting = true;
        chain.process (buffer.getArrayOfWritePointers(), 2, 512, s);
        counting = false;
        counted += allocations;
        buffer = noise (2, 512, (unsigned) block);
    }

    CHECK (chain.learn().phase() == dsp::Learn::Phase::captured);
    CHECK (counted == 0);
}

TEST_CASE ("Allocation: the display's readers allocate nothing once prepared", "[chain][realtime][feed]")
{
    // Not the audio thread, but the display reads 30 times a second: nothing it calls per frame allocates.
    for (const auto shape : { Shape::split, Shape::spectral, Shape::wide })
    {
        dsp::HaliteChain chain;
        chain.prepare (48000.0, 512, 2, LatencyMode::normal);
        dsp::ChainSettings s;
        s.shape = shape;
        s.detector.harshOn = true;

        dsp::SpectrumAnalyser analyser;
        analyser.prepare (48000.0, 700);
        dsp::LevelMeters meters;
        meters.prepare (48000.0);
        dsp::SpectralView view;
        view.reserve();
        std::vector<dsp::HistoryRecord> records (4096);
        std::vector<dsp::EssEvent> events (dsp::EventRing::size);
        std::vector<float> ess ((size_t) analyser.numPoints()), harsh ((size_t) analyser.numPoints()), target ((size_t) analyser.numPoints());
        std::int64_t historyCursor = 0;
        std::uint64_t eventCursor = 0;
        const auto cut = dsp::CutSettings::from (s.detector, shape, 48000.0);
        auto buffer = noise (2, 512, 31u);

        long counted = 0;
        for (int block = 0; block < 300; ++block)
        {
            chain.process (buffer.getArrayOfWritePointers(), 2, 512, s);
            buffer = noise (2, 512, (unsigned) block + 100u);

            allocations = 0;
            counting = true;
            analyser.update (chain.feed().samples());
            meters.update (chain.feed().samples());
            chain.feed().history().read (historyCursor, records.data(), (int) records.size());
            chain.events().read (eventCursor, events.data(), (int) events.size());
            if (chain.feed().spectralSnapshot().read (view))
            {
                chain.feed().spectralSnapshot().acknowledge (view.sequence);
                analyser.bandLevels (view.target.data(), view.bins, view.binHz, target.data());
            }
            dsp::cutCurves::ess (cut, 6.0f, &view, analyser.frequencies(), analyser.numPoints(), ess.data());
            dsp::cutCurves::harsh (cut, 3.0f, &view, analyser.frequencies(), analyser.numPoints(), harsh.data());
            counting = false;
            counted += allocations;
        }

        INFO ("shape " << (int) shape);
        CHECK (counted == 0);
    }
}
