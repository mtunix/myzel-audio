/*
    ValueNoise: SeedRng::smooth with its lattice cached, the same numbers to the bit whatever the
    path the position takes.
*/

#include "dsp/common/ValueNoise.h"

#include "common/dsp/FloatCompare.h"

#include <catch2/catch_test_macros.hpp>

using namespace inkcap::dsp;
using myzel::dsp::isExactly;

TEST_CASE ("ValueNoise: the same as SeedRng::smooth, forwards, backwards and in jumps", "[noise]")
{
    const SeedRng rng (0x4393'0A3Du, Stream::warbleDrift);
    ValueNoise noise (rng);

    bool same = true;
    for (double p = -40.0; p < 40.0; p += 0.0137)
        same = same && isExactly (noise.at (p), rng.smooth (p));
    for (double p = 40.0; p > -40.0; p -= 0.731)
        same = same && isExactly (noise.at (p), rng.smooth (p));
    for (const auto p : { 1.0e6 + 0.25, -3.5, 7.0, 7.999, 8.0, 1.0e6 + 0.5 })
        same = same && isExactly (noise.at (p), rng.smooth (p));
    CHECK (same);

    // A new source forgets the old cells.
    const SeedRng other (7u, Stream::hissRumble);
    noise.setSource (other);
    CHECK (isExactly (noise.at (8.5), other.smooth (8.5)));
}
