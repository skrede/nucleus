#include "completion/zsh_test_support.h"

#include "nucleus/completion/completion.h"
#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include "support/completion_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>

using nucleus::anchor;
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
using nucleus::zsh_test::zsh_seen;
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

schema_registry level_registry()
{
    schema_registry reg = port_registry();
    REQUIRE(reg.attach(nucleus::enum_element("level", anchor::keyspace(path_of("cluster/node")), {"debug", "info"})));
    return reg;
}

schema_registry route_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::repeated_element("route", anchor::keyspace(path_of("cluster/node")))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("cluster/node/route")))));
    return reg;
}

std::string script(const schema_registry &reg, const key_path &at = {})
{
    return script_of(generate_completion(shell::zsh, reg, "myapp", {}, at));
}

struct ordinal_row
{
    std::string              typed;
    std::vector<std::string> hits;
    std::string              buf;
    std::string              buf_before_5_9;
};

std::vector<std::string> sorted_hits(const zsh_seen &seen)
{
    std::vector<std::string> hits = records_of(seen, "HIT");
    std::ranges::sort(hits);
    return hits;
}

// Below zsh 5.9 the script offers no flag carrying `*`.
std::vector<std::string> listed_hits(std::vector<std::string> hits, const std::string &version)
{
    if(below_5_9(version))
        std::erase_if(hits, [](const std::string &hit)
                      { return hit.find("\\*") != std::string::npos; });
    std::ranges::sort(hits);
    return hits;
}

zsh_session check_rows(const std::string &script, const std::vector<ordinal_row> &rows)
{
    std::vector<std::string> typed;
    for(const ordinal_row &row : rows)
        typed.push_back(row.typed);
    const zsh_session session = drive_zsh({{"_nucleus_myapp", script}}, typed);
    for(std::size_t k = 0; k < rows.size(); ++k)
    {
        INFO(rows[k].typed);
        CHECK(sorted_hits(session.cases[k]) == listed_hits(rows[k].hits, session.version));
        CHECK(records_of(session.cases[k], "BUF") == std::vector<std::string>{below_5_9(session.version) ? rows[k].buf_before_5_9 : rows[k].buf});
    }
    return session;
}

const std::string max_typed = "myapp --cluster-node-" + std::to_string(key_path::max_ordinal) + "-";

const std::vector<ordinal_row> port_rows{
        {"myapp --cluster-node-2-", {"--cluster-node-2-port"}, "myapp --cluster-node-2-port ", "myapp --cluster-node-2-port "},
        {"myapp --cluster-node-2", {"--cluster-node-2-port"}, "myapp --cluster-node-2-port ", "myapp --cluster-node-2-port "},
        {"myapp --cluster-node-", {"--cluster-node-port", "--cluster-node-\\*-port"}, "myapp --cluster-node-port", "myapp --cluster-node-port "},
        {"myapp --cluster-node-2x-", {}, "myapp --cluster-node-2x-", "myapp --cluster-node-2x-"},
        {"myapp --cluster-node-2-portx", {}, "myapp --cluster-node-2-portx", "myapp --cluster-node-2-portx"},
        {max_typed, {max_typed.substr(6) + "port"}, max_typed + "port ", max_typed + "port "},
        {"myapp --cluster-node-4294967296-", {}, "myapp --cluster-node-4294967296-", "myapp --cluster-node-4294967296-"},
        {"myapp --cluster-node-0000000002-", {"--cluster-node-0000000002-port"}, "myapp --cluster-node-0000000002-port ", "myapp --cluster-node-0000000002-port "},
        {"myapp --cluster-node-2-port=5 --cluster-node-3-", {"--cluster-node-3-port"}, "myapp --cluster-node-2-port=5 --cluster-node-3-port ", "myapp --cluster-node-2-port=5 --cluster-node-3-port "},
};
const std::vector<ordinal_row> level_rows{
        {"myapp --cluster-node-2-level=", {"debug", "info"}, "myapp --cluster-node-2-level=", "myapp --cluster-node-2-level="},
        {"myapp --cluster-node-2-level=d", {"debug"}, "myapp --cluster-node-2-level=debug ", "myapp --cluster-node-2-level=debug "},
        {"myapp --cluster-node-level=", {"debug", "info"}, "myapp --cluster-node-level=", "myapp --cluster-node-level="},
        {"myapp --cluster-node-2-lev=", {"debug", "info"}, "myapp --cluster-node-2-level=", "myapp --cluster-node-2-level="},
        {"myapp --cluster-node-lev=", {"debug", "info"}, "myapp --cluster-node-level=", "myapp --cluster-node-level="},
        {"myapp --cluster-node-x-level=", {}, "myapp --cluster-node-x-level=", "myapp --cluster-node-x-level="},
};
const std::vector<ordinal_row> route_rows{
        {"myapp --cluster-node-0-route-2-", {"--cluster-node-0-route-2-port"}, "myapp --cluster-node-0-route-2-port ", "myapp --cluster-node-0-route-2-port "},
        {"myapp --cluster-node-0-r", {"--cluster-node-0-route", "--cluster-node-0-route-\\*-port"}, "myapp --cluster-node-0-route", "myapp --cluster-node-0-route "},
};
const std::vector<ordinal_row> anchored_rows{
        {"myapp --0-", {"--0-level", "--0-port"}, "myapp --0-", "myapp --0-"},
        {"myapp --", {"--level", "--\\*-level", "--port", "--\\*-port"}, "myapp --", "myapp --"},
};
}

TEST_CASE("a typed ordinal completes the repeated-container flag under real zsh", "[completion][smoke][zsh][ordinal]")
{
    if(!zsh_available())
        SKIP("no zsh with zsh/zpty reachable");
    const std::vector<zsh_session> sessions{
            check_rows(script(port_registry()), port_rows),
            check_rows(script(level_registry()), level_rows),
            check_rows(script(route_registry()), route_rows),
            check_rows(script(level_registry(), path_of("cluster/node")), anchored_rows),
    };
    for(const zsh_session &session : sessions)
        require_quiet(session);
}
