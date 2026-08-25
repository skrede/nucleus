#ifndef HPP_GUARD_NUCLEUS_TESTS_SUPPORT_BUILDER_RESULT_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_SUPPORT_BUILDER_RESULT_TEST_SUPPORT_H

#include "nucleus/error.h"
#include "nucleus/expected.h"
#include "nucleus/config_space.h"

#include <catch2/catch_test_macros.hpp>

#include <utility>

namespace nucleus::builder_result_test {

inline config_space built(config_space_builder &builder)
{
    expected<config_space, error> sealed = builder.build();
    REQUIRE(sealed);
    return std::move(sealed).value();
}

inline config_space built(config_space_builder &&builder)
{
    return built(builder);
}

}

#endif
