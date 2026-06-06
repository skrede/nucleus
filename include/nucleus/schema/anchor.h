#ifndef HPP_GUARD_NUCLEUS_SCHEMA_ANCHOR_H
#define HPP_GUARD_NUCLEUS_SCHEMA_ANCHOR_H

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <utility>

namespace nucleus {

// The typed anchor a host attaches a schema registration to. An anchor names the
// keyspace position a schema element hangs under -- it is code/schema-side only
// and NEVER appears in document text (a document carries values, not anchors).
//
// Two forms, both typed values (not strings the way the keyspace path is):
//
//   anchor::root()            -- attach a top-level keyspace (e.g. "plexus")
//   anchor::keyspace("name")  -- attach UNDER an already-defined keyspace node
//
// The distinction is the referential-integrity hinge: a root anchor introduces a
// new top-level keyspace; a keyspace anchor may only resolve if that keyspace was
// already defined. The registry enforces that (see schema_registry).
class anchor
{
public:
    // The top of the keyspace. An element anchored here introduces its own name
    // as a new top-level keyspace.
    [[nodiscard]] static anchor root() { return anchor(key_path{}); }

    // A position under an already-defined keyspace path. The path is the FQN of
    // the node the element attaches under (e.g. "plexus" or "plexus/udp").
    [[nodiscard]] static anchor keyspace(key_path under)
    {
        return anchor(std::move(under));
    }

    // Convenience: build a keyspace anchor from a `/`-separated string. Malformed
    // strings collapse to the root anchor; hosts that need validation parse the
    // path themselves and pass it to keyspace(key_path).
    [[nodiscard]] static anchor keyspace(const std::string &under)
    {
        if(auto parsed = key_path::parse(under); parsed)
            return anchor(std::move(parsed).value());
        return root();
    }

    [[nodiscard]] bool is_root() const noexcept { return m_under.empty(); }

    // The path this anchor attaches under. Empty for the root anchor.
    [[nodiscard]] const key_path &under() const noexcept { return m_under; }

private:
    explicit anchor(key_path under) : m_under(std::move(under)) {}

    key_path m_under;
};

}

#endif
