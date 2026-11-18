#ifndef HPP_GUARD_NUCLEUS_TESTS_COMPLETION_ADMITTED_VALUES_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_COMPLETION_ADMITTED_VALUES_TEST_SUPPORT_H

#include "support/escaped_echo_test_support.h"

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstdint>

namespace nucleus::admitted_values_test {

inline bool admitted(const std::string &value)
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("app", nucleus::anchor::root())));
    return static_cast<bool>(engine.register_element(nucleus::enum_element("mode", escaped_echo_test::under("app"), {value})));
}

inline std::vector<std::string> admitted_bytes()
{
    std::vector<std::string> out;
    for(std::uint32_t byte = 0x00; byte <= 0xff; ++byte)
    {
        const std::string value(1, static_cast<char>(byte));
        if(admitted(value))
            out.push_back(value);
    }
    return out;
}

}

#endif
