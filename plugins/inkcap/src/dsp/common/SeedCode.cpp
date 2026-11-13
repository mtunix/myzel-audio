#include "dsp/common/SeedCode.h"

#include "dsp/common/generated/LowerCaseTable.h"

#include <algorithm>

namespace inkcap::dsp::seedcode
{

namespace
{
    constexpr char32_t replacement = 0xFFFD;

    /** Decodes UTF-8; every byte that does not start a valid, shortest-form sequence becomes U+FFFD. */
    std::u32string decode (std::string_view text)
    {
        std::u32string out;
        out.reserve (text.size());

        const auto n = text.size();
        size_t i = 0;

        while (i < n)
        {
            const auto b0 = (uint8_t) text[i];

            if (b0 < 0x80)
            {
                out.push_back (b0);
                ++i;
                continue;
            }

            const int length = b0 >= 0xF0 ? 4 : b0 >= 0xE0 ? 3 : b0 >= 0xC0 ? 2 : 0;
            char32_t cp = length == 4 ? (b0 & 0x07u) : length == 3 ? (b0 & 0x0Fu) : (b0 & 0x1Fu);
            bool valid = length > 0 && i + (size_t) length <= n;

            for (int k = 1; valid && k < length; ++k)
            {
                const auto b = (uint8_t) text[i + (size_t) k];
                valid = (b & 0xC0u) == 0x80u;
                cp = (cp << 6) | (b & 0x3Fu);
            }

            constexpr char32_t minimum[] = { 0, 0, 0x80, 0x800, 0x10000 };
            valid = valid && cp >= minimum[length] && cp <= 0x10FFFF && ! (cp >= 0xD800 && cp <= 0xDFFF);

            if (valid)
            {
                out.push_back (cp);
                i += (size_t) length;
            }
            else
            {
                out.push_back (replacement);
                ++i;
            }
        }

        return out;
    }

    std::string encode (std::u32string_view text)
    {
        std::string out;
        out.reserve (text.size());

        for (const auto cp : text)
        {
            if (cp < 0x80)
            {
                out.push_back ((char) cp);
            }
            else if (cp < 0x800)
            {
                out.push_back ((char) (0xC0 | (cp >> 6)));
                out.push_back ((char) (0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back ((char) (0xE0 | (cp >> 12)));
                out.push_back ((char) (0x80 | ((cp >> 6) & 0x3F)));
                out.push_back ((char) (0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back ((char) (0xF0 | (cp >> 18)));
                out.push_back ((char) (0x80 | ((cp >> 12) & 0x3F)));
                out.push_back ((char) (0x80 | ((cp >> 6) & 0x3F)));
                out.push_back ((char) (0x80 | (cp & 0x3F)));
            }
        }

        return out;
    }

    /** Unicode's White_Space property, plus U+FEFF (a byte-order mark pasted with the text). */
    constexpr bool isSpace (char32_t c) noexcept
    {
        return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680
            || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F
            || c == 0x205F || c == 0x3000 || c == 0xFEFF;
    }

    std::u32string_view trim (std::u32string_view text) noexcept
    {
        while (! text.empty() && isSpace (text.front()))
            text.remove_prefix (1);
        while (! text.empty() && isSpace (text.back()))
            text.remove_suffix (1);
        return text;
    }

    char32_t toLower (char32_t c) noexcept
    {
        const auto& runs = generated::lowerCaseRuns;
        auto it = std::upper_bound (runs.begin(), runs.end(), c,
                                    [] (char32_t value, const generated::LowerCaseRun& run) { return value < run.first; });

        if (it == runs.begin())
            return c;

        --it;
        if (c > it->last || (c - it->first) % (char32_t) it->step != 0)
            return c;

        return (char32_t) ((int32_t) c + it->offset);
    }

    int hexValue (char c) noexcept
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    /** ASCII white space only: a code is ASCII, and this runs before any UTF-8 decoding. */
    std::string_view trimAscii (std::string_view text) noexcept
    {
        auto space = [] (char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
        while (! text.empty() && space (text.front()))
            text.remove_prefix (1);
        while (! text.empty() && space (text.back()))
            text.remove_suffix (1);
        return text;
    }
}

std::string format (uint32_t seed)
{
    constexpr const char* digits = "0123456789ABCDEF";
    std::string out (9, '-');

    for (int i = 0; i < 8; ++i)
        out[(size_t) (i < 4 ? i : i + 1)] = digits[(seed >> (28 - 4 * i)) & 0xFu];

    return out;
}

std::optional<uint32_t> parseCode (std::string_view text) noexcept
{
    // parse() trims Unicode white space before it gets here; a direct caller may pass ASCII padding.
    text = trimAscii (text);

    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text.remove_prefix (2);

    const auto dashed = text.size() == 9 && text[4] == '-';

    if (! dashed && text.size() != 8)
        return std::nullopt;

    uint32_t value = 0;

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (dashed && i == 4)
            continue;

        const auto v = hexValue (text[i]);
        if (v < 0)
            return std::nullopt;

        value = (value << 4) | (uint32_t) v;
    }

    return value;
}

std::string normaliseWord (std::string_view utf8)
{
    const auto decoded = decode (utf8);
    std::u32string folded (trim (decoded));

    for (auto& c : folded)
        c = toLower (c);

    return encode (folded);
}

uint32_t wordHash (std::string_view utf8)
{
    return fnv1a32 (normaliseWord (utf8));
}

std::optional<Parsed> parse (std::string_view utf8)
{
    const auto decoded = decode (utf8);
    const auto trimmed = trim (decoded);

    if (trimmed.empty())
        return std::nullopt;

    const auto trimmedUtf8 = encode (trimmed);

    if (const auto code = parseCode (trimmedUtf8))
        return Parsed { *code, {} };

    return Parsed { wordHash (trimmedUtf8), encode (trimmed.substr (0, (size_t) maxLabelLength)) };
}

} // namespace inkcap::dsp::seedcode
