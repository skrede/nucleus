#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/strain_scope.h"
#include "nucleus/config.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Isolated coverage for the free load(space, source_stack, load_options) front door,
// its load_options knobs (selection, scope), and the capability gate (hard-abort-named
// / soft-degrade). All assertions go directly through the new API; the legacy adapter
// is never called.

using nucleus::anchor;
using nucleus::load_options;
using nucleus::source_stack;
using nucleus::strain_scope_policy;

namespace {

// A source that declares NO capabilities: nesting / duplicate_keys / typed_scalars
// are all absent. Used to prove a hard-unmet requirement aborts the load.
struct flat_only_source
{
    nucleus::capability_descriptor capabilities() const { return {}; }

    nucleus::config_source_result pull()
    {
        return nucleus::config_source_batch{};
    }
};

// A space whose schema requires nesting (a keyed container) and optionally uses
// typed_scalars (a typed leaf). Produces a HARD nesting requirement and a SOFT
// typed_scalars requirement via derive_capability_requirements.
nucleus::config_space make_nested_typed_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("node", anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("node"))));
    REQUIRE(builder.register_element(
        nucleus::typed_element<int>("port", anchor::keyspace("node"))));
    return builder.build();
}

// Registers a cluster/node keyed container with primary key "name" and leaf "port",
// plus a general "app/label" element so scope policies can be distinguished.
void declare_cluster(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("node", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/node"))));
    REQUIRE(engine.register_element(nucleus::element("port", anchor::keyspace("cluster/node"))));
    REQUIRE(engine.register_element(nucleus::element("app", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("label", anchor::keyspace("app"))));
}

}

// ---------------------------------------------------------------------------
// last-listed-wins: two runtime_sources with a contested key
// ---------------------------------------------------------------------------

TEST_CASE("load with a two-source stack: last-listed source wins a same-key contest",
          "[load_front_door]")
{
    nucleus::config_space space = nucleus::config_space_builder{}.build();

    nucleus::runtime_source base;
    base.set("server/host", "base-host").set("server/port", "80");

    nucleus::runtime_source overlay;
    overlay.set("server/port", "8080");

    auto loaded = nucleus::load_config(space, source_stack{base, overlay}, {});
    REQUIRE(loaded);

    // The last-listed (overlay) source wins the contested "server/port".
    REQUIRE(loaded.value().get("server/port") == "8080");
    // The uncontested key from the first source survives.
    REQUIRE(loaded.value().get("server/host") == "base-host");
}

// ---------------------------------------------------------------------------
// load_options.selection: explicit strain selection observably changes the result
// ---------------------------------------------------------------------------

TEST_CASE("load_options.selection picks the named strain from a keyed container",
          "[load_front_door][knobs]")
{
    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = engine.build();

    nucleus::runtime_source src;
    src.set("cluster/node/alpha/name", "alpha")
       .set("cluster/node/alpha/port", "1000")
       .set("cluster/node/beta/name", "beta")
       .set("cluster/node/beta/port", "2000");

    // Without selection: two strains, the load must fail (or auto-select if single).
    // Here we explicitly select "alpha".
    load_options opts_alpha;
    opts_alpha.selection = "alpha";
    auto with_alpha = nucleus::load_config(space, source_stack{src}, opts_alpha);
    REQUIRE(with_alpha);
    REQUIRE(with_alpha.value().get("cluster/node/port") == "1000");

    // Selecting "beta" gives the other port.
    load_options opts_beta;
    opts_beta.selection = "beta";
    auto with_beta = nucleus::load_config(space, source_stack{src}, opts_beta);
    REQUIRE(with_beta);
    REQUIRE(with_beta.value().get("cluster/node/port") == "2000");
}

// ---------------------------------------------------------------------------
// load_options.scope: the scope policy observably gates container entries
// ---------------------------------------------------------------------------

TEST_CASE("load_options.scope = file_level excludes container entries above the defining layer",
          "[load_front_door][knobs]")
{
    nucleus::config_space_builder engine;
    declare_cluster(engine);
    nucleus::config_space space = engine.build();

    // L0 (rank 0): defines the alpha strain with its port; Ld=0.
    // L1 (rank 1): adds a DISTINCT container key (not overriding L0's port) plus a
    //              general entry. Under file_level, both are excluded (rank > Ld).
    //              Under default, only the general entry is admitted.
    nucleus::runtime_source L0, L1;
    L0.set("cluster/node/alpha/name", "alpha")
      .set("cluster/node/alpha/port", "9000"); // defining layer for alpha
    L1.set("app/label", "core");              // general entry at rank > Ld

    load_options file_level_opts;
    file_level_opts.selection = "alpha";
    file_level_opts.scope = strain_scope_policy::file_level;

    auto loaded = nucleus::load_config(space, source_stack{L0, L1}, file_level_opts);
    REQUIRE(loaded);

    // Port from L0 (rank 0 = Ld) survives under file_level.
    REQUIRE(loaded.value().get("cluster/node/port") == "9000");
    // The general entry at rank > Ld is excluded under file_level.
    REQUIRE_FALSE(loaded.value().contains("app/label"));

    // Default (space_open_container_closed) admits the general entry from L1.
    load_options default_opts;
    default_opts.selection = "alpha";
    auto with_default = nucleus::load_config(space, source_stack{L0, L1}, default_opts);
    REQUIRE(with_default);
    REQUIRE(with_default.value().get("app/label") == "core");
    REQUIRE(with_default.value().get("cluster/node/port") == "9000");
}

// ---------------------------------------------------------------------------
// Capability gate: HARD-unmet requirement aborts the load and names both parties
// ---------------------------------------------------------------------------

TEST_CASE("load with a flat-only stack fails a schema requiring nesting, naming both parties",
          "[load_front_door][gate]")
{
    nucleus::config_space space = make_nested_typed_space();

    // flat_only_source declares no capabilities; the nested schema requires nesting (HARD).
    auto loaded = nucleus::load_config(space, source_stack{flat_only_source{}}, {});
    REQUIRE_FALSE(loaded);

    const std::string &msg = loaded.error().message;
    // Both the capability name and the stack layer label must appear.
    REQUIRE(msg.find("nesting") != std::string::npos);
    REQUIRE(msg.find("stack[0]") != std::string::npos);

    // check_capabilities must agree with load's verdict.
    auto preflight = nucleus::check_capabilities(space, source_stack{flat_only_source{}}, {});
    REQUIRE_FALSE(preflight);
    REQUIRE(preflight.error().message == msg);
}

TEST_CASE("load with a capable stack satisfies the HARD nesting requirement",
          "[load_front_door][gate]")
{
    nucleus::config_space space = make_nested_typed_space();

    // runtime_source declares nesting + duplicate_keys + typed_scalars: all requirements met.
    nucleus::runtime_source src;
    src.set("node/alpha/name", "alpha")
       .set("node/alpha/port", "42");

    load_options opts;
    opts.selection = "alpha";
    auto loaded = nucleus::load_config(space, source_stack{src}, opts);
    REQUIRE(loaded);

    // The preflight must agree: nesting is honored.
    auto preflight = nucleus::check_capabilities(space, source_stack{src}, {});
    REQUIRE(preflight);
    bool nesting_honored = false;
    for(nucleus::capability cap : preflight.value().honored)
        if(cap == nucleus::capability::nesting)
            nesting_honored = true;
    REQUIRE(nesting_honored);
}

// ---------------------------------------------------------------------------
// Capability gate: SOFT-absent optional capability degrades without aborting
// ---------------------------------------------------------------------------

TEST_CASE("load degrades a SOFT-absent optional capability and does not abort",
          "[load_front_door][gate]")
{
    // Build a space with only a flat element: the flat schema derives NO capability
    // requirements, so use a nested schema to get the typed_scalars SOFT requirement.
    // The typed leaf is optional (SOFT), so a flat-only stack must degrade, not abort.
    nucleus::config_space space = make_nested_typed_space();

    // A source that provides nesting (satisfies HARD) but not typed_scalars (SOFT absent).
    struct nesting_no_typed
    {
        nucleus::capability_descriptor capabilities() const
        {
            return nucleus::capability_descriptor{nucleus::capability::nesting};
        }

        nucleus::config_source_result pull()
        {
            nucleus::config_source_batch batch;
            batch.entries.push_back(nucleus::make_entry(
                "node/alpha/name", nucleus::value::owned("alpha"), capabilities()));
            batch.entries.push_back(nucleus::make_entry(
                "node/alpha/port", nucleus::value::owned("99"), capabilities()));
            return batch;
        }
    };

    load_options opts;
    opts.selection = "alpha";
    auto loaded = nucleus::load_config(space, source_stack{nesting_no_typed{}}, opts);
    // SOFT degradation: the load must not abort.
    REQUIRE(loaded);

    // check_capabilities must agree: soft capability is in degraded, not honored.
    auto preflight = nucleus::check_capabilities(space, source_stack{nesting_no_typed{}}, {});
    REQUIRE(preflight);

    bool typed_degraded = false;
    for(const auto &d : preflight.value().degraded)
        if(d.cap == nucleus::capability::typed_scalars)
            typed_degraded = true;
    REQUIRE(typed_degraded);
}

TEST_CASE("the stack is borrowed: pre-flight then load then load again, one stack",
          "[load][stack]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("host", anchor::keyspace("server"))));
    const nucleus::config_space space = builder.build();

    nucleus::runtime_source src;
    src.set("server/host", "localhost");
    source_stack stack{std::move(src)};

    REQUIRE(nucleus::check_capabilities(space, stack, {}));

    auto first = nucleus::load_config(space, stack, {});
    REQUIRE(first);
    REQUIRE(first.value().get("server/host") == "localhost");

    // The same stack loads again: nothing was consumed by the first load.
    auto second = nucleus::load_config(space, stack, {});
    REQUIRE(second);
    REQUIRE(second.value().get("server/host") == "localhost");
}
