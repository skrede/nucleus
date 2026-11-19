#include "completion/zsh_test_support.h"

#include "nucleus/completion/zsh_emitter.h"
#include "nucleus/completion/zsh_quoted_flag.h"
#include "nucleus/completion/completion_model.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <string_view>

using nucleus::zsh_test::below_5_9;
using nucleus::zsh_test::drive_zsh;
using nucleus::zsh_test::records_of;
using nucleus::zsh_test::require_quiet;
using nucleus::zsh_test::route_text;
using nucleus::zsh_test::zsh_available;
using nucleus::zsh_test::zsh_route;
using nucleus::zsh_test::zsh_seen;
using nucleus::zsh_test::zsh_session;

namespace {

struct flag_unit
{
    std::string flag;
    std::string typed;
    std::string quoted;
};

// The samples span printable letters and symbols, a C1 control, bidirectional controls, separators, an unassigned
// code point, a noncharacter and malformed UTF-8; zsh 5.9 quotes some of them on one platform and not on another.
constexpr std::array<std::string_view, 13> non_ascii_samples{
        "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80", "\xc2\x9b", "\xe2\x80\xae", "\xff", "\xe2\x80\xa8",
        "\xe2\x80\xa9", "\xcd\xb8", "\xef\xbf\xbf", "\xe2\x80\x8e", "\xd8\x9c", "\xe2\x81\xa6"};

constexpr std::array<zsh_route, 3> install_routes{zsh_route::fpath, zsh_route::source, zsh_route::eval};

std::string hex_of(std::uint8_t byte)
{
    constexpr std::string_view digits = "0123456789abcdef";
    return {digits[byte >> 4], digits[byte & 0x0f]};
}

std::string dollar_quoted(std::string_view bytes)
{
    std::string text = "$'";
    for(const char c : bytes)
    {
        const auto byte = static_cast<std::uint8_t>(c);
        text += {'\\', static_cast<char>('0' + (byte >> 6)), static_cast<char>('0' + ((byte >> 3) & 7)),
                 static_cast<char>('0' + (byte & 7))};
    }
    return text + "'";
}

flag_unit unit_of(const std::string &label, std::string_view carried)
{
    const std::string prefix = "--k" + label;
    return {prefix + std::string(carried) + "z", "myapp " + prefix, {}};
}

std::vector<flag_unit> flag_units()
{
    std::vector<flag_unit> units;
    for(std::uint8_t byte = 0x01; byte <= 0x7f; ++byte)
        if(byte != 0x0a)
            units.push_back(unit_of(hex_of(byte), std::string(1, static_cast<char>(byte))));
    for(std::size_t k = 0; k < non_ascii_samples.size(); ++k)
    {
        units.push_back(unit_of((k < 9 ? "x0" : "x") + std::to_string(k + 1), non_ascii_samples[k]));
        units.back().quoted = units.back().typed + dollar_quoted(non_ascii_samples[k]) + "z ";
    }
    return units;
}

const std::vector<flag_unit> units = flag_units();

zsh_session drive_on(zsh_route route)
{
    nucleus::completion_model model{"myapp", {}};
    std::vector<std::string>  typed;
    for(const flag_unit &unit : units)
    {
        model.options.push_back({unit.flag, "", {}, {}});
        typed.push_back(unit.typed);
    }
    const std::string file = route == zsh_route::fpath ? "_nucleus_myapp" : "myapp.zsh";
    return drive_zsh({{file, nucleus::zsh_emitter{}.emit(model)}}, typed, {.route = route});
}

bool offered(const zsh_seen &seen, const std::string &flag)
{
    const std::vector<std::string> values = records_of(seen, "VALUE");
    return std::ranges::find(values, flag) != values.end();
}

void require_ascii_unit(const zsh_seen &seen, const flag_unit &unit, bool old)
{
    const bool                     quoted = nucleus::detail::zsh_quotes_on_insert(unit.flag);
    const std::vector<std::string> buffer = records_of(seen, "BUF");
    const std::vector<std::string> raw{"myapp " + unit.flag + " "};
    INFO("typed " << unit.typed << (quoted ? ", classified quoted" : ", classified verbatim"));
    if(!quoted)
        CHECK(buffer == raw);
    else if(old)
    {
        CHECK(records_of(seen, "HIT").empty());
        CHECK(buffer == std::vector<std::string>{unit.typed});
    }
    else
    {
        CHECK(offered(seen, unit.flag));
        CHECK(buffer != raw);
    }
}

void require_non_ascii_unit(const zsh_seen &seen, const flag_unit &unit, bool old)
{
    const bool                     quoted = nucleus::detail::zsh_quotes_on_insert(unit.flag);
    const std::vector<std::string> buffer = records_of(seen, "BUF");
    INFO("typed " << unit.typed << (quoted ? ", classified quoted" : ", classified verbatim"));
    CHECK(quoted);
    if(old)
    {
        CHECK(records_of(seen, "HIT").empty());
        CHECK(buffer == std::vector<std::string>{unit.typed});
        return;
    }
    CHECK(offered(seen, unit.flag));
    // zsh 5.9 quotes a non-ASCII character only where the C library deems it unprintable, so either form is right.
    const bool as_typed = buffer == std::vector<std::string>{"myapp " + unit.flag + " "};
    CHECK((as_typed || buffer == std::vector<std::string>{unit.quoted}));
}
}

TEST_CASE("a flag holding any byte outside printable, non-syntax ASCII is left out before zsh 5.9 and offered by zsh 5.9, on every install route", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    for(const zsh_route route : install_routes)
        DYNAMIC_SECTION(route_text(route))
        {
            const zsh_session session = drive_on(route);
            INFO("zsh " << session.version);
            REQUIRE(session.registered == "_nucleus_myapp");
            REQUIRE(session.cases.size() == units.size());
            const bool old = below_5_9(session.version);
            for(std::size_t k = 0; k < units.size(); ++k)
                if(units[k].quoted.empty())
                    require_ascii_unit(session.cases[k], units[k], old);
                else
                    require_non_ascii_unit(session.cases[k], units[k], old);
            require_quiet(session);
        }
}
