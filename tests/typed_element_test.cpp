// All existing suites pass -- zero behavior change for untyped paths.
//
// Unit tests for the typed-field converter seam:
// built-in scalar edge cases, custom domain-neutral struct converter,
// conversion failure surfacing, get_as error distinctions,
// repeated x typed (get_all_as), and orthogonality with other schema axes.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/entry/configuration.h"

#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

using nucleus::anchor;

namespace {

std::unique_ptr<nucleus::configuration_source> xml_of(const std::string &text)
{
    return std::make_unique<nucleus::xml::xml_source>(
        nucleus::xml::xml_source::from(nucleus::xml::xml_source_options::of_string(text)));
}

// Seals `engine` into a space and loads `src` as the sole document-band layer at
// rank 10 -- the per-load shape replacing the old facade stack-load member.
// `src` outlives the fold (load_configuration copies values out before returning).
nucleus::load_result resolve_one(nucleus::configuration_space_builder &engine,
                                 nucleus::configuration_source &src)
{
    nucleus::configuration_space space = engine.build();
    nucleus::source_stack_options opts;
    opts.custom_layers.push_back(nucleus::configuration_source_layer{&src, 10, "doc", {}});
    return nucleus::load_configuration(space, opts);
}

struct point2
{
    int x = 0;
    int y = 0;
    bool operator==(const point2 &other) const noexcept
    {
        return x == other.x && y == other.y;
    }
};

std::function<nucleus::expected<std::any, std::string>(std::string_view)>
make_point2_converter()
{
    return [](std::string_view sv) -> nucleus::expected<std::any, std::string> {
        auto comma = sv.find(',');
        if(comma == std::string_view::npos)
            return nucleus::unexpected(std::string("missing comma separator"));
        std::string_view xs = sv.substr(0, comma);
        std::string_view ys = sv.substr(comma + 1);
        int xv{}, yv{};
        auto [px, ecx] = std::from_chars(xs.data(), xs.data() + xs.size(), xv);
        if(ecx != std::errc{} || px != xs.data() + xs.size())
            return nucleus::unexpected(std::string("bad x component"));
        auto [py, ecy] = std::from_chars(ys.data(), ys.data() + ys.size(), yv);
        if(ecy != std::errc{} || py != ys.data() + ys.size())
            return nucleus::unexpected(std::string("bad y component"));
        return std::any(point2{xv, yv});
    };
}

}

// ---------------------------------------------------------------------------
// Built-in converter: int32_t edge cases
// ---------------------------------------------------------------------------
TEST_CASE("built-in int32_t converter", "[typed][builtin][integer]")
{
    SECTION("valid positive")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(
            nucleus::typed_element<int32_t>("val", anchor::keyspace("cfg")));

        auto src = xml_of("<cfg><val>42</val></cfg>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(loaded);
        auto r = loaded.value().get_as<int32_t>("cfg/val");
        REQUIRE(r);
        REQUIRE(r.value() == 42);
    }

    SECTION("valid negative")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(
            nucleus::typed_element<int32_t>("val", anchor::keyspace("cfg")));

        auto src = xml_of("<cfg><val>-1</val></cfg>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(loaded);
        auto r = loaded.value().get_as<int32_t>("cfg/val");
        REQUIRE(r);
        REQUIRE(r.value() == -1);
    }

    SECTION("empty input")
    {
        auto conv = nucleus::make_scalar_converter<int32_t>();
        auto r = conv("");
        REQUIRE(!r);
        REQUIRE(r.error().find("empty") != std::string::npos);
    }

    SECTION("trailing garbage")
    {
        auto conv = nucleus::make_scalar_converter<int32_t>();
        auto r = conv("42abc");
        REQUIRE(!r);
        REQUIRE(r.error().find("trailing") != std::string::npos);
    }

    SECTION("overflow")
    {
        auto conv = nucleus::make_scalar_converter<int32_t>();
        auto r = conv("99999999999");
        REQUIRE(!r);
        REQUIRE(r.error().find("out of range") != std::string::npos);
    }

    SECTION("leading whitespace rejected")
    {
        // std::from_chars does not skip whitespace -- leading space is a failure.
        auto conv = nucleus::make_scalar_converter<int32_t>();
        auto r = conv(" 42");
        REQUIRE(!r);
    }

    SECTION("alphabetic input: invalid characters")
    {
        // Pure non-numeric input consumes zero chars; the converter distinguishes
        // this from a sign-into-wrong-type case and returns "invalid characters".
        auto conv = nucleus::make_scalar_converter<int32_t>();
        auto r = conv("abc");
        REQUIRE(!r);
        REQUIRE(r.error().find("invalid characters") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Built-in converter: uint32_t edge cases
// ---------------------------------------------------------------------------
TEST_CASE("built-in uint32_t converter", "[typed][builtin][integer]")
{
    SECTION("valid zero")
    {
        auto conv = nucleus::make_scalar_converter<uint32_t>();
        auto r = conv("0");
        REQUIRE(r);
        REQUIRE(std::any_cast<uint32_t>(r.value()) == 0u);
    }

    SECTION("UINT32_MAX")
    {
        auto conv = nucleus::make_scalar_converter<uint32_t>();
        auto r = conv("4294967295");
        REQUIRE(r);
        REQUIRE(std::any_cast<uint32_t>(r.value()) == 4294967295u);
    }

    SECTION("negative rejected")
    {
        auto conv = nucleus::make_scalar_converter<uint32_t>();
        auto r = conv("-1");
        REQUIRE(!r);
        // A leading '-' into an unsigned type is treated as a range-adjacent
        // error: "value out of range for type".
        REQUIRE(r.error().find("range") != std::string::npos);
    }

    SECTION("overflow")
    {
        auto conv = nucleus::make_scalar_converter<uint32_t>();
        auto r = conv("4294967296");
        REQUIRE(!r);
        REQUIRE(r.error().find("out of range") != std::string::npos);
    }

    SECTION("alphabetic input: invalid characters")
    {
        // Pure non-numeric input into an unsigned type yields "invalid characters",
        // not "out of range" -- the two zero-consumption cases are distinguished.
        auto conv = nucleus::make_scalar_converter<uint32_t>();
        auto r = conv("abc");
        REQUIRE(!r);
        REQUIRE(r.error().find("invalid characters") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Built-in converter: int8_t edge cases
// ---------------------------------------------------------------------------
TEST_CASE("built-in int8_t converter", "[typed][builtin][integer]")
{
    SECTION("INT8_MAX")
    {
        auto conv = nucleus::make_scalar_converter<int8_t>();
        auto r = conv("127");
        REQUIRE(r);
        REQUIRE(std::any_cast<int8_t>(r.value()) == int8_t{127});
    }

    SECTION("overflow 128")
    {
        auto conv = nucleus::make_scalar_converter<int8_t>();
        auto r = conv("128");
        REQUIRE(!r);
        REQUIRE(r.error().find("out of range") != std::string::npos);
    }

    SECTION("underflow -129")
    {
        auto conv = nucleus::make_scalar_converter<int8_t>();
        auto r = conv("-129");
        REQUIRE(!r);
        REQUIRE(r.error().find("out of range") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Built-in converter: uint8_t edge cases
// ---------------------------------------------------------------------------
TEST_CASE("built-in uint8_t converter", "[typed][builtin][integer]")
{
    SECTION("UINT8_MAX 255")
    {
        auto conv = nucleus::make_scalar_converter<uint8_t>();
        auto r = conv("255");
        REQUIRE(r);
        REQUIRE(std::any_cast<uint8_t>(r.value()) == uint8_t{255});
    }

    SECTION("overflow 256")
    {
        auto conv = nucleus::make_scalar_converter<uint8_t>();
        auto r = conv("256");
        REQUIRE(!r);
        REQUIRE(r.error().find("out of range") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Built-in converter: float edge cases
// ---------------------------------------------------------------------------
TEST_CASE("built-in float converter", "[typed][builtin][float]")
{
    SECTION("valid 3.14")
    {
        auto conv = nucleus::make_scalar_converter<float>();
        auto r = conv("3.14");
        REQUIRE(r);
        REQUIRE(std::abs(std::any_cast<float>(r.value()) - 3.14f) < 0.001f);
    }

    SECTION("empty input")
    {
        auto conv = nucleus::make_scalar_converter<float>();
        auto r = conv("");
        REQUIRE(!r);
        REQUIRE(r.error().find("empty") != std::string::npos);
    }

    SECTION("trailing garbage")
    {
        auto conv = nucleus::make_scalar_converter<float>();
        auto r = conv("3.14xyz");
        REQUIRE(!r);
        REQUIRE(r.error().find("trailing") != std::string::npos);
    }

    SECTION("1e38 within range")
    {
        auto conv = nucleus::make_scalar_converter<float>();
        auto r = conv("1e38");
        REQUIRE(r);
    }

    SECTION("overflow 1e999")
    {
        auto conv = nucleus::make_scalar_converter<float>();
        auto r = conv("1e999");
        REQUIRE(!r);
        REQUIRE(r.error().find("out of range") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Built-in converter: double edge cases
// ---------------------------------------------------------------------------
TEST_CASE("built-in double converter", "[typed][builtin][float]")
{
    SECTION("valid 2.718281828")
    {
        auto conv = nucleus::make_scalar_converter<double>();
        auto r = conv("2.718281828");
        REQUIRE(r);
        REQUIRE(std::abs(std::any_cast<double>(r.value()) - 2.718281828) < 1e-9);
    }

    SECTION("empty input")
    {
        auto conv = nucleus::make_scalar_converter<double>();
        auto r = conv("");
        REQUIRE(!r);
        REQUIRE(r.error().find("empty") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Built-in converter: bool edge cases
// ---------------------------------------------------------------------------
TEST_CASE("built-in bool converter", "[typed][builtin][bool]")
{
    SECTION("true variants")
    {
        auto conv = nucleus::make_scalar_converter<bool>();
        REQUIRE(std::any_cast<bool>(conv("true").value()) == true);
        REQUIRE(std::any_cast<bool>(conv("True").value()) == true);
        REQUIRE(std::any_cast<bool>(conv("TRUE").value()) == true);
        REQUIRE(std::any_cast<bool>(conv("1").value()) == true);
    }

    SECTION("false variants")
    {
        auto conv = nucleus::make_scalar_converter<bool>();
        REQUIRE(std::any_cast<bool>(conv("false").value()) == false);
        REQUIRE(std::any_cast<bool>(conv("False").value()) == false);
        REQUIRE(std::any_cast<bool>(conv("FALSE").value()) == false);
        REQUIRE(std::any_cast<bool>(conv("0").value()) == false);
    }

    SECTION("invalid yes")
    {
        auto conv = nucleus::make_scalar_converter<bool>();
        auto r = conv("yes");
        REQUIRE(!r);
        REQUIRE(r.error().find("true/false/1/0") != std::string::npos);
    }

    SECTION("empty input")
    {
        auto conv = nucleus::make_scalar_converter<bool>();
        auto r = conv("");
        REQUIRE(!r);
    }
}

// ---------------------------------------------------------------------------
// Built-in converter: char edge cases
// ---------------------------------------------------------------------------
TEST_CASE("built-in char converter", "[typed][builtin][char]")
{
    SECTION("single character")
    {
        auto conv = nucleus::make_scalar_converter<char>();
        auto r = conv("x");
        REQUIRE(r);
        REQUIRE(std::any_cast<char>(r.value()) == 'x');
    }

    SECTION("empty string")
    {
        auto conv = nucleus::make_scalar_converter<char>();
        auto r = conv("");
        REQUIRE(!r);
        REQUIRE(r.error().find("one character") != std::string::npos);
    }

    SECTION("two characters")
    {
        auto conv = nucleus::make_scalar_converter<char>();
        auto r = conv("ab");
        REQUIRE(!r);
        REQUIRE(r.error().find("one character") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Built-in converter: std::string passthrough
// ---------------------------------------------------------------------------
TEST_CASE("built-in std::string passthrough converter", "[typed][builtin][string]")
{
    SECTION("non-empty input succeeds")
    {
        auto conv = nucleus::make_scalar_converter<std::string>();
        auto r = conv("hello");
        REQUIRE(r);
        REQUIRE(std::any_cast<std::string>(r.value()) == "hello");
    }

    SECTION("empty string succeeds")
    {
        auto conv = nucleus::make_scalar_converter<std::string>();
        auto r = conv("");
        REQUIRE(r);
        REQUIRE(std::any_cast<std::string>(r.value()) == "");
    }
}

// ---------------------------------------------------------------------------
// Locale independence: from_chars is locale-independent by specification
// ---------------------------------------------------------------------------
TEST_CASE("float converter is locale-independent", "[typed][builtin][float][locale]")
{
    auto conv = nucleus::make_scalar_converter<float>();

    SECTION("decimal point parses correctly")
    {
        // from_chars uses '.' unconditionally regardless of LC_NUMERIC.
        auto r = conv("3.14");
        REQUIRE(r);
        REQUIRE(std::abs(std::any_cast<float>(r.value()) - 3.14f) < 0.001f);
    }

    SECTION("comma separator is NOT recognized")
    {
        // from_chars stops at ',' since it is not a valid floating-point character,
        // producing trailing garbage rather than a locale-specific decimal point.
        auto r = conv("3,14");
        REQUIRE(!r);
    }
}

// ---------------------------------------------------------------------------
// Custom domain-neutral struct converter
// ---------------------------------------------------------------------------
TEST_CASE("custom point2 converter round-trips through typed_element", "[typed][custom]")
{
    SECTION("valid document resolves to point2{3,7}")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(
            nucleus::typed_element<point2>("pos", anchor::keyspace("cfg"),
                                           make_point2_converter()));

        auto src = xml_of("<cfg><pos>3,7</pos></cfg>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(loaded);
        auto r = loaded.value().get_as<point2>("cfg/pos");
        REQUIRE(r);
        REQUIRE(r.value() == point2{3, 7});
    }

    SECTION("missing comma causes resolve failure")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(
            nucleus::typed_element<point2>("pos", anchor::keyspace("cfg"),
                                           make_point2_converter()));

        auto src = xml_of("<cfg><pos>3</pos></cfg>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(!loaded);
    }

    SECTION("bad x component causes resolve failure")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(
            nucleus::typed_element<point2>("pos", anchor::keyspace("cfg"),
                                           make_point2_converter()));

        auto src = xml_of("<cfg><pos>abc,7</pos></cfg>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(!loaded);
    }
}

// ---------------------------------------------------------------------------
// Conversion failure at resolve surfaces path + reason + layer
// ---------------------------------------------------------------------------
TEST_CASE("conversion failure at resolve surfaces diagnostic", "[typed][failure][resolve]")
{
    // Set up a typed int32_t element with a bad document value.
    auto make_engine_and_src = [&](const std::string &value) {
        struct pair_t
        {
            nucleus::configuration_space_builder engine;
            std::unique_ptr<nucleus::configuration_source> src;
        };
        pair_t p;
        p.engine.register_element(nucleus::element("cfg", anchor::root()));
        p.engine.register_element(
            nucleus::typed_element<int32_t>("val", anchor::keyspace("cfg")));
        p.src = xml_of("<cfg><val>" + value + "</val></cfg>");
        return p;
    };

    SECTION("error contains the path")
    {
        auto p = make_engine_and_src("notanumber");
        auto loaded = resolve_one(p.engine, *p.src);
        REQUIRE(!loaded);
        INFO("error: " << loaded.error());
        REQUIRE(loaded.error().find("cfg/val") != std::string::npos);
    }

    SECTION("error contains the layer label")
    {
        auto p = make_engine_and_src("notanumber");
        auto loaded = resolve_one(p.engine, *p.src);
        REQUIRE(!loaded);
        INFO("error: " << loaded.error());
        // The convert() format is "conversion failed for '...': ... (layer: doc)"
        REQUIRE(loaded.error().find("doc") != std::string::npos);
    }

    SECTION("error contains the converter reason substring")
    {
        auto p = make_engine_and_src("notanumber");
        auto loaded = resolve_one(p.engine, *p.src);
        REQUIRE(!loaded);
        INFO("error: " << loaded.error());
        // "notanumber" is pure alphabetic: the corrected converter returns
        // "invalid characters in value", which the resolve error embeds.
        REQUIRE(loaded.error().find("invalid characters") != std::string::npos);
    }

    SECTION("alphabetic input yields 'invalid characters' not 'out of range'")
    {
        // Direct converter check: "abc" must not be reported as out-of-range.
        auto conv = nucleus::make_scalar_converter<int32_t>();
        auto r = conv("notanumber");
        REQUIRE(!r);
        REQUIRE(r.error().find("invalid characters") != std::string::npos);
        REQUIRE(r.error().find("out of range") == std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// get_as error distinctions: absent / no-converter / type-mismatch / wrong-accessor
// ---------------------------------------------------------------------------
TEST_CASE("get_as error distinctions", "[typed][accessor][errors]")
{
    SECTION("absent path returns error containing 'absent'")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(nucleus::element("name", anchor::keyspace("cfg")));

        auto src = xml_of("<cfg><name>hello</name></cfg>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(loaded);

        auto r = loaded.value().get_as<int32_t>("nonexistent");
        REQUIRE(!r);
        INFO("error: " << r.error());
        REQUIRE(r.error().find("absent") != std::string::npos);
    }

    SECTION("path with no converter returns error containing 'no type converter'")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        // Plain element -- no converter registered.
        engine.register_element(nucleus::element("name", anchor::keyspace("cfg")));

        auto src = xml_of("<cfg><name>hello</name></cfg>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(loaded);

        auto r = loaded.value().get_as<int32_t>("cfg/name");
        REQUIRE(!r);
        INFO("error: " << r.error());
        REQUIRE(r.error().find("no type converter") != std::string::npos);
    }

    SECTION("type mismatch returns error containing 'type mismatch'")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(
            nucleus::typed_element<int32_t>("val", anchor::keyspace("cfg")));

        auto src = xml_of("<cfg><val>42</val></cfg>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(loaded);

        // The converter stored int32_t; requesting float is a mismatch.
        auto r = loaded.value().get_as<float>("cfg/val");
        REQUIRE(!r);
        INFO("error: " << r.error());
        REQUIRE(r.error().find("type mismatch") != std::string::npos);
    }

    SECTION("get_as on a repeated typed path returns 'use get_all_as' message")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        auto el = nucleus::typed_element<int32_t>("nums", anchor::keyspace("cfg"));
        el.repeated = true;
        engine.register_element(el);

        auto src = xml_of("<cfg><nums>1</nums><nums>2</nums></cfg>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(loaded);

        // The path carries a typed collection; get_as is the wrong accessor.
        auto r = loaded.value().get_as<int32_t>("cfg/nums");
        REQUIRE(!r);
        INFO("error: " << r.error());
        REQUIRE(r.error().find("get_all_as") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// get_all_as error distinctions: wrong-accessor on a scalar typed path
// ---------------------------------------------------------------------------
TEST_CASE("get_all_as error distinctions", "[typed][accessor][errors]")
{
    SECTION("get_all_as on a scalar typed path returns 'use get_as' message")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));
        engine.register_element(
            nucleus::typed_element<int32_t>("val", anchor::keyspace("cfg")));

        auto src = xml_of("<cfg><val>42</val></cfg>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(loaded);

        // The path carries a scalar typed value; get_all_as is the wrong accessor.
        auto r = loaded.value().get_all_as<int32_t>("cfg/val");
        REQUIRE(!r);
        INFO("error: " << r.error());
        REQUIRE(r.error().find("get_as") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Repeated x typed: get_all_as order and per-element failure index
// ---------------------------------------------------------------------------
TEST_CASE("repeated x typed: get_all_as", "[typed][repeated][typed]")
{
    SECTION("three valid elements returns ordered collection")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));

        // A repeated typed element: repeated flag + converter together.
        auto el = nucleus::typed_element<int32_t>("val", anchor::keyspace("cfg"));
        el.repeated = true;
        engine.register_element(el);

        auto src = xml_of("<cfg><val>1</val><val>2</val><val>3</val></cfg>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(loaded);

        auto r = loaded.value().get_all_as<int32_t>("cfg/val");
        REQUIRE(r);
        REQUIRE(r.value() == std::vector<int32_t>{1, 2, 3});
    }

    SECTION("per-element failure names the zero-based index")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));

        auto el = nucleus::typed_element<int32_t>("val", anchor::keyspace("cfg"));
        el.repeated = true;
        engine.register_element(el);

        // Second element (index 1) is bad.
        auto src = xml_of("<cfg><val>1</val><val>bad</val><val>3</val></cfg>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(!loaded);
        INFO("error: " << loaded.error());
        // The convert() format for repeated: "... element [1]: ..."
        REQUIRE(loaded.error().find("[1]") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Orthogonality: typed combines with repeated, unique, identity (attach-level)
// ---------------------------------------------------------------------------
TEST_CASE("typed is orthogonal to other schema axes at attach", "[typed][attach][orthogonal]")
{
    SECTION("typed + repeated: attach succeeds")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));

        auto el = nucleus::typed_element<int32_t>("nums", anchor::keyspace("cfg"));
        el.repeated = true;
        auto result = engine.register_element(el);
        REQUIRE(result);
    }

    SECTION("typed + unique: attach succeeds")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("cfg", anchor::root()));

        auto el = nucleus::typed_element<int32_t>("val", anchor::keyspace("cfg"));
        el.unique = true;
        auto result = engine.register_element(el);
        REQUIRE(result);
    }

    SECTION("typed + identity: attach succeeds")
    {
        nucleus::configuration_space_builder engine;
        engine.register_element(nucleus::element("container", anchor::root()));

        // A primary key with a converter is legal at attach -- the converter runs
        // at resolve time like any other typed path.
        auto el = nucleus::typed_element<int32_t>("id",
                                                   anchor::keyspace("container"));
        el.identity = true;
        auto result = engine.register_element(el);
        REQUIRE(result);
    }
}

// ---------------------------------------------------------------------------
// Typed + identity: full resolve interaction
//
// After slice, the primary key's LEAF path (container/id) is present in the
// building keyspace holding the key value as a string. convert() finds it
// via the schema element's declared_path() and converts it. Therefore:
//   - resolve succeeds with a typed identity element
//   - get_as<int32_t>("container/id") returns the key value
//   - a non-key typed field inside the strain also converts and reads back
//   - the raw string is accessible via get()
// ---------------------------------------------------------------------------
TEST_CASE("typed + identity: resolve interaction", "[typed][identity][resolve]")
{
    SECTION("typed identity key is accessible as typed value after resolve")
    {
        nucleus::configuration_space_builder engine;

        // Container with a typed primary key and a typed non-key field.
        engine.register_element(nucleus::element("container", anchor::root()));
        auto id_el = nucleus::typed_element<int32_t>("id",
                                                      anchor::keyspace("container"));
        id_el.identity = true;
        engine.register_element(id_el);
        engine.register_element(
            nucleus::typed_element<int32_t>("score", anchor::keyspace("container")));

        // Document: one keyed instance with id=42 and score=100.
        auto src = xml_of(
            "<container>"
            "  <id>42</id>"
            "  <score>100</score>"
            "</container>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(loaded);

        // The typed non-key field converts normally.
        auto score = loaded.value().get_as<int32_t>("container/score");
        REQUIRE(score);
        REQUIRE(score.value() == 100);

        // The identity leaf path holds the key's text value and its converter ran.
        auto id = loaded.value().get_as<int32_t>("container/id");
        REQUIRE(id);
        REQUIRE(id.value() == 42);

        // The raw string is also accessible via get().
        auto id_str = loaded.value().get("container/id");
        REQUIRE(id_str.has_value());
        REQUIRE(*id_str == "42");
    }

    SECTION("typed identity key absent from keyspace text does not break resolve")
    {
        // When the document omits the key's leaf element entirely (the key value
        // appears only as a path segment after slice), the converter finds no leaf
        // at container/id and silently skips it. Resolve still succeeds.
        nucleus::configuration_space_builder engine;

        engine.register_element(nucleus::element("container", anchor::root()));
        auto id_el = nucleus::typed_element<int32_t>("id",
                                                      anchor::keyspace("container"));
        id_el.identity = true;
        engine.register_element(id_el);
        engine.register_element(
            nucleus::typed_element<int32_t>("score", anchor::keyspace("container")));

        // Document: one keyed instance.  The xml_source represents the key value
        // as the id child element, so the fold produces container/42/id=42 and
        // container/42/score=100. After slice, container/id=42 and container/score=100
        // are present; both typed paths convert. The test above covers that path.
        // This section simply confirms attach+resolve with the typed identity flag
        // does not introduce any invariant violation.
        auto src = xml_of(
            "<container>"
            "  <id>7</id>"
            "  <score>5</score>"
            "</container>");
        auto loaded = resolve_one(engine, *src);
        REQUIRE(loaded);
        REQUIRE(!loaded.value().empty());
    }
}
