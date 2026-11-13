#pragma once

#include <atomic>
#include <cstdlib>
#include <new>

/**
    Counts every global allocation of the test executable, for the "no allocation in process()"
    checks. It replaces the global operator new and delete, so include it in exactly one .cpp of
    an executable (JUCE-free suites only).
*/
namespace inkcap::testing
{

inline std::atomic<long long> allocationCount { 0 };

/** Allocations since the program started; take the difference around the code under test. */
inline long long allocations() noexcept { return allocationCount.load(); }

} // namespace inkcap::testing

// GCC cannot see that these deletes pair with the malloc-based news below.
#if defined (__GNUC__) && ! defined (__clang__)
 #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void* operator new (std::size_t size)
{
    inkcap::testing::allocationCount.fetch_add (1);
    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    return operator new (size);
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
