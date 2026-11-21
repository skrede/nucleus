#include "completion/zsh_test_support.h"

#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "support/completion_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <string_view>

using nucleus::anchor;
using nucleus::generate_completion;
using nucleus::schema_registry;
using nucleus::shell;
using nucleus::completion_result_test::script_of;
using nucleus::zsh_test::drive_zsh;
using nucleus::zsh_test::records_of;
using nucleus::zsh_test::require_quiet;
using nucleus::zsh_test::route_text;
using nucleus::zsh_test::zsh_available;
using nucleus::zsh_test::zsh_route;
using nucleus::zsh_test::zsh_script;
using nucleus::zsh_test::zsh_seen;
using nucleus::zsh_test::zsh_session;

namespace {

constexpr std::string_view vprobe_completion = "_vprobe() { _values 'thing' alpha beta; }\ncompdef _vprobe vprobe\n";

const std::vector<std::string> compsys_names{"tags", "files", "values"};

struct collision_row
{
    std::string              name;
    std::size_t              victim;
    std::vector<std::string> hits;
    std::string              buf;
    bool                     recursion;
};

const std::vector<collision_row> collision_rows{
        {"tags", 1, {}, "myapp --a", true},
        {"files", 2, {}, "othertool deco", false},
        {"values", 3, {"--alpha"}, "vprobe --alpha ", false}};

std::string alpha_script(const std::string &prog)
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("alpha", anchor::root())));
    return script_of(generate_completion(shell::zsh, reg, prog));
}

std::string unprefixed(const std::string &text, const std::string &name)
{
    const std::string prefixed = "_nucleus_" + name;
    std::string       out      = text;
    for(std::size_t at = out.find(prefixed); at != std::string::npos; at = out.find(prefixed, at))
        out.replace(at, prefixed.size(), "_" + name);
    REQUIRE(out != text);
    return out;
}

// zsh reports the runaway recursion in one of two wordings, depending on its release.
bool recursion_reported(const std::string &terminal)
{
    return terminal.find("maximum nested function level reached") != std::string::npos || terminal.find("recursion limit exceeded") != std::string::npos;
}

// Ubuntu 24.04's zsh 5.9 hangs on the runaway recursion instead of reporting it, at the program's own completion
// or at the victim's; a hang at or before the victim is that symptom.
bool hung_by_victim(const zsh_session &session, std::size_t victim)
{
    return !session.hung.empty() && std::stoul(session.hung) <= victim + 1;
}

std::vector<zsh_script> installed(zsh_route route, const std::string &name, const std::string &text)
{
    const std::string prefixed = "_nucleus_" + name;
    const std::string function = text.find(prefixed + "()") != std::string::npos ? prefixed : "_" + name;
    if(route == zsh_route::fpath)
        return {{"_nucleus_myapp", alpha_script("myapp")}, {function, text}};
    return {{"myapp.zsh", alpha_script("myapp")}, {name + ".zsh", text}};
}

zsh_session drive_named(zsh_route route, const std::string &name, const std::string &text, bool hang_ok = false)
{
    const std::vector<std::string> typed{name + " --a", "myapp --a", "othertool deco", "vprobe "};
    return drive_zsh(installed(route, name, text), typed, {.route = route, .setup = std::string(vprobe_completion), .decoys = {"decoyfile"}, .hang_ok = hang_ok});
}

void check_broken(const zsh_session &session, const collision_row &row)
{
    const zsh_seen &victim = session.cases[row.victim];
    INFO(victim.terminal);
    if(row.recursion && hung_by_victim(session, row.victim))
        return;
    CHECK(session.hung.empty());
    CHECK(records_of(victim, "HIT") == row.hits);
    CHECK(records_of(victim, "BUF") == std::vector<std::string>{row.buf});
    CHECK(recursion_reported(victim.terminal) == row.recursion);
}

std::vector<std::string> sorted_hits(const zsh_seen &seen)
{
    std::vector<std::string> hits = records_of(seen, "HIT");
    std::ranges::sort(hits);
    return hits;
}
}

TEST_CASE("a program named after a zsh completion function completes and leaves that function in place", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    for(const zsh_route route : {zsh_route::fpath, zsh_route::source, zsh_route::eval})
        for(const std::string &name : compsys_names)
            DYNAMIC_SECTION(route_text(route) << " " << name)
            {
                const zsh_session session = drive_named(route, name, alpha_script(name));
                INFO(session.setup);
                REQUIRE(session.registered == "_nucleus_myapp");
                require_quiet(session);
                CHECK(records_of(session.cases[0], "BUF") == std::vector<std::string>{name + " --alpha "});
                CHECK(records_of(session.cases[1], "BUF") == std::vector<std::string>{"myapp --alpha "});
                CHECK(records_of(session.cases[2], "BUF") == std::vector<std::string>{"othertool decoyfile "});
                CHECK(sorted_hits(session.cases[3]) == std::vector<std::string>{"alpha", "beta"});
            }
}

TEST_CASE("a script defining zsh's own completion function under its name breaks what calls it", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    for(const zsh_route route : {zsh_route::fpath, zsh_route::source})
        for(const collision_row &row : collision_rows)
            DYNAMIC_SECTION(route_text(route) << " " << row.name)
            {
                const zsh_session session = drive_named(route, row.name, unprefixed(alpha_script(row.name), row.name), row.recursion);
                REQUIRE(session.registered == "_nucleus_myapp");
                check_broken(session, row);
            }
}
