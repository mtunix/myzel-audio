#pragma once

#include <string>

namespace inkcap::calibrate
{

/** Measures Match's tables through the real chain and writes them to `path` (MatchCalibration.cpp). */
int writeTables (const std::string& path);

/** --report: Match's gate, each module's loudness over the Damage range with Match on. */
int report();

/** --voicing: each module at Damage 25 / 50 / 100 % in the unit it is heard in (Voicing.cpp). */
int voicing();

} // namespace inkcap::calibrate
