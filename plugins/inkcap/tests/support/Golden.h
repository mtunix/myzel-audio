#pragma once

#include "support/Analysis.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

/**
    Golden renders: a stereo render stored as interleaved float32 under tests/<path>. A refactor
    that changes the sound fails; after an intended sound change, re-record with
    INKCAP_UPDATE_GOLDEN=1 (and say so in the commit).
*/
namespace inkcap::testing
{

/** Compares (or, with INKCAP_UPDATE_GOLDEN set, records) a render; the gate is error energy, since libms round differently. */
inline void checkGolden (const std::string& relativePath, const analysis::Stereo& out, double gateDb = -90.0)
{
    std::vector<float> interleaved (2 * out.size());
    for (size_t i = 0; i < out.size(); ++i)
    {
        interleaved[2 * i] = out.left[i];
        interleaved[2 * i + 1] = out.right.empty() ? out.left[i] : out.right[i];
    }

    const auto path = std::string (INKCAP_TEST_DATA_DIR) + "/" + relativePath;
    INFO (path);
    if (std::getenv ("INKCAP_UPDATE_GOLDEN") != nullptr)
    {
        std::ofstream file (path, std::ios::binary);
        file.write (reinterpret_cast<const char*> (interleaved.data()), (std::streamsize) (interleaved.size() * sizeof (float)));
        REQUIRE (file.good());
        return;
    }

    std::ifstream file (path, std::ios::binary);
    REQUIRE (file.good());
    std::vector<float> golden (interleaved.size());
    file.read (reinterpret_cast<char*> (golden.data()), (std::streamsize) (golden.size() * sizeof (float)));
    REQUIRE ((size_t) file.gcount() == golden.size() * sizeof (float));

    const auto error = analysis::nullDb (interleaved, golden);
    INFO (error << " dB from the golden render");
    CHECK (error < gateDb);
}

} // namespace inkcap::testing
