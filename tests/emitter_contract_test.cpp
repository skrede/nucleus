#include "nucleus/error.h"
#include "nucleus/expected.h"
#include "nucleus/config_emitter.h"

#include "nucleus/detail/emitter_delivery.h"

#include "nucleus/env/env_emitter.h"
#include "nucleus/argv/argv_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <ios>
#include <array>
#include <limits>
#include <string>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <streambuf>

namespace {

struct owned_emitter
{
    nucleus::expected<std::string, nucleus::error>
    render_template(const nucleus::config_space &) const
    {
        return std::string{};
    }

    nucleus::expected<std::string, nucleus::error>
    render_document(const nucleus::config &, const nucleus::config_space &) const
    {
        return std::string{};
    }
};

struct stream_only_emitter
{
    nucleus::expected<void, nucleus::error>
    emit_template(const nucleus::config_space &, std::ostream &) const
    {
        return {};
    }

    nucleus::expected<void, nucleus::error>
    emit_document(const nucleus::config &, std::ostream &) const
    {
        return {};
    }
};

static_assert(nucleus::config_emitter<owned_emitter>);
static_assert(nucleus::config_emitter<nucleus::env::emitter>);
static_assert(nucleus::config_emitter<nucleus::argv::emitter>);
static_assert(!nucleus::config_emitter<stream_only_emitter>);

class recording_streambuf final : public std::streambuf
{
public:
    explicit recording_streambuf(std::streamsize limit)
            : m_accepted()
            , m_limit(limit)
            , m_write_calls(0)
            , m_sync_calls(0)
    {
    }

    const std::string &accepted() const noexcept { return m_accepted; }

    std::uint32_t write_calls() const noexcept { return m_write_calls; }

    std::uint32_t sync_calls() const noexcept { return m_sync_calls; }

private:
    std::string     m_accepted;
    std::streamsize m_limit;
    std::uint32_t   m_write_calls;
    std::uint32_t   m_sync_calls;

    std::streamsize xsputn(const char *data, std::streamsize count) override
    {
        ++m_write_calls;
        const std::streamsize accepted = count < m_limit ? count : m_limit;
        m_accepted.append(data, static_cast<std::size_t>(accepted));
        return accepted;
    }

    int sync() override
    {
        ++m_sync_calls;
        return 0;
    }
};

nucleus::expected<std::string, nucleus::error> rendered(std::string bytes)
{
    return bytes;
}

void check_delivery(std::size_t size, std::streamsize limit)
{
    const std::string   bytes(size, 'x');
    recording_streambuf buffer(limit);
    std::ostream        out(&buffer);
    auto                result   = nucleus::detail::deliver_rendered(rendered(bytes), out);
    const auto          count    = static_cast<std::streamsize>(size);
    const auto          accepted = count < limit ? count : limit;
    if(accepted == count)
        REQUIRE(result);
    else
    {
        REQUIRE_FALSE(result);
        CHECK(result.error().code == nucleus::errc::unwritable_destination);
        CHECK(result.error().message ==
              "emit: destination did not accept the complete rendered artifact");
    }
    CHECK(buffer.accepted() == bytes.substr(0, static_cast<std::size_t>(accepted)));
    CHECK(buffer.write_calls() == (bytes.empty() ? 0U : 1U));
    CHECK(buffer.sync_calls() == 0);
}

void sweep_delivery(std::size_t size)
{
    check_delivery(size, 0);
    if(size > 1)
        check_delivery(size, 1);
    if(size > 2)
        check_delivery(size, static_cast<std::streamsize>(size - 1));
    if(size != 0)
        check_delivery(size, static_cast<std::streamsize>(size));
}

}

TEST_CASE("owned emitter errors precede destination inspection", "[emit][contract]")
{
    const nucleus::error                           rejection{nucleus::errc::malformed_source,
                                                             "render rejected"};
    nucleus::expected<std::string, nucleus::error> failed =
            nucleus::unexpected(rejection);
    recording_streambuf buffer(8);
    std::ostream        out(&buffer);

    auto result = nucleus::detail::deliver_rendered(std::move(failed), out);

    REQUIRE_FALSE(result);
    CHECK(result.error() == rejection);
    CHECK(buffer.write_calls() == 0);
    CHECK(buffer.sync_calls() == 0);
}

TEST_CASE("failed destinations reject before stream-buffer delivery",
          "[emit][contract]")
{
    recording_streambuf buffer(8);
    std::ostream        out(&buffer);
    out.setstate(std::ios::badbit);

    auto result = nucleus::detail::deliver_rendered(rendered("rendered"), out);

    REQUIRE_FALSE(result);
    CHECK(result.error().code == nucleus::errc::unwritable_destination);
    CHECK(result.error().message == "emit: destination is not writable");
    CHECK(buffer.write_calls() == 0);
    CHECK(buffer.sync_calls() == 0);
}

TEST_CASE("null destinations report unwritable destination", "[emit][contract]")
{
    std::ostream out(nullptr);
    auto         result = nucleus::detail::deliver_rendered(rendered("rendered"), out);

    REQUIRE_FALSE(result);
    CHECK(result.error().code == nucleus::errc::unwritable_destination);
    CHECK(result.error().message == "emit: destination is not writable");
}

TEST_CASE("delivery sweeps exact acceptance counts without flushing",
          "[emit][contract][matrix]")
{
    constexpr std::array<std::size_t, 4> sizes{0, 1, 2, 17};
    for(const std::size_t size : sizes)
    {
        CAPTURE(size);
        sweep_delivery(size);
    }
}

TEST_CASE("delivery count validation prevents narrowing", "[emit][contract]")
{
    CHECK(nucleus::detail::fits_streamsize(0));
    if constexpr(std::numeric_limits<std::size_t>::digits > std::numeric_limits<std::streamsize>::digits)
        CHECK_FALSE(nucleus::detail::fits_streamsize(
                std::numeric_limits<std::size_t>::max()));
}
