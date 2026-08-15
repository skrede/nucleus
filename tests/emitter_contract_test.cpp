#include "nucleus/error.h"
#include "nucleus/expected.h"
#include "nucleus/config_emitter.h"

#include "nucleus/detail/emitter_delivery.h"

#include <catch2/catch_test_macros.hpp>

#include <ios>
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
    render_template(const nucleus::config_space &) const;

    nucleus::expected<std::string, nucleus::error>
    render_document(const nucleus::config &, const nucleus::config_space &) const;
};

struct stream_only_emitter
{
    nucleus::expected<void, nucleus::error>
    emit_template(const nucleus::config_space &, std::ostream &) const;

    nucleus::expected<void, nucleus::error>
    emit_document(const nucleus::config &, std::ostream &) const;
};

static_assert(nucleus::config_emitter<owned_emitter>);
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

}

TEST_CASE("owned emitter errors precede destination inspection", "[emit][contract]")
{
    const nucleus::error                           rejection{nucleus::errc::malformed_source,
                                                             "render rejected"};
    nucleus::expected<std::string, nucleus::error> failed =
            nucleus::unexpected(rejection);
    std::ostream no_buffer(nullptr);

    auto result = nucleus::detail::deliver_rendered(std::move(failed), no_buffer);

    REQUIRE_FALSE(result);
    CHECK(result.error() == rejection);
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
    CHECK(buffer.write_calls() == 0);
}

TEST_CASE("null destinations report unwritable destination", "[emit][contract]")
{
    std::ostream out(nullptr);
    auto         result = nucleus::detail::deliver_rendered(rendered("rendered"), out);

    REQUIRE_FALSE(result);
    CHECK(result.error().code == nucleus::errc::unwritable_destination);
}

TEST_CASE("delivery accepts exact and empty artifacts without flushing",
          "[emit][contract]")
{
    recording_streambuf buffer(5);
    std::ostream        out(&buffer);

    REQUIRE(nucleus::detail::deliver_rendered(rendered(""), out));
    CHECK(buffer.write_calls() == 0);
    REQUIRE(nucleus::detail::deliver_rendered(rendered("bytes"), out));
    CHECK(buffer.accepted() == "bytes");
    CHECK(buffer.write_calls() == 1);
    CHECK(buffer.sync_calls() == 0);
}

TEST_CASE("zero and prefix acceptance report unwritable destination",
          "[emit][contract]")
{
    recording_streambuf zero_buffer(0);
    std::ostream        zero_out(&zero_buffer);
    auto                zero = nucleus::detail::deliver_rendered(rendered("bytes"), zero_out);
    REQUIRE_FALSE(zero);
    CHECK(zero.error().code == nucleus::errc::unwritable_destination);

    recording_streambuf prefix_buffer(3);
    std::ostream        prefix_out(&prefix_buffer);
    auto                prefix = nucleus::detail::deliver_rendered(rendered("bytes"), prefix_out);
    REQUIRE_FALSE(prefix);
    CHECK(prefix.error().code == nucleus::errc::unwritable_destination);
    CHECK(prefix_buffer.accepted() == "byt");
    CHECK(prefix_buffer.write_calls() == 1);
}

TEST_CASE("delivery count validation prevents narrowing", "[emit][contract]")
{
    CHECK(nucleus::detail::fits_streamsize(0));
    if constexpr(std::numeric_limits<std::size_t>::digits > std::numeric_limits<std::streamsize>::digits)
        CHECK_FALSE(nucleus::detail::fits_streamsize(
                std::numeric_limits<std::size_t>::max()));
}

TEST_CASE("destination failure has a stable machine-readable name",
          "[emit][contract]")
{
    CHECK(nucleus::to_string(nucleus::errc::unwritable_destination) == "unwritable_destination");
}
