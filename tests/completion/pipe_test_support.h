#ifndef HPP_GUARD_NUCLEUS_TESTS_COMPLETION_PIPE_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_COMPLETION_PIPE_TEST_SUPPORT_H

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <cstdint>

namespace nucleus::bash_test {

#ifdef _MSC_VER
inline std::int32_t close_pipe(std::FILE *pipe) { return _pclose(pipe); }
inline std::FILE   *open_pipe(const std::string &command) { return _popen(command.c_str(), "r"); }
#else
inline std::int32_t close_pipe(std::FILE *pipe) { return pclose(pipe); }
inline std::FILE   *open_pipe(const std::string &command) { return popen(command.c_str(), "r"); }
#endif

using pipe_handle = std::unique_ptr<std::FILE, decltype(&close_pipe)>;

inline pipe_handle opened_pipe(const std::string &command) { return pipe_handle(open_pipe(command), &close_pipe); }

inline std::string run_captured(const std::string &command)
{
    std::string  captured;
    std::int32_t status = -1;
    if(pipe_handle pipe = opened_pipe(command))
    {
        std::array<char, 256> buf{};
        while(std::fgets(buf.data(), static_cast<int>(buf.size()), pipe.get()))
            captured += buf.data();
        status = close_pipe(pipe.release());
    }
    INFO(captured);
    REQUIRE(status == 0);
    return captured;
}
}

#endif
