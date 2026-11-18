// The audio thread allocates nothing: every operator new on the calling thread is counted while
// processBlock runs (CLAUDE.md, "Audio thread").

#include "support/ProcessorHelpers.h"

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

using namespace basalt;
using namespace basalt::testing;

TEST_CASE ("Allocation: processBlock allocates nothing while every parameter is automated", "[processor][realtime]")
{
    const juce::ScopedJuceInitialiser_GUI juce;

    for (const auto& main : { juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
        for (const auto& key : { juce::AudioChannelSet::disabled(), juce::AudioChannelSet::stereo() })
        {
            BasaltProcessor p;
            REQUIRE (p.setBusesLayout (layout (main, main, key)));
            p.prepareToPlay (48000.0, 4096);

            const auto channels = main.size() + key.size();
            auto buffer = noise (channels, 4096, 5u);
            juce::MidiBuffer midi;
            std::mt19937 rng (17u);
            std::uniform_real_distribution<float> unit (0.0f, 1.0f);
            std::uniform_int_distribution<int> length (1, 4096);
            const auto& parameters = p.getParameters();

            long counted = 0;
            for (int block = 0; block < 400; ++block)
            {
                // Parameter changes come from the host's thread, outside the count; so does the latency setting.
                for (int k = 0; k < 3; ++k)
                    parameters[(int) (unit (rng) * (float) parameters.size()) % parameters.size()]->setValueNotifyingHost (unit (rng));
                if (block % 37 == 0)
                    p.setLatencyMode (block % 74 == 0 ? LatencyMode::live : LatencyMode::normal);

                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), 0, length (rng));

                allocations = 0;
                counting = true;
                p.processBlock (view, midi);
                counting = false;
                counted += allocations;

                // Keep the signal bounded while Output wanders.
                buffer = noise (channels, 4096, (unsigned) block);
            }

            INFO (main.getDescription() << ", key " << key.getDescription());
            CHECK (counted == 0);
        }
}
