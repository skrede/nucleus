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
};

// U+00E9, U+20AC and U+1F600 are printable, U+009B is a C1 control, U+202E is Bidi_Control, and a lone 0xff is
// malformed UTF-8.
constexpr std::array<std::string_view, 6> non_ascii_samples{"\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80",
                                                            "\xc2\x9b", "\xe2\x80\xae", "\xff"};

// The category rule marks every C1 and Bidi_Control character quoted on purpose, so it over-matches U+202E, which
// zsh 5.9 inserts unchanged; older releases still leave such a flag out.
const std::vector<std::string> over_matched{"--kx5\xe2\x80\xaez"};

constexpr std::array<zsh_route, 3> install_routes{zsh_route::fpath, zsh_route::source, zsh_route::eval};

std::string hex_of(std::uint8_t byte)
{
    constexpr std::string_view digits = "0123456789abcdef";
    return {digits[byte >> 4], digits[byte & 0x0f]};
}

flag_unit unit_of(const std::string &label, std::string_view carried)
{
    const std::string prefix = "--k" + label;
    return {prefix + std::string(carried) + "z", "myapp " + prefix};
}

std::vector<flag_unit> flag_units()
{
    std::vector<flag_unit> units;
    for(std::uint8_t byte = 0x01; byte <= 0x7f; ++byte)
        if(byte != 0x0a)
            units.push_back(unit_of(hex_of(byte), std::string(1, static_cast<char>(byte))));
    for(std::size_t k = 0; k < non_ascii_samples.size(); ++k)
        units.push_back(unit_of("x" + std::to_string(k + 1), non_ascii_samples[k]));
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

void require_unit(const zsh_seen &seen, const flag_unit &unit, bool old)
{
    const bool                     quoted = nucleus::detail::zsh_quotes_on_insert(unit.flag);
    const bool                     over   = std::ranges::find(over_matched, unit.flag) != over_matched.end();
    const std::vector<std::string> buffer = records_of(seen, "BUF");
    const std::vector<std::string> raw{"myapp " + unit.flag + " "};
    INFO("typed " << unit.typed << (quoted ? ", classified quoted" : ", classified verbatim"));
    if(over)
        CHECK(quoted);
    if(!quoted || (over && !old))
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
}

TEST_CASE("a flag is left out before zsh 5.9 exactly when the classifier marks it, and the classifier covers what zsh 5.9 quotes, on every install route", "[completion][smoke][zsh]")
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
            for(std::size_t k = 0; k < units.size(); ++k)
                require_unit(session.cases[k], units[k], below_5_9(session.version));
            require_quiet(session);
        }
}
