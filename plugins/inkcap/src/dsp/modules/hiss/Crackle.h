#pragma once

#include "dsp/common/SeedRng.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace inkcap::dsp::hiss
{

/**
    Vinyl crackle: dust and scratches on a record turning at 33⅓ rpm, as a seeded impulse
    process on the timeline (NOTES.md, Vinyl).

    The timeline is cut into 1 ms cells, 1800 to a turn. A dust particle sits at one cell of one
    turn (stream hissCrackle decides where) and, as the stylus moves inwards, is hit again on the
    next few turns at the same angle, a little weaker each time: steady wear repeats with the
    record (Chaos 0: up to four turns), unsteady wear is fresh dust every turn, in bursts (Chaos
    1). Scratches tick once per turn. Each event is a click shape (stream hissCrackleShape): a
    tick, a short burst of grit or a pop. The excitation goes through the phono stage with the
    surface noise (HissModule), which gives the clicks their colour.

    Everything about a cell is a pure function of the seed and the cell index, and a click is a
    closed-form function of the samples since its start, so the crackle at any timeline index is
    the same from wherever playback started: restartAt() rebuilds the clicks already under way.
    Real-time safe; no allocation.
*/
class Crackle
{
public:
    /** What the wear allows now (from Amount × Damage and Chaos, per block). */
    struct Wear
    {
        double rate = 0.0;          // dust events per second, on average
        double chaos = 0.0;         // 0 .. 1
        double popShare = 0.03;     // of dust events, the share that are pops
        int scratches = 0;          // 0 .. maxScratches
    };

    static constexpr int cellsPerTurn = 1800;
    static constexpr double cellSeconds = 0.001;
    static constexpr int maxTurns = 4;          // the most turns a particle is heard on
    static constexpr int maxScratches = 2;
    static constexpr int maxVoices = 24;

    void prepare (double sampleRate) noexcept;
    void setSeed (uint32_t seed) noexcept;

    /** Clears the clicks and starts at timeline index t, with every click under way there sounding from its middle. */
    void restartAt (int64_t t, const Wear&) noexcept;

    /** The excitation at timeline index t, which must follow the last call's (or restartAt's) by one. */
    void next (int64_t t, const Wear&, float& left, float& right) noexcept;

    int samplesPerCell() const noexcept { return cellSamples; }
    int samplesPerTurn() const noexcept { return cellSamples * cellsPerTurn; }

    /**
        Scales that make an event of level 1 come out at the bed's RMS after the phono stage (set
        by the module, which measures its phono stage): a tick's peak, a grit burst's RMS, a pop's
        peak at its middle width. They differ because the phono stage is not flat.
    */
    void setScales (float tick, float grit, float pop) noexcept
    {
        tickScale = tick;
        gritScale = grit;
        popScale = pop;
    }

    /** A pop's shape at sample k of `samples`: the stylus's velocity over a bump, so its area is 0. */
    static float popShape (int k, double samples) noexcept { return (float) std::cos (((double) k + 0.5) * std::numbers::pi / samples); }

    static constexpr double middlePopMs = 0.8;

private:
    enum class Kind : uint8_t { tick, grit, pop };

    struct Voice
    {
        int64_t start = 0;
        int length = 0;
        Kind kind = Kind::tick;
        float left = 0.0f, right = 0.0f;
        float decay = 0.0f;         // grit: -1 / time constant (samples); pop: pi / width
        uint64_t key = 0;           // grit: its noise
        bool active = false;
    };

    void enterCell (int64_t cell, const Wear&) noexcept;
    void addDust (int64_t cell, const Wear&) noexcept;
    void addScratches (int64_t cell, const Wear&) noexcept;
    void start (int64_t at, Kind, double level, double balance, double width, uint64_t key) noexcept;
    double densityAt (int64_t cell, double chaos) const noexcept;

    double fs = 48000.0;
    int cellSamples = 48;
    int longestEvent = 96;
    int64_t nextCell = 0;
    float tickScale = 1.0f, gritScale = 1.0f, popScale = 1.0f;

    uint32_t seed = 0;
    SeedRng dust { 0, Stream::hissCrackle }, shape { 0, Stream::hissCrackleShape };
    SeedRng density { 0, Stream::hissCrackle }, bursts { 0, Stream::hissCrackle };
    std::array<int, maxScratches> scratchAngle {}, scratchOffset {};

    std::array<Voice, maxVoices> voices {};
};

} // namespace inkcap::dsp::hiss
