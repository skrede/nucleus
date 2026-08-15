#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_emitter.h"

#include <string>
#include <vector>
#include <iostream>

namespace {

bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(nucleus::element("server", nucleus::anchor::root())) && builder.register_element(nucleus::primary_key_element("name", nucleus::anchor::keyspace("server"))) && builder.register_element(nucleus::unique_element("profile", nucleus::anchor::keyspace("server"))) && builder.register_element(nucleus::enum_element("mode", nucleus::anchor::keyspace("server"), std::vector<std::string>{"primary", "secondary"}));
}

}

int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    const nucleus::config_space space    = builder.build();
    const auto                  rendered = nucleus::xml::render_template(space);
    if(!rendered)
    {
        std::cerr << "render failed: " << rendered.error() << '\n';
        return 1;
    }
    std::cout << rendered.value();
    return 0;
}
