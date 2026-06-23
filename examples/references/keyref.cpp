// keyref: schema-declared references by identifier (the xs:keyref analog).
//
// Covers: keyref_element(into="<identity_group>"), dangling-reference validation with a
// did-you-mean, and follow_keyref() dereference to the target node. Domain-neutral: a
// `route/target` references an `endpoint`/`output` `name` namespace.

#include "nucleus/query/query.h"
#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/runtime/runtime_source.h"

#include <iostream>
#include <string>

using namespace nucleus;

// endpoints/{output,endpoint}[name] pooled into the `endpoint_names` namespace; a
// route/target keyref points into it.
static config_space make_space()
{
    config_space_builder b;
    b.register_element(element("endpoints", anchor::root()));
    b.register_element(repeated_element("output", anchor::keyspace("endpoints")));
    b.register_element(element("name", anchor::keyspace("endpoints/output")));
    b.register_element(repeated_element("endpoint", anchor::keyspace("endpoints")));
    b.register_element(element("name", anchor::keyspace("endpoints/endpoint")));
    b.register_identity_group(
        identity_group("endpoint_names", anchor::keyspace("endpoints"))
            .members({"output", "endpoint"}).field("name"));
    b.register_element(element("route", anchor::root()));
    b.register_element(keyref_element("target", anchor::keyspace("route"), "endpoint_names"));
    return std::move(b).build();
}

static runtime_source populated(const char *target)
{
    runtime_source s;
    s.set("endpoints/output[0]/name", "primary")
     .set("endpoints/endpoint[0]/name", "secondary")
     .set("route/target", target);
    return s;
}

int main()
{
    const config_space space = make_space();
    const auto ctx = space.query_context();

    std::cout << "--- valid keyref: route/target = 'primary' ---\n";
    if(auto cfg = load_config(space, source_stack{populated("primary")}, {}))
    {
        config_node keyref = cfg->root()["route"]["target"];
        if(auto target = follow_keyref(keyref, ctx))
            std::cout << "  dereferenced to: " << target->path()
                      << " (name=" << target->operator[]("name").value().value_or("?") << ")\n\n";
    }

    std::cout << "--- dangling keyref: route/target = 'primery' ---\n";
    if(auto cfg = load_config(space, source_stack{populated("primery")}, {}); !cfg)
        std::cout << "  REJECTED: " << cfg.error().message << "\n";

    return 0;
}
