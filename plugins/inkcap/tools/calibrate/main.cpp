// InkcapCalibrate: Match's tables and the voicing ladder (PLAN.md, M3). Pure DSP, no JUCE.
//
//     InkcapCalibrate [header]      measures and writes Match's tables
//                                   (default: src/dsp/chain/MatchTables.h, run from plugins/inkcap)
//     InkcapCalibrate --report      Match's gate: loudness over the Damage range, Match on
//     InkcapCalibrate --voicing     each module at Damage 25 / 50 / 100 % (the NOTES.md targets)

#include "calibrate/Calibrate.h"

#include <cstring>

int main (int argc, char** argv)
{
    if (argc > 1 && std::strcmp (argv[1], "--voicing") == 0)
        return inkcap::calibrate::voicing();
    if (argc > 1 && std::strcmp (argv[1], "--report") == 0)
        return inkcap::calibrate::report();

    return inkcap::calibrate::writeTables (argc > 1 ? argv[1] : "src/dsp/chain/MatchTables.h");
}
