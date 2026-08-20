#ifndef HPP_GUARD_NUCLEUS_TESTS_XML_PERSIST_ARTIFACT_H
#define HPP_GUARD_NUCLEUS_TESTS_XML_PERSIST_ARTIFACT_H

#include "nucleus/expected.h"

#include <cerrno>
#include <random>
#include <string>
#include <cstdint>
#include <fstream>
#include <ostream>
#include <sstream>
#include <utility>
#include <filesystem>
#include <functional>
#include <string_view>
#include <system_error>

namespace nucleus::xml_persist_test {

// The directory to try for one claim attempt; an empty path ends the sequence.
// Injectable so a test can preclaim a candidate and drive the retry branch without
// racing a second process for it.
using candidate_cb = std::function<std::filesystem::path(std::int32_t)>;

inline std::string artifact_failure(std::string_view             operation,
                                    const std::filesystem::path &path,
                                    const std::error_code       &code)
{
    return std::string(operation) + " '" + path.string() + "': " + code.message();
}

inline std::error_code stream_error() noexcept
{
    return std::error_code(errno, std::generic_category());
}

// Two live fixtures in one process share the seed and therefore collide on the
// first candidate, which the claim retry resolves into distinct directories.
inline std::filesystem::path unique_candidate(std::int32_t attempt)
{
    static const std::uint64_t seed =
            static_cast<std::uint64_t>(std::random_device{}());
    std::error_code             code;
    const std::filesystem::path root = std::filesystem::temp_directory_path(code);
    if(code)
        return {};
    return root / ("nucleus_xml_persist_" + std::to_string(seed) + "_" + std::to_string(attempt));
}

// A test-owned temporary directory holding one file. create_directory reports an
// existing directory rather than adopting it, so no two live fixtures can share
// one and truncating creation inside is collision-safe without a fixed filename.
class temporary_artifact
{
public:
    temporary_artifact(const temporary_artifact &)            = delete;
    temporary_artifact &operator=(const temporary_artifact &) = delete;
    temporary_artifact &operator=(temporary_artifact &&)      = delete;

    temporary_artifact(temporary_artifact &&other) noexcept
            : m_directory(std::move(other.m_directory))
            , m_file(std::move(other.m_file))
            , m_out(std::move(other.m_out))
    {
        other.m_directory.clear();
        other.m_file.clear();
    }

    // A last-resort fallback for a failed test only: the success path calls
    // clean_up() and checks it, which leaves nothing here to remove.
    ~temporary_artifact()
    {
        if(m_directory.empty())
            return;
        std::error_code code;
        std::filesystem::remove_all(m_directory, code);
    }

    static expected<temporary_artifact, std::string> claim(
            std::string file_name, const candidate_cb &next = unique_candidate)
    {
        for(std::int32_t attempt = 0;; ++attempt)
        {
            const std::filesystem::path candidate = next(attempt);
            if(candidate.empty())
                return unexpected(std::string("no candidate directory remains"));
            std::error_code code;
            if(std::filesystem::create_directory(candidate, code))
                return temporary_artifact(candidate, std::move(file_name));
            if(code)
                return unexpected(
                        artifact_failure("create_directory", candidate, code));
        }
    }

    const std::filesystem::path &file() const noexcept { return m_file; }

    std::ostream &out() noexcept { return m_out; }

    expected<void, std::string> open_out()
    {
        m_out.open(m_file, std::ios::out | std::ios::trunc | std::ios::binary);
        if(!m_out.is_open())
            return unexpected(artifact_failure("open for write", m_file, stream_error()));
        return {};
    }

    expected<void, std::string> flush_and_close()
    {
        m_out.flush();
        if(!m_out.good())
            return unexpected(artifact_failure("flush", m_file, stream_error()));
        m_out.close();
        if(m_out.is_open() || m_out.fail())
            return unexpected(artifact_failure("close", m_file, stream_error()));
        return {};
    }

    expected<std::string, std::string> read() const
    {
        std::ifstream input(m_file, std::ios::in | std::ios::binary);
        if(!input.is_open())
            return unexpected(artifact_failure("open for read", m_file, stream_error()));
        std::ostringstream buffer;
        buffer << input.rdbuf();
        if(input.bad())
            return unexpected(artifact_failure("read", m_file, stream_error()));
        input.close();
        if(input.is_open())
            return unexpected(artifact_failure("close after read", m_file, stream_error()));
        return buffer.str();
    }

    expected<void, std::string> clean_up()
    {
        std::error_code code;
        std::filesystem::remove(m_file, code);
        if(code)
            return unexpected(artifact_failure("remove", m_file, code));
        if(!std::filesystem::remove(m_directory, code) || code)
            return unexpected(artifact_failure("remove", m_directory, code));
        m_directory.clear();
        m_file.clear();
        return {};
    }

private:
    temporary_artifact(std::filesystem::path directory, const std::string &file_name)
            : m_directory(std::move(directory))
            , m_file(m_directory / file_name)
    {
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_file;
    std::ofstream         m_out;
};

}

#endif
