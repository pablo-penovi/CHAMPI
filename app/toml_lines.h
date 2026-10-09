// The small subset of TOML that CHAMPI's config files use (keymap.toml, connections.toml):
// `name = value` lines, where a value is a string, a non-negative integer or an [array] of them
// (which may span lines), and # comments. No tables, no other types.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace champi
{
struct TomlValue
{
    bool        is_string = false;
    std::string text;       // a string's contents
    long        number = 0; // an integer's value
    int         line   = 0; // where it starts, for error messages
};

struct TomlLine
{
    std::string            name;
    std::vector<TomlValue> values; // one, or an array's
    bool                   is_array = false;
    int                    line     = 0;
};

/**
 * Parses `text`. `what` describes a value in errors ("a key name in quotes, a scancode"), and
 * `tables` says what to write instead of a [table]. Throws std::runtime_error as
 * "source:line: message". Integers too large for a long come back as LONG_MAX.
 */
std::vector<TomlLine> ParseTomlLines(std::string_view text, const std::string& source,
                                     const std::string& what, const std::string& tables);

/** Throws std::runtime_error as "source:line: what". */
[[noreturn]] void TomlFail(const std::string& source, int line, const std::string& what);

/** `s` as a TOML basic string, in double quotes, with " and \ escaped. */
std::string TomlQuote(std::string_view s);

} // namespace champi
