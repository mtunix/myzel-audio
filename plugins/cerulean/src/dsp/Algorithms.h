#pragma once

namespace cerulean
{

/** Reverb algorithms, in panel order (two rows of five keys). Never reorder: saved in state. */
enum class Algorithm
{
    room = 0,
    hall,
    plate,
    spring,
    nimbus,
    halo,
    rise,
    billow,
    choir,
    echo
};

inline constexpr int numAlgorithms = 10;

} // namespace cerulean
