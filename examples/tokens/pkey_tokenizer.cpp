#include "nucleus/config.h"
#include "nucleus/format.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/tokenizer/tree_tokenizer.h"

#include "nucleus/runtime/runtime_source.h"

#include <string>
#include <ostream>
#include <utility>
#include <iostream>
#include <string_view>

using space_result = nucleus::expected<nucleus::config_space, nucleus::error>;

template<typename Builder>
static nucleus::registration_result define_space(Builder &builder, std::string leaf)
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
            nucleus::element(std::move(leaf), nucleus::anchor::keyspace("cluster/server")));
}

template<typename Builder>
static space_result make_server_space(Builder &builder)
{
    if(auto result = define_space(builder, "description"); !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}
static space_result make_server_space()
{
    nucleus::config_space_builder builder;
    return make_server_space(builder);
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

static int show_expected(const nucleus::config &config, std::string_view path,
                         std::string_view expected, std::string_view prefix,
                         std::ostream &output, std::ostream &errors)
{
    const auto actual = config.get(path);
    if(!actual || *actual != expected)
    {
        errors << "unexpected value for " << path << '\n';
        return 1;
    }
    output << prefix << *actual << '\n';
    return 0;
}

static int show_selected_server(const nucleus::config_space   &space,
                                const nucleus::runtime_source &source,
                                std::string_view selection, std::string_view expected,
                                std::string_view prefix, std::ostream &output,
                                std::ostream &errors)
{
    nucleus::load_options options;
    options.selection = selection;
    const auto loaded = nucleus::load_config(space, nucleus::source_stack{source}, options);
    if(!loaded)
    {
        errors << "load failed (" << selection << "): " << loaded.error() << '\n';
        return 1;
    }
    return show_expected(*loaded, "cluster/server/description", expected,
                         prefix, output, errors);
}

static int run_builtin_tokenizer(space_result space, std::ostream &output,
                                 std::ostream &errors)
{
    if(!space)
    {
        errors << "space setup failed (built-in tokenizer): " << space.error() << '\n';
        return 1;
    }
    const nucleus::runtime_source source = make_builtin_source();
    if(const int status = show_selected_server(
               *space, source, "primary", "primary at 10.0.0.1:9000",
               "primary:   ", output, errors))
        return status;
    return show_selected_server(
            *space, source, "secondary", "secondary at 10.0.0.2:9000",
            "secondary: ", output, errors);
}

static nucleus::tree_tokenizer make_host_tokenizer()
{
    const nucleus::key_path container = nucleus::key_path{}.child("cluster").child("server");
    return nucleus::tree_tokenizer("server",
                                   [container](const nucleus::tree_access &access) -> nucleus::token_result
                                   {
                                       if(access.building.find(container.child("name")) == nullptr)
                                           return nucleus::unexpected(
                                                   nucleus::resolve_error(nucleus::resolve_errc::missing_field,
                                                                          nucleus::format(
                                                                                  "${{{}}} requires a selected primary-key instance; this configuration has none in scope",
                                                                                  std::string(access.category) + "." + std::string(access.field_name))));
                                       const nucleus::value *value = access.building.find(
                                               container.child(std::string(access.field_name)));
                                       if(value == nullptr)
                                           return nucleus::unexpected(
                                                   nucleus::resolve_error(nucleus::resolve_errc::missing_field,
                                                                          nucleus::format(
                                                                                  "${{{}}} has no field '{}' in the selected instance",
                                                                                  access.category, access.field_name)));
                                       return std::string(value->text());
                                   });
}

static nucleus::runtime_source make_host_source()
{
    nucleus::runtime_source source;
    source.set("cluster/server/alpha/name", "alpha")
            .set("cluster/server/alpha/endpoint", "10.0.1.1:9000")
            .set("cluster/server/alpha/label", "host: ${server.name} at ${server.endpoint}");
    return source;
}

template<typename Builder>
static space_result make_host_space(Builder &builder)
{
    if(auto result = define_space(builder, "label"); !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = builder.install_tree_tokenizer(make_host_tokenizer()); !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}
static space_result make_host_space()
{
    nucleus::config_space_builder builder;
    return make_host_space(builder);
}

static int run_host_tokenizer(space_result space, std::ostream &output,
                              std::ostream &errors)
{
    if(!space)
    {
        errors << "space setup failed (host tokenizer): " << space.error() << '\n';
        return 1;
    }
    nucleus::load_options options;
    options.selection              = "alpha";
    nucleus::runtime_source source = make_host_source();
    const auto              loaded = nucleus::load_config(
            *space, nucleus::source_stack{std::move(source)}, options);
    if(!loaded)
    {
        errors << "load failed (host tokenizer): " << loaded.error() << '\n';
        return 1;
    }
    return show_expected(*loaded, "cluster/server/label",
                         "host: alpha at 10.0.1.1:9000", "host tok:  ",
                         output, errors);
}

int main()
{
    if(const int status = run_builtin_tokenizer(make_server_space(), std::cout, std::cerr))
        return status;
    return run_host_tokenizer(make_host_space(), std::cout, std::cerr);
}
