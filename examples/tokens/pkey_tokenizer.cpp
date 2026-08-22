// pkey_tokenizer: demonstrates the auto-named ${server.field} pkey tokenizer
// and a host-defined install_tree_tokenizer that replicates the built-in behavior.

#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/tokenizer/tree_tokenizer.h"

#include "nucleus/runtime/runtime_source.h"

#include <string>
#include <utility>
#include <iostream>

// Schema setup: cluster/server is a keyed container whose primary key is
// "name". The "endpoint" leaf holds a value we compose using the pkey token.
static nucleus::registration_result define_server_space(
        nucleus::config_space_builder &builder)
{
    if(auto result = builder.register_element(
               nucleus::element("cluster", nucleus::anchor::root()));
       !result)
        return result;
    if(auto result = builder.register_element(
               nucleus::element("server", nucleus::anchor::keyspace("cluster")));
       !result)
        return result;
    if(auto result = builder.register_element(
               nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server")));
       !result)
        return result;
    if(auto result = builder.register_element(
               nucleus::element("endpoint", nucleus::anchor::keyspace("cluster/server")));
       !result)
        return result;
    return builder.register_element(
            nucleus::element("description", nucleus::anchor::keyspace("cluster/server")));
}

static nucleus::expected<nucleus::config_space, nucleus::error> make_server_space()
{
    nucleus::config_space_builder builder;
    if(auto result = define_server_space(builder); !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}

static nucleus::runtime_source make_builtin_source()
{
    nucleus::runtime_source source;
    source.set("cluster/server/primary/name", "primary")
            .set("cluster/server/primary/endpoint", "10.0.0.1:9000")
            .set("cluster/server/primary/description", "${server.name} at ${server.endpoint}")
            .set("cluster/server/secondary/name", "secondary")
            .set("cluster/server/secondary/endpoint", "10.0.0.2:9000")
            .set("cluster/server/secondary/description", "${server.name} at ${server.endpoint}");
    return source;
}

static int show_selected_server(const nucleus::config_space   &space,
                                const nucleus::runtime_source &source,
                                const std::string             &selection,
                                const std::string             &prefix)
{
    nucleus::load_options options;
    options.selection = selection;
    const auto loaded = nucleus::load_config(space, nucleus::source_stack{source}, options);
    if(!loaded)
    {
        std::cerr << "load failed (" << selection << "): " << loaded.error() << '\n';
        return 1;
    }
    const auto description = loaded.value().get("cluster/server/description");
    std::cout << prefix << description.value_or("(absent)") << '\n';
    return 0;
}

// ${server.name} expands to the selected strain's primary-key value.
// ${server.endpoint} expands to the same strain's "endpoint" field.
// The tokenizer is auto-registered by build() — no host install needed.
static int demonstrate_builtin_tokenizer()
{
    auto space = make_server_space();
    if(!space)
    {
        std::cerr << "space setup failed (built-in tokenizer): " << space.error() << '\n';
        return 1;
    }
    const nucleus::runtime_source source = make_builtin_source();
    if(const int status = show_selected_server(*space, source, "primary", "primary:   "))
        return status;
    // Prints: primary:   primary at 10.0.0.1:9000
    const int status = show_selected_server(*space, source, "secondary", "secondary: ");
    // Prints: secondary: secondary at 10.0.0.2:9000
    return status;
}

// A host can replace or augment the auto-named tokenizer by calling
// install_tree_tokenizer before build(). Last-registration wins, so the
// host's resolver shadows the auto-named built-in for the same category.
// This proves host/built-in equivalence: a host-defined resolver is equivalent
// to the auto-named built-in — both read cluster/server/<field> post-slice.
// The resolver reads cluster/server/<field> directly from the assembled tree.
static nucleus::tree_tokenizer make_host_tokenizer()
{
    return nucleus::tree_tokenizer("server",
                                   [](const nucleus::tree_access &access) -> nucleus::token_result
                                   {
                                       const auto field_path = nucleus::key_path::parse(
                                               "cluster/server/" + std::string(access.field_name));
                                       if(!field_path)
                                           return nucleus::unexpected(
                                                   nucleus::resolve_error(nucleus::resolve_errc::missing_field,
                                                                          "invalid field path"));
                                       const nucleus::value *value = access.building.find(field_path.value());
                                       if(value == nullptr)
                                           return nucleus::unexpected(
                                                   nucleus::resolve_error(nucleus::resolve_errc::missing_field,
                                                                          "field not found"));
                                       return std::string(value->text());
                                   });
}

static nucleus::runtime_source make_host_source()
{
    nucleus::runtime_source source;
    source.set("cluster/server/alpha/name", "alpha")
            .set("cluster/server/alpha/endpoint", "10.0.1.1:9000")
            .set("cluster/server/alpha/label", "host: ${server.name}");
    return source;
}

static nucleus::registration_result define_host_space(
        nucleus::config_space_builder &builder)
{
    if(auto result = builder.register_element(
               nucleus::element("cluster", nucleus::anchor::root()));
       !result)
        return result;
    if(auto result = builder.register_element(
               nucleus::element("server", nucleus::anchor::keyspace("cluster")));
       !result)
        return result;
    if(auto result = builder.register_element(
               nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server")));
       !result)
        return result;
    if(auto result = builder.register_element(
               nucleus::element("endpoint", nucleus::anchor::keyspace("cluster/server")));
       !result)
        return result;
    return builder.register_element(
            nucleus::element("label", nucleus::anchor::keyspace("cluster/server")));
}

static nucleus::expected<nucleus::config_space, nucleus::error> make_host_space()
{
    nucleus::config_space_builder builder;
    if(auto result = define_host_space(builder); !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = builder.install_tree_tokenizer(make_host_tokenizer()); !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}

static int demonstrate_host_tokenizer()
{
    auto space = make_host_space();
    if(!space)
    {
        std::cerr << "space setup failed (host tokenizer): " << space.error() << '\n';
        return 1;
    }
    nucleus::load_options options;
    options.selection              = "alpha";
    nucleus::runtime_source source = make_host_source();
    const auto              loaded = nucleus::load_config(
            *space, nucleus::source_stack{std::move(source)}, options);
    if(!loaded)
    {
        std::cerr << "load failed (host tokenizer): " << loaded.error() << '\n';
        return 1;
    }
    std::cout << "host tok:  " << loaded.value().get("cluster/server/label").value_or("(absent)") << '\n';
    // Prints: host tok:  host: alpha
    return 0;
}

int main()
{
    if(const int status = demonstrate_builtin_tokenizer())
        return status;
    return demonstrate_host_tokenizer();
}
