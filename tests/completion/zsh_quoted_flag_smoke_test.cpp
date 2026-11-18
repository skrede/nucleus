#include "completion/zsh_test_support.h"

#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/cli_flag.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include "support/completion_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <algorithm>
#include <string_view>

using nucleus::anchor;
using nucleus::cli_delimiter;
using nucleus::generate_completion;
using nucleus::key_path;
using nucleus::schema_registry;
using nucleus::shell;
using nucleus::completion_result_test::script_of;
using nucleus::zsh_test::below_5_9;
using nucleus::zsh_test::drive_zsh;
using nucleus::zsh_test::records_of;
using nucleus::zsh_test::require_quiet;
using nucleus::zsh_test::zsh_available;
using nucleus::zsh_test::zsh_session;

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

schema_registry port_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("cluster/node")))));
    return reg;
}

schema_registry delim_registry()
{
    schema_registry reg = port_registry();
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("cluster")))));
    return reg;
}

std::string script(const schema_registry &reg, std::string_view delimiter = "-")
{
    return script_of(generate_completion(shell::zsh, reg, "myapp", cli_delimiter::parse(delimiter).value()));
}

zsh_session drive(const std::string &script, const std::string &typed) { return drive_zsh({{"_nucleus_myapp", script}}, {typed}); }

std::vector<std::string> buffer_of(const zsh_session &session) { return records_of(session.cases[0], "BUF"); }

bool offers_star(const zsh_session &session)
{
    const std::vector<std::string> hits = records_of(session.cases[0], "HIT");
    return std::ranges::any_of(hits, [](const std::string &hit)
                               { return hit.find('*') != std::string::npos; });
}
}

TEST_CASE("a typed backslash-escaped star under a star delimiter completes the flag", "[completion][smoke][zsh][ordinal]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::string starred = "myapp --cluster\\*node\\*2\\*";
    const zsh_session session = drive(script(port_registry(), "*"), starred);
    INFO("zsh " << session.version);
    CHECK(buffer_of(session) == std::vector<std::string>{below_5_9(session.version) ? starred : starred + "port "});
    require_quiet(session);
    const std::string delimiter = "\\$\\(touch\\ PWN_DELIM\\)";
    const std::string typed     = "myapp --cluster" + delimiter + "node" + delimiter + "2" + delimiter;
    const zsh_session hostile   = drive(script(delim_registry(), "$(touch PWN_DELIM)"), typed);
    REQUIRE(hostile.payload.empty());
    CHECK(buffer_of(hostile) == std::vector<std::string>{below_5_9(hostile.version) ? typed : typed + "port "});
    require_quiet(hostile);
}

TEST_CASE("below zsh 5.9 a flag carrying a character zsh quotes is not offered", "[completion][smoke][zsh][ordinal]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::string spaced_typed = "myapp --cluster\\ node\\ 2\\ ";
    const zsh_session starred      = drive(script(port_registry(), "*"), "myapp --cluster*node*2*");
    const zsh_session wildcard     = drive(script(port_registry()), "myapp --cluster-node-\\*-p");
    const zsh_session spaced       = drive(script(port_registry(), " "), spaced_typed);
    const bool        old          = below_5_9(starred.version);
    INFO("zsh " << starred.version);
    CHECK(buffer_of(starred) == std::vector<std::string>{old ? "myapp --cluster*node*2*" : "myapp --cluster\\*node\\*2\\*port "});
    CHECK(buffer_of(wildcard) == std::vector<std::string>{old ? "myapp --cluster-node-\\*-p" : "myapp --cluster-node-\\*-port "});
    CHECK(buffer_of(spaced) == std::vector<std::string>{old ? spaced_typed : spaced_typed + "port "});
    for(const zsh_session &session : {starred, wildcard, spaced})
    {
        if(old)
            CHECK_FALSE(offers_star(session));
        require_quiet(session);
    }
}
