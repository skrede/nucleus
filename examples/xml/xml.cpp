// xml: layer an XML document under the command line onto one keyspace.
//
// An XML document is a source like any other: nested elements become `/`-paths
// and attributes become values. The document factory turns a path into a source
// so the core never knows a file format. argv outranks the document, so it
// overrides `mode` while the document's `host` survives.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include "nucleus/argv/argv_source.h"

#include <vector>
#include <string>
#include <iostream>

static bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(nucleus::element("server", nucleus::anchor::root())) &&
            builder.register_element(
                    nucleus::required_element("host", nucleus::anchor::keyspace("server"))) &&
            builder.register_element(
                    nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                                          {"http", "https"}));
}

// Parse from an in-memory string so the example needs no file on disk. The
// document factory builds the xml_source from an xml_source_options value --
// the only seam through which the xml module reaches the core.
static nucleus::source_handle make_document(const std::string &)
{
    const char *document = R"(<server host="127.0.0.1" mode="http"/>)";
    return nucleus::source_handle(
            nucleus::xml_source::from(
                    nucleus::xml_source_options::of_string(document)));
}

// argv outranks the document band, so it overrides `mode`; the document's
// `host` survives.
int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    nucleus::config_space space = builder.build();

    nucleus::argv_source argv(std::vector<std::string>{"--server-mode=https"});
    argv.recognize_with(nucleus::recognizer_of(space));

    auto loaded = nucleus::load_config(space,
                                       nucleus::source_stack{std::move(argv)},
                                       nucleus::load_options{.document_paths = {"config.xml"}, .make_document = make_document});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::config &config = loaded.value();
    for(const std::string &key : config.keys())
        std::cout << key << " = " << config.get(key).value() << '\n';
    return 0;
}
