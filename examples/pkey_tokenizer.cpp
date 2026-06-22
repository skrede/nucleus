// pkey_tokenizer: demonstrates the auto-named ${server.field} pkey tokenizer
// and a host-defined install_tree_tokenizer that replicates the built-in behavior.

#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/tokenizer/tree_tokenizer.h"

#include "nucleus/runtime/runtime_source.h"

#include <iostream>
#include <string>

// -----------------------------------------------------------------------
// Schema setup: cluster/server is a keyed container whose primary key is
// "name". The "endpoint" leaf holds a value we compose using the pkey token.
// -----------------------------------------------------------------------
static nucleus::config_space make_server_space()
{
    nucleus::config_space_builder engine;
    engine.register_element(nucleus::element("cluster", nucleus::anchor::root()));
    engine.register_element(nucleus::element("server", nucleus::anchor::keyspace("cluster")));
    engine.register_element(
        nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::element("endpoint", nucleus::anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::element("description", nucleus::anchor::keyspace("cluster/server")));
    return engine.build();
}

int main()
{
    // -----------------------------------------------------------------------
    // 1. Auto-named ${server.field} tokenizer: resolves pkey-anchored fields.
    //
    // ${server.name} expands to the selected strain's primary-key value.
    // ${server.endpoint} expands to the same strain's "endpoint" field.
    // The tokenizer is auto-registered by build() — no host install needed.
    // -----------------------------------------------------------------------
    {
        auto space = make_server_space();

        nucleus::runtime_source src;
        src.set("cluster/server/primary/name",        "primary")
           .set("cluster/server/primary/endpoint",    "10.0.0.1:9000")
           .set("cluster/server/primary/description", "${server.name} at ${server.endpoint}")
           .set("cluster/server/secondary/name",        "secondary")
           .set("cluster/server/secondary/endpoint",    "10.0.0.2:9000")
           .set("cluster/server/secondary/description", "${server.name} at ${server.endpoint}");

        // Select "primary" — ${server.name} resolves to "primary".
        nucleus::load_options opts;
        opts.selection = "primary";
        auto loaded = nucleus::load_config(space, nucleus::source_stack{src}, opts);
        if(!loaded)
        {
            std::cerr << "load failed (primary): " << loaded.error() << '\n';
            return 1;
        }
        const auto desc = loaded.value().get("cluster/server/description");
        std::cout << "primary:   " << desc.value_or("(absent)") << '\n';
        // Prints: primary:   primary at 10.0.0.1:9000

        // Select "secondary" — same token, different strain.
        opts.selection = "secondary";
        auto loaded2 = nucleus::load_config(space, nucleus::source_stack{src}, opts);
        if(!loaded2)
        {
            std::cerr << "load failed (secondary): " << loaded2.error() << '\n';
            return 1;
        }
        const auto desc2 = loaded2.value().get("cluster/server/description");
        std::cout << "secondary: " << desc2.value_or("(absent)") << '\n';
        // Prints: secondary: secondary at 10.0.0.2:9000
    }

    // -----------------------------------------------------------------------
    // 2. Host tree tokenizer: install_tree_tokenizer replicates the built-in.
    //
    // A host can replace or augment the auto-named tokenizer by calling
    // install_tree_tokenizer before build(). Last-registration wins, so the
    // host's resolver shadows the auto-named built-in for the same category.
    //
    // This proves host parity (TOK-02): a host-defined resolver is equivalent
    // to the auto-named built-in — both read cluster/server/<field> post-slice.
    // -----------------------------------------------------------------------
    {
        nucleus::config_space_builder engine;
        engine.register_element(nucleus::element("cluster", nucleus::anchor::root()));
        engine.register_element(nucleus::element("server", nucleus::anchor::keyspace("cluster")));
        engine.register_element(
            nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server")));
        engine.register_element(
            nucleus::element("endpoint", nucleus::anchor::keyspace("cluster/server")));
        engine.register_element(
            nucleus::element("label", nucleus::anchor::keyspace("cluster/server")));

        // Host-defined tree tokenizer for category "server": reads cluster/server/<field>
        // directly from the assembled tree, mirroring the auto-named built-in.
        auto install = engine.install_tree_tokenizer(
            nucleus::tree_tokenizer("server",
                [](const nucleus::tree_access &access) -> nucleus::token_result
                {
                    auto field_path = nucleus::key_path::parse(
                        "cluster/server/" + std::string(access.field_name));
                    if(!field_path)
                        return nucleus::unexpected(
                            nucleus::resolve_error(nucleus::resolve_errc::missing_field,
                                "invalid field path"));
                    const nucleus::value *v = access.building.find(field_path.value());
                    if(v == nullptr)
                        return nucleus::unexpected(
                            nucleus::resolve_error(nucleus::resolve_errc::missing_field,
                                "field not found"));
                    return std::string(v->text());
                }));
        if(!install)
        {
            std::cerr << "install_tree_tokenizer failed: " << install.error() << '\n';
            return 1;
        }

        auto space = engine.build();

        nucleus::runtime_source src;
        src.set("cluster/server/alpha/name",     "alpha")
           .set("cluster/server/alpha/endpoint", "10.0.1.1:9000")
           .set("cluster/server/alpha/label",    "host: ${server.name}");

        nucleus::load_options opts;
        opts.selection = "alpha";
        auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(src)}, opts);
        if(!loaded)
        {
            std::cerr << "load failed (host tokenizer): " << loaded.error() << '\n';
            return 1;
        }
        const auto label = loaded.value().get("cluster/server/label");
        std::cout << "host tok:  " << label.value_or("(absent)") << '\n';
        // Prints: host tok:  host: alpha
    }

    return 0;
}
