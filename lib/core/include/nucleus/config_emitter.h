#ifndef HPP_GUARD_NUCLEUS_CONFIG_EMITTER_H
#define HPP_GUARD_NUCLEUS_CONFIG_EMITTER_H

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "nucleus/expected.h"
#include "nucleus/config_space.h"

#include <iosfwd>
#include <concepts>

namespace nucleus {

// The format-agnostic OUTPUT contract every per-format module models. An emitter is
// a STATELESS type with two operations, both writing into a caller-owned ostream:
// emit_template projects a sealed space's declared schema into a blank document
// template; emit_document projects a resolved config into a populated one.
// Compile-time (zero-overhead) customization -- the format is known at the call
// site -- so this is a concept, not a virtual base; runtime-valued format selection
// is deliberately out of scope.
// Partial-write contract (all-or-nothing): on any emit failure nothing is written
// to the caller-owned out; a returned error leaves the stream as it was on entry.
template<typename Emitter>
concept config_emitter = requires(const Emitter e, const config_space &space,
                                  const config &config, std::ostream &out) {
    { e.emit_template(space, out) } -> std::same_as<expected<void, error>>;
    { e.emit_document(config, out) } -> std::same_as<expected<void, error>>;
};

}

#endif
