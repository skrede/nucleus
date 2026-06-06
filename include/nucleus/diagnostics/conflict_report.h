#ifndef HPP_GUARD_NUCLEUS_DIAGNOSTICS_CONFLICT_REPORT_H
#define HPP_GUARD_NUCLEUS_DIAGNOSTICS_CONFLICT_REPORT_H

#include "nucleus/format.h"
#include "nucleus/identity.h"

#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

// One party to a key-path collision: a host-readable location label (where the
// claim was made -- a file, a flag, a registration call site) paired with the
// opaque owner token of whoever made it. The token is carried for identity, never
// interpreted; the location is the human-facing "where".
struct claimant
{
    std::string location;
    owner_token owner;
};

// A report that two-or-more registrations claim the same key path. It deliberately
// does NOT pick a winner: the core's job is to surface who claimed what and where,
// and leave adjudication to the host (which alone knows what its owner tokens
// mean). This is the conflict/provenance reporting the flat-ownership design
// promises -- mechanism, not policy.
class conflict_report
{
public:
    explicit conflict_report(std::string key_path) : m_key(std::move(key_path)) {}

    conflict_report &add(claimant who)
    {
        m_claimants.push_back(std::move(who));
        return *this;
    }

    [[nodiscard]] const std::string &key_path() const noexcept { return m_key; }

    [[nodiscard]] const std::vector<claimant> &claimants() const noexcept
    {
        return m_claimants;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_claimants.size(); }

    // A non-adjudicating, human-readable rendering: it names the colliding key and
    // every claimant's location, explicitly stating that no winner is chosen. The
    // host decides what to do; the engine only reports.
    [[nodiscard]] std::string describe() const
    {
        std::string out = ::nucleus::format(
            "key '{}' is claimed by {} registrations:", m_key, m_claimants.size());
        for(const claimant &who : m_claimants)
            out += ::nucleus::format("\n  - {}", who.location);
        out += "\nno winner is chosen; the host adjudicates ownership.";
        return out;
    }

private:
    std::string m_key;
    std::vector<claimant> m_claimants;
};

}

#endif
