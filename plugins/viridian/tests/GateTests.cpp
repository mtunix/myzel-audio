// PLAN.md's quality gates through the real chain (tests/support/ChainGates.h, which ViridianMeasure
// prints in full; tools/measure/compare.py checks the curves against ngspice).

#include "support/ChainGates.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>

using namespace viridian;
using namespace viridian::gates;

namespace
{
    /** A few settings per card, among them the later card's high-pass joined to its bands. */
    std::vector<NamedSettings> sampleSettings()
    {
        std::vector<NamedSettings> picked;
        for (const bool alt : { false, true })
            for (auto& s : consoleGrid (alt, 3))
                for (const auto* name : { ": Init", ": HPF 70 %", ": HPF 100 %", ": LPF 0 %", ": HMF boost, Q 100 %, freq 100 %",
                                          ": HF shelf boost, freq 0 %", ": LF bell cut, freq 0 %", ": every band at work, LF shelf, HF bell",
                                          ": random 2" })
                    if (s.name.ends_with (name))
                        picked.push_back (s);
        return picked;
    }
} // namespace

TEST_CASE ("Gates: display = audio through the chain, and the networks' H(s), at every rate", "[gates][curves]")
{
    const auto hz = curveHz();
    const auto settings = sampleSettings();
    REQUIRE (settings.size() == 18);
    for (const auto fs : rates)
    {
        dsp::EqChain chain;
        chain.prepare (fs, 512, 2, consoleFlat (false));
        const auto amps = ampsOnlyResponse (chain, fs, hz);
        double audio = 0.0, analog = 0.0;
        std::string where;
        for (const auto& s : settings)
        {
            const auto c = consoleCurve (chain, s.settings, fs, hz, amps);
            const auto d = worstDb (c.display, c.measured, hz);
            if (d > audio)
                audio = d, where = s.name;
            analog = std::max (analog, worstDb (c.analog, c.measured, hz));
        }
        INFO (fs << " Hz: display = audio within " << audio << " dB (" << where << "), the networks' H(s) within " << analog << " dB");
        CHECK (audio <= 0.05);
        CHECK (analog <= 0.1);
    }
}

TEST_CASE ("Gates: flat is the circuit's flat (Settled 12), at every rate", "[gates][flat]")
{
    for (const bool alt : { false, true })
        for (const auto fs : rates)
        {
            const auto f = flatness (consoleFlat (alt), fs);
            INFO ((alt ? "later" : "early") << " card, " << fs << " Hz: " << f.fromNetwork << " dB from the netlists' flat; the circuit lifts "
                                            << f.fromAmps << " dB over the amps alone");
            CHECK (f.fromNetwork <= 0.1);
        }
}

TEST_CASE ("Gates: Drive changes the colour, not the level, over the whole range", "[gates][drive]")
{
    for (const bool alt : { false, true })
        for (const auto fs : { 44100.0, 192000.0 })
        {
            const auto d = driveDeviationDb (consoleWorking (alt), fs);
            INFO ((alt ? "later" : "early") << " card, " << fs << " Hz: " << d << " dB");
            CHECK (d <= 0.1);
        }
}

TEST_CASE ("Gates: every knob sweeps through the chain without a click", "[gates][automation]")
{
    for (const bool alt : { false, true })
        for (const auto c : { Control::hpfFreq, Control::lowFreq, Control::lowGain, Control::lmFreq, Control::lmGain, Control::lmQ,
                              Control::hmFreq, Control::hmGain, Control::hmQ, Control::highFreq, Control::highGain, Control::lpfFreq })
        {
            const auto crest = sweepCrestDb (consoleWorking (alt), c, 44100.0);
            INFO ((alt ? "later" : "early") << " card, control " << (int) c << ": " << crest << " dB");
            CHECK (crest <= -60.0);
        }
    // The filters and band frequencies at 48 kHz (the map's band edge at 20 kHz) and 192 kHz (a map of degree 2).
    for (const auto fs : { 48000.0, 192000.0 })
        for (const bool alt : { false, true })
            for (const auto c : { Control::hpfFreq, Control::lowFreq, Control::lmFreq, Control::hmFreq, Control::highFreq, Control::lpfFreq })
            {
                const auto crest = sweepCrestDb (consoleWorking (alt), c, fs);
                INFO ((alt ? "later" : "early") << " card, " << fs << " Hz, control " << (int) c << ": " << crest << " dB");
                CHECK (crest <= -60.0);
            }
}

TEST_CASE ("Gates: every knob jumps across its travel without a click", "[gates][automation]")
{
    // A host jumps a knob in one block: the pots glide (Network) instead of the coefficients
    // stepping. From and to each end of the travel and the middle (travelPoints()).
    for (const auto fs : { 44100.0, 48000.0 })
        for (const bool alt : { false, true })
            for (const auto c : { Control::hpfFreq, Control::lowFreq, Control::lowGain, Control::lmFreq, Control::lmGain, Control::lmQ,
                                  Control::hmFreq, Control::hmGain, Control::hmQ, Control::highFreq, Control::highGain, Control::lpfFreq })
                for (const auto from : travelPoints (c))
                    for (const auto to : travelPoints (c))
                    {
                        if (std::abs (from - to) < 0.01f)
                            continue;
                        const auto crest = jumpCrestDb (consoleWorking (alt), c, from, to, fs);
                        INFO ((alt ? "later" : "early") << " card, " << fs << " Hz, control " << (int) c << " from " << from << " to " << to
                                                        << ": " << crest << " dB");
                        CHECK (crest <= -60.0);
                    }
}

TEST_CASE ("Gates: Drive jumps without a click; its compensation follows the model's delay", "[gates][automation][drive]")
{
    // Drive -12 <-> +24 dB every 50 ms, the amps linear (-40 dBFS): the chain divides by the Drive
    // that went in a model latency earlier, so the level holds while Drive glides.
    for (const auto fs : { 44100.0, 96000.0 })
        for (const bool alt : { false, true })
        {
            const auto crest = driveJumpCrestDb (consoleWorking (alt), fs);
            INFO ((alt ? "later" : "early") << " card, " << fs << " Hz: " << crest << " dB");
            CHECK (crest <= -60.0);
        }
}

TEST_CASE ("Gates: Output jumps without a click", "[gates][automation]")
{
    // Output -24 <-> +24 dB every 50 ms: the two-stage glide has no corner where it starts or
    // stops (a linear ramp in dB has one at each end).
    for (const auto fs : { 44100.0, 96000.0 })
        for (const bool alt : { false, true })
        {
            const auto crest = outputJumpCrestDb (consoleWorking (alt), fs);
            INFO ((alt ? "later" : "early") << " card, " << fs << " Hz: " << crest << " dB");
            CHECK (crest <= -60.0);
        }
}

TEST_CASE ("Gates: Alt crossfades the cards without a click, both ways", "[gates][automation]")
{
    // The fading card keeps its own pots: the current ones follow the other card's laws.
    for (const auto fs : { 44100.0, 48000.0 })
        for (const bool from : { false, true })
            for (const int block : { 32, 256 })
            {
                const auto at = (size_t) (0.5 * fs);
                dsp::EqChain chain;
                chain.prepare (fs, block, 2, consoleWorking (from));
                const auto out = render (
                    chain,
                    [&] (size_t pos)
                    {
                        auto s = consoleWorking (from);
                        s.alt = pos >= at ? ! from : from;
                        return s;
                    },
                    sine (330.0, fs, 1.0, 0.25), block);
                const auto crest = std::max (clickCrestDb (out.left, (size_t) (0.05 * fs)), clickCrestDb (out.right, (size_t) (0.05 * fs)));
                INFO ((from ? "later" : "early") << " to " << (from ? "early" : "later") << ", " << fs << " Hz, blocks of " << block << ": " << crest << " dB");
                CHECK (crest <= -60.0);
            }
}

namespace
{
    /** Each Console knob group (consoleGroups()) swept together at every rate from one base on one card. */
    void groupSweeps (bool alt, int base)
    {
        const auto b = consoleBases (alt)[(size_t) base];
        for (const auto fs : rates)
            for (const auto& [name, group] : consoleGroups (b))
            {
                const auto crest = groupSweepCrestDb (b.settings, group, fs);
                INFO ((alt ? "later" : "early") << " card, " << b.name << ", " << fs << " Hz, " << name << ": " << crest << " dB");
                CHECK (crest <= -60.0);
            }
    }

    /** Each Console knob group jumped together between its travel points at every rate from one base, at the tone's worst phase. */
    void groupJumps (bool alt, int base)
    {
        const auto b = consoleBases (alt)[(size_t) base];
        for (const auto fs : rates)
            for (const auto& [name, group] : consoleGroups (b))
            {
                const auto crest = groupJumpsCrestDb (b.settings, group, fs);
                INFO ((alt ? "later" : "early") << " card, " << b.name << ", " << fs << " Hz, " << name << ": " << crest << " dB");
                CHECK (crest <= -60.0);
            }
    }

    /** Every knob jumped at once between 200 seeded random settings from one base at one rate, at the tone's worst phase. */
    void randomJumpCrests (bool alt, int base, double fs)
    {
        const auto b = consoleBases (alt)[(size_t) base];
        const auto [crest, pair] = randomJumpsCrestDb (b, fs);
        INFO ((alt ? "later" : "early") << " card, " << b.name << ", " << fs << " Hz: " << crest << " dB (pair " << pair << " of 200)");
        CHECK (crest <= -60.0);
    }
} // namespace

// One test case per card and base (consoleBases()), the random jumps also per rate, so ctest runs them side by side.
#define VIRIDIAN_RANDOM_JUMP_TEST(card, alt, base, baseName, rate, rateName)                                                                 \
    TEST_CASE ("Gates: every knob jumped at once between random settings, " card ", " baseName ", " rateName, "[gates][automation][groups]") \
    {                                                                                                                                        \
        randomJumpCrests (alt, base, rate);                                                                                                  \
    }

#define VIRIDIAN_GROUP_TESTS(card, alt, base, baseName)                                                                 \
    TEST_CASE ("Gates: knobs moved together sweep without a click, " card ", " baseName, "[gates][automation][groups]") \
    {                                                                                                                   \
        groupSweeps (alt, base);                                                                                        \
    }                                                                                                                   \
    TEST_CASE ("Gates: knobs jumped together without a click, " card ", " baseName, "[gates][automation][groups]")      \
    {                                                                                                                   \
        groupJumps (alt, base);                                                                                         \
    }                                                                                                                   \
    VIRIDIAN_RANDOM_JUMP_TEST (card, alt, base, baseName, 44100.0, "44.1 kHz")                                          \
    VIRIDIAN_RANDOM_JUMP_TEST (card, alt, base, baseName, 48000.0, "48 kHz")                                            \
    VIRIDIAN_RANDOM_JUMP_TEST (card, alt, base, baseName, 96000.0, "96 kHz")                                            \
    VIRIDIAN_RANDOM_JUMP_TEST (card, alt, base, baseName, 192000.0, "192 kHz")

VIRIDIAN_GROUP_TESTS ("early card", false, 0, "LF shelf, HF bell, both filters in")
VIRIDIAN_GROUP_TESTS ("early card", false, 1, "LF bell, HF shelf, both filters in")
VIRIDIAN_GROUP_TESTS ("early card", false, 2, "LF shelf, HF shelf, high-pass in, low-pass Off")
VIRIDIAN_GROUP_TESTS ("early card", false, 3, "LF bell, HF bell, both filters Off")
VIRIDIAN_GROUP_TESTS ("later card", true, 0, "LF shelf, HF bell, both filters in")
VIRIDIAN_GROUP_TESTS ("later card", true, 1, "LF bell, HF shelf, both filters in")
VIRIDIAN_GROUP_TESTS ("later card", true, 2, "LF shelf, HF shelf, high-pass in, low-pass Off")
VIRIDIAN_GROUP_TESTS ("later card", true, 3, "LF bell, HF bell, both filters Off")
VIRIDIAN_GROUP_TESTS ("early card", false, 4, "LF bell, HF shelf, high-pass Off, low-pass in")
VIRIDIAN_GROUP_TESTS ("later card", true, 4, "LF bell, HF shelf, high-pass Off, low-pass in")

namespace
{
    // A whole setting's change moves the tone's level by tens of dB (Output against the bands, each
    // gliding at its own pace, overshoots in between): its clicks are read against the tone's level
    // around them (worstPhaseLocalCrestDb()), which reads a clean tone the same at any level.

    /** 200 seeded preset changes from one card (presetJumpPairs()) at one rate. */
    void presetJumpCrests (bool alt, double fs)
    {
        const auto [crest, pair] = jumpPairsCrestDb (presetJumpPairs (alt, 200), fs, 1, true);
        INFO ("from the " << (alt ? "later" : "early") << " card, " << fs << " Hz: " << crest << " dB (pair " << pair << " of 200)");
        CHECK (crest <= -60.0);
    }

    /** 100 seeded pairs of whole settings (presetJumpPairs()) toggled every 50 ms and every 200 ms at one rate. */
    void abToggleCrests (bool alt, double fs)
    {
        const auto pairs = presetJumpPairs (alt, 100);
        for (const auto period : { 0.05, 0.2 })
        {
            const auto [crest, pair] = togglePairsCrestDb (pairs, fs, period, 1, true);
            INFO ("from the " << (alt ? "later" : "early") << " card, every " << 1000.0 * period << " ms, " << fs << " Hz: " << crest
                              << " dB (pair " << pair << " of 100)");
            CHECK (crest <= -60.0);
        }
    }
} // namespace

// PLAN.md Provisional 7: a preset change or an A/B switch moves every parameter at once.
#define VIRIDIAN_PRESET_TESTS(card, alt, rate, rateName)                                                                                     \
    TEST_CASE ("Gates: every parameter jumped at once as a preset change does, from the " card ", " rateName, "[gates][automation][groups]") \
    {                                                                                                                                        \
        presetJumpCrests (alt, rate);                                                                                                        \
    }                                                                                                                                        \
    TEST_CASE ("Gates: A/B toggled between whole settings, from the " card ", " rateName, "[gates][automation][groups]")                     \
    {                                                                                                                                        \
        abToggleCrests (alt, rate);                                                                                                          \
    }

VIRIDIAN_PRESET_TESTS ("early card", false, 44100.0, "44.1 kHz")
VIRIDIAN_PRESET_TESTS ("early card", false, 48000.0, "48 kHz")
VIRIDIAN_PRESET_TESTS ("early card", false, 96000.0, "96 kHz")
VIRIDIAN_PRESET_TESTS ("early card", false, 192000.0, "192 kHz")
VIRIDIAN_PRESET_TESTS ("later card", true, 44100.0, "44.1 kHz")
VIRIDIAN_PRESET_TESTS ("later card", true, 48000.0, "48 kHz")
VIRIDIAN_PRESET_TESTS ("later card", true, 96000.0, "96 kHz")
VIRIDIAN_PRESET_TESTS ("later card", true, 192000.0, "192 kHz")

TEST_CASE ("Gates: knobs moved together, the long runs, reported", "[gates][.report]")
{
    // What ViridianMeasure gates: 1000 random every-knob jumps per card, base and rate, 1000 preset
    // changes per card and rate, 500 pairs toggled every 50 and every 200 ms, from measureSeeds; on
    // every core. The whole settings' changes also against the whole render's level, and their
    // overshoot.
    for (const bool alt : { false, true })
        for (const auto fs : rates)
        {
            for (const auto& b : consoleBases (alt))
            {
                const auto [crest, pair] = randomJumpsCrestDb (b, fs, 1000, allCores(), measureSeeds);
                std::printf ("Console, %s card, %-48s %6.1f kHz: random jumps %7.2f dB (pair %d of 1000)\n", alt ? "later" : "early",
                             b.name.c_str(), fs / 1000.0, crest, pair);
            }
            const auto presets = presetJumpPairs (alt, 1000, measureSeeds);
            const auto [preset, presetPair] = jumpPairsCrestDb (presets, fs, allCores(), true);
            const auto [whole, wholePair] = jumpPairsCrestDb (presets, fs, allCores());
            const auto [overshoot, overshootPair] = jumpPairsOvershootDb (presets, fs, allCores());
            std::printf ("Console, from the %s card, %6.1f kHz: preset changes %7.2f dB (pair %d of 1000; against the whole level %.2f dB, "
                         "pair %d; overshoot %.1f dB, pair %d)",
                         alt ? "later" : "early", fs / 1000.0, preset, presetPair, whole, wholePair, overshoot, overshootPair);
            const auto pairs = presetJumpPairs (alt, 500, measureSeeds);
            for (const auto period : { 0.05, 0.2 })
            {
                const auto [toggled, togglePair] = togglePairsCrestDb (pairs, fs, period, allCores(), true);
                std::printf (", A/B every %.0f ms %7.2f dB (pair %d of 500)", 1000.0 * period, toggled, togglePair);
            }
            std::printf ("\n");
            std::fflush (stdout);
        }
}

TEST_CASE ("Gates: the bases are the ones the group tests name", "[gates][groups]")
{
    for (const bool alt : { false, true })
    {
        const auto bases = consoleBases (alt);
        REQUIRE (bases.size() == 5);
        CHECK (bases[0].name == "LF shelf, HF bell, both filters in");
        CHECK (bases[1].name == "LF bell, HF shelf, both filters in");
        CHECK (bases[2].name == "LF shelf, HF shelf, high-pass in, low-pass Off");
        CHECK (bases[3].name == "LF bell, HF bell, both filters Off");
        CHECK (bases[4].name == "LF bell, HF shelf, high-pass Off, low-pass in");
    }
}

namespace
{
    /**
        The aliasing gate's ctest subset (PLAN.md Settled 16, Provisional 4, 6 and 11; the full grid
        is ViridianMeasure's): per card and tone, Init, every band at work and three settings among
        the worst over the full grid (ViridianMeasure --aliasing, 2026-11-14), at every Drive
        -12 ... +24 dB in 3 dB steps; on the later card at 7 kHz also the HF bell at full boost near
        the top of its travel (Provisional 4's case), and at 15 kHz the random setting that reads
        worst against the output's tone (Provisional 11).
    */
    void aliasingGate (bool alt, double tone, std::initializer_list<const char*> worst)
    {
        std::vector<NamedSettings> settings { { "Init", consoleFlat (alt) }, { "every band at work", consoleWorking (alt) } };
        std::vector<std::string> names (worst.begin(), worst.end());
        if (alt && myzel::dsp::isExactly (tone, 7000.0))
            names.push_back ("HF boosted, bell, freq 90 %");
        auto all = boostedSettings (alt);
        for (auto& c : cutSettings (alt))
            all.push_back (std::move (c));
        for (const auto& name : names)
        {
            const auto it = std::find_if (all.begin(), all.end(), [&] (const NamedSettings& n) { return n.name == name; });
            REQUIRE (it != all.end());
            settings.push_back (*it);
        }
        std::vector<AliasCase> cases;
        for (const auto& [name, s] : settings)
            for (float drive = -12.0f; drive <= 24.0f; drive += 3.0f)
                cases.push_back ({ name, s, drive, tone });
        const auto results = aliasDbc (cases, 1);
        for (size_t k = 0; k < cases.size(); ++k)
        {
            INFO ((alt ? "later" : "early") << " card, " << cases[k].name << ", Drive " << cases[k].driveDb << " dB, " << tone << " Hz: "
                                            << results[k] << " dBc");
            CHECK (results[k] <= -80.0);
        }
    }

    /** The tone scan (aliasToneScan()) on one card at one Drive, tones from `from` to `to`. */
    void toneScanGate (bool alt, float drive, int from, int to)
    {
        std::vector<AliasCase> cases;
        for (const auto& c : aliasToneScan())
            if (c.settings.alt == alt && myzel::dsp::isExactly (c.driveDb, drive) && c.toneHz >= from && c.toneHz <= to)
                cases.push_back (c);
        REQUIRE (cases.size() >= 65);
        const auto results = aliasDbc (cases, 1);
        for (size_t k = 0; k < cases.size(); ++k)
        {
            INFO (cases[k].name << ", Drive " << drive << " dB, " << cases[k].toneHz << " Hz: " << results[k] << " dBc");
            CHECK (results[k] <= -80.0);
        }
    }
} // namespace

// Drive +6.3 dB clips the tone at the input amp's rails, a band boosted earlier at T8; the clip and
// the slew run at 8x on the cubic spline of the input, antialiased (console NOTES.md, "Amplifier").
#define VIRIDIAN_ALIAS_GATE(card, alt, tone, ...)                                                                                  \
    TEST_CASE ("Gates: the amps alias below -80 dBc at every Drive and setting, " card " card, " #tone " Hz", "[gates][aliasing]") \
    {                                                                                                                              \
        aliasingGate (alt, (double) tone, { __VA_ARGS__ });                                                                        \
    }

VIRIDIAN_ALIAS_GATE ("early", false, 1000, "HF boosted, shelf, freq 50 %", "HF boosted, shelf, freq 75 %", "HF boosted, bell, freq 100 %")
VIRIDIAN_ALIAS_GATE ("later", true, 1000, "HF boosted, bell, freq 90 %", "HF boosted, bell, freq 100 %", "HF boosted, shelf, freq 50 %")
VIRIDIAN_ALIAS_GATE ("early", false, 3000, "HF boosted, shelf, freq 90 %", "HF boosted, shelf, freq 100 %", "HF boosted, bell, freq 90 %")
VIRIDIAN_ALIAS_GATE ("later", true, 3000, "HF boosted, shelf, freq 75 %", "HF boosted, shelf, freq 90 %", "HF boosted, bell, freq 75 %")
VIRIDIAN_ALIAS_GATE ("early", false, 7000, "every band boosted, shelves", "LF boosted, bell, freq 0 %", "HMF boosted, highest Q, freq 100 %")
VIRIDIAN_ALIAS_GATE ("later", true, 7000, "HMF boosted, highest Q, freq 100 %", "every band boosted, shelves", "every band boosted, bells")
VIRIDIAN_ALIAS_GATE ("early", false, 11000, "LF boosted, bell, freq 100 %", "LMF boosted, lowest Q, freq 0 %", "LMF boosted, highest Q, freq 75 %")
VIRIDIAN_ALIAS_GATE ("later", true, 11000, "random 19", "LMF boosted, highest Q, freq 90 %", "LF boosted, shelf, freq 90 %")
VIRIDIAN_ALIAS_GATE ("early", false, 15000, "HMF boosted, lowest Q, freq 90 %", "HMF boosted, highest Q, freq 90 %", "HF boosted, bell, freq 0 %")
VIRIDIAN_ALIAS_GATE ("later", true, 15000, "LMF boosted, lowest Q, freq 100 %", "HF boosted, bell, freq 100 %", "HPF at its top", "random 4")

// A clipped harmonic within reach of Nyquist meets the chain's 2x pairs' transition band
// (ChainBandLimit); the five tones above never put one there.
#define VIRIDIAN_TONE_SCAN(card, alt, drive, from, to)                                                                                          \
    TEST_CASE ("Gates: tones from " #from " to " #to " Hz alias below -80 dBc, Init, " card " card, Drive +" #drive " dB", "[gates][aliasing]") \
    {                                                                                                                                           \
        toneScanGate (alt, (float) drive, from, to);                                                                                            \
    }

VIRIDIAN_TONE_SCAN ("early", false, 12, 2000, 8400)
VIRIDIAN_TONE_SCAN ("early", false, 12, 8500, 15000)
VIRIDIAN_TONE_SCAN ("early", false, 24, 2000, 8400)
VIRIDIAN_TONE_SCAN ("early", false, 24, 8500, 15000)
VIRIDIAN_TONE_SCAN ("later", true, 12, 2000, 8400)
VIRIDIAN_TONE_SCAN ("later", true, 12, 8500, 15000)
VIRIDIAN_TONE_SCAN ("later", true, 24, 2000, 8400)
VIRIDIAN_TONE_SCAN ("later", true, 24, 8500, 15000)

TEST_CASE ("Gates: input near Nyquist aliases below -80 dB in the input amp", "[gates][aliasing]")
{
    // The 2x pairs image half of what lies near Nyquist; the band-limit in front of the first pair
    // keeps it from the input amp's clip (ChainBandLimit). Read against the input tone, which the
    // band-limit removes above 20.3 kHz.
    const auto cases = aliasNyquistScan (200);
    std::vector<AliasCase> picked;
    for (const auto& c : cases)
        if (c.toneHz >= 19000.0 && ! myzel::dsp::isExactly (c.driveDb, 18.0f))
            picked.push_back (c);
    REQUIRE (picked.size() == 120);
    const auto results = aliasDbc (picked, 1, AliasReference::input);
    for (size_t k = 0; k < picked.size(); ++k)
    {
        INFO (picked[k].name << ", Drive " << picked[k].driveDb << " dB, " << picked[k].toneHz << " Hz: " << results[k] << " dB re the input");
        CHECK (results[k] <= -80.0);
    }
}

TEST_CASE ("Gates: aliasing and THD against level", "[gates][.report]")
{
    for (const bool alt : { false, true })
    {
        std::printf ("Console, %s card: 7 kHz at -1 dBFS, 44.1 kHz, worst alias:", alt ? "later" : "early");
        for (float drive = -12.0f; drive <= 24.0f; drive += 3.0f)
            std::printf ("  Drive %+.0f %.1f dBc", drive, worstAliasDbc (consoleFlat (alt), drive));
        std::printf ("\nConsole, %s card: THD (H2 ... H9), 96 kHz, Drive 0:\n", alt ? "later" : "early");
        for (const auto level : { -40.0, -20.0, -12.0, -6.0, -1.0 })
            std::printf ("  %5.1f dBFS: 50 Hz %7.1f dB, 1 kHz %7.1f dB, 10 kHz %7.1f dB\n", level, thdDb (consoleFlat (alt), 50.0, level, 0.0f),
                         thdDb (consoleFlat (alt), 1000.0, level, 0.0f), thdDb (consoleFlat (alt), 10000.0, level, 0.0f));
    }
}

TEST_CASE ("Gates: CPU within the model's budget, at rest and during a model crossfade", "[gates][.perf]")
{
    // PLAN.md, Performance budget (Settled 22, Provisional 10): the worst case with the controls at
    // rest up to the amps' rails at Drive 0 against the budget, driven into them (by Drive, the
    // input's level or a boost) against the model's own ceiling (consoleCpuCases()); a model
    // crossfade against the two budgets' sum. Medians of seven runs; the cost while controls move
    // is reported.
    const auto& console = info (Model::console);
    for (const bool alt : { false, true })
        for (const auto& c : consoleCpuCases (alt))
        {
            const auto limit = c.intoRails ? console.cpuDriveCeilingPercent : console.cpuBudgetPercent;
            const auto cpu = medianOf (7, [&] { return cpuPercent (c.settings, false, 3.0, c.input); });
            std::printf ("Console, %s card, %-72s 48 kHz stereo %6.2f %% (%s %.1f %%)\n", alt ? "later" : "early", (c.name + ":").c_str(), cpu,
                         c.intoRails ? "ceiling" : "budget", limit);
            INFO ((alt ? "later" : "early") << " card, " << c.name << ": " << cpu << " % against " << limit << " %");
            CHECK (cpu <= limit);
        }
    for (const bool alt : { false, true })
    {
        auto s = consoleWorking (alt);
        s.noise = s.tolerance = 1.0f;
        std::printf ("Console, %s card, every band at work: at -6 dBFS %.2f %%, Drive +6 dB %.2f %%, four frequencies moving %.2f %% (reported)\n",
                     alt ? "later" : "early", medianOf (7, [&] { return cpuPercent (s, false, 3.0, (float) fromDb (-6.0)); }),
                     medianOf (7,
                               [&]
                               {
                                   auto d = s;
                                   d.driveDb = 6.0f;
                                   return cpuPercent (d, false, 3.0);
                               }),
                     cpuPercent (s, true, 3.0));
        for (int m = 0; m < numModels; ++m)
            if (m != (int) Model::console)
            {
                const auto other = static_cast<Model> (m);
                const auto sum = console.cpuBudgetPercent + info (other).cpuBudgetPercent;
                const auto changing = medianOf (7, [&] { return modelChangeCpuPercent (s, other, 3.0); });
                std::printf ("  while a change to %s and back runs both: %.2f %% (budget %.1f %%)\n", info (other).name, changing, sum);
                INFO ("to " << info (other).name << ": " << changing << " %");
                CHECK (changing <= sum);
            }
    }
}

TEST_CASE ("Gates: no callback around a change exceeds three quarters of its period (Settled 19)", "[gates][.perf]")
{
    // The longest process() call, best of five fresh runs (a stray preemption does not count),
    // after every switch and model change, and the first call after prepare() and after reset(),
    // at 96 kHz in blocks of 32 and 48 kHz in blocks of 64 (192 kHz in blocks of 64 reported).
    // Three quarters of the period leaves the host the rest of its callback.
    for (const auto& [fs, block, gated] : { std::tuple { 96000.0, 32, true }, std::tuple { 48000.0, 64, true }, std::tuple { 192000.0, 64, false } })
    {
        const auto period = 1000.0 * block / fs;
        std::printf ("%.0f Hz, blocks of %d (period %.3f ms):\n", fs, block, period);
        double worst = 0.0;
        std::string where;
        // At Drive +24 dB the amps' 8x clip runs on every sample (OpAmpClip).
        for (const auto drive : { 0.0f, 24.0f })
            for (auto c : consoleChanges())
            {
                c.before.driveDb = c.after.driveDb = drive;
                if (drive > 0.0f)
                    c.name += ", Drive +24 dB";
                const auto ms = worstCallbackMs (c.before, c.after, fs, block);
                std::printf ("  %-56s %7.3f ms  %5.1f %% of the period\n", c.name.c_str(), ms, 100.0 * ms / period);
                if (ms > worst)
                    worst = ms, where = c.name;
            }
        auto with = [] (dsp::ChainSettings s, auto&& edit)
        {
            edit (s);
            return s;
        };
        for (const auto& [name, s] : { std::pair { "early card", consoleWorking (false) }, std::pair { "later card", consoleWorking (true) },
                                       std::pair { "later card, Tolerance 50 %", with (consoleWorking (true), [] (auto& x) { x.tolerance = 0.5f; }) },
                                       std::pair { "Inductor (a placeholder)", with (consoleWorking (true), [] (auto& x) { x.model = Model::inductor; }) } })
        {
            const auto [prepared, reset] = firstCallbackMs (s, fs, block);
            std::printf ("  first after prepare / reset, %-27s %7.3f / %.3f ms\n", name, prepared, reset);
            for (const auto& [ms, which] : { std::pair { prepared, "after prepare" }, std::pair { reset, "after reset" } })
                if (ms > worst)
                    worst = ms, where = std::string ("first callback ") + which + ", " + name;
        }
        INFO (fs << " Hz, blocks of " << block << ": " << worst << " ms (" << where << ") against a period of " << period << " ms");
        if (gated)
            CHECK (worst <= 0.75 * period);
    }
}

TEST_CASE ("Gates: no callback exceeds three quarters of its period while every knob moves and switches change (Settled 19)", "[gates][.perf]")
{
    // Every knob moving every block and a switch changing every 50 ms (automatedCallbackMs()):
    // the active set's pot designs, a change's bounded build steps and, at Drive, the amps' 8x
    // path in the same callbacks. Best of five fresh runs per setting.
    for (const auto& [fs, block] : { std::pair { 96000.0, 32 }, std::pair { 48000.0, 64 } })
    {
        const auto period = 1000.0 * block / fs;
        double worst = 0.0;
        std::string where;
        for (const bool alt : { false, true })
            for (const auto tolerance : { 0.0f, 0.5f })
                for (const auto drive : { 0.0f, 24.0f })
                {
                    auto s = consoleWorking (alt);
                    s.tolerance = tolerance;
                    s.driveDb = drive;
                    const auto ms = automatedCallbackMs (s, fs, block);
                    const auto name = std::string (alt ? "later" : "early") + " card, Tolerance " + std::to_string ((int) (100.0f * tolerance))
                                      + " %, Drive " + std::to_string ((int) drive) + " dB";
                    std::printf ("%.0f Hz, blocks of %d, every knob moving, switches changing, %-40s %7.3f ms  %5.1f %% of the period\n", fs, block,
                                 name.c_str(), ms, 100.0 * ms / period);
                    if (ms > worst)
                        worst = ms, where = name;
                }
        INFO (fs << " Hz, blocks of " << block << ": " << worst << " ms (" << where << ") against a period of " << period << " ms");
        CHECK (worst <= 0.75 * period);
    }
}
