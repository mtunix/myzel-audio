#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// A part of a generated evaluate(): kept a function of its own, so no compiler sees one function
// of several megabytes (tools/netlist/codegen.py).
#if defined (_MSC_VER) && ! defined (__clang__)
 #define VIRIDIAN_GENERATED_PART __declspec (noinline)
#else
 #define VIRIDIAN_GENERATED_PART __attribute__ ((noinline))
#endif

namespace viridian::dsp
{

/**
    A control of a generated network: a pot position (0 ... 1), a switch (0 ... positions - 1)
    that selects a topology, or a stepped control (0 ... positions - 1): a rotary switch that sets
    one component's value in the same topology, so the value can glide between its detents.
    evaluate() takes a stepped control as its coordinate 0 ... 1 over the values' range.
*/
struct NetworkControl
{
    enum class Kind
    {
        pot,
        selector,
        stepped
    };

    const char* name;
    Kind kind;
    int positions;                      // switches and stepped controls; 0 for pots
    double defaultValue;                // a pot's position; a switch's or a stepped control's position
    const double* steps = nullptr;      // stepped: the component value at each position
};

/**
    What a generated network's evaluate() writes: per block, the numerator and denominator in
    sigma = s / omegaRef (ascending), the controls the block depends on, and the gain that does not
    depend on s.
*/
template <int MaxBlocks, int MaxDegree>
struct NetworkPolynomials
{
    struct Block
    {
        int numDegree = 0, denDegree = 0;
        std::array<double, (std::size_t) MaxDegree + 1> num {}, den {};
        std::uint32_t controls = 0;     // bit i: depends on control i
    };

    int numBlocks = 0;
    std::array<Block, (std::size_t) MaxBlocks> blocks {};
    double gain = 1.0;
};

} // namespace viridian::dsp
