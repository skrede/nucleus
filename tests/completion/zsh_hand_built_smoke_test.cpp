#include "completion/zsh_test_support.h"

#include "nucleus/completion/zsh_emitter.h"
#include "nucleus/completion/completion_model.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstddef>

using nucleus::zsh_test::drive_zsh;
using nucleus::zsh_test::records_of;
using nucleus::zsh_test::zsh_available;
using nucleus::zsh_test::zsh_script;
using nucleus::zsh_test::zsh_seen;
using nucleus::zsh_test::zsh_session;

namespace {

struct hand_built
{
    std::string prog;
    std::string flag;
};

const std::vector<hand_built> refused{{"starflag", "*x"}, {"parenflag", "(a)b"}, {"bangflag", "!e"}, {"colonflag", ":c"}};

zsh_script script_of(const hand_built &program)
{
    const nucleus::completion_model model{program.prog, {{"--ok", "d", {}, {}}, {program.flag, "d", {}, {}}}};
    return {"_" + program.prog, nucleus::zsh_emitter{}.emit(model)};
}

void require_no_completion(const zsh_seen &seen, const std::string &typed)
{
    INFO(typed);
    INFO(seen.terminal);
    CHECK(records_of(seen, "HIT").empty());
    CHECK(records_of(seen, "BUF") == std::vector<std::string>{typed});
    CHECK(seen.terminal.find("invalid argument") != std::string::npos);
}

}

TEST_CASE("a hand-built flag that opens with neither dash nor plus leaves its program with no completion", "[completion][smoke][zsh]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    std::vector<zsh_script>  scripts;
    std::vector<std::string> typed;
    for(const hand_built &program : refused)
    {
        scripts.push_back(script_of(program));
        typed.push_back(program.prog + " --o");
    }
    scripts.push_back(script_of({"plusflag", "+g"}));
    typed.insert(typed.end(), {"plusflag --o", "plusflag +"});
    const zsh_session session = drive_zsh(scripts, typed, {.prog = "plusflag"});
    INFO("zsh " << session.version);
    INFO(session.setup);
    REQUIRE(session.registered == "_plusflag");
    for(std::size_t k = 0; k < refused.size(); ++k)
        require_no_completion(session.cases[k], typed[k]);
    CHECK(records_of(session.cases[4], "BUF") == std::vector<std::string>{"plusflag --ok "});
    CHECK(records_of(session.cases[5], "HIT") == std::vector<std::string>{"+g"});
    REQUIRE(session.payload.empty());
}
