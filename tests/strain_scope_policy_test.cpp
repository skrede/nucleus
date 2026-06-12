#include "nucleus/config_space.h"

#include "nucleus/strain_scope.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Composition-scope policy tests: capable (path -> value) feeders are placed in
// explicit source_stack positions to simulate a three-layer scenario without requiring
// XML. The schema declares a cluster/server keyed container plus a top-level app/name
// element so the three policies can be distinguished. The selection and scope policy
// are per-load parameters on load_options.

using nucleus::anchor;
using nucleus::strain_scope_policy;

namespace {

// Registers a cluster/server keyed container with primary key "name", leaves
// "port" and "protocol", plus a general "app/name" element at the root.
void declare_cluster_with_app(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(nucleus::element("port", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(nucleus::element("protocol", anchor::keyspace("cluster/server"))));
    REQUIRE(engine.register_element(nucleus::element("app", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("name", anchor::keyspace("app"))));
}

// Initializes the three sources for the standard three-layer scenario:
//   stack[0]: web/port=80 (web's defining layer)
//   stack[1]: web/protocol=tcp, app/name=core (derived layer above defining layer)
//   stack[2]: db/port=5432 (competing strain first appears)
void setup_three_layer(nucleus::runtime_source &L0,
                       nucleus::runtime_source &Lderived,
                       nucleus::runtime_source &Lcompeting)
{
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lderived.set("cluster/server/web/protocol", "tcp")
            .set("app/name", "core");
    Lcompeting.set("cluster/server/db/name", "db")
              .set("cluster/server/db/port", "5432");
}

}

TEST_CASE("default policy (space-open container-closed) excludes container entries above "
          "the defining layer but admits general entries",
          "[scope_policy][keyed]")
{
    nucleus::runtime_source L0, Lderived, Lcompeting;
    setup_three_layer(L0, Lderived, Lcompeting);

    nucleus::config_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::config_space space = engine.build();

    // L0(stack[0]) < Lderived(stack[1]) < Lcompeting(stack[2]); no scope override.
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(L0), std::move(Lderived), std::move(Lcompeting)},
        nucleus::load_options{.selection = "web"});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // web/port=80 is at stack[0]; survives under all policies.
    REQUIRE(config.get("cluster/server/port") == "80");

    // web/protocol=tcp is at stack[1] > stack[0]; excluded (container, above Ld).
    REQUIRE_FALSE(config.contains("cluster/server/protocol"));

    // app/name=core is a general entry at stack[1]; admitted (general entries
    // are unconstrained under space_open_container_closed).
    REQUIRE(config.get("app/name") == "core");

    // db entries are pruned entirely (non-selected strain).
    REQUIRE_FALSE(config.contains("cluster/server/db/port"));
}

TEST_CASE("file_level policy excludes container entries and general entries above the "
          "defining layer",
          "[scope_policy][keyed]")
{
    nucleus::runtime_source L0, Lderived, Lcompeting;
    setup_three_layer(L0, Lderived, Lcompeting);

    nucleus::config_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::config_space space = engine.build();

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(L0), std::move(Lderived), std::move(Lcompeting)},
        nucleus::load_options{.selection = "web", .scope = strain_scope_policy::file_level});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // web/port=80 is at stack[0]; survives.
    REQUIRE(config.get("cluster/server/port") == "80");

    // web/protocol=tcp is at stack[1] > stack[0]; excluded (container, above Ld).
    REQUIRE_FALSE(config.contains("cluster/server/protocol"));

    // app/name=core is at stack[1] > stack[0]; excluded (general entry cut by file_level).
    REQUIRE_FALSE(config.contains("app/name"));
}

TEST_CASE("container_open_until_next_strain admits container entries below Ls and general "
          "entries",
          "[scope_policy][keyed]")
{
    nucleus::runtime_source L0, Lderived, Lcompeting;
    setup_three_layer(L0, Lderived, Lcompeting);

    nucleus::config_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::config_space space = engine.build();

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(L0), std::move(Lderived), std::move(Lcompeting)},
        nucleus::load_options{
            .selection = "web",
            .scope = strain_scope_policy::container_open_until_next_strain});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // web/port=80 is at stack[0] < Ls=stack[2]; survives.
    REQUIRE(config.get("cluster/server/port") == "80");

    // web/protocol=tcp is at stack[1] < Ls=stack[2]; admitted (container, below Ls).
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
    // Only web strain at two layers; no competing strain at all.
    nucleus::runtime_source L0, Lderived;
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lderived.set("cluster/server/web/protocol", "tcp");

    nucleus::config_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::config_space space = engine.build();

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(L0), std::move(Lderived)},
        nucleus::load_options{
            .selection = "web",
            .scope = strain_scope_policy::container_open_until_next_strain});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // Ls = unbounded (no competing strain); all container entries compose.
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE(config.get("cluster/server/protocol") == "tcp");
}

TEST_CASE("a competing strain introduced below the defining layer never bounds the "
          "selected strain",
          "[scope_policy][keyed]")
{
    // db is introduced at stack[0], BELOW web's defining layer at stack[1]: db is
    // not the "next" strain after web's layer, so Ls stays unbounded and every
    // web entry survives under container_open_until_next_strain.
    nucleus::runtime_source Learly, Lweb, Lderived;
    Learly.set("cluster/server/db/name", "db")
          .set("cluster/server/db/port", "5432");
    Lweb.set("cluster/server/web/name", "web")
        .set("cluster/server/web/port", "80");
    Lderived.set("cluster/server/web/protocol", "tcp");

    nucleus::config_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::config_space space = engine.build();

    // Learly(stack[0]) < Lweb(stack[1]) < Lderived(stack[2]).
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(Learly), std::move(Lweb), std::move(Lderived)},
        nucleus::load_options{
            .selection = "web",
            .scope = strain_scope_policy::container_open_until_next_strain});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // The selected strain survives in full: Ld=stack[1], no competitor above Ld, so
    // Ls is unbounded and the stack[2] derived entry composes too.
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE(config.get("cluster/server/protocol") == "tcp");

    // The competing strain is pruned as usual.
    REQUIRE_FALSE(config.contains("cluster/server/db/port"));
}

TEST_CASE("Ls is bound by the layer that INTRODUCES the competing strain, not the "
          "layer that last overwrote it",
          "[scope_policy][keyed]")
{
    // db is introduced at stack[1] and its only entry overwritten at stack[3].
    // web's container entry at stack[2] sits between: it must be EXCLUDED, since
    // the composable window ends at the layer that introduced db (Ls=stack[1]).
    nucleus::runtime_source L0, Lcompeting, Lbetween, Loverwrite;
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lcompeting.set("cluster/server/db/name", "db")
              .set("cluster/server/db/port", "5432");
    Lbetween.set("cluster/server/web/protocol", "tcp");
    Loverwrite.set("cluster/server/db/port", "5433");

    nucleus::config_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::config_space space = engine.build();

    // L0(stack[0]) < Lcompeting(stack[1]) < Lbetween(stack[2]) < Loverwrite(stack[3]).
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(L0), std::move(Lcompeting),
                              std::move(Lbetween), std::move(Loverwrite)},
        nucleus::load_options{
            .selection = "web",
            .scope = strain_scope_policy::container_open_until_next_strain});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // web/port=80 at stack[0] < Ls=stack[1]; survives.
    REQUIRE(config.get("cluster/server/port") == "80");

    // web/protocol at stack[2] >= Ls=stack[1]; excluded despite the stack[3] overwrite
    // of db's entry.
    REQUIRE_FALSE(config.contains("cluster/server/protocol"));
}

TEST_CASE("scope policies apply when the single named strain auto-resolves",
          "[scope_policy][keyed]")
{
    // No selection: web is the only named strain and auto-resolves. The default
    // policy must behave exactly as it does under an explicit selection of "web".
    nucleus::runtime_source L0, Lderived;
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lderived.set("cluster/server/web/protocol", "tcp")
            .set("app/name", "core");

    nucleus::config_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::config_space space = engine.build();

    // No selection, no scope override -- default space_open_container_closed.
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(L0), std::move(Lderived)},
        {});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("cluster/server/protocol"));
    REQUIRE(config.get("app/name") == "core");
}

TEST_CASE("file_level applies on auto-resolve and cuts general entries above the "
          "defining layer",
          "[scope_policy][keyed]")
{
    nucleus::runtime_source L0, Lderived;
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lderived.set("app/name", "core");

    nucleus::config_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::config_space space = engine.build();

    // No selection: the single named strain auto-resolves with the policy active.
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(L0), std::move(Lderived)},
        nucleus::load_options{.scope = strain_scope_policy::file_level});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // The world as web's layer saw it: the higher-rank general entry is gone.
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("app/name"));
}
