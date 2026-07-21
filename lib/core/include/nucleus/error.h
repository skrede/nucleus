#ifndef HPP_GUARD_NUCLEUS_ERROR_H
#define HPP_GUARD_NUCLEUS_ERROR_H

#include <string>
#include <ostream>
#include <string_view>
#include <system_error>

namespace nucleus {

// The machine-readable identity of every failure the library reports. The codes
// follow the load pipeline (source -> inheritance -> gate -> fold -> tokens ->
// selection -> schema -> conversion), then registration, then typed reads, then
// query terminals. A host branches on the code; the human detail travels in error::message.
enum class errc
{
    unreadable_source,
    malformed_source,
    invalid_inheritance,
    unmet_capability,
    layering_violation,
    unresolved_token,
    invalid_selection,
    schema_violation,
    failed_conversion,
    rejected_registration,
    sealed_builder,
    absent_key,
    index_required,
    missing_converter,
    mismatched_type,
    ambiguous_result,
};

constexpr std::string_view to_string(errc code) noexcept
{
    switch(code)
    {
        case errc::unreadable_source:     return "unreadable_source";
        case errc::malformed_source:      return "malformed_source";
        case errc::invalid_inheritance:   return "invalid_inheritance";
        case errc::unmet_capability:      return "unmet_capability";
        case errc::layering_violation:    return "layering_violation";
        case errc::unresolved_token:      return "unresolved_token";
        case errc::invalid_selection:     return "invalid_selection";
        case errc::schema_violation:      return "schema_violation";
        case errc::failed_conversion:     return "failed_conversion";
        case errc::rejected_registration: return "rejected_registration";
        case errc::sealed_builder:        return "sealed_builder";
        case errc::absent_key:            return "absent_key";
        case errc::index_required:        return "index_required";
        case errc::missing_converter:     return "missing_converter";
        case errc::mismatched_type:       return "mismatched_type";
        case errc::ambiguous_result:      return "ambiguous_result";
    }
    return "unknown";
}

// The error payload of every public result channel: a code for programs and a
// verbatim human-readable reason. Host-supplied converters and registration
// policies traffic in plain message strings; the engine attaches the code at
// the seam where the failure class is known.
struct error
{
    errc        code;
    std::string message;

    friend bool operator==(const error &, const error &) = default;
};

inline std::string to_string(const error &e)
{
    std::string out(to_string(e.code));
    out += ": ";
    out += e.message;
    return out;
}

inline std::ostream &operator<<(std::ostream &os, const error &e)
{
    return os << to_string(e.code) << ": " << e.message;
}

namespace detail {
class errc_category final : public std::error_category
{
public:
    const char *name() const noexcept override { return "nucleus"; }
    std::string message(int code) const override
    {
        return std::string(to_string(static_cast<errc>(code)));
    }
};
}

// std::error_code interop for errc: a singleton category whose message() reuses
// to_string(errc), so an errc drops into any std::error_code sink. Additive --
// the native result channel still carries error (code + verbatim message).
inline const std::error_category &errc_category() noexcept
{
    static const detail::errc_category instance;
    return instance;
}

inline std::error_code make_error_code(errc code) noexcept
{
    return std::error_code(static_cast<int>(code), errc_category());
}

}

template <>
struct std::is_error_code_enum<nucleus::errc> : std::true_type {};

#endif
