// schema: three element kinds, and what the schema rejects at resolve.
//
// element / required_element / enum_element declare nodes.
// anchor::root() introduces a top-level node; anchor::keyspace(path) attaches a
// child under an already-declared one. Here the resolve fails: `host` is required
// but no source supplies it.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/runtime/runtime_source.h"

#include <string>
#include <utility>
#include <iostream>

template<typename Builder>
static nucleus::registration_result define_space(Builder &builder)
{
    if(auto result = builder.register_element(nucleus::element("server", nucleus::anchor::root()));
       !result)
        return result;
    if(auto result = builder.register_element(
               nucleus::required_element("host", nucleus::anchor::keyspace("server")));
       !result)
        return result;
    return builder.register_element(
            nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                                  {"http", "https"}));
}

template<typename Builder>
static nucleus::expected<nucleus::config_space, nucleus::error> make_space(Builder &builder)
{
    if(auto result = define_space(builder); !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}

static nucleus::expected<nucleus::config_space, nucleus::error> make_space()
{
    nucleus::config_space_builder builder;
    return make_space(builder);
}

static bool is_missing_host(const nucleus::error &error)
{
    return error.code == nucleus::errc::schema_violation &&
            error.message.find("required field 'server/host' is missing") !=
            std::string::npos;
}

static int report_load_result(nucleus::load_result loaded,
                              std::ostream        &output,
                              std::ostream        &errors)
{
    if(loaded)
    {
        errors << "unexpected success\n";
        return 1;
    }
    if(!is_missing_host(loaded.error()))
    {
        errors << "unexpected rejection: " << loaded.error() << '\n';
        return 1;
    }
    output << "rejected as expected: " << loaded.error() << '\n';
    return 0;
}

static int run_schema_example(nucleus::expected<nucleus::config_space, nucleus::error> space,
                              std::ostream                                            &output,
                              std::ostream                                            &errors)
{
    if(!space)
    {
        errors << "space setup failed: " << space.error() << '\n';
        return 1;
    }

    nucleus::runtime_source values;
    values.set("server/mode", "http");

    auto loaded = nucleus::load_config(*space, nucleus::source_stack{std::move(values)}, {});
    return report_load_result(std::move(loaded), output, errors);
}

int main()
{
    return run_schema_example(make_space(), std::cout, std::cerr);
}
