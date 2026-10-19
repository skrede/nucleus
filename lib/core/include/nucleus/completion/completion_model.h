#ifndef HPP_GUARD_NUCLEUS_COMPLETION_COMPLETION_MODEL_H
#define HPP_GUARD_NUCLEUS_COMPLETION_COMPLETION_MODEL_H

#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <functional>

namespace nucleus {

// The shell-neutral description of one completable flag: the flag text exactly as
// the CLI surface projects it (e.g. "--logging-level"), an optional human-readable
// description, and the closed set of values it accepts (empty = unconstrained, so
// only the flag name completes). ordinal_slots holds the byte offset of each '*' in
// flag that stands for an instance ordinal, one per repeated container the path
// crosses (e.g. {15} for "--cluster-node-*-endpoint-port"). Positions rather than
// the byte mark the slots, so a delimiter carrying '*' stays literal text. A slot list
// is used only when it is strictly ascending and every slot is a '*' inside flag; any
// other list is ignored, and the option completes as its literal flag.
// This carries no shell syntax whatsoever -- no quoting, no word-break handling,
// no escaping. Those concerns belong to the per-shell emitter, never to this model
// or the generator that builds it.
struct completion_option
{
    std::string flag;
    std::string description;
    std::vector<std::string> values;
    std::vector<std::size_t> ordinal_slots;
};

// The whole completion surface for one program: the program name a shell binds the
// completion to, plus every projected option in a stable, deterministic order (so
// the generated script is reproducible byte-for-byte). The generator builds this
// purely from the schema; each shell emitter renders it into that shell's dialect.
struct completion_model
{
    std::string prog;
    std::vector<completion_option> options;
};

inline bool has_ordinal_slots(const completion_option &option)
{
    const std::vector<std::size_t> &slots = option.ordinal_slots;
    if(slots.empty() || std::ranges::adjacent_find(slots, std::greater_equal<>{}) != slots.end())
        return false;
    return std::ranges::all_of(slots, [&option](const std::size_t slot)
                               { return slot < option.flag.size() && option.flag[slot] == '*'; });
}

// The literal flag text between ordinal slots: k slots give k + 1 pieces, and an
// option without usable slots gives its whole flag.
inline std::vector<std::string> ordinal_pieces(const completion_option &option)
{
    if(!has_ordinal_slots(option))
        return {option.flag};
    std::vector<std::string> pieces;
    std::size_t              start = 0;
    for(const std::size_t slot : option.ordinal_slots)
    {
        pieces.push_back(option.flag.substr(start, slot - start));
        start = slot + 1;
    }
    pieces.push_back(option.flag.substr(start));
    return pieces;
}

inline bool has_ordinal_options(const completion_model &model)
{
    return std::ranges::any_of(model.options, has_ordinal_slots);
}
}

#endif
