#include "nucleus/config.h"

#include "nucleus/env/env_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <sstream>
#include <utility>

namespace {

nucleus::config checked_config(std::map<std::string, std::string> values)
{
    auto made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}

}

TEST_CASE("selected line-breaking values fail before stream output",
          "[flat][emit][env][error]")
{
    for(const std::string &value : {std::string("line\nbreak"),
                                    std::string("line\rbreak")})
    {
        const nucleus::config config = checked_config({{"cluster/port", value}});
        std::ostringstream    output;
        const auto            result = nucleus::env::emit_document(config, output);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code == nucleus::errc::malformed_source);
        REQUIRE(result.error().message.find("cluster/port") != std::string::npos);
        REQUIRE(output.str().empty());
    }
}
