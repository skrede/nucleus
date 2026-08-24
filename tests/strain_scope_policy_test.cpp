#include "builder_result_test_support.h"

#include "nucleus/strain_scope.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Composition-scope policy tests: capable (path -> value) feeders are placed in
// explicit source_stack positions to simulate a three-layer scenario without requiring
// XML. The schema declares a cluster/server keyed container plus a top-level app/name
// element so the three policies can be distinguished. The selection and scope policy
// are per-load parameters on load_options. A few tests also use a genuine document
// (inheritance-chain) layer, via xml_of/filename_of below, where the policy under
// test must distinguish chain-introduced content from stack-introduced content.

using nucleus::anchor;
using nucleus::strain_scope_policy;

namespace {

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
        nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

// Returns the filename portion of a (possibly absolute) path string so factory
// lambdas can dispatch without knowledge of the working-directory prefix that
// weakly_canonical() prepends to relative paths when following inherit= links.
std::string filename_of(const std::string &path)
{
    const auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

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
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

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

TEST_CASE("file_level policy excludes chain content above the defining layer but admits "
          "stack content",
          "[scope_policy][keyed]")
{
    // base.xml introduces strain "web" (Ld = inheritance layer 0); derived.xml
    // (inherit="base.xml") introduces protocol at inheritance layer 1, above Ld --
    // chain content that file_level's own policy purpose still must cut. A stack
    // source (runtime_source, no inheritance_layer) at a rank above the whole
    // chain sets app/name: per strain_scope.h's documented contract, a stack
    // entry always wins by plain precedence and must survive file_level's prune.
    const char *base_doc =
        R"(<cluster><server name="web"><port>80</port></server></cluster>)";
    const char *derived_doc =
        R"(<cluster inherit="base.xml">)"
        R"(<server name="web" extend="narrow"><protocol>tcp</protocol></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml") return xml_of(base_doc);
        return xml_of(derived_doc);
    };

    nucleus::runtime_source stack_entry;
    stack_entry.set("app/name", "core");

    nucleus::load_options opts;
    opts.document_paths = {"derived.xml"};
    opts.make_document = factory;
    opts.selection = "web";
    opts.scope = strain_scope_policy::file_level;

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(stack_entry)}, opts);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // port is at Ld itself; survives.
    REQUIRE(config.get("cluster/server/port") == "80");

    // protocol is chain content at inheritance layer 1 > Ld; excluded (the
    // policy's real purpose -- freezing chain content at the defining layer --
    // is preserved).
    REQUIRE_FALSE(config.contains("cluster/server/protocol"));

    // app/name is a stack entry above Ld with no inheritance_layer; survives
    // (the stack-entry exemption this fix restores).
    REQUIRE(config.get("app/name") == "core");
}

TEST_CASE("container_open_until_next_strain admits container entries below Ls and general "
          "entries",
          "[scope_policy][keyed]")
{
    nucleus::runtime_source L0, Lderived, Lcompeting;
    setup_three_layer(L0, Lderived, Lcompeting);

    nucleus::config_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

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
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

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
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

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
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

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
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

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

TEST_CASE("file_level applies on auto-resolve, cutting chain content but admitting stack "
          "content above the defining layer",
          "[scope_policy][keyed]")
{
    // Same shape as the explicit-selection test above, but with no selection:
    // "web" is the only named strain and auto-resolves.
    const char *base_doc =
        R"(<cluster><server name="web"><port>80</port></server></cluster>)";
    const char *derived_doc =
        R"(<cluster inherit="base.xml">)"
        R"(<server name="web" extend="narrow"><protocol>tcp</protocol></server></cluster>)";

    nucleus::config_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto factory = [&](const std::string &path) -> nucleus::source_handle {
        const std::string name = filename_of(path);
        if(name == "base.xml") return xml_of(base_doc);
        return xml_of(derived_doc);
    };

    nucleus::runtime_source stack_entry;
    stack_entry.set("app/name", "core");

    nucleus::load_options opts;
    opts.document_paths = {"derived.xml"};
    opts.make_document = factory;
    opts.scope = strain_scope_policy::file_level;

    // No selection: the single named strain auto-resolves with the policy active.
    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(stack_entry)}, opts);
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // The world as web's layer saw it: chain content above Ld is gone, but the
    // stack entry survives by plain precedence.
    REQUIRE(config.get("cluster/server/port") == "80");
    REQUIRE_FALSE(config.contains("cluster/server/protocol"));
    REQUIRE(config.get("app/name") == "core");
}

TEST_CASE("file_level exempts stack-sourced entries above the defining layer, matching "
          "the documented always-wins-by-stack-order contract",
          "[scope_policy][keyed]")
{
    // A fixture built entirely from stacked runtime_source layers, no
    // inheritance chain anywhere -- so every entry's origin carries no
    // inheritance_layer. Under the corrected file_level semantics, stack
    // entries above the defining layer are exempt from the rank-bounded
    // prune and survive by stack order: strain_scope.h's own doc comment
    // has always promised this, but no test proved it directly before.
    nucleus::runtime_source L0, Lderived, Lcompeting;
    setup_three_layer(L0, Lderived, Lcompeting);

    nucleus::config_space_builder engine;
    declare_cluster_with_app(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(L0), std::move(Lderived), std::move(Lcompeting)},
        nucleus::load_options{.selection = "web", .scope = strain_scope_policy::file_level});
    REQUIRE(loaded);
    const nucleus::config &config = loaded.value();

    // web/port=80 is at stack[0]; survives.
    REQUIRE(config.get("cluster/server/port") == "80");

    // web/protocol=tcp is a KEYED (instance-scoped) entry: relay_strain freezes
    // the chosen strain's own keyed entries at Ld under every non-wide-extend
    // policy regardless of inheritance_layer, exactly as space_open_container_closed
    // already does -- this fix does not touch that path, so protocol is still
    // excluded here.
    REQUIRE_FALSE(config.contains("cluster/server/protocol"));

    // app/name=core is a general (non-instance-scoped) stack entry with no
    // inheritance_layer at stack[1] > stack[0]; the file_level general pre-pass
    // exempts it from the rank-bounded prune, so it survives by stack order --
    // the behavior strain_scope.h's doc comment always promised.
    REQUIRE(config.get("app/name") == "core");
}
