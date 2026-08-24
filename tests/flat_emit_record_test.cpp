#include "nucleus/config.h"
#include "builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/cli_flag.h"

#include "nucleus/config_source/source_stack.h"

#include "nucleus/env/env_emitter.h"

#include "nucleus/argv/argv_emitter.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <array>
#include <string>
#include <vector>
#include <utility>

namespace {

const std::array<std::string, 3> unrenderable{"bad=key", "bad\rkey", "bad\nkey"};

// The one unrenderable key a schema can still declare: a name carrying a carriage
// return or a newline is refused at registration, so '=' is the only delimiter that
// reaches a renderer through a declared path.
const std::string declarable{"bad=key"};

nucleus::config checked_config(std::map<std::string, std::string> values)
{
    auto made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}

nucleus::config_space space_beside(const std::string &outside)
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("port", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(nucleus::element(outside, nucleus::anchor::root())));
    return nucleus::builder_result_test::built(builder);
}

nucleus::config_space annotated_space(std::vector<std::string> allowed)
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(nucleus::enum_element("port", nucleus::anchor::keyspace("cluster"), std::move(allowed))));
    return nucleus::builder_result_test::built(builder);
}

// A key carrying a forbidden byte reaches a checked config the way a host's data
// does -- through a declared schema path and a production source -- so the emit
// defense stays reachable independently of what raw construction accepts.
nucleus::config resolved_config(const std::string &outside)
{
    nucleus::runtime_source source;
    source.set("cluster/port", "8000").set(outside, "value");
    auto loaded = nucleus::load_config(
            space_beside(outside), nucleus::source_stack{std::move(source)}, {});
    REQUIRE(loaded);
    return std::move(loaded).value();
}

}

TEST_CASE("both flat renderers reject a resolved key carrying record delimiters",
          "[flat][emit][argv][env][record]")
{
    const nucleus::config config = resolved_config(declarable);

    const auto argv = nucleus::argv::render_document(config);
    REQUIRE_FALSE(argv);
    REQUIRE(argv.error().code == nucleus::errc::malformed_source);
    REQUIRE(argv.error().message.find(declarable) != std::string::npos);

    const auto environment = nucleus::env::render_document(config);
    REQUIRE_FALSE(environment);
    REQUIRE(environment.error().code == nucleus::errc::malformed_source);
    REQUIRE(environment.error().message.find(declarable) != std::string::npos);
}

TEST_CASE("an unrenderable key outside the anchor never blocks emission",
          "[flat][emit][argv][anchor][record]")
{
    const auto selected = nucleus::argv::render_document(
            resolved_config(declarable), {},
            nucleus::key_path(std::vector<std::string>{"cluster"}));
    REQUIRE(selected);
    REQUIRE(selected.value() == "--port=8000\n");
}

TEST_CASE("selected values keep every '=' and reject line breaks",
          "[flat][emit][argv][env][record]")
{
    const nucleus::config config = checked_config({{"cluster/port", "a=b=c"}});
    const auto            argv   = nucleus::argv::render_document(config);
    REQUIRE(argv);
    REQUIRE(argv.value() == "--cluster-port=a=b=c\n");
    const auto environment = nucleus::env::render_document(config);
    REQUIRE(environment);
    REQUIRE(environment.value() == "cluster/port=a=b=c\n");

    for(const char *broken : {"a\rb", "a\nb"})
    {
        const auto rejected = nucleus::argv::render_document(checked_config({{"cluster/port", broken}}));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error().code == nucleus::errc::malformed_source);
        REQUIRE(rejected.error().message.find("cluster/port") != std::string::npos);
    }
}

TEST_CASE("template keys and allowed-value annotations obey the record grammar",
          "[flat][emit][argv][env][template][record]")
{
    const auto valid = nucleus::argv::render_template(annotated_space({"fast", "slow"}));
    REQUIRE(valid);
    REQUIRE(valid.value() == "--cluster-port= # allowed: fast|slow\n");

    for(const char *broken : {"a\rb", "a\nb"})
    {
        const auto rejected = nucleus::argv::render_template(annotated_space({"fast", broken}));
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error().code == nucleus::errc::malformed_source);
    }

    const auto unspellable = nucleus::env::render_template(space_beside(declarable));
    REQUIRE_FALSE(unspellable);
    REQUIRE(unspellable.error().code == nucleus::errc::malformed_source);
    REQUIRE(unspellable.error().message.find(declarable) != std::string::npos);
}

TEST_CASE("an argv space name obeys the same key grammar",
          "[flat][emit][argv][record]")
{
    const nucleus::config       config = checked_config({{"cluster/port", "8000"}});
    const nucleus::config_space space  = annotated_space({});

    const auto accepted = nucleus::argv::render_document(config, {}, {}, "cfg");
    REQUIRE(accepted);
    REQUIRE(accepted.value() == "--cfg-cluster-port=8000\n");
    const auto blueprint = nucleus::argv::render_template(space, {}, {}, "cfg");
    REQUIRE(blueprint);
    REQUIRE(blueprint.value() == "--cfg-cluster-port=\n");

    for(const std::string &space_name : unrenderable)
    {
        const auto document = nucleus::argv::render_document(config, {}, {}, space_name);
        REQUIRE_FALSE(document);
        REQUIRE(document.error().code == nucleus::errc::malformed_source);
        REQUIRE(document.error().message.find(space_name) != std::string::npos);

        const auto blocked = nucleus::argv::render_template(space, {}, {}, space_name);
        REQUIRE_FALSE(blocked);
        REQUIRE(blocked.error().code == nucleus::errc::malformed_source);
        REQUIRE(blocked.error().message.find(space_name) != std::string::npos);

        const nucleus::argv::emitter carrier{{}, {}, space_name};
        REQUIRE_FALSE(carrier.render_document(config, space));
        REQUIRE_FALSE(carrier.render_template(space));
    }
}

TEST_CASE("a CLI delimiter rejects line breaks and keeps its prior grammar",
          "[cli][delimiter][record]")
{
    for(const char *accepted : {"-", "__", "/", "."})
        REQUIRE(nucleus::cli_delimiter::parse(accepted));

    for(const char *rejected : {"", "=", "a=b", "a/b", "[", "]", "12", "\n", "\r", "a\nb", "a\rb"})
        REQUIRE_FALSE(nucleus::cli_delimiter::parse(rejected));
}

TEST_CASE("raw construction rejects universally unrenderable keys",
          "[config][construction][record]")
{
    for(const std::string &key : unrenderable)
    {
        const auto made = nucleus::config::from_values({{key, "value"}});
        REQUIRE_FALSE(made);
        REQUIRE(made.error().code == nucleus::errc::malformed_source);
        REQUIRE(made.error().message.find(key) != std::string::npos);
    }

    REQUIRE(nucleus::config::from_values({{"cluster/node[1]/port", "a=b=c"}}));
    REQUIRE(nucleus::config::from_values({{"cluster.node/port", "="}}));
}
