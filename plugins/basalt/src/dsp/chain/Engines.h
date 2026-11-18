#pragma once

#include "dsp/Modes.h"
#include "dsp/dynamics/Model.h"

#include <memory>

// What runs for each model and VCA detector: the chain's dispatch table (PLAN.md, Models). v1
// builds the six offered models, the VCA's two detectors as two engines; 1.1's three run Clean
// until they are built (runningModel()).

namespace basalt::dsp
{

enum class Engine
{
    clean = 0,
    vcaPeak,
    vcaRms,
    fet,
    opto,
    variMu,
    upward
};

inline constexpr int numEngines = 7;

/** Room for every engine v1 and 1.1 build (Clean, VCA Peak and RMS, FET, Opto, Vari-mu, Upward, and three). */
inline constexpr int maxEngines = 12;

/** The engine a model and VCA detector run on. A switch between two settings on one engine is no switch. */
inline constexpr Engine engineFor (Model m, VcaDetector d) noexcept
{
    switch (runningModel (m))
    {
        case Model::clean:  return Engine::clean;
        case Model::vca:    return d == VcaDetector::rms ? Engine::vcaRms : Engine::vcaPeak;
        case Model::fet:    return Engine::fet;
        case Model::opto:   return Engine::opto;
        case Model::variMu: return Engine::variMu;
        case Model::upward: return Engine::upward;

        // 1.1's three: runningModel() already sends them to Clean.
        case Model::diodeBridge:
        case Model::pwm:
        case Model::pumping:
            return Engine::clean;
    }
    return Engine::clean;
}

/** The model the listener hears for an engine (the status line's). */
inline constexpr Model modelOf (Engine e) noexcept
{
    switch (e)
    {
        case Engine::clean:   return Model::clean;
        case Engine::vcaPeak:
        case Engine::vcaRms:  return Model::vca;
        case Engine::fet:     return Model::fet;
        case Engine::opto:    return Model::opto;
        case Engine::variMu:  return Model::variMu;
        case Engine::upward:  return Model::upward;
    }
    return Model::clean;
}

/** The VCA detector an engine runs; Peak for every engine but VCA RMS. */
inline constexpr VcaDetector detectorOf (Engine e) noexcept
{
    return e == Engine::vcaRms ? VcaDetector::rms : VcaDetector::peak;
}

/** A new engine's model. Not real-time safe. */
std::unique_ptr<dynamics::DynamicsModel> makeEngine (Engine);

} // namespace basalt::dsp
