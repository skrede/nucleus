#ifndef HPP_GUARD_NUCLEUS_QUERY_NODE_ROLE_H
#define HPP_GUARD_NUCLEUS_QUERY_NODE_ROLE_H

namespace nucleus {

// The schema-authoritative role of a node in the declared hierarchy.
// Structural kind (leaf/container) and the primary-key and repeated-container
// distinctions are orthogonal to each other; this enum collapses them into
// the four roles the selector API exposes.
enum class node_role
{
    primary_key,
    leaf,
    container,
    repeated_container,
};

}

#endif
