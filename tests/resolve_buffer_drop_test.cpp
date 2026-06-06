#include "nucleus/nucleus.h"
#include "nucleus/entry/precedence.h"
#include "nucleus/entry/configuration.h"
#include "nucleus/source/env/env_source.h"
#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <memory>
#include <vector>
#include <optional>

// The convergence buffer-lifetime checkpoint -- the project's top use-after-free
// guard. It loads from a stack that INCLUDES an XML document source (whose values
// are string_views into the pugixml DOM arena), resolves to an immutable
// configuration, DROPS every source and its retained buffer/document, then reads
// every value back. Under AddressSanitizer this proves the copy-out at the
// resolve boundary is complete and no view-node escaped into the configuration.

namespace {

constexpr const char *kDocument = R"(<app>
  <logging level="debug" file="/var/log/app.log"/>
  <server host="0.0.0.0" port="8080"/>
</app>)";

}

TEST_CASE("resolved values survive dropping every source buffer", "[resolution][lifetime]")
{
    std::optional<nucleus::configuration> config;

    {
        nucleus::nucleus engine;

        // A document source (views into the parser arena) layered beneath an env
        // overlay that overrides one key -- so both a view-backed and an
        // owned-backed value reach the freeze, and one key is contested.
        auto doc = nucleus::xml::xml_source::from_string(kDocument);
        nucleus::env_source overlay;
        overlay.set("app/server/port", "9090");

        nucleus::source_stack stack;
        stack.add(doc, nucleus::layer_rank::base, "base-document");
        stack.add(overlay, nucleus::layer_rank::overlay, "overlay");

        auto loaded = engine.resolve(stack);
        REQUIRE(loaded);
        config = std::move(loaded).value();

        // engine, doc, overlay, and the pulled batch (and thus the pugixml arena)
        // are all destroyed at the end of this scope.
    }

    REQUIRE(config.has_value());

    // Read every value AFTER every buffer/document is gone. Under ASan this is the
    // proof the configuration is self-owning.
    REQUIRE(config->get("app/logging/level") == "debug");
    REQUIRE(config->get("app/logging/file") == "/var/log/app.log");
    REQUIRE(config->get("app/server/host") == "0.0.0.0");
    REQUIRE(config->get("app/server/port") == "9090"); // overlay won

    // Provenance survived the drop too, and points at the winning layer.
    const nucleus::origin *port_origin = config->provenance_of("app/server/port");
    REQUIRE(port_origin != nullptr);
    REQUIRE(port_origin->layer == "overlay");

    const nucleus::origin *host_origin = config->provenance_of("app/server/host");
    REQUIRE(host_origin != nullptr);
    REQUIRE(host_origin->layer == "base-document");
}
