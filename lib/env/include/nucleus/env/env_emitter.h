#ifndef HPP_GUARD_NUCLEUS_ENV_ENV_EMITTER_H
#define HPP_GUARD_NUCLEUS_ENV_ENV_EMITTER_H

#include "nucleus/env/detail/env_key_render.h"

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/detail/flat_emitter.h"

#include <string>
#include <ostream>

namespace nucleus::env {

inline expected<std::string, error> render_template(const config_space &space)
{
    return nucleus::detail::render_flat_template(
            space, "", {}, [](const key_path &path)
            { return detail::render_env_key(path); });
}

inline expected<std::string, error> render_document(const config &config)
{
    return nucleus::detail::render_flat_document(
            config, "", {}, [](const key_path &path)
            { return detail::render_env_key(path); });
}

inline expected<void, error> emit_template(const config_space &space,
                                           std::ostream       &out)
{
    auto rendered = render_template(space);
    if(!rendered)
        return unexpected(rendered.error());
    out << rendered.value();
    return {};
}

inline expected<void, error> emit_document(const config &config,
                                           std::ostream &out)
{
    auto rendered = render_document(config);
    if(!rendered)
        return unexpected(rendered.error());
    out << rendered.value();
    return {};
}

struct emitter
{
    static expected<std::string, error> render_template(const config_space &space)
    {
        return nucleus::env::render_template(space);
    }

    static expected<std::string, error> render_document(const config &config)
    {
        return nucleus::env::render_document(config);
    }

    static expected<void, error> emit_template(const config_space &space,
                                               std::ostream       &out)
    {
        return nucleus::env::emit_template(space, out);
    }

    static expected<void, error> emit_document(const config &config,
                                               std::ostream &out)
    {
        return nucleus::env::emit_document(config, out);
    }
};

}

#endif
