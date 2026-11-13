// The audio thread allocates nothing, the chain's first block included: whatever a module builds
// lazily (the windowed sinc's table) must be built in prepare(). ctest runs each case in a fresh
// process, so nothing is built before the chain is prepared.

#include "support/AllocationCount.h"
#include "support/ChainGates.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace inkcap;
using namespace inkcap::dsp;
using namespace inkcap::gates;

TEST_CASE ("Chain: processing allocates nothing, from the first block on", "[chain][realtime]")
{
    constexpr double fs = 48000.0;
    const auto in = analysis::programme (fs, 3.0, 5);
    auto out = in;

    InkcapChain chain;
    chain.prepare (fs, 256, 2);

    // Every module on and busy, then every type, a seed change, Damage to 0 and back, a jump.
    const auto before = testing::allocations();
    for (int64_t start = 0; start + 256 <= (int64_t) in.size(); start += 256)
    {
        const auto t = (double) start / fs;
        const auto type = (int) (t * 2.0) % 5;
        auto s = everyModule (t > 2.0 && t < 2.2 ? 0.0f : 0.9f, 0.6f, 1.0f, type);
        s.seed = t < 1.5 ? 1u : 2u;
        const testing::Transport transport { true, t < 2.5 ? 0.0 : 64.0, 120.0 };
        float* channels[] = { out.left.data() + start, out.right.data() + start };
        chain.process (channels, 2, 256, s, transport.at (start, fs));
    }
    const auto counted = testing::allocations() - before;
    INFO (counted << " allocations while processing");
    CHECK (counted == 0);
}
