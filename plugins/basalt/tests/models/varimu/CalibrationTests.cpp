// The attack calibration (NOTES.md, Attack): κ = R_ref/(Φ·R_∞) measured on this engine, printed as
// the table VariMuModel.cpp and Reference.h carry. Hidden; run it after a change to the loop or the
// curve, paste the table, and the attack gates check the result:
//
//     BasaltTests_varimu "[.calibrate]"

#include "VariMuHarness.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using namespace basalt;
using namespace basalt::testing;
using namespace basalt::testing::varimu;

TEST_CASE ("Vari-mu: κ measured on the reference step at Attack 30 ms", "[.calibrate]")
{
    // At a node the table gives the node's own Φ_t, so running the knob at 30·Φ_t/Φ realises
    // τ = 30 ms/Φ: the engine is measured as it ships, without a test hook. τ is scaled by the
    // measured/wanted ratio until the reference step reads 30 ms; κ = R_ref/(Φ·R_∞) follows. Ratio
    // 1 has no reference point; its κ is 1, the limit the low ratios approach.
    constexpr double rate = 48000.0, wantedMs = 30.0;

    std::printf ("\n        1.0,");
    auto worstResidual = 0.0;

    for (int i = 1; i < VariMuModel::kappaRatios; ++i)
    {
        const auto curve = dynamics::Curve { -30.0, 1.0 / VariMuModel::kappaGridRatio (i), 0.0, 40.0 };
        const auto table = VariMuModel::attackCalibration (curve);
        auto tauMs = wantedMs / table, measuredMs = 0.0;

        auto m = prepared (rate);
        for (int pass = 0; pass < 8; ++pass)
        {
            const auto p = params (curve.thresholdDb, curve.inverseRatio, 0.0, 40.0, tauMs * table, 1000.0);
            measuredMs = attackStep (m, p, ref::referenceReductionDb, 3.0, rate).attackS * 1000.0;
            if (std::abs (measuredMs / wantedMs - 1.0) < 1.0e-6)
                break;
            tauMs *= wantedMs / measuredMs;
        }

        worstResidual = std::max (worstResidual, std::abs (measuredMs / wantedMs - 1.0));
        const auto phi = wantedMs / tauMs;
        const auto kappa = 1.0 / (VariMuModel::referenceInverseRatio (curve) * phi * VariMuModel::asymptoticRatio (curve.inverseRatio));
        std::printf (" %.4f,%s", kappa, i % 10 == 9 ? "\n       " : "");
    }

    std::printf ("\n    // worst residual |measured/30 ms − 1| = %.2e\n", worstResidual);
    CHECK (worstResidual < 1.0e-4);
}
