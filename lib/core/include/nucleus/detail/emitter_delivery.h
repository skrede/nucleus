#ifndef HPP_GUARD_NUCLEUS_DETAIL_EMITTER_DELIVERY_H
#define HPP_GUARD_NUCLEUS_DETAIL_EMITTER_DELIVERY_H

#include "nucleus/error.h"
#include "nucleus/expected.h"

#include <limits>
#include <string>
#include <cstddef>
#include <ostream>
#include <utility>
#include <streambuf>

namespace nucleus::detail {

constexpr bool fits_streamsize(std::size_t size) noexcept
{
    if constexpr(std::numeric_limits<std::size_t>::digits <= std::numeric_limits<std::streamsize>::digits)
        return true;
    return size <= static_cast<std::size_t>(
                           std::numeric_limits<std::streamsize>::max());
}

inline error destination_error(std::string message)
{
    return error{errc::unwritable_destination, std::move(message)};
}

inline expected<void, error> deliver_rendered(
        expected<std::string, error> rendered, std::ostream &out)
{
    if(!rendered)
        return unexpected(std::move(rendered).error());
    std::streambuf *buffer = out.rdbuf();
    if(!out.good() || buffer == nullptr)
        return unexpected(destination_error("emit: destination is not writable"));
    const std::string &bytes = rendered.value();
    if(!fits_streamsize(bytes.size()))
        return unexpected(destination_error(
                "emit: rendered artifact exceeds the destination count range"));
    if(bytes.empty())
        return {};
    const auto count = static_cast<std::streamsize>(bytes.size());
    if(buffer->sputn(bytes.data(), count) != count)
        return unexpected(destination_error(
                "emit: destination did not accept the complete rendered artifact"));
    return {};
}

}

#endif
