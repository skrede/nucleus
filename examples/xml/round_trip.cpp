// round_trip: resolve one config, then render it through three source formats.
//
// A runtime_source builds the scalar base in code via chained .set() -- no document,
// no parser. A small XML overlay supplies the repeated tag values (a flat source can
// carry at most one value per repeated field per layer, so the duplicate_keys-capable
// XML source is what genuinely supplies a repeated field). load unifies them, then
// the ONE resolved config is rendered as XML (nested), env (KEY=value), and
// args (--KEY=value). Each emitter models the format-agnostic config_emitter seam and
// returns owned storage, so the caller decides what reaches a stream. The repeated
// tag keeps all its values in every format.

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

// A server container with a host leaf, a constrained mode leaf, and a repeated
// tag leaf.
nucleus::expected<nucleus::config_space, nucleus::error> make_space()
{
    nucleus::config_space_builder builder;
    if(auto result = builder.register_element(
               nucleus::element("server", nucleus::anchor::root()));
       !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = builder.register_element(
               nucleus::element("host", nucleus::anchor::keyspace("server")));
       !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = builder.register_element(
               nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                                     {"primary", "secondary"}));
       !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = builder.register_element(
               nucleus::repeated_element("tag", nucleus::anchor::keyspace("server")));
       !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}

// The runtime_source sits at stack[0] and carries the scalars; the document
// overlay arrives through load_options at higher precedence and is the only
// layer that can supply more than one `tag`.
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
    const auto sealed = make_space();
    if(!sealed)
        return 1;
    const nucleus::config_space &space  = sealed.value();
    const nucleus::load_result   loaded = load_values(space);
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    const nucleus::config &config = loaded.value();
    // The blank schema template first, for contrast with the resolved config
    // rendered through each of the three formats.
    if(!print_artifact("# xml template", nucleus::xml::render_template(space)))
        return 1;
    if(!print_artifact("\n# xml document", nucleus::xml::render_document(config, space)))
        return 1;
    if(!print_artifact("\n# env document", nucleus::env::render_document(config)))
        return 1;
    if(!print_artifact("\n# args document", nucleus::argv::render_document(config)))
        return 1;
    return 0;
}
