#ifndef HPP_GUARD_NUCLEUS_CONFIG_EMITTER_H
#define HPP_GUARD_NUCLEUS_CONFIG_EMITTER_H

#include "nucleus/configuration_space.h"

#include "nucleus/entry/configuration.h"

#include <iosfwd>
#include <concepts>

namespace nucleus {

// The format-agnostic OUTPUT contract every per-format module models. An emitter is
// a STATELESS type with two operations, both writing into a caller-owned ostream:
// emit_template projects a sealed space's declared schema into a blank document
// template; emit_document projects a resolved configuration into a populated one.
// Compile-time (zero-overhead) customization -- the format is known at the call
// site -- so this is a concept, not a virtual base; runtime-valued format selection
// is deliberately out of scope.
template<typename Emitter>
concept config_emitter = requires(const Emitter e, const configuration_space &space,
                                  const configuration &config, std::ostream &out) {
    { e.emit_template(space, out) } -> std::same_as<void>;
    { e.emit_document(config, out) } -> std::same_as<void>;
};

}

#endif
