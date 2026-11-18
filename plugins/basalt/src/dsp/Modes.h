#pragma once

// The choice parameters' values, in their saved index order (PLAN.md, Parameter IDs). Sessions and
// automation store the index: never reorder, never remove, never insert.

namespace basalt
{

/** The compressor model (PLAN.md, Models). All nine are in the frozen list from M0. */
enum class Model
{
    clean = 0,
    vca,
    fet,
    opto,
    variMu,
    upward,
    diodeBridge,    // 1.1
    pwm,            // 1.1
    pumping         // 1.1
};

inline constexpr int numModels = 9;

/** The models v1 builds and offers in its own menu; the rest of the list waits for 1.1. */
inline constexpr int numOfferedModels = 6;

inline constexpr bool isOffered (Model m) noexcept { return static_cast<int> (m) < numOfferedModels; }

/** What runs for a model: one v1 does not build yet runs as Clean (PLAN.md, Frozen with the IDs). */
inline constexpr Model runningModel (Model m) noexcept { return isOffered (m) ? m : Model::clean; }

/** The VCA model's detector (PLAN.md, VCA detector); the other models ignore it. */
enum class VcaDetector
{
    peak = 0,   // the console bus type
    rms         // true RMS, one time constant (models/vca/NOTES.md)
};

inline constexpr int numVcaDetectors = 2;

// Which controls the model running reads (PLAN.md, Controls); the UI dims the others, and their
// parameters stay.

inline constexpr bool readsVcaDetector (Model m) noexcept { return runningModel (m) == Model::vca; }

inline constexpr bool readsFloor (Model m) noexcept { return runningModel (m) == Model::upward; }

/** FET documents no program-dependent release; VCA RMS's constant rate already lengthens with depth. */
inline constexpr bool readsAutoRelease (Model m, VcaDetector d) noexcept
{
    const auto running = runningModel (m);
    return running != Model::fet && ! (running == Model::vca && d == VcaDetector::rms);
}

/** The Character section's shape (PLAN.md, Character). */
enum class CharacterMode
{
    off = 0,
    warm,
    iron,
    edge
};

inline constexpr int numCharacterModes = 4;

enum class StereoMode
{
    linked = 0, // one gain for both channels, Link sets how much the louder one wins
    unlinked,   // one detector and gain per channel
    mid,        // detect and compress only the mid
    side,       // detect and compress only the side
    midSide     // mid and side, each with its own gain
};

inline constexpr int numStereoModes = 5;

enum class KeySource
{
    internal = 0,
    external    // the side-chain bus
};

inline constexpr int numKeySources = 2;

/** What the output carries. Monitoring only: not in presets, not undoable. */
enum class Listen
{
    output = 0,
    key,        // what the detector hears, after the key EQ
    delta       // what the compressor takes out
};

inline constexpr int numListenModes = 3;

/** A session setting, not a parameter: a preset never changes the latency the host sees. */
enum class LatencyMode
{
    normal = 0, // look-ahead and Character's linear-phase oversampler, constant and reported
    live        // 0 samples; no look-ahead
};

inline constexpr int numLatencyModes = 2;

} // namespace basalt
