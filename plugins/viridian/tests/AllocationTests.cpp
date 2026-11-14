// The audio thread allocates nothing: every operator new on the calling thread is counted while
// the chain runs (CLAUDE.md, "Audio thread").

#include "dsp/chain/EqChain.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <new>
#include <random>
#include <vector>

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

using namespace viridian;

TEST_CASE ("Allocation: the chain allocates nothing while every parameter is automated", "[chain][realtime]")
{
    for (const auto fs : { 44100.0, 192000.0 })
    {
        const int block = 64;
        const auto blocks = (int) (2.0 * fs) / block;

        // Every parameter at random, a few changing per block: model, Alt, shape keys, filters into
        // and out of their Off zones, EQ In, Stereo, Tolerance on and off, Noise, Drive, Output.
        std::mt19937 rng (17);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        std::vector<dsp::ChainSettings> settings ((size_t) blocks);
        dsp::ChainSettings s;
        s.model = Model::console;
        for (auto& at : settings)
        {
            for (int k = 0; k < 3; ++k)
            {
                const auto which = (int) (unit (rng) * 23.0f);
                if (which < numControls)
                    s.positions[(size_t) which] = unit (rng) < 0.3f ? (unit (rng) < 0.5f ? 0.0f : 1.0f) : unit (rng);
                else if (which == numControls)
                    s.model = unit (rng) < 0.7f ? Model::console : static_cast<Model> ((int) (unit (rng) * numModels) % numModels);
                else if (which == numControls + 1)
                    s.alt = ! s.alt;
                else if (which == numControls + 2)
                    s.eqIn = unit (rng) < 0.8f;
                else if (which == numControls + 3)
                    s.stereo = static_cast<StereoMode> ((int) (unit (rng) * 3.0f) % 3);
                else if (which == numControls + 4)
                    s.tolerance = unit (rng) < 0.5f ? 0.0f : unit (rng);
                else if (which == numControls + 5)
                    s.noise = unit (rng);
                else
                    s.driveDb = -12.0f + 36.0f * unit (rng), s.outputDb = -24.0f + 48.0f * unit (rng);
            }
            at = s;
        }

        dsp::EqChain chain;
        chain.prepare (fs, block, 2, settings.front());
        std::vector<float> l ((size_t) block), r ((size_t) block);
        float* ptrs[] { l.data(), r.data() };
        std::uniform_real_distribution<float> audio (-0.5f, 0.5f);
        for (auto& x : l)
            x = audio (rng);
        r = l;
        chain.process (ptrs, 2, block, settings.front());

        counting = true;
        allocations = 0;
        for (const auto& at : settings)
            chain.process (ptrs, 2, block, at);
        counting = false;

        INFO (fs << " Hz: " << allocations << " allocations in " << blocks << " blocks");
        CHECK (allocations == 0);
    }
}
