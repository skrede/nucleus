// round_trip: resolve one configuration, then render it through three source formats.
//
// A runtime_source builds the scalar base in code via chained .set() -- no document,
// no parser. A small XML overlay supplies the repeated tag values (a flat source can
// carry at most one value per repeated field per layer, so the duplicate_keys-capable
// XML source is what genuinely supplies a repeated field). load unifies them, then
// the ONE resolved configuration is emitted as XML (nested), env (KEY=value), and
// args (--KEY=value) into std::cout. Each emitter models the format-agnostic
// config_emitter seam; the user owns the stream. The repeated tag keeps all its
// values in every format.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/entry/configuration.h"

#include "nucleus/sources/xml_source.h"
#include "nucleus/sources/xml_emitter.h"
#include "nucleus/sources/runtime_source.h"

#include "nucleus/configuration_source/env/env_emitter.h"
#include "nucleus/configuration_source/argv/argv_emitter.h"

#include <string>
#include <iostream>

int main()
{
    // Schema: a server container with a host leaf, a constrained mode leaf, and a
    // repeated tag leaf.
    nucleus::configuration_space_builder builder;
    builder.register_element(nucleus::element("server", nucleus::anchor::root()));
    builder.register_element(
        nucleus::element("host", nucleus::anchor::keyspace("server")));
    builder.register_element(
        nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                              {"primary", "secondary"}));
    builder.register_element(
        nucleus::repeated_element("tag", nucleus::anchor::keyspace("server")));
    nucleus::configuration_space space = builder.build();

    // The scalar base, built in code.
    nucleus::runtime_source base;
    base.set("server/host", "localhost").set("server/mode", "primary");

    // The repeated tag values arrive from a document overlay.
    const char *document = "<server><tag>alpha</tag><tag>beta</tag></server>";
    auto make = [document](const std::string &) -> nucleus::source_handle {
        return nucleus::source_handle(
            nucleus::xml::xml_source::from(
                nucleus::xml::xml_source_options::of_string(document)));
    };

    // runtime_source at lower precedence (stack[0]); document overlay at higher via load_options.
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(base)},
        nucleus::load_options{.document_paths = {"config.xml"}, .make_document = make});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();

    // The blank schema template (declared fields, no values) for contrast.
    std::cout << "# xml template\n";
    nucleus::xml::emit_template(space, std::cout);

    // The same resolved configuration rendered through each source format.
    std::cout << "\n# xml document\n";
    nucleus::xml::emit_document(config, std::cout);
    std::cout << "\n# env document\n";
    nucleus::env::emit_document(config, std::cout);
    std::cout << "\n# args document\n";
    nucleus::args::emit_document(config, std::cout);

    return 0;
}
