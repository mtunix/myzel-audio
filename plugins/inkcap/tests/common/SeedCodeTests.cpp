/*
    Seeds as text: the code format, what Paste accepts, and the frozen word hash with its
    vectors (a word must give the same seed in every version and on every machine).
*/

#include "dsp/common/SeedCode.h"
#include "dsp/common/generated/LowerCaseTable.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <random>

using namespace inkcap::dsp;

namespace
{
    std::string utf8 (char32_t cp)
    {
        std::string s;
        if (cp < 0x80)
        {
            s += (char) cp;
        }
        else if (cp < 0x800)
        {
            s += (char) (0xC0 | (cp >> 6));
            s += (char) (0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            s += (char) (0xE0 | (cp >> 12));
            s += (char) (0x80 | ((cp >> 6) & 0x3F));
            s += (char) (0x80 | (cp & 0x3F));
        }
        else
        {
            s += (char) (0xF0 | (cp >> 18));
            s += (char) (0x80 | ((cp >> 12) & 0x3F));
            s += (char) (0x80 | ((cp >> 6) & 0x3F));
            s += (char) (0x80 | (cp & 0x3F));
        }
        return s;
    }

    struct WordVector
    {
        const char* word;
        const char* normalised;
        uint32_t seed;
    };

    // From tests/common/validation/seed_reference.py: Unicode 15.0 simple lower case, Python's own tables.
    const std::array<WordVector, 19> wordVectors { {
        { "inkcap", "inkcap", 0x43930A3Du },  // 4393-0A3D
        { "thrift", "thrift", 0x87198E02u },  // 8719-8E02
        { "Thrift", "thrift", 0x87198E02u },  // 8719-8E02
        { "  thrift\x09", "thrift", 0x87198E02u },  // 8719-8E02
        { "THRIFT SHOP", "thrift shop", 0xF99E6E2Au },  // F99E-6E2A
        { "\xC3\x84pfel", "\xC3\xA4pfel", 0x8393A579u },  // 8393-A579
        { "\xC3\xA4pfel", "\xC3\xA4pfel", 0x8393A579u },  // 8393-A579
        { "\xC3\x87" "A VA", "\xC3\xA7" "a va", 0x3755E6FBu },  // 3755-E6FB
        { "\xC4\xB0stanbul", "istanbul", 0x3F2BED47u },  // 3F2B-ED47
        { "Stra\xC3\x9F" "e", "stra\xC3\x9F" "e", 0x2626F818u },  // 2626-F818
        { "\xCE\xA3\xCE\x9F\xCE\xA6\xCE\x99\xCE\x91", "\xCF\x83\xCE\xBF\xCF\x86\xCE\xB9\xCE\xB1", 0x841F44F1u },  // 841F-44F1
        { "\xD0\x81\xD0\xBB\xD0\xBA\xD0\xB0", "\xD1\x91\xD0\xBB\xD0\xBA\xD0\xB0", 0x486E1BACu },  // 486E-1BAC
        { "Ti\xE1\xBA\xBFng Vi\xE1\xBB\x87t", "ti\xE1\xBA\xBFng vi\xE1\xBB\x87t", 0xFFC185B9u },  // FFC1-85B9
        { "\xEF\xBC\xA1\xEF\xBC\xA2\xEF\xBC\xA3", "\xEF\xBD\x81\xEF\xBD\x82\xEF\xBD\x83", 0xE940B6DBu },  // E940-B6DB
        { "\xC2\xA0spaced\xE3\x80\x80", "spaced", 0xD40E2833u },  // D40E-2833
        { "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", 0x805F5CE7u },  // 805F-5CE7
        { "\xF0\x9F\x98\x80 emoji", "\xF0\x9F\x98\x80 emoji", 0xDE688546u },  // DE68-8546
        { "deadbeefs", "deadbeefs", 0x93ABB7B2u },  // 93AB-B7B2
        { "3F2A-91C", "3f2a-91c", 0x7AA20433u },  // 7AA2-0433
    } };
}

TEST_CASE ("SeedCode: codes read 3F2A-91C7", "[seeds]")
{
    CHECK (seedcode::format (0x3F2A91C7u) == "3F2A-91C7");
    CHECK (seedcode::format (0u) == "0000-0000");
    CHECK (seedcode::format (0xFFFFFFFFu) == "FFFF-FFFF");
    CHECK (seedcode::format (0x0000ABCDu) == "0000-ABCD");
}

TEST_CASE ("SeedCode: Paste accepts the three code forms and nothing else", "[seeds]")
{
    for (const auto* text : { "3F2A-91C7", "3f2a91c7", "0x3F2A91C7", "0X3f2a91C7", "0x3F2A-91C7", "3f2a-91c7",
                              "  3F2A-91C7\n", "\t0x3F2A91C7 " })
    {
        INFO (text);
        const auto parsed = seedcode::parseCode (text);
        REQUIRE (parsed.has_value());
        CHECK (*parsed == 0x3F2A91C7u);
    }

    for (const auto* text : { "", "3F2A91C", "3F2A91C70", "3F2A-91C7X", "3F2A--91C7", "3F2-A91C7", "3F2A91-C7",
                              "0x", "0x3F2A91C", "G3F2A91C", "3F2A 91C7", "0x0x3F2A91C7", "-3F2A91C7",
                              "+3F2A91C7", "3F2A_91C7", "x3F2A91C7", "3F2A-91C7-" })
    {
        INFO ("'" << text << "'");
        CHECK_FALSE (seedcode::parseCode (text).has_value());
    }
}

TEST_CASE ("SeedCode: copy then paste gives the same seed, for any seed", "[seeds]")
{
    std::mt19937 rng (20261113u);
    std::vector<uint32_t> seeds { 0u, 1u, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 0x0000FFFFu, 0xFFFF0000u };
    for (int i = 0; i < 20000; ++i)
        seeds.push_back ((uint32_t) rng());

    for (const auto seed : seeds)
    {
        const auto code = seedcode::format (seed);
        const auto parsed = seedcode::parse (code);
        REQUIRE (parsed.has_value());
        CHECK (parsed->seed == seed);
        CHECK (parsed->label.empty());
    }
}

TEST_CASE ("SeedCode: FNV-1a 32 is the standard one", "[seeds][golden]")
{
    // The published test vectors (Fowler, Noll, Vo).
    CHECK (seedcode::fnv1a32 ("") == 0x811C9DC5u);
    CHECK (seedcode::fnv1a32 ("a") == 0xE40C292Cu);
    CHECK (seedcode::fnv1a32 ("foobar") == 0xBF9CF968u);
    static_assert (seedcode::defaultSeed == 0x43930A3Du);
}

TEST_CASE ("SeedCode: the word hash is frozen (golden vectors)", "[seeds][golden]")
{
    for (const auto& v : wordVectors)
    {
        INFO (v.word);
        CHECK (seedcode::normaliseWord (v.word) == v.normalised);
        CHECK (seedcode::wordHash (v.word) == v.seed);

        const auto parsed = seedcode::parse (v.word);
        REQUIRE (parsed.has_value());
        CHECK (parsed->seed == v.seed);
    }

    CHECK (seedcode::format (seedcode::defaultSeed) == "4393-0A3D");
    CHECK (seedcode::wordHash (seedcode::defaultWord) == seedcode::defaultSeed);
}

TEST_CASE ("SeedCode: a word keeps its trimmed text as the label; empty text is nothing", "[seeds]")
{
    const auto word = seedcode::parse ("  Thrift Shop\n");
    REQUIRE (word.has_value());
    CHECK (word->label == "Thrift Shop");
    CHECK (word->seed == seedcode::wordHash ("thrift shop"));

    const auto code = seedcode::parse ("\xE3\x80\x80" "0x3F2A91C7" "\xC2\xA0");    // Unicode spaces around a code
    REQUIRE (code.has_value());
    CHECK (code->seed == 0x3F2A91C7u);
    CHECK (code->label.empty());

    for (const auto* nothing : { "", " ", "\t\r\n", "\xC2\xA0", "\xE3\x80\x80", "\xEF\xBB\xBF", " \xE2\x80\x83 " })
    {
        INFO ("'" << nothing << "'");
        CHECK_FALSE (seedcode::parse (nothing).has_value());
    }

    // The label stops after 64 code points (never inside one); the hash covers the whole text.
    std::string longWord;
    for (int i = 0; i < 100; ++i)
        longWord += "\xC3\xA9";     // é
    const auto parsed = seedcode::parse (longWord);
    REQUIRE (parsed.has_value());
    CHECK (parsed->label.size() == 2u * (size_t) seedcode::maxLabelLength);
    CHECK (parsed->seed == seedcode::wordHash (longWord));
    CHECK (parsed->seed != seedcode::wordHash (parsed->label));
}

TEST_CASE ("SeedCode: broken UTF-8 hashes as replacement characters", "[seeds]")
{
    const std::string fffd = "\xEF\xBF\xBD";
    CHECK (seedcode::normaliseWord ("\xFF") == fffd);
    CHECK (seedcode::normaliseWord ("a\x80Z") == "a" + fffd + "z");
    CHECK (seedcode::normaliseWord ("\xC0\xAF") == fffd + fffd);                 // overlong
    CHECK (seedcode::normaliseWord ("\xED\xA0\x80") == fffd + fffd + fffd);       // a surrogate
    CHECK (seedcode::normaliseWord ("\xE6\x97") == fffd + fffd);                 // cut short
    CHECK (seedcode::normaliseWord ("\xF4\x90\x80\x80") == fffd + fffd + fffd + fffd);   // above U+10FFFF
}

TEST_CASE ("SeedCode: the fold is Unicode's simple lower case, complete and idempotent", "[seeds]")
{
    struct Fold
    {
        char32_t from, to;
    };

    for (const auto [from, to] : { Fold { U'A', U'a' }, Fold { U'Z', U'z' }, Fold { U'a', U'a' },
                                   Fold { 0x00C4, 0x00E4 }, Fold { 0x00D7, 0x00D7 }, Fold { 0x00DF, 0x00DF },
                                   Fold { 0x0130, 0x0069 }, Fold { 0x0131, 0x0131 }, Fold { 0x0178, 0x00FF },
                                   Fold { 0x01C4, 0x01C6 }, Fold { 0x01C5, 0x01C6 }, Fold { 0x0218, 0x0219 },
                                   Fold { 0x03A3, 0x03C3 }, Fold { 0x03C2, 0x03C2 }, Fold { 0x0401, 0x0451 },
                                   Fold { 0x0490, 0x0491 }, Fold { 0x1E9E, 0x00DF }, Fold { 0x1EA0, 0x1EA1 },
                                   Fold { 0x2126, 0x03C9 }, Fold { 0x212A, 0x006B }, Fold { 0xFF21, 0xFF41 },
                                   Fold { 0x10400, 0x10428 }, Fold { 0x1E900, 0x1E922 }, Fold { 0x65E5, 0x65E5 } })
    {
        INFO ("U+" << std::hex << (uint32_t) from);
        CHECK (seedcode::normaliseWord (utf8 (from)) == utf8 (to));
    }

    // Every code point: exactly the table's mappings change, and folding twice changes nothing more.
    int changed = 0;
    for (char32_t cp = 0x21; cp <= 0x10FFFF; ++cp)
    {
        if (cp >= 0xD800 && cp <= 0xDFFF)
            continue;
        if (cp == 0x85 || cp == 0xA0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028
            || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000 || cp == 0xFEFF)
            continue;   // white space, trimmed away

        const auto single = utf8 (cp);
        const auto folded = seedcode::normaliseWord (single);
        if (folded != single)
        {
            ++changed;
            CHECK (seedcode::normaliseWord (folded) == folded);
        }
    }
    CHECK (changed == seedcode::generated::lowerCaseMappings);
}
