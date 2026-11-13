/*
    The fade shape: exact ends, zero slope and curvature at both ends, equal power where it says
    so, and a Fade that lands exactly on its target.
*/

#include "dsp/common/Crossfade.h"

#include "common/dsp/FloatCompare.h"

#include <catch2/catch_test_macros.hpp>

using namespace inkcap::dsp;
using myzel::dsp::isExactly;

TEST_CASE ("Crossfade: exact ends, C2 at both, equal power and equal gain", "[crossfade]")
{
    CHECK (isExactly (fadeShape (0.0), 0.0));
    CHECK (isExactly (fadeShape (1.0), 1.0));
    CHECK (isExactly (fadeShape (-3.0), 0.0));
    CHECK (isExactly (fadeShape (7.0), 1.0));
    CHECK (std::abs (fadeShape (0.5) - 0.5) < 1.0e-15);

    // Slope and curvature vanish at the ends (finite differences of h).
    const double e = 1.0e-4;
    CHECK (fadeShape (e) / e < 1.0e-6);
    CHECK ((1.0 - fadeShape (1.0 - e)) / e < 1.0e-6);
    CHECK (std::abs (fadeShape (2.0 * e) - 2.0 * fadeShape (e)) / (e * e) < 1.0e-2);     // h'' (e) = 4 pi^2 e

    for (double u = 0.0; u <= 1.0; u += 0.01)
    {
        const auto p = equalPower (u);
        CHECK (std::abs (p.out * p.out + p.in * p.in - 1.0f) < 1.0e-6f);
        const auto g = equalGain (u);
        CHECK (std::abs (g.out + g.in - 1.0f) < 1.0e-6f);
    }
    CHECK ((isExactly (equalPower (0.0).out, 1.0) && isExactly (equalPower (0.0).in, 0.0)));
    CHECK ((isExactly (equalPower (1.0).out, 0.0) && isExactly (equalPower (1.0).in, 1.0)));
}

TEST_CASE ("Crossfade: a Fade lands exactly, restarts from where it is, and jumps when told", "[crossfade]")
{
    Fade f;
    f.setDuration (1000.0, 0.01);     // 10 samples
    f.jumpTo (1.0f);
    CHECK (f.isAt (1.0f));

    f.moveTo (0.25f);
    float last = 1.0f;
    for (int i = 0; i < 9; ++i)
    {
        const auto v = f.next();
        CHECK (v <= last);
        last = v;
    }
    CHECK (f.isMoving());
    CHECK (isExactly (f.next(), 0.25f));
    CHECK (f.isAt (0.25f));
    CHECK (isExactly (f.next(), 0.25f));

    f.moveTo (1.0f);
    for (int i = 0; i < 4; ++i)
        f.next();
    const auto midway = f.current();
    f.moveTo (0.0f);
    CHECK (f.next() <= midway);
    for (int i = 0; i < 20; ++i)
        f.next();
    CHECK (f.isAt (0.0f));
}
