#pragma once

#include <array>
#include <span>

namespace viridian
{

/** The EQ models, in panel order. Never reorder: the Model choice parameter stores the index. */
enum class Model
{
    passive = 0,    // passive LCR program EQ, valve make-up amp
    inductor,       // class-A module, inductor mid and high-pass
    proportional,   // discrete module, stepped, proportional Q
    console,        // console channel EQ, two curve families
    mastering       // clean continuous parametric
};

inline constexpr int numModels = 5;

/** Which part of a stereo signal goes through the EQ network. Never reorder: stored in state. */
enum class StereoMode
{
    stereo = 0,
    mid,
    side
};

inline constexpr int numStereoModes = 3;

/** The panel's band slots, left to right. */
enum class Slot
{
    hpf = 0,
    low,
    lowMid,
    highMid,
    high,
    lpf
};

inline constexpr int numSlots = 6;

/** Every slot control, in parameter order. Never reorder: the tables and parameters index by it. */
enum class Control
{
    hpfFreq = 0,
    lowFreq, lowGain, lowQ, lowShape,
    lmFreq, lmGain, lmQ,
    hmFreq, hmGain, hmQ,
    highFreq, highGain, highQ, highShape,
    lpfFreq
};

inline constexpr int numControls = 16;

Slot slotOf (Control) noexcept;

/** What a slot control is on one model. */
enum class Kind
{
    dim,            // the model lacks it: the knob dims and does nothing
    fixed,          // the model has it at one value (min); the knob dims
    continuous,     // min .. max over the travel
    stepped,        // a rotary switch: the knob snaps to the nearest detent
    toggle          // a key: position < 0.5 is the first state, otherwise the second
};

enum class Unit
{
    none,
    hz,
    db,
    q,
    dial            // the hardware's 0 .. 10 legend; the network sets what it does
};

/** Where a filter's Off position sits on the travel. */
enum class OffAt
{
    none,
    start,          // high-pass: fully left is Off
    end             // low-pass: fully right is Off
};

struct ControlInfo
{
    Kind kind = Kind::dim;
    const char* legend = "";            // panel legend (upper case)
    Unit unit = Unit::none;
    float min = 0.0f, max = 0.0f;       // continuous: the range; fixed: the value in min
    bool logScale = false;              // continuous: equal travel is an equal ratio
    std::span<const float> detents {};  // stepped: the physical values, ascending
    OffAt off = OffAt::none;
    float flat = -1.0f;                 // the position where the control leaves the sound alone; < 0: none
    const char* offName = "";           // toggle: the two states
    const char* onName = "";
};

/** A control's physical setting. */
struct ControlValue
{
    float value = 0.0f;     // in the control's unit; toggle: 0 or 1
    int detent = -1;        // stepped and toggle: the position index (the Off position counts)
    bool off = false;       // a filter at its Off position
};

/** The Alt key's meaning on one model. */
struct AltInfo
{
    bool active = false;
    const char* legend = "";
    const char* meaning = "";
};

/** One section of the panel (the EQ slots); a dynamics section can join it beside the EQ. */
struct EqSection
{
    std::array<ControlInfo, numControls> controls {};

    const ControlInfo& operator[] (Control c) const noexcept { return controls[(size_t) c]; }
};

/**
    What a model is, for everything outside its DSP: parameter text, the UI, the processor and the
    tests. Values and their sources are in each model's NOTES.md.
*/
struct ModelInfo
{
    Model model = Model::inductor;
    const char* name = "";          // display name, also the Model choice parameter's strings
    const char* key = "";           // lower case: directories, golden files, tool arguments
    const char* tagline = "";       // a few words on its character (studio list)
    AltInfo alt;
    EqSection eq;                   // Alt off
    EqSection eqAlt;                // Alt on; equals eq where Alt changes no slot
    float cpuBudgetPercent = 1.0f;  // PLAN.md, 48 kHz stereo, one core

    const EqSection& section (bool altOn) const noexcept { return altOn && alt.active ? eqAlt : eq; }
};

const std::array<ModelInfo, numModels>& modelInfos() noexcept;
const ModelInfo& info (Model) noexcept;
const ControlInfo& controlInfo (Model, bool altOn, Control) noexcept;

/** A continuous filter's Off zone at its end of the travel. */
inline constexpr float filterOffZone = 0.01f;

/** Positions of a stepped control (Off included) or a toggle (2); 0 for the others. */
int numPositions (const ControlInfo&) noexcept;

/** The position index a normalised knob position selects: the nearest detent. */
int positionIndex (const ControlInfo&, float position) noexcept;

/** The normalised knob position of a position index: detents spread over the whole travel. */
float detentPosition (const ControlInfo&, int index) noexcept;

/** Where a stepped control or a toggle snaps to; other kinds return the position unchanged. */
float snap (const ControlInfo&, float position) noexcept;

/** Normalised knob position (0 .. 1) to the physical setting. */
ControlValue map (const ControlInfo&, float position) noexcept;

/** The knob position of a physical value: the nearest detent, or the inverse map. */
float positionOf (const ControlInfo&, float value) noexcept;

/** The knob position of a filter's Off; -1 for a control without one. */
float offPosition (const ControlInfo&) noexcept;

} // namespace viridian
