#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"

#include "nucleus/runtime/runtime_source.h"

#include "nucleus/env/env_emitter.h"

#include "nucleus/argv/argv_emitter.h"

#include <string>
#include <utility>
#include <iostream>

namespace {

bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(nucleus::element("server", nucleus::anchor::root())) && builder.register_element(nucleus::element("host", nucleus::anchor::keyspace("server"))) && builder.register_element(nucleus::enum_element("mode", nucleus::anchor::keyspace("server"), {"primary", "secondary"})) && builder.register_element(nucleus::repeated_element("tag", nucleus::anchor::keyspace("server")));
}

nucleus::load_result load_values(const nucleus::config_space &space)
{
    nucleus::runtime_source base;
    base.set("server/host", "localhost").set("server/mode", "primary");
    const std::string document =
            "<server><tag>alpha</tag><tag>beta</tag></server>";
    nucleus::load_options options;
    options.document_paths = {"config.xml"};
    options.make_document  = [document](const std::string &) -> nucleus::source_handle
    {
        return nucleus::source_handle(nucleus::xml_source::from(
                nucleus::xml_source_options::of_string(document)));
    };
    return nucleus::load_config(
            space, nucleus::source_stack{std::move(base)}, options);
}

bool print_artifact(
        const std::string                             &heading,
        nucleus::expected<std::string, nucleus::error> artifact)
{
    if(!artifact)
    {
        std::cerr << heading << " failed: " << artifact.error() << '\n';
        return false;
    }
    std::cout << heading << '\n'
              << artifact.value();
    return true;
}

}

int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    const nucleus::config_space space  = builder.build();
    const nucleus::load_result  loaded = load_values(space);
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    const nucleus::config &config = loaded.value();
    if(!print_artifact("# xml template", nucleus::xml::render_template(space)) || !print_artifact("\n# xml document", nucleus::xml::render_document(config, space)) || !print_artifact("\n# env document", nucleus::env::render_document(config)) || !print_artifact("\n# args document", nucleus::argv::render_document(config)))
        return 1;
    return 0;
}
