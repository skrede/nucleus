#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/xml/xml_source.h"

#include <memory>
#include <iostream>
#include <string>
#include <vector>

// quickstart: register a small schema, resolve it from a command line, and read
// the values back with their provenance.
//
// The schema is the single authority. The same set of declared elements dictates
// both the document structure (what key paths may carry values) and the CLI
// surface (what `--flag` names exist); the command line below is just one
// projection of the schema. An undeclared flag would be rejected, because the
// schema -- not the source -- decides what is admissible.

namespace {

nucleus::key_path path_of(const char *text)
{
    return nucleus::key_path::parse(text).value();
}

} // namespace

int main()
{
    nucleus::configuration_space engine;

    // A root element, a nested element under it, and a closed-value-set element.
    // anchor::root() introduces a top-level node; anchor::keyspace(path) attaches
    // a child under an already-declared node.
    engine.register_element(nucleus::element("server", nucleus::anchor::root()));
    engine.register_element(nucleus::required_element(
        "host", nucleus::anchor::keyspace(path_of("server"))));
    engine.register_element(nucleus::enum_element(
        "mode", nucleus::anchor::keyspace(path_of("server")),
        {"http", "https"}));

    // An XML document is one source; the command line is another. Both feed the
    // SAME keyspace the schema declares: nested elements become `/`-separated key
    // paths and attributes become values. The document factory turns a path into
    // a source, so the core never has to know a file format -- here we parse from
    // an in-memory string so the example needs no file on disk.
    const char *document = R"(<server host="127.0.0.1" mode="http"/>)";
    auto make = [document](const std::string &) -> std::unique_ptr<nucleus::source> {
        return std::make_unique<nucleus::xml::xml_source>(
            nucleus::xml::xml_source::from_string(document));
    };

    // Resolve from the document AND the command line. The `--a-b-c` flag form maps
    // onto the same `a/b/c` keyspace; the command line outranks the document, so
    // it overrides the document's `mode`, while the document's `host` survives
    // because no flag contests it.
    std::vector<std::string> args{
        "--server-mode=https",
    };
    std::vector<std::string> paths{"config.xml"};

    auto loaded = engine.load(args, paths, make);
    if(!loaded)
    {
        std::cerr << "resolve failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();

    std::cout << "resolved " << config.size() << " key(s):\n";
    for(const std::string &key : config.keys())
    {
        const std::string value = config.get(key).value();
        const nucleus::origin *from = config.provenance_of(key);
        std::cout << "  " << key << " = " << value
                  << "  (from " << (from ? from->layer : "?") << ")\n";
    }

    std::cout << "contains server/host: " << std::boolalpha
              << config.contains("server/host") << '\n';

    return 0;
}
