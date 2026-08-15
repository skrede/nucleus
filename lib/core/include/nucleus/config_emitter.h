#ifndef HPP_GUARD_NUCLEUS_CONFIG_EMITTER_H
#define HPP_GUARD_NUCLEUS_CONFIG_EMITTER_H

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "nucleus/expected.h"
#include "nucleus/config_space.h"

#include <string>
#include <concepts>

namespace nucleus {

template<typename Emitter>
concept config_emitter = requires(const Emitter e, const config_space &space,
                                  const config &config) {
    { e.render_template(space) }
      -> std::same_as<expected<std::string, error>>;
    { e.render_document(config, space) }
      -> std::same_as<expected<std::string, error>>;
};

}

#endif
