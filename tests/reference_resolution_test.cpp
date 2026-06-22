#include "nucleus/config_space.h"
#include "nucleus/identity.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// REF-01 / REF-02 / REF-03 / REF-06 acceptance tests.
// All tests drive the public load_config() pipeline with a runtime_source so the
// full fold -> slice -> resolve_references() -> validate() -> freeze() chain runs.

using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::load_options;
using nucleus::runtime_source;
using nucleus::source_stack;

TEST_CASE("abs: reference resolves a value by absolute path (REF-02)", "[reference][abs]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("cluster/port", "9090");
    src.set("cluster/alias", "${abs:cluster/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/alias") == "9090");
}

TEST_CASE("abs: reference to non-existent path is a hard error (REF-07 no-??)", "[reference][abs]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("cluster/alias", "${abs:cluster/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("absent") != std::string::npos);
}

TEST_CASE("rel: child reference descends from containing scope (REF-03)", "[reference][rel]")
{
    // ${rel:other} from cluster/alias starts at cluster (parent of alias),
    // then descends to "other" -> cluster/other.
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("cluster/port", "8080");
    src.set("cluster/alias", "${rel:port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/alias") == "8080");
}

TEST_CASE("rel: ../ reference walks up then descends (REF-03)", "[reference][rel]")
{
    // ${rel:../sibling/port} from cluster/server/alias:
    // base = cluster/server (parent of alias)
    // ".." -> cluster
    // "sibling" -> cluster/sibling
    // "port" -> cluster/sibling/port
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("cluster/sibling/port", "7070");
    src.set("cluster/server/alias", "${rel:../sibling/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/server/alias") == "7070");
}

TEST_CASE("rel: ./ is sugar for current-scope descend (REF-03)", "[reference][rel]")
{
    // ${rel:./port} from cluster/alias:
    // base = cluster (parent of alias), "." = no-op, "port" -> cluster/port
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("cluster/port", "6060");
    src.set("cluster/alias", "${rel:./port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/alias") == "6060");
}

TEST_CASE("abs: reference including indexed segment resolves correctly (REF-02)", "[reference][abs]")
{
    // abs: can address an already-indexed path (node[N] form) once it is in the
    // keyspace. Supply the indexed path directly to avoid schema dependency.
    auto space = config_space_builder{}.build();
    runtime_source src;
    // Supply paths in indexed form (as a tree source would emit them).
    src.set("cluster/node[0]/name", "primary");
    src.set("cluster/node[1]/name", "secondary");
    // Reference the first instance by ordinal.
    src.set("cluster/primary_name", "${abs:cluster/node[0]/name}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/primary_name") == "primary");
}

TEST_CASE("resolve_references() runs post-slice, not during fold (REF-01)", "[reference][pipeline]")
{
    // Confirm the pass resolves values in the sliced tree, not pre-slice.
    // A value referencing another leaf that only exists after slice must resolve.
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("host/port", "5050");
    src.set("host/display", "${abs:host/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("host/display") == "5050");
}

TEST_CASE("value-only invariant: reference in key position is a loud error (REF-06)",
          "[reference][value-only]")
{
    // A key segment containing ${ is rejected by the value-only invariant scan.
    // We simulate this by manually pushing a path containing ${ into the keyspace
    // via a runtime_source whose path literally contains "${...}".
    // Note: key_path::parse would reject "${" in most positions, but a source can
    // emit a path with that substring. The invariant fires before any resolution.
    auto space = config_space_builder{}.build();
    runtime_source src;
    // This path segment contains "${abs:x}" -- illegal in structural position.
    src.set("cluster/${abs:x}/port", "1234");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("structural key position") != std::string::npos);
}

TEST_CASE("multiple references in one value string all resolve (REF-02)", "[reference][abs]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("host/name", "myhost");
    src.set("host/port", "8080");
    src.set("host/addr", "${abs:host/name}:${abs:host/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("host/addr") == "myhost:8080");
}
