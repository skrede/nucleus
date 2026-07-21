// round_trip: resolve one config, then render it through three source formats.
//
// A runtime_source builds the scalar base in code via chained .set() -- no document,
// no parser. A small XML overlay supplies the repeated tag values (a flat source can
// carry at most one value per repeated field per layer, so the duplicate_keys-capable
// XML source is what genuinely supplies a repeated field). load unifies them, then
// the ONE resolved config is emitted as XML (nested), env (KEY=value), and
// args (--KEY=value) into std::cout. Each emitter models the format-agnostic
// config_emitter seam; the user owns the stream. The repeated tag keeps all its
// values in every format.

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
#include <iostream>

int main()
{
    // Schema: a server container with a host leaf, a constrained mode leaf, and a
    // repeated tag leaf.
    nucleus::config_space_builder builder;
    if(!builder.register_element(nucleus::element("server", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(
        nucleus::element("host", nucleus::anchor::keyspace("server"))))
        return 1;
    if(!builder.register_element(
        nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                              {"primary", "secondary"})))
        return 1;
    if(!builder.register_element(
        nucleus::repeated_element("tag", nucleus::anchor::keyspace("server"))))
        return 1;
    nucleus::config_space space = builder.build();

    // The scalar base, built in code.
    nucleus::runtime_source base;
    base.set("server/host", "localhost").set("server/mode", "primary");

    // The repeated tag values arrive from a document overlay.
    const char *document = "<server><tag>alpha</tag><tag>beta</tag></server>";
    auto make = [document](const std::string &) -> nucleus::source_handle {
        return nucleus::source_handle(
            nucleus::xml_source::from(
                nucleus::xml_source_options::of_string(document)));
    };

    // runtime_source at lower precedence (stack[0]); document overlay at higher via load_options.
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(base)},
        nucleus::load_options{.document_paths = {"config.xml"}, .make_document = make});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::config &config = loaded.value();

    // The blank schema template (declared fields, no values) for contrast.
    std::cout << "# xml template\n";
    if(auto emitted = nucleus::xml::emit_template(space, std::cout); !emitted)
    {
        std::cerr << "xml template emit failed: " << emitted.error() << '\n';
        return 1;
    }

    // The same resolved config rendered through each source format.
    std::cout << "\n# xml document\n";
    if(auto emitted = nucleus::xml::emit_document(config, std::cout); !emitted)
    {
        std::cerr << "xml document emit failed: " << emitted.error() << '\n';
        return 1;
    }
    std::cout << "\n# env document\n";
    if(auto emitted = nucleus::env::emit_document(config, std::cout); !emitted)
    {
        std::cerr << "env document emit failed: " << emitted.error() << '\n';
        return 1;
    }
    std::cout << "\n# args document\n";
    if(auto emitted = nucleus::argv::emit_document(config, std::cout); !emitted)
    {
        std::cerr << "args document emit failed: " << emitted.error() << '\n';
        return 1;
    }

    return 0;
}
