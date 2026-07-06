#include "nucleus/log_sink.h"
#include "nucleus/capability.h"
#include "nucleus/config.h"
#include "nucleus/config_space.h"
#include "nucleus/error.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/config_source/source_handle.h"
#include "nucleus/config_source/config_source.h"
#include "nucleus/config_source/source_stack.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/schema/cli_flag.h"

#include "nucleus/argv/argv_source.h"
#include "nucleus/argv/cli_surface.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>
#include <string_view>

using nucleus::key_path;
using nucleus::argv_source;
using nucleus::normalize_arg;
using nucleus::cli_delimiter;

namespace {

// Collects the warn-level messages a source emits, to observe lenient behavior.
class capturing_sink final : public nucleus::log_sink
{
public:
    void log(nucleus::log_level level, std::string_view message) override
    {
        if(level == nucleus::log_level::warn)
            warnings.emplace_back(message);
    }

    std::vector<std::string> warnings;
};

}

TEST_CASE("normalize_arg maps `-` to the keyspace separator", "[argv]")
{
    auto kv = normalize_arg("--plexus-udp-auth_mode=auth");
    REQUIRE(kv);
    REQUIRE(kv.value().key.str() == "plexus/udp/auth_mode");
    REQUIRE(kv.value().value == "auth");
}

TEST_CASE("normalize_arg treats a bare flag as a truthy presence", "[argv]")
{
    auto kv = normalize_arg("--node-anonymous");
    REQUIRE(kv);
    REQUIRE(kv.value().key.str() == "node/anonymous");
    REQUIRE(kv.value().value == "true");
}

TEST_CASE("normalize_arg splits on the first `=` only", "[argv]")
{
    auto kv = normalize_arg("--token-url=https://h/p?a=b&c=d");
    REQUIRE(kv);
    REQUIRE(kv.value().key.str() == "token/url");
    REQUIRE(kv.value().value == "https://h/p?a=b&c=d");
}

TEST_CASE("normalize_arg rejects non-`--` tokens", "[argv]")
{
    REQUIRE_FALSE(normalize_arg("plexus-udp=x"));
    REQUIRE_FALSE(normalize_arg("--"));
    REQUIRE_FALSE(normalize_arg("--=x"));
}

TEST_CASE("the `-` <-> `/` bijection is invertible", "[argv]")
{
    auto kv = normalize_arg("--plexus-udp-auth_mode=auth").value();
    REQUIRE(nucleus::flag_of(kv.key) == "--plexus-udp-auth_mode");
}

TEST_CASE("cli_delimiter validates its text", "[argv][delimiter]")
{
    REQUIRE(cli_delimiter::parse("-"));
    REQUIRE(cli_delimiter::parse("__"));
    REQUIRE(cli_delimiter::parse("/")); // the identity mapping
    REQUIRE_FALSE(cli_delimiter::parse(""));
    REQUIRE_FALSE(cli_delimiter::parse("=")); // eaten by the key/value split
    REQUIRE_FALSE(cli_delimiter::parse("a/b")); // would forge path structure
    REQUIRE(cli_delimiter() == cli_delimiter::parse("-").value());
}

TEST_CASE("normalize_arg maps a custom delimiter to the separator", "[argv][delimiter]")
{
    const auto delim = cli_delimiter::parse("__").value();

    auto kv = normalize_arg("--plexus__udp__auth_mode=auth", delim);
    REQUIRE(kv);
    REQUIRE(kv.value().key.str() == "plexus/udp/auth_mode");
    REQUIRE(kv.value().value == "auth");

    // The inverse projection speaks the same delimiter.
    REQUIRE(nucleus::flag_of(kv.value().key, delim) == "--plexus__udp__auth_mode");
}

TEST_CASE("the `/` delimiter makes flag body and key path one string", "[argv][delimiter]")
{
    const auto delim = cli_delimiter::parse("/").value();
    auto kv = normalize_arg("--plexus/udp/auth_mode=auth", delim);
    REQUIRE(kv);
    REQUIRE(kv.value().key.str() == "plexus/udp/auth_mode");
    REQUIRE(nucleus::flag_of(kv.value().key, delim) == "--plexus/udp/auth_mode");
}

TEST_CASE("a raw separator in a flag is rejected under a non-`/` delimiter", "[argv][delimiter]")
{
    auto kv = normalize_arg("--plexus/udp-auth_mode=auth");
    REQUIRE_FALSE(kv);
    REQUIRE(kv.error().find("keyspace separator") != std::string::npos);
}

TEST_CASE("argv_source pulls under a host-chosen delimiter", "[argv][delimiter]")
{
    argv_source src(std::vector<std::string>{"--plexus__udp__auth_mode=auth"});
    src.delimit_with(cli_delimiter::parse("__").value());

    auto batch = src.pull();
    REQUIRE(batch);
    REQUIRE(batch.value().entries.size() == 1);
    REQUIRE(batch.value().entries[0].path == "plexus/udp/auth_mode");
    REQUIRE(batch.value().entries[0].value.text() == "auth");
}

TEST_CASE("an anchored source maps every flag relative to the anchor", "[argv][anchor]")
{
    argv_source src(std::vector<std::string>{"--udp-auth_mode=auth", "--name=alpha"});
    src.anchor_at(key_path::parse("plexus").value());

    auto batch = src.pull();
    REQUIRE(batch);
    REQUIRE(batch.value().entries.size() == 2);
    REQUIRE(batch.value().entries[0].path == "plexus/udp/auth_mode");
    REQUIRE(batch.value().entries[1].path == "plexus/name");
}

TEST_CASE("the recognizer sees the full anchored path", "[argv][anchor]")
{
    std::set<std::string> declared{"plexus/udp/auth_mode"};
    argv_source src(std::vector<std::string>{"--udp-auth_mode=auth"});
    src.anchor_at(key_path::parse("plexus").value())
        .recognize_with([&](const key_path &p)
                        { return declared.contains(p.str()); });

    REQUIRE(src.pull());

    // The same flag without the anchor maps to a bare path the schema does not
    // declare: strict validation rejects it.
    argv_source unanchored(std::vector<std::string>{"--udp-auth_mode=auth"});
    unanchored.recognize_with([&](const key_path &p)
                              { return declared.contains(p.str()); });
    REQUIRE_FALSE(unanchored.pull());
}

TEST_CASE("anchor and delimiter compose", "[argv][anchor][delimiter]")
{
    argv_source src(std::vector<std::string>{"--udp__auth_mode=auth"});
    src.anchor_at(key_path::parse("plugin/alpha").value())
        .delimit_with(cli_delimiter::parse("__").value());

    auto batch = src.pull();
    REQUIRE(batch);
    REQUIRE(batch.value().entries[0].path == "plugin/alpha/udp/auth_mode");
}

TEST_CASE("argv_source emits keyspace entries through the source seam", "[argv]")
{
    argv_source src(std::vector<std::string>{
        "--plexus-udp-auth_mode=auth", "--node-name=alpha"});

    nucleus::source_handle seam{std::move(src)}; // pulled through the erased seam
    auto batch = seam.pull();
    REQUIRE(batch);
    REQUIRE(batch.value().entries.size() == 2);
    REQUIRE(batch.value().entries[0].path == "plexus/udp/auth_mode");
    REQUIRE(batch.value().entries[0].value.text() == "auth");
    REQUIRE(batch.value().entries[1].path == "node/name");
}

TEST_CASE("schema validation is a separate step after mapping (strict)", "[argv]")
{
    std::set<std::string> declared{"plexus/udp/auth_mode"};
    argv_source src(std::vector<std::string>{"--plexus-udp-bogus=x"});
    src.recognize_with([&](const key_path &p)
                       { return declared.contains(p.str()); });

    auto batch = src.pull();
    REQUIRE_FALSE(batch); // strict by default: unknown path is an error
    REQUIRE(batch.error().message.find("undeclared key") != std::string::npos);
}

TEST_CASE("lenient mode stores unknown flags as strings with a warning", "[argv]")
{
    std::set<std::string> declared{"plexus/udp/auth_mode"};
    capturing_sink sink;
    argv_source src(std::vector<std::string>{"--plexus-udp-bogus=x"});
    src.recognize_with([&](const key_path &p)
                       { return declared.contains(p.str()); })
        .policy(nucleus::unknown_key_policy::lenient)
        .log_to(sink);

    auto batch = src.pull();
    REQUIRE(batch);
    REQUIRE(batch.value().entries.size() == 1);
    REQUIRE(batch.value().entries[0].value.text() == "x");
    REQUIRE_FALSE(sink.warnings.empty());
}

TEST_CASE("a recognized flag passes strict validation", "[argv]")
{
    std::set<std::string> declared{"plexus/udp/auth_mode"};
    argv_source src(std::vector<std::string>{"--plexus-udp-auth_mode=auth"});
    src.recognize_with([&](const key_path &p)
                       { return declared.contains(p.str()); });

    auto batch = src.pull();
    REQUIRE(batch);
    REQUIRE(batch.value().entries.size() == 1);
}

TEST_CASE("argv declares nesting and duplicate_keys, not typing", "[argv][capability]")
{
    const auto caps = argv_source::descriptor();
    REQUIRE(caps.supports(nucleus::capability::nesting));
    REQUIRE(caps.supports(nucleus::capability::duplicate_keys));
    REQUIRE_FALSE(caps.supports(nucleus::capability::typed_scalars));
    REQUIRE_FALSE(caps.supports(nucleus::capability::comments));
}

namespace {

// A nested schema with a repeated leaf: the shape that derives HARD nesting and
// HARD duplicate_keys -- the requirements argv must satisfy on its own.
nucleus::config_space make_nested_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
        nucleus::element("server", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::element("host", nucleus::anchor::keyspace("server"))));
    REQUIRE(builder.register_element(
        nucleus::repeated_element("tag", nucleus::anchor::keyspace("server"))));
    return builder.build();
}

}

TEST_CASE("a nested element schema loads from argv alone", "[argv][capability]")
{
    const nucleus::config_space space = make_nested_space();

    argv_source src(std::vector<std::string>{"--server-host=edge"});
    src.recognize_with(nucleus::recognizer_of(space));

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(src)}, {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("server/host").value() == "edge");
}

TEST_CASE("repeated flags compose into one ordered collection", "[argv][capability]")
{
    const nucleus::config_space space = make_nested_space();

    argv_source src(std::vector<std::string>{
        "--server-host=edge", "--server-tag=alpha", "--server-tag=beta"});
    src.recognize_with(nucleus::recognizer_of(space));

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(src)}, {});
    REQUIRE(loaded);
    const auto tags = loaded.value().get_all("server/tag");
    REQUIRE(tags == std::vector<std::string>{"alpha", "beta"});
}

// ---------------------------------------------------------------------------
// CLI ordinal addressing for repeated containers
// ---------------------------------------------------------------------------

namespace {

nucleus::xml_source xml_of_cluster(const std::string &text)
{
    return nucleus::xml_source::from(nucleus::xml_source_options::of_string(text));
}

// Schema: cluster -> node (repeated container) -> endpoint (container) -> port (leaf).
// Mirrors the schema from the plan.
nucleus::config_space make_cluster_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
        nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::repeated_element("node", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
        nucleus::element("endpoint", nucleus::anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
        nucleus::element("port", nucleus::anchor::keyspace("cluster/node/endpoint"))));
    return builder.build();
}

}

TEST_CASE("cli ordinal addressing -- argv override of indexed instance",
          "[argv][repeated_container]")
{
    const nucleus::config_space space = make_cluster_space();

    // XML base: two node instances (port 80 and 443).
    auto xml = xml_of_cluster(
        "<cluster>"
        "<node><endpoint><port>80</port></endpoint></node>"
        "<node><endpoint><port>443</port></endpoint></node>"
        "</cluster>");

    // argv override layer: --cluster-node-0-endpoint-port=90 targets node[0].
    argv_source argv(std::vector<std::string>{"--cluster-node-0-endpoint-port=90"});
    argv.recognize_with(nucleus::recognizer_of(space));

    auto loaded = nucleus::load_config(
        space,
        nucleus::source_stack{std::move(xml), std::move(argv)},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    // node[0] overridden by argv; node[1] unchanged from XML.
    REQUIRE(cfg.get("cluster/node[0]/endpoint/port") == "90");
    REQUIRE(cfg.get("cluster/node[1]/endpoint/port") == "443");
}

TEST_CASE("argv out-of-range ordinal -- loud error", "[argv][repeated_container]")
{
    const nucleus::config_space space = make_cluster_space();

    auto xml = xml_of_cluster(
        "<cluster>"
        "<node><endpoint><port>80</port></endpoint></node>"
        "<node><endpoint><port>443</port></endpoint></node>"
        "</cluster>");

    // ordinal 5 is far out of range (only 2 instances: 0 and 1).
    argv_source argv(std::vector<std::string>{"--cluster-node-5-endpoint-port=90"});
    argv.recognize_with(nucleus::recognizer_of(space));

    auto loaded = nucleus::load_config(
        space,
        nucleus::source_stack{std::move(xml), std::move(argv)},
        {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("out of range") != std::string::npos);
    // The error names the actual count of instances (2).
    REQUIRE(loaded.error().message.find('2') != std::string::npos);
}

TEST_CASE("cli ordinal digit run over 18 digits is rejected, not silently wrapped",
          "[argv][repeated_container]")
{
    const nucleus::config_space space = make_cluster_space();

    auto xml = xml_of_cluster(
        "<cluster>"
        "<node><endpoint><port>80</port></endpoint></node>"
        "</cluster>");

    // 19 digits: one past the 18-digit cap key_path::is_indexed_segment also enforces.
    argv_source argv(std::vector<std::string>{
        "--cluster-node-9999999999999999999-endpoint-port=90"});
    argv.recognize_with(nucleus::recognizer_of(space));

    auto loaded = nucleus::load_config(
        space,
        nucleus::source_stack{std::move(xml), std::move(argv)},
        {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::malformed_source);
}

TEST_CASE("cli ordinal digit run at 18 digits is still accepted (boundary)",
          "[argv][repeated_container]")
{
    const nucleus::config_space space = make_cluster_space();

    auto xml = xml_of_cluster(
        "<cluster>"
        "<node><endpoint><port>80</port></endpoint></node>"
        "</cluster>");

    // 18 digits, out of instance range (only 1 exists) -- must fail on range,
    // not on the digit cap, proving the boundary itself is not rejected.
    argv_source argv(std::vector<std::string>{
        "--cluster-node-999999999999999999-endpoint-port=90"});
    argv.recognize_with(nucleus::recognizer_of(space));

    auto loaded = nucleus::load_config(
        space,
        nucleus::source_stack{std::move(xml), std::move(argv)},
        {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().code == nucleus::errc::schema_violation);
    REQUIRE(loaded.error().message.find("out of range") != std::string::npos);
}

TEST_CASE("argv ordinal == count is out of range -- cannot append",
          "[argv][repeated_container]")
{
    const nucleus::config_space space = make_cluster_space();

    auto xml = xml_of_cluster(
        "<cluster>"
        "<node><endpoint><port>80</port></endpoint></node>"
        "<node><endpoint><port>443</port></endpoint></node>"
        "</cluster>");

    // ordinal 2 == count (2 instances: 0 and 1): append is not allowed.
    argv_source argv(std::vector<std::string>{"--cluster-node-2-endpoint-port=90"});
    argv.recognize_with(nucleus::recognizer_of(space));

    auto loaded = nucleus::load_config(
        space,
        nucleus::source_stack{std::move(xml), std::move(argv)},
        {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("out of range") != std::string::npos);
    REQUIRE(loaded.error().message.find('2') != std::string::npos);
}

TEST_CASE("argv lower-rank than document with valid ordinal 0 succeeds",
          "[argv][repeated_container][WR03]")
{
    // argv has a LOWER rank than the XML document (it appears first in the stack).
    // With the old eager check, m_building is empty when argv is processed, so
    // instance_count = 0 and ordinal 0 is incorrectly rejected.
    const nucleus::config_space space = make_cluster_space();

    argv_source argv(std::vector<std::string>{"--cluster-node-0-endpoint-port=999"});
    argv.recognize_with(nucleus::recognizer_of(space));

    auto xml = xml_of_cluster(
        "<cluster>"
        "<node><endpoint><port>80</port></endpoint></node>"
        "<node><endpoint><port>443</port></endpoint></node>"
        "</cluster>");

    // argv (lower rank) is listed first, xml (higher rank) second.
    // The load must succeed: ordinal 0 is valid (2 instances: 0 and 1).
    // xml wins because it has higher rank, so port stays 80, not 999.
    auto loaded = nucleus::load_config(
        space,
        nucleus::source_stack{std::move(argv), std::move(xml)},
        {});
    REQUIRE(loaded);
    // XML wins (higher rank), port for node[0] is still 80.
    REQUIRE(loaded.value().get("cluster/node[0]/endpoint/port") == "80");
}
