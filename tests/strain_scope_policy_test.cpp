#include "nucleus/configuration_space.h"

#include "nucleus/entry/precedence.h"
#include "nucleus/entry/strain_scope.h"
#include "nucleus/entry/configuration.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "support/capable_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Composition-scope policy tests: capable (path -> value) feeders are placed at explicit
// numeric ranks (via the per-load custom layers) to simulate a three-layer
// scenario without requiring XML. The schema declares a cluster/server keyed
// container plus a top-level app/name element so the three policies can be
// distinguished. The selection and scope policy are now per-load parameters on
// source_stack_options, not registrations.

using nucleus::anchor;
using nucleus::strain_scope_policy;

namespace {

// Registers a cluster/server keyed container with primary key "name", leaves
// "port" and "protocol", plus a general "app/name" element at the root.
void declare_cluster_with_app(nucleus::configuration_space_builder &engine)
{
    engine.register_element(nucleus::element("cluster", anchor::root()));
    engine.register_element(nucleus::element("server", anchor::keyspace("cluster")));
    engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server")));
    engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server")));
    engine.register_element(nucleus::element("protocol", anchor::keyspace("cluster/server")));
    engine.register_element(nucleus::element("app", anchor::root()));
    engine.register_element(nucleus::element("name", anchor::keyspace("app")));
}

// Borrows one source at an explicit rank into the per-load options.
void add_layer(nucleus::source_stack_options &opts, nucleus::configuration_source &src,
               std::size_t rank, std::string label)
{
    opts.custom_layers.push_back(
        nucleus::configuration_source_layer{&src, rank, std::move(label), {}});
}

// Builds the per-load options layering three sources:
//   rank=10: web/port=80 (web's defining layer; Ld=10)
//   rank=20: web/protocol=tcp, app/name=core (derived layer above Ld; general entry)
//   rank=30: db/port=5432 (competing strain first appears; Ls=30)
nucleus::source_stack_options three_layer_opts(nucleus::testing::capable_source &L0,
                                               nucleus::testing::capable_source &Lderived,
                                               nucleus::testing::capable_source &Lcompeting)
{
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lderived.set("cluster/server/web/protocol", "tcp")
            .set("app/name", "core");
    Lcompeting.set("cluster/server/db/name", "db")
              .set("cluster/server/db/port", "5432");

    nucleus::source_stack_options opts;
    add_layer(opts, L0, 10, "L0");
    add_layer(opts, Lderived, 20, "Lderived");
    add_layer(opts, Lcompeting, 30, "Lcompeting");
    return opts;
}

}

TEST_CASE("default policy (space-open container-closed) excludes container entries above "
          "the defining layer but admits general entries",
          "[scope_policy][keyed]")
{
    nucleus::testing::capable_source L0, Lderived, Lcompeting;
    nucleus::source_stack_options opts = three_layer_opts(L0, Lderived, Lcompeting);
    opts.selection = "web";
    // No scope override -- default is space_open_container_closed.

    nucleus::configuration_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = nucleus::load_configuration(space, opts);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // web/port=80 is at Ld=10; survives under all policies.
    REQUIRE(config.get("cluster/server/port") == "80");

    // web/protocol=tcp is at rank=20 > Ld=10; excluded (container, above Ld).
    REQUIRE_FALSE(config.contains("cluster/server/protocol"));

    // app/name=core is a general entry at rank=20; admitted (general entries
    // are unconstrained under space_open_container_closed).
    REQUIRE(config.get("app/name") == "core");

    // db entries are pruned entirely (non-selected strain).
    REQUIRE_FALSE(config.contains("cluster/server/db/port"));
}

TEST_CASE("file_level policy excludes container entries and general entries above the "
          "defining layer",
          "[scope_policy][keyed]")
{
    nucleus::testing::capable_source L0, Lderived, Lcompeting;
    nucleus::source_stack_options opts = three_layer_opts(L0, Lderived, Lcompeting);
    opts.selection = "web";
    opts.scope = strain_scope_policy::file_level;

    nucleus::configuration_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = nucleus::load_configuration(space, opts);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // web/port=80 is at Ld=10; survives.
    REQUIRE(config.get("cluster/server/port") == "80");

    // web/protocol=tcp is at rank=20 > Ld=10; excluded (container, above Ld).
    REQUIRE_FALSE(config.contains("cluster/server/protocol"));

    // app/name=core is at rank=20 > Ld=10; excluded (general entry cut by file_level).
    REQUIRE_FALSE(config.contains("app/name"));
}

TEST_CASE("container_open_until_next_strain admits container entries below Ls and general "
          "entries",
          "[scope_policy][keyed]")
{
    nucleus::testing::capable_source L0, Lderived, Lcompeting;
    nucleus::source_stack_options opts = three_layer_opts(L0, Lderived, Lcompeting);
    opts.selection = "web";
    opts.scope = strain_scope_policy::container_open_until_next_strain;

    nucleus::configuration_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = nucleus::load_configuration(space, opts);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // web/port=80 is at rank=10 < Ls=30; survives.
    REQUIRE(config.get("cluster/server/port") == "80");

    // web/protocol=tcp is at rank=20 < Ls=30; admitted (container, below Ls).
    REQUIRE(config.get("cluster/server/protocol") == "tcp");

    // app/name=core is a general entry; unconstrained under
    // container_open_until_next_strain.
    REQUIRE(config.get("app/name") == "core");

    // db entries are pruned entirely (non-selected strain).
    REQUIRE_FALSE(config.contains("cluster/server/db/port"));
}

TEST_CASE("container_open_until_next_strain with no competing strain is fully open for "
          "the container",
          "[scope_policy][keyed]")
{
    // Only web strain at ranks 10 and 20; no competing strain at all.
    nucleus::testing::capable_source L0, Lderived;
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lderived.set("cluster/server/web/protocol", "tcp");

    nucleus::source_stack_options opts;
    add_layer(opts, L0, 10, "L0");
    add_layer(opts, Lderived, 20, "Lderived");
    opts.selection = "web";
    opts.scope = strain_scope_policy::container_open_until_next_strain;

    nucleus::configuration_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = nucleus::load_configuration(space, opts);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // Ls = unbounded (no competing strain); all container entries compose.
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE(config.get("cluster/server/protocol") == "tcp");
}

TEST_CASE("a competing strain introduced below the defining layer never bounds the "
          "selected strain",
          "[scope_policy][keyed]")
{
    // db is introduced at rank=10, BELOW web's defining layer at rank=30: db is
    // not the "next" strain after web's file, so Ls stays unbounded and every
    // web entry survives under container_open_until_next_strain.
    nucleus::testing::capable_source Learly, Lweb, Lderived;
    Learly.set("cluster/server/db/name", "db")
          .set("cluster/server/db/port", "5432");
    Lweb.set("cluster/server/web/name", "web")
        .set("cluster/server/web/port", "80");
    Lderived.set("cluster/server/web/protocol", "tcp");

    nucleus::source_stack_options opts;
    add_layer(opts, Learly, 10, "Learly");
    add_layer(opts, Lweb, 30, "Lweb");
    add_layer(opts, Lderived, 40, "Lderived");
    opts.selection = "web";
    opts.scope = strain_scope_policy::container_open_until_next_strain;

    nucleus::configuration_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = nucleus::load_configuration(space, opts);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // The selected strain survives in full: Ld=30, no competitor above Ld, so
    // Ls is unbounded and the rank-40 derived entry composes too.
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE(config.get("cluster/server/protocol") == "tcp");

    // The competing strain is pruned as usual.
    REQUIRE_FALSE(config.contains("cluster/server/db/port"));
}

TEST_CASE("Ls is bound by the layer that INTRODUCES the competing strain, not the "
          "layer that last overwrote it",
          "[scope_policy][keyed]")
{
    // db is introduced at rank=30 and its only entry overwritten at rank=50.
    // web's container entry at rank=40 sits between: it must be EXCLUDED, since
    // the composable window ends at the layer that introduced db (Ls=30), not
    // at the overwriting layer (50).
    nucleus::testing::capable_source L0, Lcompeting, Lbetween, Loverwrite;
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lcompeting.set("cluster/server/db/name", "db")
              .set("cluster/server/db/port", "5432");
    Lbetween.set("cluster/server/web/protocol", "tcp");
    Loverwrite.set("cluster/server/db/port", "5433");

    nucleus::source_stack_options opts;
    add_layer(opts, L0, 10, "L0");
    add_layer(opts, Lcompeting, 30, "Lcompeting");
    add_layer(opts, Lbetween, 40, "Lbetween");
    add_layer(opts, Loverwrite, 50, "Loverwrite");
    opts.selection = "web";
    opts.scope = strain_scope_policy::container_open_until_next_strain;

    nucleus::configuration_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = nucleus::load_configuration(space, opts);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // web/port=80 at rank=10 < Ls=30; survives.
    REQUIRE(config.get("cluster/server/port") == "80");

    // web/protocol at rank=40 >= Ls=30; excluded despite the rank-50 overwrite
    // of db's entry.
    REQUIRE_FALSE(config.contains("cluster/server/protocol"));
}

TEST_CASE("scope policies apply when the single named strain auto-resolves",
          "[scope_policy][keyed]")
{
    // No selection: web is the only named strain and auto-resolves. The default
    // policy must behave exactly as it does under an explicit selection of "web".
    nucleus::testing::capable_source L0, Lderived;
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lderived.set("cluster/server/web/protocol", "tcp")
            .set("app/name", "core");

    nucleus::source_stack_options opts;
    add_layer(opts, L0, 10, "L0");
    add_layer(opts, Lderived, 20, "Lderived");
    // No selection, no scope override -- default space_open_container_closed.

    nucleus::configuration_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = nucleus::load_configuration(space, opts);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("cluster/server/protocol"));
    REQUIRE(config.get("app/name") == "core");
}

TEST_CASE("file_level applies on auto-resolve and cuts general entries above the "
          "defining layer",
          "[scope_policy][keyed]")
{
    nucleus::testing::capable_source L0, Lderived;
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lderived.set("app/name", "core");

    nucleus::source_stack_options opts;
    add_layer(opts, L0, 10, "L0");
    add_layer(opts, Lderived, 20, "Lderived");
    opts.scope = strain_scope_policy::file_level;
    // No selection: the single named strain auto-resolves with the policy active.

    nucleus::configuration_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::configuration_space space = engine.build();

    auto loaded = nucleus::load_configuration(space, opts);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // The world as web's file saw it: the rank-20 general entry is gone.
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("app/name"));
}
