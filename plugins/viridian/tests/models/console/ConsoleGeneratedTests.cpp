#include "dsp/common/Network.h"
#include "dsp/models/console/generated/ConsoleEarlyBack.h"
#include "dsp/models/console/generated/ConsoleEarlyFront.h"
#include "dsp/models/console/generated/ConsoleLaterBack.h"
#include "dsp/models/console/generated/ConsoleLaterCoreZ.h"
#include "dsp/models/console/generated/ConsoleLaterCoupling.h"
#include "dsp/models/console/generated/ConsoleLaterCouplingZ.h"
#include "dsp/models/console/generated/ConsoleLaterFilters.h"
#include "dsp/models/console/generated/ConsoleLaterFrontDirect.h"
#include "dsp/models/console/generated/ConsoleLaterSource.h"
#include "dsp/models/console/generated/ConsoleLaterSourceZ.h"
#include "models/console/generated/ConsoleEarlyBackVectors.h"
#include "models/console/generated/ConsoleEarlyFrontVectors.h"
#include "models/console/generated/ConsoleLaterBackVectors.h"
#include "models/console/generated/ConsoleLaterCoreZVectors.h"
#include "models/console/generated/ConsoleLaterCouplingVectors.h"
#include "models/console/generated/ConsoleLaterCouplingZVectors.h"
#include "models/console/generated/ConsoleLaterFiltersVectors.h"
#include "models/console/generated/ConsoleLaterFrontDirectVectors.h"
#include "models/console/generated/ConsoleLaterSourceVectors.h"
#include "models/console/generated/ConsoleLaterSourceZVectors.h"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <complex>

using namespace viridian::dsp;
namespace gen = viridian::dsp::generated;

namespace
{
    template <typename Topology>
    struct VectorsOf;
    template <>
    struct VectorsOf<gen::ConsoleEarlyFront> { using type = gen::ConsoleEarlyFrontVectors; };
    template <>
    struct VectorsOf<gen::ConsoleEarlyBack> { using type = gen::ConsoleEarlyBackVectors; };
    template <>
    struct VectorsOf<gen::ConsoleLaterFilters> { using type = gen::ConsoleLaterFiltersVectors; };
    template <>
    struct VectorsOf<gen::ConsoleLaterCoupling> { using type = gen::ConsoleLaterCouplingVectors; };
    template <>
    struct VectorsOf<gen::ConsoleLaterCouplingZ> { using type = gen::ConsoleLaterCouplingZVectors; };
    template <>
    struct VectorsOf<gen::ConsoleLaterFrontDirect> { using type = gen::ConsoleLaterFrontDirectVectors; };
    template <>
    struct VectorsOf<gen::ConsoleLaterBack> { using type = gen::ConsoleLaterBackVectors; };
    template <>
    struct VectorsOf<gen::ConsoleLaterSource> { using type = gen::ConsoleLaterSourceVectors; };
    template <>
    struct VectorsOf<gen::ConsoleLaterSourceZ> { using type = gen::ConsoleLaterSourceZVectors; };
    template <>
    struct VectorsOf<gen::ConsoleLaterCoreZ> { using type = gen::ConsoleLaterCoreZVectors; };
} // namespace

TEMPLATE_TEST_CASE ("Console: the generated networks reproduce the netlists' exact H(s)", "[console][generated]",
                    gen::ConsoleEarlyFront, gen::ConsoleEarlyBack, gen::ConsoleLaterFilters, gen::ConsoleLaterCoupling,
                    gen::ConsoleLaterCouplingZ, gen::ConsoleLaterFrontDirect, gen::ConsoleLaterBack, gen::ConsoleLaterSource,
                    gen::ConsoleLaterSourceZ, gen::ConsoleLaterCoreZ)
{
    using Vectors = typename VectorsOf<TestType>::type;
    double worst = 0.0;
    for (const auto& point : Vectors::points)
    {
        Network<TestType> n;
        typename Network<TestType>::Controls c {};
        for (int i = 0; i < TestType::numControls; ++i)
            c[(size_t) i] = point.controls[(size_t) i];
        n.setTargets (c);
        n.prepare (48000.0);
        const std::complex<double> exact { point.re, point.im };
        worst = std::max (worst, std::abs (n.analogResponse (point.frequency) - exact) / std::max (std::abs (exact), 1.0e-12));
    }
    INFO ("worst relative error against the netlist's exact response: " << worst);
    CHECK (worst < 1.0e-9);
}
