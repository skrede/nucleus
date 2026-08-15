#ifndef HPP_GUARD_NUCLEUS_ARGV_ARGV_EMITTER_H
#define HPP_GUARD_NUCLEUS_ARGV_ARGV_EMITTER_H

#include "nucleus/argv/detail/argv_key_render.h"

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/detail/flat_emitter.h"

#include "nucleus/schema/cli_flag.h"

#include <string>
#include <ostream>
#include <string_view>

namespace nucleus::argv {

namespace detail {

inline std::string render_prefix(const cli_delimiter &delimiter,
                                 std::string_view     space_name)
{
    return space_name.empty()
            ? std::string("--")
            : std::string("--") + std::string(space_name) + delimiter.str();
}

}

inline expected<std::string, error> render_template(
        const config_space &space, const cli_delimiter &delimiter = {},
        const key_path &anchor = {}, std::string_view space_name = {})
{
    return nucleus::detail::render_flat_template(
            space, detail::render_prefix(delimiter, space_name), anchor,
            [&delimiter](const key_path &path)
            {
                return detail::render_argv_key(path, delimiter);
            });
}

inline expected<std::string, error> render_document(
        const config &config, const cli_delimiter &delimiter = {},
        const key_path &anchor = {}, std::string_view space_name = {})
{
    return nucleus::detail::render_flat_document(
            config, detail::render_prefix(delimiter, space_name), anchor,
            [&delimiter](const key_path &path)
            {
                return detail::render_argv_key(path, delimiter);
            });
}

inline expected<void, error> emit_template(
        const config_space &space, std::ostream &out,
        const cli_delimiter &delimiter = {}, const key_path &anchor = {},
        std::string_view space_name = {})
{
    auto rendered = render_template(space, delimiter, anchor, space_name);
    if(!rendered)
        return unexpected(rendered.error());
    out << rendered.value();
    return {};
}

inline expected<void, error> emit_document(
        const config &config, std::ostream &out,
        const cli_delimiter &delimiter = {}, const key_path &anchor = {},
        std::string_view space_name = {})
{
    auto rendered = render_document(config, delimiter, anchor, space_name);
    if(!rendered)
        return unexpected(rendered.error());
    out << rendered.value();
    return {};
}

struct emitter
{
    cli_delimiter delimiter;
    key_path      anchor;
    std::string   space_name;

    expected<std::string, error> render_template(const config_space &space) const
    {
        return nucleus::argv::render_template(space, delimiter, anchor, space_name);
    }

    expected<std::string, error> render_document(const config &config) const
    {
        return nucleus::argv::render_document(config, delimiter, anchor, space_name);
    }

    expected<void, error> emit_template(const config_space &space,
                                        std::ostream       &out) const
    {
        return nucleus::argv::emit_template(space, out, delimiter, anchor, space_name);
    }

    expected<void, error> emit_document(const config &config,
                                        std::ostream &out) const
    {
        return nucleus::argv::emit_document(config, out, delimiter, anchor, space_name);
    }
};

}

#endif
