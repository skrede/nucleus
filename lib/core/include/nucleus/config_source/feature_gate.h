#ifndef HPP_GUARD_NUCLEUS_CONFIG_SOURCE_FEATURE_GATE_H
#define HPP_GUARD_NUCLEUS_CONFIG_SOURCE_FEATURE_GATE_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"
#include "nucleus/log_sink.h"
#include "nucleus/capability.h"

#include <string>
#include <vector>
#include <utility>
#include <string_view>

namespace nucleus {

// How badly a consumer needs a capability.
//
// required: the consumer cannot function without it. If the source lacks it,
//           that is a hard, named error -- never a silent half-result.
// optional: the consumer would use it but can proceed without. If the source
//           lacks it, the feature degrades observably (logged + recorded), so
//           the user can find out WHY their data changed shape.
enum class requirement_strength : std::uint8_t
{
    required,
    optional,
};

// A single capability a consumer (a schema, a host) asks a source to provide.
// It names the capability and how strongly it is needed; the gate intersects
// the set of these with a source's declared descriptor.
struct feature_requirement
{
    capability cap;
    requirement_strength strength;
};

// A feature that was asked for optionally but the source could not provide. It
// is surfaced (not dropped silently): emitted through the log_sink at warn level
// and recorded here so it can become value provenance ("field X degraded because
// source 'env' lacks nesting").
struct degradation
{
    capability cap;
    std::string note;
};

// The error a required-but-unsatisfiable capability produces. It names BOTH
// parties -- the requesting consumer and the source's capability shortfall --
// so the diagnostic is actionable, never a bare "unsupported".
using gate_error = error;

// What survives gating: the capabilities a source actually honors for this
// consumer, plus the observable degradations for the optional ones it lacked.
struct gated_features
{
    std::vector<capability> honored;
    std::vector<degradation> degraded;
};

using gate_result = expected<gated_features, gate_error>;

// Computes feature availability as the intersection of a consumer's requirements
// with a source's capabilities, applying the loud-vs-quiet contract:
//
//   * a REQUIRED capability the source lacks -> a loud, named error (both the
//     consumer and the source named) and gating stops.
//   * an OPTIONAL capability the source lacks -> a degradation: a warn-level
//     log_sink message and a recorded provenance note. Resolution proceeds.
//
// This is the mechanism that makes the capability descriptor load-bearing: a
// test can prove behavior CHANGES with the descriptor, which a stub "supports
// everything" descriptor never could.
inline gate_result gate_features(std::string_view consumer,
                                               std::string_view source_name,
                                               const capability_descriptor &caps,
                                               const std::vector<feature_requirement> &required,
                                               log_sink &log)
{
    gated_features out;
    for(const feature_requirement &req : required)
    {
        if(caps.supports(req.cap))
        {
            out.honored.push_back(req.cap);
            continue;
        }

        if(req.strength == requirement_strength::required)
        {
            return unexpected(gate_error{errc::unmet_capability, nucleus::format(
                "source '{}' cannot satisfy capability '{}' required by '{}'",
                source_name, to_string(req.cap), consumer)});
        }

        std::string note = nucleus::format(
            "'{}' degraded: source '{}' lacks optional capability '{}'",
            consumer, source_name, to_string(req.cap));
        log.log(log_level::warn, note);
        out.degraded.push_back(degradation{req.cap, std::move(note)});
    }
    return out;
}

// The whole-stack generalization of gate_features. A HARD capability is satisfied
// when ANY layer in the stack supports it (union): the flat env/argv layers are
// always present, so a single capable layer (e.g. a document) suffices. On a HARD
// shortfall the error names the consumer, the capability, and EVERY layer label
// (none provide it). A SOFT shortfall degrades observably (warn + recorded note)
// and resolution proceeds. An empty stack with a HARD requirement is still loud.
inline gate_result gate_stack(std::string_view consumer,
        const std::vector<std::pair<std::string, capability_descriptor>> &layers,
        const std::vector<feature_requirement> &required,
        log_sink &log)
{
    gated_features out;
    for(const feature_requirement &req : required)
    {
        bool provided = false;
        for(const auto &layer : layers)
        {
            if(layer.second.supports(req.cap))
            {
                provided = true;
                break;
            }
        }
        if(provided)
        {
            out.honored.push_back(req.cap);
            continue;
        }

        // No layer provides it -- name every lacking layer so the diagnostic is actionable.
        std::string lacking;
        for(const auto &layer : layers)
        {
            if(!lacking.empty())
                lacking += ", ";
            lacking += layer.first;
        }

        if(req.strength == requirement_strength::required)
        {
            return unexpected(gate_error{errc::unmet_capability, nucleus::format(
                "no source can satisfy capability '{}' required by '{}'; lacking layers: {}",
                to_string(req.cap), consumer, lacking)});
        }

        std::string note = nucleus::format(
            "'{}' degraded: no source provides optional capability '{}'",
            consumer, to_string(req.cap));
        log.log(log_level::warn, note);
        out.degraded.push_back(degradation{req.cap, std::move(note)});
    }
    return out;
}

}

#endif
