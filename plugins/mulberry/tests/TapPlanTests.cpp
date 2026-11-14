#include "dsp/TapPlan.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace mulberry;
using namespace mulberry::dsp;

namespace
{
    TapPlanInput base (Mode mode)
    {
        TapPlanInput in;
        in.mode = mode;
        in.stepLevels.fill (1.0f);
        return in;
    }

    float feedbackInto (const TapPlan& plan, int line)
    {
        float sum = 0.0f;
        for (const auto& l : plan.lines)
            for (int i = 0; i < l.numReads; ++i)
                sum += l.reads[(size_t) i].weights[(size_t) (line == 0 ? feedback0 : feedback1)];
        return sum;
    }

    int outputs (const LinePlan& l)
    {
        int n = 0;
        for (int i = 0; i < l.numReads; ++i)
            if (l.reads[(size_t) i].weights[outLeft] + l.reads[(size_t) i].weights[outRight] > 0.0f)
                ++n;
        return n;
    }
}

TEST_CASE ("TapPlan: feedback into every line sums to at most 1 in every mode", "[tapplan]")
{
    for (int m = 0; m < numModes; ++m)
        for (int mask = 1; mask < 8; ++mask)
            for (float c : { 0.0f, 0.4f, 1.0f })
                for (float g : { 0.0f, 0.3f, 0.5f })
                {
                    auto in = base (static_cast<Mode> (m));
                    in.headMask = mask;
                    in.crossFeed = c;
                    in.groove = g;
                    in.steps = 7;
                    const auto plan = compileTapPlan (in);
                    INFO ("mode " << m << " heads " << mask << " cross " << c << " groove " << g);
                    CHECK (feedbackInto (plan, 0) <= 1.0f + 1.0e-6f);
                    CHECK (feedbackInto (plan, 1) <= 1.0f + 1.0e-6f);
                    CHECK (feedbackInto (plan, 0) + feedbackInto (plan, 1) > 0.99f);
                }
}

TEST_CASE ("TapPlan: Single keeps the stereo image, Width 0 is a mono echo", "[tapplan]")
{
    auto in = base (Mode::single);
    const auto wide = compileTapPlan (in);
    CHECK (wide.lines[0].inputFromLeft == 1.0f);
    CHECK (wide.lines[1].inputFromRight == 1.0f);
    CHECK (wide.lines[0].reads[0].fraction == 1.0f);
    CHECK (wide.lines[0].reads[0].weights[outLeft] == Catch::Approx (1.0f));
    CHECK (wide.lines[0].reads[0].weights[outRight] == Catch::Approx (0.0f));

    in.width = 0.0f;
    const auto mono = compileTapPlan (in);
    CHECK (mono.lines[0].reads[0].weights[outLeft] == Catch::Approx (0.5f));
    CHECK (mono.lines[0].reads[0].weights[outRight] == Catch::Approx (0.5f));
    CHECK (mono.holdFraction[0] == 1.0f);
}

TEST_CASE ("TapPlan: Groove in Single makes a loop two echoes long with the odd echo swung", "[tapplan]")
{
    auto in = base (Mode::single);
    in.groove = 0.5f;   // 75 % swing
    const auto plan = compileTapPlan (in);
    const auto& l = plan.lines[0];
    REQUIRE (l.numReads == 3);
    CHECK (l.reads[0].fraction == Catch::Approx (1.5f));
    CHECK (l.reads[1].fraction == Catch::Approx (2.0f));
    CHECK (l.reads[2].fraction == Catch::Approx (2.0f));
    CHECK (l.reads[2].isFeedback());
    CHECK (plan.holdFraction[0] == Catch::Approx (2.0f));
}

TEST_CASE ("TapPlan: Loop's heads replace the single head, the pattern replaces the heads", "[tapplan]")
{
    auto in = base (Mode::single);
    in.headMask = 0b111;
    const auto heads = compileTapPlan (in);
    CHECK (outputs (heads.lines[0]) == 3);
    CHECK (heads.lines[0].reads[2].fraction == 3.0f);
    CHECK (feedbackInto (heads, 0) == Catch::Approx (1.0f));
    CHECK (heads.holdFraction[0] == 3.0f);

    in.mode = Mode::pattern;
    in.steps = 5;
    const auto pattern = compileTapPlan (in);
    CHECK (outputs (pattern.lines[0]) == 5);
}

TEST_CASE ("TapPlan: Drum's four heads sit at quarters of Time; Swell spreads the feedback over all four", "[tapplan]")
{
    auto in = base (Mode::single);
    in.headUnit = 0.25f;
    in.swellHeads = 4;
    in.headMask = 0b0001;
    const auto plain = compileTapPlan (in);
    REQUIRE (plain.lines[0].numReads == 2);
    CHECK (plain.lines[0].reads[0].fraction == 0.25f);
    CHECK (plain.lines[0].reads[1].fraction == 0.25f);
    CHECK (plain.lines[0].reads[1].weights[feedback0] == 1.0f);
    CHECK (plain.holdFraction[0] == 0.25f);

    in.swell = 0.5f;
    const auto swelled = compileTapPlan (in);
    REQUIRE (swelled.lines[0].numReads == 5);
    CHECK (outputs (swelled.lines[0]) == 1);
    CHECK (swelled.lines[0].reads[1].weights[feedback0] == Catch::Approx (0.5f + 0.125f));
    for (int t = 2; t <= 4; ++t)
    {
        CHECK (swelled.lines[0].reads[(size_t) t].fraction == Catch::Approx (0.25f * (float) t));
        CHECK (swelled.lines[0].reads[(size_t) t].weights[feedback0] == Catch::Approx (0.125f));
    }
    CHECK (feedbackInto (swelled, 0) == Catch::Approx (1.0f));
    CHECK (swelled.holdFraction[0] == 1.0f);

    // Riding Swell keeps the reads in place: only weights change, so the chain ramps them.
    in.swell = 0.9f;
    CHECK (compileTapPlan (in).samePositions (swelled));

    for (int m = 0; m < numModes; ++m)
        for (int mask = 1; mask < 16; ++mask)
            for (float s : { 0.0f, 0.3f, 1.0f })
                for (float c : { 0.0f, 0.6f })
                {
                    in.mode = static_cast<Mode> (m);
                    in.headMask = mask;
                    in.swell = s;
                    in.crossFeed = c;
                    in.steps = 6;
                    const auto plan = compileTapPlan (in);
                    INFO ("mode " << m << " heads " << mask << " swell " << s << " cross " << c);
                    CHECK (feedbackInto (plan, 0) <= 1.0f + 1.0e-6f);
                    CHECK (feedbackInto (plan, 1) <= 1.0f + 1.0e-6f);
                    CHECK (feedbackInto (plan, 0) + feedbackInto (plan, 1) > 0.99f);
                }
}

TEST_CASE ("TapPlan: Ping-Pong sums the input into the left line and crosses the feedback", "[tapplan]")
{
    const auto plan = compileTapPlan (base (Mode::pingPong));
    CHECK (plan.lines[0].inputFromLeft == Catch::Approx (0.5f));
    CHECK (plan.lines[0].inputFromRight == Catch::Approx (0.5f));
    CHECK (plan.lines[1].inputFromLeft == 0.0f);
    CHECK (plan.lines[1].inputFromRight == 0.0f);

    // Line 0's tap feeds line 1 and back.
    CHECK (plan.lines[0].reads[1].weights[feedback1] == 1.0f);
    CHECK (plan.lines[1].reads[1].weights[feedback0] == 1.0f);
    CHECK (plan.holdFraction[0] == 2.0f);
}

TEST_CASE ("TapPlan: Dual has two transports and cross-feed", "[tapplan]")
{
    auto in = base (Mode::dual);
    in.crossFeed = 0.25f;
    const auto plan = compileTapPlan (in);
    CHECK (plan.twoTransports);
    CHECK (plan.holdLine[1] == 1);
    CHECK (plan.lines[0].reads[1].weights[feedback0] == Catch::Approx (0.75f));
    CHECK (plan.lines[0].reads[1].weights[feedback1] == Catch::Approx (0.25f));
}

TEST_CASE ("TapPlan: Pattern steps are even over Time, even steps swing, pans follow the rule", "[tapplan]")
{
    auto in = base (Mode::pattern);
    in.steps = 4;
    in.stepLevels = { 1.0f, 0.5f, 0.0f, 0.25f };
    const auto plan = compileTapPlan (in);
    const auto& l = plan.lines[0];
    CHECK (l.reads[0].fraction == Catch::Approx (0.25f));
    CHECK (l.reads[3].fraction == Catch::Approx (1.0f));
    CHECK (l.reads[1].weights[outLeft] + l.reads[1].weights[outRight] == Catch::Approx (0.5f));

    // Alternate at full width: step 1 hard left on both lines, step 2 hard right.
    CHECK (plan.lines[0].reads[0].weights[outRight] == Catch::Approx (0.0f));
    CHECK (plan.lines[1].reads[0].weights[outRight] == Catch::Approx (0.0f));
    CHECK (plan.lines[1].reads[1].weights[outLeft] == Catch::Approx (0.0f));

    in.groove = 0.5f;
    const auto swung = compileTapPlan (in);
    CHECK (swung.lines[0].reads[1].fraction == Catch::Approx (0.5f + 0.125f));
    CHECK (swung.lines[0].reads[0].fraction == Catch::Approx (0.25f));

    for (int r = 0; r < numPanRules; ++r)
        for (int k = 0; k < 16; ++k)
        {
            const auto p = patternPan (static_cast<PanRule> (r), k, 16);
            CHECK (p >= -1.0f);
            CHECK (p <= 1.0f);
        }
}

TEST_CASE ("TapPlan: Feel moves the output heads, never the feedback taps", "[tapplan]")
{
    auto in = base (Mode::single);
    in.feelMs = 12.0f;
    const auto plan = compileTapPlan (in);
    for (int i = 0; i < plan.lines[0].numReads; ++i)
    {
        const auto& r = plan.lines[0].reads[(size_t) i];
        CHECK (r.offsetMs == (r.isFeedback() ? 0.0f : 12.0f));
    }

    auto other = in;
    other.feelMs = 0.0f;
    CHECK_FALSE (compileTapPlan (other).samePositions (plan));
    other = in;
    other.width = 0.3f;
    CHECK (compileTapPlan (other).samePositions (plan));
}
