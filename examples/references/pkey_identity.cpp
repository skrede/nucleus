#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"

#include <string>
#include <iostream>

namespace {

bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(nucleus::element("cluster", nucleus::anchor::root())) && builder.register_element(nucleus::element("server", nucleus::anchor::keyspace("cluster"))) && builder.register_element(nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server"))) && builder.register_element(nucleus::element("port", nucleus::anchor::keyspace("cluster/server"))) && builder.register_element(nucleus::element("protocol", nucleus::anchor::keyspace("cluster/server")));
}

nucleus::load_result load_selected(const nucleus::config_space &space)
{
    const std::string     document = R"(
        <cluster>
            <server><protocol>tcp</protocol></server>
            <server name="web"><port>80</port></server>
            <server name="db"><port>5432</port></server>
        </cluster>)";
    nucleus::load_options options;
    options.selection      = "web";
    options.document_paths = {"config.xml"};
    options.make_document  = [document](const std::string &) -> nucleus::source_handle
    {
        return nucleus::source_handle(nucleus::xml_source::from(
                nucleus::xml_source_options::of_string(document)));
    };
    return nucleus::load_config(space, nucleus::source_stack{}, options);
}

nucleus::load_result reload(const nucleus::config_space &space,
                            const std::string           &document)
{
    nucleus::load_options options;
    options.document_paths = {"emitted.xml"};
    options.make_document  = [document](const std::string &) -> nucleus::source_handle
    {
        return nucleus::source_handle(nucleus::xml_source::from(
                nucleus::xml_source_options::of_string(document)));
    };
    return nucleus::load_config(space, nucleus::source_stack{}, options);
}

void print_values(const nucleus::config &config)
{
    if(const auto name = config.get("cluster/server/name"))
        std::cout << "cluster/server/name = " << *name << '\n';
    for(const std::string &key : config.keys())
        std::cout << key << " = " << config.get(key).value_or("") << '\n';
}

bool is_fixpoint(const nucleus::config &initial,
                 const nucleus::config &reloaded)
{
    if(reloaded.keys() != initial.keys())
    {
        std::cerr << "round-trip key mismatch\n";
        return false;
    }
    if(reloaded.get("cluster/server/name") !=
       initial.get("cluster/server/name"))
    {
        std::cerr << "round-trip pkey value mismatch\n";
        return false;
    }
    return true;
}

bool render_and_reload(const nucleus::config_space &space,
                       const nucleus::config       &initial)
{
    const auto rendered = nucleus::xml::render_document(initial, space);
    if(!rendered)
    {
        std::cerr << "render failed: " << rendered.error() << '\n';
        return false;
    }
    std::cout << "\nEmitted XML:\n"
              << rendered.value();
    const nucleus::load_result result = reload(space, rendered.value());
    if(!result)
    {
        std::cerr << "reload failed: " << result.error() << '\n';
        return false;
    }
    if(!is_fixpoint(initial, result.value()))
        return false;
    std::cout << "Round-trip fixpoint: key set and pkey value preserved.\n";
    return true;
}

}

int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    const nucleus::config_space space  = builder.build();
    const nucleus::load_result  loaded = load_selected(space);
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    const nucleus::config &initial = loaded.value();
    print_values(initial);
    if(!render_and_reload(space, initial))
        return 1;
    return 0;
}
