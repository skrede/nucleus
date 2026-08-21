// keyed_composition: cross-layer merge modes on a repeated/identified collection.
//
// Covers: wholesale_replace (default), unite (strict-additive union), replace_by_key
// (matching identifier replaces the whole element), and the loud duplicate-across-layers
// error. Two stacked layers (the second overrides). Domain-neutral (endpoints/output[name]).

#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/runtime/runtime_source.h"

#include <iostream>
#include <string>

using namespace nucleus;

// endpoints/output (repeated, `mode`) keyed by `name` via an identity group.
static config_space make_space(merge_mode mode, bool with_identity)
{
    config_space_builder b;
    b.register_element(element("endpoints", anchor::root()));
    b.register_element(
        merging(repeated_element("output", anchor::keyspace("endpoints")), mode));
    b.register_element(element("name", anchor::keyspace("endpoints/output")));
    b.register_element(element("addr", anchor::keyspace("endpoints/output")));
    if(with_identity)
        b.register_identity_group(
            identity_group("output_names", anchor::keyspace("endpoints"))
                .members({"output"}).field("name"));
    return std::move(b).build();
}

static void show(const char *title, merge_mode mode, bool with_identity,
                 runtime_source base, runtime_source over)
{
    std::cout << "--- " << title << " ---\n";
    const config_space space = make_space(mode, with_identity);
    auto r = load_config(space, source_stack{std::move(base), std::move(over)}, {});
    if(!r)
    {
        std::cout << "  REJECTED: " << r.error().message << "\n\n";
        return;
    }
    std::cout << "  outputs: ";
    for(const std::string &n : r->get_all("endpoints/output/name"))
        std::cout << n << " ";
    std::cout << "\n\n";
}

static runtime_source base_outputs()
{
    runtime_source s;
    s.set("endpoints/output[0]/name", "a").set("endpoints/output[0]/addr", "base-a")
     .set("endpoints/output[1]/name", "b").set("endpoints/output[1]/addr", "base-b");
    return s;
}

static runtime_source override_output(const char *name, const char *addr)
{
    runtime_source s;
    s.set("endpoints/output[0]/name", name).set("endpoints/output[0]/addr", addr);
    return s;
}

int main()
{
    // The override supplies output[0] alone, so {a} is replaced and {b} stays.
    show("wholesale_replace (default): base {a,b} + override {c}",
         merge_mode::wholesale_replace, false, base_outputs(),
         override_output("c", "over-c"));

    // Layers union -> {a,b,c}.
    show("unite: base {a,b} + override {c}",
         merge_mode::unite, true, base_outputs(), override_output("c", "over-c"));

    // A duplicate identifier across layers is loud (strict-additive).
    show("unite: base {a,b} + override redefines {a} (loud)",
         merge_mode::unite, true, base_outputs(), override_output("a", "over-a"));

    // Matching 'b' is replaced by the override's whole element -> {a,b,...}.
    show("replace_by_key: base {a,b} + override redefines {b}",
         merge_mode::replace_by_key, true, base_outputs(),
         override_output("b", "over-b"));
    return 0;
}
