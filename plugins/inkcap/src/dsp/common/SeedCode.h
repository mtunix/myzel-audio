#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

/**
    Seeds as text: the code a user copies and pastes (3F2A-91C7), and words that hash to seeds.
    Every rule here is frozen: a pasted code or a typed word must give the same seed in every
    version and on every machine. The rules and their test vectors: src/dsp/common/NOTES.md and
    tests/common/SeedCodeTests.cpp. UTF-8 in and out, no JUCE, no locale.
*/
namespace inkcap::dsp::seedcode
{

/** The code of a seed: 8 upper-case hex digits in two groups, "3F2A-91C7". */
std::string format (uint32_t seed);

/** The code forms only: "3F2A-91C7", "3f2a91c7", "0x3F2A91C7" (surrounding white space allowed). */
std::optional<uint32_t> parseCode (std::string_view text) noexcept;

/** FNV-1a, 32 bits, over bytes. */
constexpr uint32_t fnv1a32 (std::string_view bytes) noexcept
{
    uint32_t hash = 2166136261u;
    for (const auto c : bytes)
    {
        hash ^= (uint8_t) c;
        hash *= 16777619u;
    }
    return hash;
}

/** A word as it is hashed: trimmed of Unicode white space and folded to Unicode 15.0 simple lower case. */
std::string normaliseWord (std::string_view utf8);

/** The seed of a word: FNV-1a 32 over its normalised UTF-8. */
uint32_t wordHash (std::string_view utf8);

/** What a pasted or typed text means. */
struct Parsed
{
    uint32_t seed = 0;
    std::string label;      // a word: the trimmed text as typed (at most maxLabelLength code points); a code: empty
};

inline constexpr int maxLabelLength = 64;

/** A code is that seed, any other text is a word; empty or white space only is nothing. */
std::optional<Parsed> parse (std::string_view utf8);

/** A fresh instance's seed: the word "inkcap" (4393-0A3D). */
inline constexpr std::string_view defaultWord = "inkcap";
inline constexpr uint32_t defaultSeed = fnv1a32 (defaultWord);

} // namespace inkcap::dsp::seedcode
