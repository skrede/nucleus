#ifndef HPP_GUARD_NUCLEUS_TESTS_SUPPORT_COMPLETION_RESULT_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_SUPPORT_COMPLETION_RESULT_TEST_SUPPORT_H

#include "nucleus/error.h"
#include "nucleus/expected.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace nucleus::completion_result_test {

inline std::string script_of(const expected<std::string, error> &result)
{
    if(!result)
        FAIL(result.error());
    return result.value();
}

}

#endif
