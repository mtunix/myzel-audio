#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace inkcap::readout
{

/**
    Text helpers for the typed controls' read-outs (ModuleInfo's typedControlText hands each
    module its own): locale-free, so "0.5" reads the same in every host.
*/

/** A plain decimal number at the start ("42", "-3.5", "+.5"); NaN if there is none. `rest` gets what follows. */
inline double parseNumber (std::string_view text, std::string_view* rest = nullptr)
{
    size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
        ++i;

    auto sign = 1.0;
    if (i < text.size() && (text[i] == '+' || text[i] == '-'))
        sign = text[i++] == '-' ? -1.0 : 1.0;

    double value = 0.0, scale = 1.0;
    bool digits = false, fraction = false;
    for (; i < text.size(); ++i)
    {
        const auto c = text[i];
        if (c == '.' && ! fraction)
        {
            fraction = true;
        }
        else if (c >= '0' && c <= '9')
        {
            digits = true;
            if (fraction)
                value += (scale *= 0.1) * (c - '0');
            else
                value = value * 10.0 + (c - '0');
        }
        else
        {
            break;
        }
    }

    if (rest != nullptr)
        *rest = text.substr (i);
    return digits ? sign * value : std::nan ("");
}

/** The text in lower case, without spaces, tabs or a trailing '%'. */
inline std::string squeezed (std::string_view text)
{
    std::string s;
    for (const auto c : text)
        if (c != ' ' && c != '\t')
            s += (char) std::tolower ((unsigned char) c);
    while (! s.empty() && s.back() == '%')
        s.pop_back();
    return s;
}

inline std::string percentText (float percent)
{
    return std::to_string ((int) std::lround (std::clamp (percent, 0.0f, 100.0f))) + " %";
}

/** A position typed as a percentage; -1 if it is not a number. */
inline float percentFromText (std::string_view text)
{
    const auto value = parseNumber (squeezed (text));
    return std::isnan (value) ? -1.0f : (float) std::clamp (value, 0.0, 100.0);
}

/** "8 ms", "49 ms", "1.20 s". */
inline std::string secondsText (double seconds)
{
    char text[32];
    if (seconds < 1.0)
        std::snprintf (text, sizeof (text), "%d ms", (int) std::lround (seconds * 1000.0));
    else
        std::snprintf (text, sizeof (text), "%.2f s", seconds);
    return text;
}

/** "120", "120 ms", "0.12 s", "0.12s": seconds, or NaN. A bare number is milliseconds. */
inline double secondsFromText (std::string_view text)
{
    const auto s = squeezed (text);
    std::string_view rest;
    const auto value = parseNumber (s, &rest);
    if (std::isnan (value))
        return value;
    if (rest == "s" || rest == "sec")
        return value;
    if (rest.empty() || rest == "ms")
        return value * 0.001;
    return std::nan ("");
}

/** "0.55 Hz", "7.0 Hz", "28 Hz". */
inline std::string hertzText (double hz)
{
    char text[32];
    if (hz < 1.0)
        std::snprintf (text, sizeof (text), "%.2f Hz", hz);
    else if (hz < 10.0)
        std::snprintf (text, sizeof (text), "%.1f Hz", hz);
    else
        std::snprintf (text, sizeof (text), "%d Hz", (int) std::lround (hz));
    return text;
}

/** "2", "2 Hz", "2hz": hertz, or NaN. */
inline double hertzFromText (std::string_view text)
{
    const auto s = squeezed (text);
    std::string_view rest;
    const auto value = parseNumber (s, &rest);
    return std::isnan (value) || ! (rest.empty() || rest == "hz") ? std::nan ("") : value;
}

} // namespace inkcap::readout
