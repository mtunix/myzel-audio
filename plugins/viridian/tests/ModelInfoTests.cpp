#include "dsp/EqParams.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace viridian;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
    constexpr std::array<Model, numModels> allModels { Model::passive, Model::inductor, Model::proportional,
                                                       Model::console, Model::mastering };

    constexpr std::array<Control, 4> gainControls { Control::lowGain, Control::lmGain, Control::hmGain, Control::highGain };
    constexpr std::array<Control, 6> freqControls { Control::hpfFreq, Control::lowFreq, Control::lmFreq,
                                                    Control::hmFreq, Control::highFreq, Control::lpfFreq };

    Control control (int i) { return static_cast<Control> (i); }

    std::string where (Model m, bool alt, Control c)
    {
        return std::string (info (m).key) + (alt ? " (Alt)" : "") + ", control " + std::to_string ((int) c);
    }

    std::vector<float> detentsOf (Model m, bool alt, Control c)
    {
        const auto& d = controlInfo (m, alt, c).detents;
        return { d.begin(), d.end() };
    }

    bool isGain (Control c)
    {
        return std::find (gainControls.begin(), gainControls.end(), c) != gainControls.end();
    }

    bool isFreq (Control c)
    {
        return std::find (freqControls.begin(), freqControls.end(), c) != freqControls.end();
    }
}

TEST_CASE ("ModelInfo: five models in panel order", "[modelinfo]")
{
    const std::array<std::string, numModels> names { "Passive", "Inductor", "Proportional", "Console", "Mastering" };
    const std::array<std::string, numModels> keys { "passive", "inductor", "proportional", "console", "mastering" };

    for (int i = 0; i < numModels; ++i)
    {
        const auto& m = modelInfos()[(size_t) i];
        CHECK ((int) m.model == i);
        CHECK (m.name == names[(size_t) i]);
        CHECK (m.key == keys[(size_t) i]);
        CHECK (std::string (m.tagline).size() > 0);
        CHECK (&info (m.model) == &m);
    }

    // PLAN.md, Performance budget (Settled 22).
    CHECK (juce::exactlyEqual (info (Model::passive).cpuBudgetPercent, 5.0f));
    CHECK (juce::exactlyEqual (info (Model::inductor).cpuBudgetPercent, 5.0f));
    CHECK (juce::exactlyEqual (info (Model::proportional).cpuBudgetPercent, 4.0f));
    CHECK (juce::exactlyEqual (info (Model::console).cpuBudgetPercent, 3.0f));
    CHECK (juce::exactlyEqual (info (Model::mastering).cpuBudgetPercent, 3.0f));
    CHECK (juce::exactlyEqual (info (Model::console).cpuDriveCeilingPercent, 10.0f));
    for (const auto m : { Model::passive, Model::inductor, Model::proportional, Model::mastering })
        CHECK (juce::exactlyEqual (info (m).cpuDriveCeilingPercent, 0.0f));

    CHECK (slotOf (Control::hpfFreq) == Slot::hpf);
    CHECK (slotOf (Control::lowShape) == Slot::low);
    CHECK (slotOf (Control::lmQ) == Slot::lowMid);
    CHECK (slotOf (Control::hmFreq) == Slot::highMid);
    CHECK (slotOf (Control::highShape) == Slot::high);
    CHECK (slotOf (Control::lpfFreq) == Slot::lpf);
}

TEST_CASE ("ModelInfo: step tables follow the models' NOTES", "[modelinfo]")
{
    using V = std::vector<float>;

    SECTION ("Passive")
    {
        CHECK (detentsOf (Model::passive, false, Control::lowFreq) == V { 20, 30, 60, 100 });
        CHECK (detentsOf (Model::passive, false, Control::lmFreq) == V { 200, 300, 500, 700, 1000, 1500, 2000, 3000, 4000, 5000, 7000 });
        CHECK (detentsOf (Model::passive, false, Control::hmFreq) == V { 3000, 4000, 5000, 8000, 10000, 12000, 16000 });
        CHECK (detentsOf (Model::passive, false, Control::highFreq) == V { 5000, 10000, 20000 });
        CHECK (numPositions (controlInfo (Model::passive, false, Control::lmFreq)) == 11);
        CHECK (numPositions (controlInfo (Model::passive, false, Control::hmFreq)) == 7);
    }

    SECTION ("Inductor")
    {
        CHECK (detentsOf (Model::inductor, false, Control::hpfFreq) == V { 50, 80, 160, 300 });
        CHECK (detentsOf (Model::inductor, false, Control::lowFreq) == V { 35, 60, 110, 220 });
        CHECK (detentsOf (Model::inductor, false, Control::lmFreq) == V { 360, 700, 1600, 3200, 4800, 7200 });
        CHECK (detentsOf (Model::inductor, false, Control::lpfFreq) == V { 6000, 8000, 10000, 14000, 18000 });

        // The filters' Off counts as a position: Off + 4 and 5 + Off.
        CHECK (numPositions (controlInfo (Model::inductor, false, Control::hpfFreq)) == 5);
        CHECK (numPositions (controlInfo (Model::inductor, false, Control::lpfFreq)) == 6);
        CHECK (controlInfo (Model::inductor, false, Control::hpfFreq).off == OffAt::start);
        CHECK (controlInfo (Model::inductor, false, Control::lpfFreq).off == OffAt::end);

        CHECK (juce::exactlyEqual (controlInfo (Model::inductor, false, Control::lowGain).max, 16.0f));
        CHECK (juce::exactlyEqual (controlInfo (Model::inductor, false, Control::lmGain).max, 18.0f));
        CHECK (juce::exactlyEqual (controlInfo (Model::inductor, false, Control::highGain).max, 16.0f));
        CHECK (controlInfo (Model::inductor, false, Control::highFreq).kind == Kind::fixed);
        CHECK (juce::exactlyEqual (controlInfo (Model::inductor, false, Control::highFreq).min, 12000.0f));
    }

    SECTION ("Proportional")
    {
        CHECK (detentsOf (Model::proportional, false, Control::lowFreq) == V { 30, 40, 50, 100, 200, 300, 400 });
        CHECK (detentsOf (Model::proportional, false, Control::lmFreq) == V { 75, 150, 180, 240, 500, 700, 1000 });
        CHECK (detentsOf (Model::proportional, false, Control::hmFreq) == V { 800, 1500, 3000, 5000, 8000, 10000, 12500 });
        CHECK (detentsOf (Model::proportional, false, Control::highFreq) == V { 2500, 5000, 7000, 10000, 12500, 15000, 20000 });

        for (const auto c : gainControls)
        {
            INFO ((int) c);
            CHECK (detentsOf (Model::proportional, false, c) == V { -12, -9, -6, -4, -2, 0, 2, 4, 6, 9, 12 });
            CHECK (controlInfo (Model::proportional, false, c).kind == Kind::stepped);
        }
    }

    SECTION ("Console")
    {
        // console/NOTES.md, "Slot mapping": the circuit's travel per card.
        struct Range
        {
            Control control;
            float early0, early1, later0, later1;
        };
        for (const auto& r : { Range { Control::hpfFreq, 10.74f, 308.96f, 15.11f, 409.64f },
                               Range { Control::lowFreq, 36.37f, 424.83f, 36.50f, 442.53f },
                               Range { Control::lmFreq, 193.98f, 2265.74f, 135.73f, 2201.33f },
                               Range { Control::hmFreq, 619.08f, 7231.08f, 635.33f, 10304.11f },
                               Range { Control::highFreq, 1261.03f, 14729.29f, 1294.14f, 20988.88f },
                               Range { Control::lpfFreq, 3252.85f, 22000.0f, 3188.89f, 22000.0f } })
        {
            INFO ((int) r.control);
            CHECK (juce::exactlyEqual (controlInfo (Model::console, false, r.control).min, r.early0));
            CHECK (juce::exactlyEqual (controlInfo (Model::console, false, r.control).max, r.early1));
            CHECK (juce::exactlyEqual (controlInfo (Model::console, true, r.control).min, r.later0));
            CHECK (juce::exactlyEqual (controlInfo (Model::console, true, r.control).max, r.later1));
            CHECK (controlInfo (Model::console, true, r.control).logScale);
        }
        CHECK (controlInfo (Model::console, false, Control::hpfFreq).off == OffAt::start);
        CHECK (controlInfo (Model::console, true, Control::lpfFreq).off == OffAt::end);
    }

    SECTION ("Mastering")
    {
        CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, Control::lowFreq).min, 15.0f));
        CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, Control::lowFreq).max, 800.0f));
        CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, Control::lmFreq).min, 120.0f));
        CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, Control::lmFreq).max, 8000.0f));
        CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, Control::hmFreq).min, 400.0f));
        CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, Control::hmFreq).max, 26000.0f));
        CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, Control::highFreq).min, 400.0f));
        CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, Control::highFreq).max, 26000.0f));

        for (const auto c : { Control::lowQ, Control::lmQ, Control::hmQ, Control::highQ })
        {
            CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, c).min, 0.4f));
            CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, c).max, 4.0f));
        }

        // No filters on the reference unit: PLAN.md's values.
        CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, Control::hpfFreq).min, 10.0f));
        CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, Control::hpfFreq).max, 500.0f));
        CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, Control::lpfFreq).min, 2000.0f));
        CHECK (juce::exactlyEqual (controlInfo (Model::mastering, false, Control::lpfFreq).max, 40000.0f));
    }
}

TEST_CASE ("ModelInfo: every control is described completely", "[modelinfo]")
{
    for (const auto m : allModels)
        for (const auto alt : { false, true })
            for (int i = 0; i < numControls; ++i)
            {
                const auto c = control (i);
                const auto& ci = controlInfo (m, alt, c);
                INFO (where (m, alt, c));

                const std::string legend (ci.legend);
                CHECK (legend.empty() == (ci.kind == Kind::dim));

                if (ci.kind == Kind::continuous)
                {
                    CHECK (ci.min < ci.max);
                    CHECK (ci.unit != Unit::none);
                    if (ci.logScale)
                        CHECK (ci.min > 0.0f);
                    CHECK (ci.detents.empty());
                }

                if (ci.kind == Kind::stepped)
                {
                    REQUIRE (ci.detents.size() >= 2);
                    for (size_t k = 1; k < ci.detents.size(); ++k)
                        CHECK (ci.detents[k - 1] < ci.detents[k]);
                }

                if (ci.kind == Kind::fixed)
                    CHECK (ci.unit != Unit::none);

                if (ci.kind == Kind::toggle)
                {
                    CHECK (std::string (ci.offName) == "Shelf");
                    CHECK (std::string (ci.onName).size() > 0);
                }

                // Shapes are keys; nothing else is.
                if (c == Control::lowShape || c == Control::highShape)
                    CHECK ((ci.kind == Kind::toggle || ci.kind == Kind::dim));
                else
                    CHECK (ci.kind != Kind::toggle);

                if (isFreq (c) && ci.kind != Kind::dim)
                    CHECK (ci.unit == Unit::hz);
                if (isGain (c) && ci.kind != Kind::dim)
                    CHECK ((ci.unit == Unit::db || ci.unit == Unit::dial));

                // Off only at the filters' outer ends.
                if (c == Control::hpfFreq)
                    CHECK ((ci.off == OffAt::start || ci.kind == Kind::dim || ci.kind == Kind::fixed));
                else if (c == Control::lpfFreq)
                    CHECK ((ci.off == OffAt::end || ci.kind == Kind::dim || ci.kind == Kind::fixed));
                else
                    CHECK (ci.off == OffAt::none);
            }
}

TEST_CASE ("ModelInfo: which slots dim", "[modelinfo]")
{
    auto kinds = [] (Model m, bool alt)
    {
        std::string s;
        for (int i = 0; i < numControls; ++i)
        {
            switch (controlInfo (m, alt, control (i)).kind)
            {
                case Kind::dim:        s += '-'; break;
                case Kind::fixed:      s += 'f'; break;
                case Kind::continuous: s += 'c'; break;
                case Kind::stepped:    s += 's'; break;
                case Kind::toggle:     s += 't'; break;
            }
        }
        return s;
    };

    // hpf | low freq gain q shape | lm freq gain q | hm freq gain q | high freq gain q shape | lpf
    CHECK (kinds (Model::passive, false)      == "-scc-sc-scc" "sc--" "-");
    CHECK (kinds (Model::inductor, false)     == "ssc--sc----" "fc--" "s");
    CHECK (kinds (Model::proportional, false) == "-ss-tss-ss-" "ss-t" "-");
    CHECK (kinds (Model::proportional, true)  == "fss-tss-ss-" "ss-t" "f");
    CHECK (kinds (Model::console, false)      == "cccftccccccccft" "c");
    CHECK (kinds (Model::console, true)       == "cccftccccccccft" "c");
    CHECK (kinds (Model::mastering, false)    == "cccctccccccccct" "c");
}

TEST_CASE ("ModelInfo: Alt's meaning per model", "[modelinfo]")
{
    CHECK_FALSE (info (Model::passive).alt.active);
    CHECK_FALSE (info (Model::mastering).alt.active);
    CHECK (std::string (info (Model::inductor).alt.legend) == "HI-Q");
    CHECK (std::string (info (Model::proportional).alt.legend) == "FILTERS");
    CHECK (std::string (info (Model::console).alt.legend) == "TIGHT");

    for (const auto m : allModels)
    {
        INFO (info (m).key);
        CHECK (std::string (info (m).alt.legend).empty() == ! info (m).alt.active);

        // A model without Alt reads the same section either way.
        if (! info (m).alt.active)
            CHECK (&info (m).section (true) == &info (m).eq);
    }

    // Inductor: Hi-Q changes the bell's circuit, not what the knobs mean.
    for (int i = 0; i < numControls; ++i)
    {
        const auto& a = controlInfo (Model::inductor, false, control (i));
        const auto& b = controlInfo (Model::inductor, true, control (i));
        CHECK (a.kind == b.kind);
        CHECK (juce::exactlyEqual (a.min, b.min));
        CHECK (juce::exactlyEqual (a.max, b.max));
        CHECK (a.detents.data() == b.detents.data());
    }

    // Proportional: Alt switches the fixed band-pass in.
    CHECK (controlInfo (Model::proportional, false, Control::hpfFreq).kind == Kind::dim);
    CHECK (juce::exactlyEqual (controlInfo (Model::proportional, true, Control::hpfFreq).min, 50.0f));
    CHECK (juce::exactlyEqual (controlInfo (Model::proportional, true, Control::lpfFreq).min, 15000.0f));

    // Console: the later card widens the gain and the mids' Q; each band has its own fixed bell Q.
    struct Band
    {
        Control control;
        float early, later;
    };
    for (const auto& b : { Band { Control::lowGain, 15.2f, 18.0f }, Band { Control::lmGain, 15.0f, 18.0f },
                           Band { Control::hmGain, 15.0f, 18.0f }, Band { Control::highGain, 16.1f, 18.0f } })
    {
        INFO ((int) b.control);
        CHECK (juce::exactlyEqual (controlInfo (Model::console, false, b.control).max, b.early));
        CHECK (juce::exactlyEqual (controlInfo (Model::console, false, b.control).min, -b.early));
        CHECK (juce::exactlyEqual (controlInfo (Model::console, true, b.control).max, b.later));
    }
    CHECK (juce::exactlyEqual (controlInfo (Model::console, false, Control::lmQ).min, 0.49f));
    CHECK (juce::exactlyEqual (controlInfo (Model::console, false, Control::lmQ).max, 2.89f));
    CHECK (juce::exactlyEqual (controlInfo (Model::console, true, Control::lmQ).min, 0.50f));
    CHECK (juce::exactlyEqual (controlInfo (Model::console, true, Control::lmQ).max, 4.26f));
    CHECK (juce::exactlyEqual (controlInfo (Model::console, false, Control::hmQ).min, 0.53f));
    CHECK (juce::exactlyEqual (controlInfo (Model::console, false, Control::hmQ).max, 2.78f));
    CHECK (juce::exactlyEqual (controlInfo (Model::console, true, Control::hmQ).min, 0.54f));
    CHECK (juce::exactlyEqual (controlInfo (Model::console, true, Control::hmQ).max, 4.06f));
    CHECK (juce::exactlyEqual (controlInfo (Model::console, false, Control::lowQ).min, 0.91f));
    CHECK (juce::exactlyEqual (controlInfo (Model::console, true, Control::lowQ).min, 2.12f));
    CHECK (juce::exactlyEqual (controlInfo (Model::console, false, Control::highQ).min, 0.62f));
    CHECK (juce::exactlyEqual (controlInfo (Model::console, true, Control::highQ).min, 0.94f));
}

TEST_CASE ("ModelInfo: maps are monotonic and reach every detent", "[modelinfo]")
{
    constexpr int steps = 4000;

    for (const auto m : allModels)
        for (const auto alt : { false, true })
            for (int i = 0; i < numControls; ++i)
            {
                const auto c = control (i);
                const auto& ci = controlInfo (m, alt, c);
                INFO (where (m, alt, c));

                if (ci.kind == Kind::continuous)
                {
                    const auto first = ci.off == OffAt::start ? filterOffZone : 0.0f;
                    const auto last = ci.off == OffAt::end ? 1.0f - filterOffZone : 1.0f;
                    CHECK (juce::exactlyEqual (map (ci, first).value, ci.min));
                    CHECK_THAT (map (ci, last).value, WithinRel (ci.max, 1.0e-6f));

                    auto previous = -1.0e9f;
                    for (int k = 0; k <= steps; ++k)
                    {
                        const auto v = map (ci, (float) k / (float) steps);
                        if (v.off)
                            continue;
                        REQUIRE (v.value > previous);
                        previous = v.value;
                    }
                }

                if (ci.kind == Kind::stepped || ci.kind == Kind::toggle)
                {
                    const auto n = numPositions (ci);
                    std::vector<int> hits ((size_t) n, 0);
                    auto previousIndex = -1;
                    auto previousValue = -1.0e9f;

                    for (int k = 0; k <= steps; ++k)
                    {
                        const auto x = (float) k / (float) steps;
                        const auto v = map (ci, x);
                        REQUIRE (v.detent >= previousIndex);
                        previousIndex = v.detent;
                        ++hits[(size_t) v.detent];

                        if (! v.off)
                        {
                            REQUIRE (v.value >= previousValue);
                            previousValue = v.value;
                        }
                    }

                    for (int k = 0; k < n; ++k)
                        CHECK (hits[(size_t) k] > 0);

                    // Detents span the whole travel: the ends are the first and last positions.
                    CHECK (map (ci, 0.0f).detent == 0);
                    CHECK (map (ci, 1.0f).detent == n - 1);
                }
            }
}

TEST_CASE ("ModelInfo: filters have an Off at their outer end", "[modelinfo]")
{
    for (const auto m : allModels)
        for (const auto alt : { false, true })
        {
            INFO (info (m).key << (alt ? " Alt" : ""));
            const auto& hpf = controlInfo (m, alt, Control::hpfFreq);
            const auto& lpf = controlInfo (m, alt, Control::lpfFreq);

            if (hpf.kind == Kind::stepped || hpf.kind == Kind::continuous)
            {
                CHECK (map (hpf, 0.0f).off);
                CHECK_FALSE (map (hpf, 1.0f).off);
                CHECK (juce::exactlyEqual (offPosition (hpf), 0.0f));
            }
            if (lpf.kind == Kind::stepped || lpf.kind == Kind::continuous)
            {
                CHECK (map (lpf, 1.0f).off);
                CHECK_FALSE (map (lpf, 0.0f).off);
                CHECK (juce::exactlyEqual (offPosition (lpf), 1.0f));
            }
        }

    // A continuous filter keeps its Off zone small: 1 % of the travel.
    const auto& hpf = controlInfo (Model::console, false, Control::hpfFreq);
    CHECK (map (hpf, 0.009f).off);
    CHECK_FALSE (map (hpf, 0.01f).off);
    CHECK (juce::exactlyEqual (map (hpf, 0.01f).value, 10.74f));
}

TEST_CASE ("ModelInfo: Gain 0.5 is flat on the bipolar models", "[modelinfo]")
{
    for (const auto m : allModels)
    {
        if (m == Model::passive)
            continue;

        for (const auto alt : { false, true })
            for (const auto c : gainControls)
            {
                const auto& ci = controlInfo (m, alt, c);
                INFO (where (m, alt, c));
                if (ci.kind == Kind::dim)
                    continue;
                CHECK (ci.unit == Unit::db);
                CHECK (juce::exactlyEqual (ci.flat, 0.5f));
                CHECK (juce::exactlyEqual (map (ci, 0.5f).value, 0.0f));

                // Boost and cut span the same range.
                CHECK (juce::exactlyEqual (map (ci, 0.0f).value, -map (ci, 1.0f).value));
                if (ci.kind == Kind::continuous)
                    for (const auto d : { 0.1f, 0.25f, 0.37f })
                        CHECK_THAT (map (ci, 0.5f + d).value, WithinAbs (-map (ci, 0.5f - d).value, 1.0e-5));
            }
    }
}

TEST_CASE ("ModelInfo: Passive's knobs read 0 at the start", "[modelinfo]")
{
    // PLAN.md: Boost, Atten and Dip are flat at the knob's start, not at 0.5.
    for (const auto c : { Control::lowGain, Control::lowQ, Control::lmGain, Control::hmGain, Control::highGain })
    {
        const auto& ci = controlInfo (Model::passive, false, c);
        INFO ((int) c);
        CHECK (ci.unit == Unit::dial);
        CHECK (juce::exactlyEqual (ci.flat, 0.0f));
        CHECK (juce::exactlyEqual (map (ci, 0.0f).value, 0.0f));
        CHECK (juce::exactlyEqual (map (ci, 1.0f).value, 10.0f));
        CHECK (juce::exactlyEqual (map (ci, 0.5f).value, 5.0f));
    }

    CHECK (std::string (controlInfo (Model::passive, false, Control::lowGain).legend) == "BOOST");
    CHECK (std::string (controlInfo (Model::passive, false, Control::lowQ).legend) == "ATTEN");
    CHECK (std::string (controlInfo (Model::passive, false, Control::lmGain).legend) == "DIP");
    CHECK (std::string (controlInfo (Model::passive, false, Control::hmGain).legend) == "BOOST");
    CHECK (std::string (controlInfo (Model::passive, false, Control::highGain).legend) == "ATTEN");

    // Bandwidth has no neutral setting: it shapes the boost, it adds nothing by itself.
    const auto& bandwidth = controlInfo (Model::passive, false, Control::hmQ);
    CHECK (std::string (bandwidth.legend) == "BANDWIDTH");
    CHECK (bandwidth.flat < 0.0f);
}

TEST_CASE ("ModelInfo: snapping and the inverse map", "[modelinfo]")
{
    for (const auto m : allModels)
        for (const auto alt : { false, true })
            for (int i = 0; i < numControls; ++i)
            {
                const auto c = control (i);
                const auto& ci = controlInfo (m, alt, c);
                INFO (where (m, alt, c));

                if (ci.kind == Kind::stepped || ci.kind == Kind::toggle)
                {
                    for (int k = 0; k < numPositions (ci); ++k)
                    {
                        const auto x = detentPosition (ci, k);
                        CHECK (positionIndex (ci, x) == k);
                        CHECK (juce::exactlyEqual (snap (ci, x), x));

                        // The physical value finds its own detent back.
                        if (const auto v = map (ci, x); ! v.off)
                            CHECK (juce::exactlyEqual (positionOf (ci, v.value), x));
                    }

                    for (const auto x : { 0.03f, 0.31f, 0.5f, 0.77f, 0.99f })
                        CHECK (juce::exactlyEqual (snap (ci, snap (ci, x)), snap (ci, x)));
                }

                if (ci.kind == Kind::continuous)
                {
                    for (const auto x : { 0.02f, 0.1f, 0.33f, 0.5f, 0.71f, 0.98f })
                    {
                        const auto v = map (ci, x);
                        REQUIRE_FALSE (v.off);
                        CHECK_THAT (positionOf (ci, v.value), WithinAbs (x, 1.0e-5));
                        CHECK (juce::exactlyEqual (snap (ci, x), x));
                    }
                }
            }

    // Out-of-range and NaN positions read as the nearest end.
    const auto& gain = controlInfo (Model::inductor, false, Control::lowGain);
    CHECK (juce::exactlyEqual (map (gain, -1.0f).value, -16.0f));
    CHECK (juce::exactlyEqual (map (gain, 2.0f).value, 16.0f));
    CHECK (juce::exactlyEqual (map (gain, std::nanf ("")).value, -16.0f));
}

TEST_CASE ("EqParams: the defaults are the Inductor, flat", "[modelinfo]")
{
    const auto p = defaultPositions();
    CHECK (juce::exactlyEqual (p[(size_t) Control::hpfFreq], 0.0f));
    CHECK (juce::exactlyEqual (p[(size_t) Control::lpfFreq], 1.0f));
    CHECK (juce::exactlyEqual (p[(size_t) Control::lowShape], 0.0f));
    CHECK (juce::exactlyEqual (p[(size_t) Control::highShape], 0.0f));
    CHECK (juce::exactlyEqual (p[(size_t) Control::lmGain], 0.5f));

    const auto eq = resolveEq (Model::inductor, false, p);
    CHECK (eq[Control::hpfFreq].off);
    CHECK (eq[Control::lpfFreq].off);
    for (const auto c : { Control::lowGain, Control::lmGain, Control::highGain })
        CHECK (juce::exactlyEqual (eq[c].value, 0.0f));
    CHECK (juce::exactlyEqual (eq[Control::highFreq].value, 12000.0f));
}

TEST_CASE ("EqParams: flat positions leave every model flat", "[modelinfo]")
{
    for (const auto m : allModels)
    {
        // The flat point does not move with Alt (flatPositions reads the Alt-off section).
        for (int i = 0; i < numControls; ++i)
            CHECK (juce::exactlyEqual (controlInfo (m, false, control (i)).flat, controlInfo (m, true, control (i)).flat));

        for (const auto alt : { false, true })
        {
            INFO (info (m).key << (alt ? " Alt" : ""));
            const auto eq = resolveEq (m, alt, flatPositions (m));

            for (int i = 0; i < numControls; ++i)
                if (controlInfo (m, alt, control (i)).flat >= 0.0f)
                    CHECK (juce::exactlyEqual (eq[control (i)].value, 0.0f));

            for (const auto c : { Control::hpfFreq, Control::lpfFreq })
            {
                const auto& ci = controlInfo (m, alt, c);
                if (ci.kind == Kind::stepped || ci.kind == Kind::continuous)
                    CHECK (eq[c].off);
            }
        }
    }
}

TEST_CASE ("EqParams: a model switch reinterprets the knobs", "[modelinfo]")
{
    auto p = defaultPositions();
    p[(size_t) Control::lowGain] = 0.62f;
    p[(size_t) Control::lowFreq] = 0.0f;

    CHECK_THAT (resolveEq (Model::inductor, false, p)[Control::lowGain].value, WithinAbs (3.84, 1.0e-5));
    CHECK (juce::exactlyEqual (resolveEq (Model::proportional, false, p)[Control::lowGain].value, 2.0f));   // nearest detent
    CHECK (resolveEq (Model::proportional, false, p)[Control::lowGain].detent == 6);
    CHECK_THAT (resolveEq (Model::console, false, p)[Control::lowGain].value, WithinAbs (3.648, 1.0e-5));
    CHECK_THAT (resolveEq (Model::console, true, p)[Control::lowGain].value, WithinAbs (4.32, 1.0e-5));
    CHECK_THAT (resolveEq (Model::passive, false, p)[Control::lowGain].value, WithinAbs (6.2, 1.0e-5));

    CHECK (juce::exactlyEqual (resolveEq (Model::inductor, false, p)[Control::lowFreq].value, 35.0f));
    CHECK (juce::exactlyEqual (resolveEq (Model::passive, false, p)[Control::lowFreq].value, 20.0f));
    CHECK (juce::exactlyEqual (resolveEq (Model::mastering, false, p)[Control::lowFreq].value, 15.0f));

    // Alt is ignored on a model without one.
    CHECK_FALSE (resolveEq (Model::mastering, true, p).alt);
    CHECK (resolveEq (Model::console, true, p).alt);
    CHECK (juce::exactlyEqual (resolveEq (Model::proportional, true, p)[Control::hpfFreq].value, 50.0f));
}
