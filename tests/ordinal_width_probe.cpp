#include "nucleus/detail/flat_anchor.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/ordinal_sort_key.h"

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

// Reports a broken expectation and contributes one to the exit status, so a run
// names every property that failed rather than only the first.
std::int32_t broken(bool held, std::string_view what)
{
    if(held)
        return 0;
    std::cerr << "ordinal-width: " << what << " does not hold\n";
    return 1;
}

std::string indexed(const std::string &ordinal)
{
    return "cluster/node[" + ordinal + "]/port";
}

std::int32_t check_parses(const std::string &at_bound)
{
    const auto parsed = nucleus::key_path::parse(indexed(at_bound));
    if(!parsed)
        return broken(false, "the maximum ordinal parses");
    return broken(nucleus::key_path::ordinal_of(parsed->segments()[1]) == nucleus::key_path::max_ordinal,
                  "the parsed ordinal equals the bound") +
            broken(parsed->str() == indexed(at_bound),
                   "the maximum ordinal spells back exactly");
}

std::int32_t check_sorts(const std::string &at_bound)
{
    return broken(nucleus::ordinal_sort_key(indexed("2")) < nucleus::ordinal_sort_key(indexed(at_bound)),
                  "a small ordinal sorts before the maximum") +
            broken(!(nucleus::ordinal_sort_key(indexed(at_bound)) < nucleus::ordinal_sort_key(indexed("2"))),
                   "the maximum ordinal does not sort before a small one");
}

std::int32_t check_anchors(const std::string &at_bound)
{
    const auto path      = nucleus::key_path::parse(indexed(at_bound));
    const auto canonical = nucleus::key_path::parse("cluster/node");
    const auto concrete  = nucleus::key_path::parse("cluster/node[" + at_bound + "]");
    if(!path || !canonical || !concrete)
        return broken(false, "the anchor fixtures parse");
    const auto canonically =
            nucleus::detail::select_flat_path(path.value(), canonical.value());
    const auto concretely =
            nucleus::detail::select_flat_path(path.value(), concrete.value());
    return broken(canonically && canonically.value() && canonically.value()->str() == at_bound + "/port",
                  "a canonical anchor retains the maximum ordinal exactly") +
            broken(concretely && concretely.value() && concretely.value()->str() == "port",
                   "a concrete anchor at the maximum selects its one instance");
}

std::int32_t check_past_bound(const std::string &past_bound)
{
    const auto parsed = nucleus::key_path::parse(indexed(past_bound));
    if(parsed)
        return broken(false, "the first value above the bound is rejected");
    return broken(parsed.error().find("malformed indexed segment") != std::string::npos,
                  "the rejection names the malformed indexed segment");
}

std::int32_t verify_bound()
{
    const std::string at_bound = std::to_string(nucleus::key_path::max_ordinal);
    const std::string past_bound =
            std::to_string(nucleus::key_path::max_ordinal + 1);
    const std::int32_t failures =
            broken(sizeof(std::size_t) == 4, "size_t is four bytes on this target") + check_parses(at_bound) + check_sorts(at_bound) + check_anchors(at_bound) + check_past_bound(past_bound);
    if(failures != 0)
        return 1;
    std::cout << "ordinal-width=32 max-ordinal=" << at_bound << " lossless\n";
    return 0;
}

// The same statuses and diagnostics the driver script reports, for a build whose
// CMake predates the ability to choose a script exit code.
std::int32_t report_unavailable(const std::vector<std::string> &args)
{
    const auto field = [&args](std::size_t i)
    { return i < args.size() ? args[i] : std::string("unspecified"); };
    std::cout << "ordinal-width: 32-bit verification unavailable\n"
              << "ordinal-width: attempted command: " << field(3) << '\n'
              << "ordinal-width: compiler diagnostic: " << field(4) << '\n'
              << "ordinal-width: remediation: " << field(5) << '\n';
    return field(2) == "strict" ? 1 : 77;
}

}

int main(int argc, char **argv)
{
    const std::vector<std::string> args(argv, argv + argc);
    if(args.size() > 1 && args[1] == "--unsupported")
        return report_unavailable(args);
    return verify_bound();
}
