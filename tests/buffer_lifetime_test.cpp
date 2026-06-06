#include "nucleus/xml/xml_source.h"

#include "nucleus/source/source.h"
#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/entry.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <memory>
#include <optional>

namespace {

// The XML the source walks. Nested elements become key paths; attributes and
// pure-text leaves become values. Every value the source yields is a view into
// the document arena -- so reading any of them after the arena is dropped would
// be a use-after-free, which is exactly what this test forces (and ASan guards).
constexpr const char *kDocument = R"(<app>
  <logging level="debug" file="/var/log/app.log">
    <rotate>daily</rotate>
  </logging>
  <server host="0.0.0.0" port="8080"/>
</app>)";

// Copies every entry's value out into an owned map, severing the dependency on
// the document arena. This is the copy-out resolution performs at its boundary.
std::map<std::string, std::string> copy_out(nucleus::source &src)
{
    auto pulled = src.pull();
    REQUIRE(pulled);
    auto &batch = pulled.value();

    // A document source MUST pin its arena (its values are views into it).
    REQUIRE(batch.buffer.pins_anything());

    std::map<std::string, std::string> owned;
    for(const nucleus::keyspace_entry &entry : batch.entries)
    {
        REQUIRE(entry.value.is_view());                 // zero-copy on the wire.
        owned.emplace(entry.path, std::string(entry.value.text()));
    }

    // `batch` (and thus the arena) is dropped when this function returns: the
    // owned copies must survive that drop.
    return owned;
}

}

TEST_CASE("xml source values survive dropping the document arena", "[xml][lifetime]")
{
    std::map<std::string, std::string> values;
    {
        auto src = nucleus::xml::xml_source::from_string(kDocument);
        values = copy_out(src);
        // src and the pulled batch are destroyed at the end of this scope; the
        // pugixml document arena is freed here.
    }

    // Re-read every value AFTER the arena is gone. Under AddressSanitizer this is
    // the proof that copy-out is complete and no view escaped into `values`.
    REQUIRE(values.at("app/logging/level") == "debug");
    REQUIRE(values.at("app/logging/file") == "/var/log/app.log");
    REQUIRE(values.at("app/logging/rotate") == "daily");
    REQUIRE(values.at("app/server/host") == "0.0.0.0");
    REQUIRE(values.at("app/server/port") == "8080");
}

TEST_CASE("the xml source declares document-shaped capabilities", "[xml]")
{
    auto src = nucleus::xml::xml_source::from_string("<r a=\"1\"/>");
    auto caps = src.capabilities();
    REQUIRE(caps.supports(nucleus::capability::nesting));
    REQUIRE(caps.supports(nucleus::capability::ordering));
    REQUIRE(caps.supports(nucleus::capability::duplicate_keys));
    // XML does not type its own scalars: every value is text until interpreted.
    REQUIRE_FALSE(caps.supports(nucleus::capability::typed_scalars));
}

TEST_CASE("the xml source reports a parse failure rather than dangling", "[xml]")
{
    auto src = nucleus::xml::xml_source::from_string("<unterminated>");
    auto pulled = src.pull();
    REQUIRE_FALSE(pulled);
    REQUIRE(pulled.error().find("xml source") != std::string::npos);
}

TEST_CASE("a document source reaches the engine only as a source", "[xml]")
{
    // The xml source is driven purely through the virtual source interface --
    // the same path env and the fake parser use. The arena lives inside the
    // batch's retained_buffer, invisible to this call site.
    auto src = nucleus::xml::xml_source::from_string("<root key=\"v\"/>");
    nucleus::source &as_source = src;

    auto pulled = as_source.pull();
    REQUIRE(pulled);
    REQUIRE(pulled.value().entries.size() == 1);
    REQUIRE(pulled.value().entries[0].path == "root/key");
}
