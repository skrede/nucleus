#ifndef HPP_GUARD_NUCLEUS_RESOLVE_KEYED_MERGE_RULES_H
#define HPP_GUARD_NUCLEUS_RESOLVE_KEYED_MERGE_RULES_H

#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/keyed_merge_state.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/provenance.h"

#include <map>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <algorithm>

namespace nucleus {

// The composition rules of one keyed collection: how diverted leaves group into
// source instances, the order those instances compose in (defining-layer rank,
// then source ordinal -- never the arrival order of the accumulator), and which
// instances survive under each keyed merge mode. Holds no state; every rule is a
// pure function of the instances handed to it.
class keyed_merge_rules
{
public:
    // One source instance: the layer rank and per-layer ordinal identifying it, its
    // merge-key value, and the diverted leaves that belong to it.
    struct merged_instance
    {
        merged_instance()
            : rank(0)
            , ordinal(0)
            , has_key(false)
        {
        }

        std::size_t rank;
        std::size_t ordinal;
        std::string key;
        bool        has_key;
        origin      prov;
        std::vector<keyed_merge_state::keyed_instance_entry *> leaves;
    };

    using instance_key = std::pair<std::size_t, std::size_t>;

    static std::map<instance_key, merged_instance>
    group_by_instance(std::vector<keyed_merge_state::keyed_instance_entry> &entries,
                      const std::string &field)
    {
        std::map<instance_key, merged_instance> grouped;
        for(keyed_merge_state::keyed_instance_entry &e : entries)
        {
            merged_instance &mi = grouped[{e.source_rank, e.source_ordinal}];
            mi.rank = e.source_rank;
            mi.ordinal = e.source_ordinal;
            mi.leaves.push_back(&e);
            if(e.suffix == field)
            {
                mi.key = e.value;
                mi.has_key = true;
                mi.prov = e.prov;
            }
        }
        return grouped;
    }

    static std::vector<merged_instance *>
    ordered_instances(std::map<instance_key, merged_instance> &grouped)
    {
        std::vector<merged_instance *> instances;
        instances.reserve(grouped.size());
        for(auto &[key, mi] : grouped)
            instances.push_back(&mi);
        // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order): ordered by the stable rank and ordinal keys, not by pointer address, so the result is deterministic.
        std::sort(instances.begin(), instances.end(),
                  [](const merged_instance *a, const merged_instance *b) {
                      return a->rank != b->rank ? a->rank < b->rank
                                                : a->ordinal < b->ordinal;
                  });
        return instances;
    }

    static expected<void, resolve_fold_error>
    require_merge_key(const std::vector<merged_instance *> &instances,
                      const std::string &canonical, const std::string &field)
    {
        for(const merged_instance *mi : instances)
            if(!mi->has_key)
                return unexpected(error{errc::schema_violation, nucleus::format(
                    "keyed collection '{}' has an instance with no '{}' identifier; "
                    "the keyed merge modes require the merge key on every element",
                    canonical, field)});
        return {};
    }

    static expected<std::vector<merged_instance *>, resolve_fold_error>
    united_survivors(const std::vector<merged_instance *> &instances,
                     const std::string &canonical, const std::string &field)
    {
        std::vector<merged_instance *> survivors;
        std::map<std::string, merged_instance *> by_key;
        for(merged_instance *mi : instances)
        {
            const auto it = by_key.find(mi->key);
            const merged_instance *seen = it == by_key.end() ? nullptr : it->second;
            if(auto additive = reject_reintroduction(seen, mi, canonical, field); !additive)
                return unexpected(additive.error());
            by_key.emplace(mi->key, mi);
            survivors.push_back(mi);
        }
        return survivors;
    }

    // The highest-rank instance per key wins.
    static expected<std::vector<merged_instance *>, resolve_fold_error>
    replaced_survivors(const std::vector<merged_instance *> &instances,
                       const std::string &canonical, const std::string &field)
    {
        std::vector<merged_instance *> survivors;
        std::map<std::string, std::size_t> winner;
        for(merged_instance *mi : instances)
        {
            const auto it = winner.find(mi->key);
            if(it == winner.end())
            {
                winner[mi->key] = survivors.size();
                survivors.push_back(mi);
            }
            else if(survivors[it->second]->rank == mi->rank)
                return unexpected(error{errc::layering_violation, nucleus::format(
                    "keyed collection '{}': identifier '{}'='{}' is duplicated "
                    "within layer '{}'; replace_by_key composes across layers but "
                    "does not admit duplicate identifiers within one layer",
                    canonical, field, mi->key, mi->prov.layer)});
            else
                survivors[it->second] = mi;
        }
        return survivors;
    }

private:
    // unite is strict-additive in both directions: an identifier may appear at most
    // once within one layer, and may not be reintroduced by a higher layer either.
    static expected<void, resolve_fold_error>
    reject_reintroduction(const merged_instance *seen, const merged_instance *mi,
                          const std::string &canonical, const std::string &field)
    {
        if(seen == nullptr)
            return {};
        if(seen->rank == mi->rank)
            return unexpected(error{errc::layering_violation, nucleus::format(
                "keyed collection '{}': identifier '{}'='{}' is duplicated "
                "within layer '{}'; unite is strict-additive (no duplicate "
                "identifiers within one layer)",
                canonical, field, mi->key, mi->prov.layer)});
        return unexpected(error{errc::layering_violation, nucleus::format(
            "keyed collection '{}': identifier '{}'='{}' is introduced at "
            "two layers ('{}' and '{}'); unite is strict-additive "
            "(no override across layers)",
            canonical, field, mi->key, seen->prov.layer, mi->prov.layer)});
    }
};

}

#endif
