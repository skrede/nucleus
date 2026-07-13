#ifndef HPP_GUARD_NUCLEUS_SCHEMA_ANCHOR_H
#define HPP_GUARD_NUCLEUS_SCHEMA_ANCHOR_H

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <utility>

namespace nucleus {

// The typed keyspace position a schema element hangs under -- code/schema-side only,
// never in document text. Two forms: anchor::root() introduces a new top-level
// keyspace; anchor::keyspace("name") attaches under an already-defined node. The
// distinction is the referential-integrity hinge a keyspace anchor may only attach
// under a node that already exists (enforced by schema_registry).
class anchor
{
public:
    // The top of the keyspace. An element anchored here introduces its own name
    // as a new top-level keyspace.
    static anchor root() { return anchor(key_path{}); }

    // A position under an already-defined keyspace path. The path is the FQN of
    // the node the element attaches under (e.g. "net" or "net/udp").
    static anchor keyspace(key_path under)
    {
        return anchor(std::move(under));
    }

    // Convenience: build a keyspace anchor from a `/`-separated string. A malformed
    // path yields an invalid anchor that carries the offending string; the
    // registration seam rejects it loudly with errc::malformed_source rather than
    // silently re-anchoring at root.
    static anchor keyspace(const std::string &under)
    {
        if(auto parsed = key_path::parse(under); parsed)
            return anchor(std::move(parsed).value());
        return anchor(invalid_tag{}, under);
    }

    bool is_root() const noexcept { return !m_invalid && m_under.empty(); }

    // True when keyspace(string) was handed a malformed path. Such an anchor never
    // attaches: the registration entry points reject it before attach.
    bool is_invalid() const noexcept { return m_invalid; }

    // The path this anchor attaches under. Empty for the root and invalid anchors.
    const key_path &under() const noexcept { return m_under; }

    // The offending string when this anchor is invalid; empty otherwise.
    const std::string &invalid_path() const noexcept { return m_invalid_path; }

private:
    struct invalid_tag {};

    explicit anchor(key_path under) : m_under(std::move(under)) {}
    anchor(invalid_tag, std::string bad) : m_invalid_path(std::move(bad)), m_invalid(true) {}

    key_path    m_under;
    std::string m_invalid_path;
    bool        m_invalid = false;
};

}

#endif
