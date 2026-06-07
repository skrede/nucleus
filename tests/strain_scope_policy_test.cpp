#include "nucleus/configuration_space.h"

#include "nucleus/entry/precedence.h"
#include "nucleus/entry/strain_scope.h"
#include "nucleus/entry/configuration.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/source/env/env_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Composition-scope policy tests: env_source instances are placed at explicit
// numeric ranks to simulate a three-layer scenario without requiring XML. The
// schema declares a cluster/server keyed container plus a top-level app/name
// element so the three policies can be distinguished: file_level excludes
// general entries above Ld; the other two admit them.
// space_open_container_closed excludes the strain's keyed entries above Ld;
// container_open_until_next_strain admits them below Ls. Shapes are generic --
// no host vocabulary.

using nucleus::anchor;
using nucleus::strain_scope_policy;

namespace {

// Registers a cluster/server keyed container with primary key "name", leaves
// "port" and "protocol", plus a general "app/name" element at the root.
void declare_cluster_with_app(nucleus::configuration_space &engine)
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

// Builds a three-layer source_stack:
//   rank=10: web/port=80 (web's defining layer; Ld=10)
//   rank=20: web/protocol=tcp, app/name=core (derived layer above Ld; general entry)
//   rank=30: db/port=5432 (competing strain first appears; Ls=30)
nucleus::source_stack three_layer_stack(nucleus::env_source &L0,
                                        nucleus::env_source &Lderived,
                                        nucleus::env_source &Lcompeting)
{
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lderived.set("cluster/server/web/protocol", "tcp")
            .set("app/name", "core");
    Lcompeting.set("cluster/server/db/name", "db")
              .set("cluster/server/db/port", "5432");

    nucleus::source_stack stack;
    stack.add(L0, std::size_t{10}, "L0");
    stack.add(Lderived, std::size_t{20}, "Lderived");
    stack.add(Lcompeting, std::size_t{30}, "Lcompeting");
    return stack;
}

}

TEST_CASE("default policy (space-open container-closed) excludes container entries above "
          "the defining layer but admits general entries",
          "[scope_policy][keyed]")
{
    nucleus::env_source L0, Lderived, Lcompeting;
    nucleus::source_stack stack = three_layer_stack(L0, Lderived, Lcompeting);

    nucleus::configuration_space engine;
    declare_cluster_with_app(engine);
    REQUIRE(engine.select("web"));
    // No set_strain_scope call -- default is space_open_container_closed.

    auto loaded = engine.resolve(stack);
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
    nucleus::env_source L0, Lderived, Lcompeting;
    nucleus::source_stack stack = three_layer_stack(L0, Lderived, Lcompeting);

    nucleus::configuration_space engine;
    declare_cluster_with_app(engine);
    REQUIRE(engine.select("web"));
    REQUIRE(engine.set_strain_scope(strain_scope_policy::file_level));

    auto loaded = engine.resolve(stack);
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
    nucleus::env_source L0, Lderived, Lcompeting;
    nucleus::source_stack stack = three_layer_stack(L0, Lderived, Lcompeting);

    nucleus::configuration_space engine;
    declare_cluster_with_app(engine);
    REQUIRE(engine.select("web"));
    REQUIRE(engine.set_strain_scope(strain_scope_policy::container_open_until_next_strain));

    auto loaded = engine.resolve(stack);
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
    nucleus::env_source L0, Lderived;
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lderived.set("cluster/server/web/protocol", "tcp");

    nucleus::source_stack stack;
    stack.add(L0, std::size_t{10}, "L0");
    stack.add(Lderived, std::size_t{20}, "Lderived");

    nucleus::configuration_space engine;
    declare_cluster_with_app(engine);
    REQUIRE(engine.select("web"));
    REQUIRE(engine.set_strain_scope(strain_scope_policy::container_open_until_next_strain));

    auto loaded = engine.resolve(stack);
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
    // web entry survives under container_open_until_next_strain. (A naive Ls
    // over all competitors would compute Ls=10 < Ld and silently delete the
    // explicitly selected strain.)
    nucleus::env_source Learly, Lweb, Lderived;
    Learly.set("cluster/server/db/name", "db")
          .set("cluster/server/db/port", "5432");
    Lweb.set("cluster/server/web/name", "web")
        .set("cluster/server/web/port", "80");
    Lderived.set("cluster/server/web/protocol", "tcp");

    nucleus::source_stack stack;
    stack.add(Learly, std::size_t{10}, "Learly");
    stack.add(Lweb, std::size_t{30}, "Lweb");
    stack.add(Lderived, std::size_t{40}, "Lderived");

    nucleus::configuration_space engine;
    declare_cluster_with_app(engine);
    REQUIRE(engine.select("web"));
    REQUIRE(engine.set_strain_scope(strain_scope_policy::container_open_until_next_strain));

    auto loaded = engine.resolve(stack);
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
    nucleus::env_source L0, Lcompeting, Lbetween, Loverwrite;
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lcompeting.set("cluster/server/db/name", "db")
              .set("cluster/server/db/port", "5432");
    Lbetween.set("cluster/server/web/protocol", "tcp");
    Loverwrite.set("cluster/server/db/port", "5433");

    nucleus::source_stack stack;
    stack.add(L0, std::size_t{10}, "L0");
    stack.add(Lcompeting, std::size_t{30}, "Lcompeting");
    stack.add(Lbetween, std::size_t{40}, "Lbetween");
    stack.add(Loverwrite, std::size_t{50}, "Loverwrite");

    nucleus::configuration_space engine;
    declare_cluster_with_app(engine);
    REQUIRE(engine.select("web"));
    REQUIRE(engine.set_strain_scope(strain_scope_policy::container_open_until_next_strain));

    auto loaded = engine.resolve(stack);
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
    // No select() call: web is the only named strain and auto-resolves. The
    // default policy must behave exactly as it does under an explicit
    // select("web"): the strain's keyed entry above the defining layer is
    // excluded, general entries compose.
    nucleus::env_source L0, Lderived;
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lderived.set("cluster/server/web/protocol", "tcp")
            .set("app/name", "core");

    nucleus::source_stack stack;
    stack.add(L0, std::size_t{10}, "L0");
    stack.add(Lderived, std::size_t{20}, "Lderived");

    nucleus::configuration_space engine;
    declare_cluster_with_app(engine);
    // No select(), no set_strain_scope() -- default space_open_container_closed.

    auto loaded = engine.resolve(stack);
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
    nucleus::env_source L0, Lderived;
    L0.set("cluster/server/web/name", "web")
      .set("cluster/server/web/port", "80");
    Lderived.set("app/name", "core");

    nucleus::source_stack stack;
    stack.add(L0, std::size_t{10}, "L0");
    stack.add(Lderived, std::size_t{20}, "Lderived");

    nucleus::configuration_space engine;
    declare_cluster_with_app(engine);
    REQUIRE(engine.set_strain_scope(strain_scope_policy::file_level));
    // No select(): the single named strain auto-resolves with the policy active.

    auto loaded = engine.resolve(stack);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // The world as web's file saw it: the rank-20 general entry is gone.
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("app/name"));
}

TEST_CASE("set_strain_scope() after resolve() is rejected",
          "[scope_policy][facade]")
{
    nucleus::env_source src;
    src.set("cluster/server/web/name", "web")
       .set("cluster/server/web/port", "80");

    nucleus::source_stack stack;
    stack.add(src, std::size_t{10}, "L0");

    nucleus::configuration_space engine;
    declare_cluster_with_app(engine);

    // Resolve without setting scope.
    auto loaded = engine.resolve(stack);
    REQUIRE(loaded);

    // After resolve the facade is sealed; set_strain_scope is a state-machine
    // error, and the diagnostic names the operation that was attempted.
    auto result = engine.set_strain_scope(strain_scope_policy::file_level);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().find("set_strain_scope") != std::string::npos);
    REQUIRE(result.error().find("resolved") != std::string::npos);
}
