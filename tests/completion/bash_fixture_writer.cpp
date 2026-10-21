#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/cli_flag.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <utility>
#include <iostream>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace {

using nucleus::element;
using nucleus::enum_element;
using nucleus::repeated_element;

std::int32_t refused(std::string_view what, std::string_view why)
{
    std::cerr << "bash-fixture-writer: " << what << ": " << why << '\n';
    return 1;
}

std::int32_t attached(nucleus::schema_registry &reg, nucleus::schema_element element)
{
    const std::string                   name   = element.name;
    const nucleus::schema_attach_result result = reg.attach(std::move(element));
    if(result)
        return 0;
    return refused("attach " + name, result.error());
}

nucleus::schema_registry ordinal_fixture(std::int32_t &failures)
{
    nucleus::schema_registry reg;
    failures += attached(reg, element("cluster", nucleus::anchor::root()));
    failures += attached(reg, repeated_element("node", nucleus::anchor::keyspace("cluster")));
    failures += attached(reg, element("port", nucleus::anchor::keyspace("cluster/node")));
    failures += attached(reg, enum_element("level", nucleus::anchor::keyspace("cluster/node"), {"debug", "info"}));
    return reg;
}

nucleus::schema_registry nested_fixture(std::int32_t &failures)
{
    nucleus::schema_registry reg;
    failures += attached(reg, element("cluster", nucleus::anchor::root()));
    failures += attached(reg, repeated_element("node", nucleus::anchor::keyspace("cluster")));
    failures += attached(reg, repeated_element("route", nucleus::anchor::keyspace("cluster/node")));
    failures += attached(reg, element("port", nucleus::anchor::keyspace("cluster/node/route")));
    return reg;
}

nucleus::schema_registry root_node_fixture(std::int32_t &failures)
{
    nucleus::schema_registry reg;
    failures += attached(reg, repeated_element("node", nucleus::anchor::root()));
    failures += attached(reg, element("port", nucleus::anchor::keyspace("node")));
    return reg;
}

nucleus::schema_registry values_fixture(std::int32_t &failures)
{
    nucleus::schema_registry reg;
    failures += attached(reg, element("cluster", nucleus::anchor::root()));
    failures += attached(reg, repeated_element("node", nucleus::anchor::keyspace("cluster")));
    failures += attached(reg, enum_element("level", nucleus::anchor::keyspace("cluster/node"), {"has space", "x;y", "in$(touch${IFS}PWN_VAL)fo", "*", "~root", "co:lon"}));
    failures += attached(reg, enum_element("mode", nucleus::anchor::keyspace("cluster/node"), {"only"}));
    return reg;
}

nucleus::schema_registry quoted_fixture(std::int32_t &failures)
{
    nucleus::schema_registry reg;
    failures += attached(reg, element("app", nucleus::anchor::root()));
    failures += attached(reg, enum_element("mode", nucleus::anchor::keyspace("app"), {"only one", "has space", "has spice", "it's", "end'", "say\"hi\"", "q\"", "a!b", "b!", "back\\slash", "z'$(touch PWN_SQ)'b", "w\"$(touch PWN_DQ)\"y", "k`touch PWN_BQ`k", "p$(touch PWN_DS)", "x;touch PWN_SEMI", "$(touch PWN_A)"}));
    return reg;
}

nucleus::schema_registry delim_fixture(std::int32_t &failures)
{
    nucleus::schema_registry reg;
    failures += attached(reg, element("cluster", nucleus::anchor::root()));
    failures += attached(reg, element("port", nucleus::anchor::keyspace("cluster")));
    failures += attached(reg, repeated_element("node", nucleus::anchor::keyspace("cluster")));
    failures += attached(reg, element("port", nucleus::anchor::keyspace("cluster/node")));
    return reg;
}

nucleus::schema_registry plain_fixture(std::int32_t &failures)
{
    nucleus::schema_registry reg;
    failures += attached(reg, element("logging", nucleus::anchor::root()));
    failures += attached(reg, enum_element("level", nucleus::anchor::keyspace("logging"), {"debug", "info"}));
    return reg;
}

std::int32_t write_script(const std::filesystem::path &dir, std::string_view name,
                          const nucleus::schema_registry &reg, const nucleus::cli_delimiter &delimiter,
                          const nucleus::key_path &anchor, std::string_view space)
{
    const nucleus::expected<std::string, nucleus::error> script =
            nucleus::generate_completion(nucleus::shell::bash, reg, "myapp", delimiter, anchor, space);
    if(!script)
        return refused(name, "generate: " + script.error().message);
    std::ofstream out(dir / std::filesystem::path(name), std::ios::binary);
    out << *script;
    out.close();
    if(!out)
        return refused(name, "write to " + dir.string() + " failed");
    return 0;
}

std::int32_t write_shapes(const std::filesystem::path &dir, const nucleus::cli_delimiter &star,
                          const nucleus::cli_delimiter &hostile, const nucleus::key_path &anchored)
{
    std::int32_t                   failures  = 0;
    const nucleus::schema_registry ordinal   = ordinal_fixture(failures);
    const nucleus::schema_registry nested    = nested_fixture(failures);
    const nucleus::schema_registry root_node = root_node_fixture(failures);
    const nucleus::schema_registry values    = values_fixture(failures);
    const nucleus::schema_registry quoted    = quoted_fixture(failures);
    const nucleus::schema_registry delim     = delim_fixture(failures);
    const nucleus::schema_registry plain     = plain_fixture(failures);
    failures += write_script(dir, "ordinal.bash", ordinal, {}, {}, {});
    failures += write_script(dir, "nested.bash", nested, {}, {}, {});
    failures += write_script(dir, "star.bash", ordinal, star, {}, {});
    failures += write_script(dir, "spaced.bash", root_node, {}, {}, "a/7");
    failures += write_script(dir, "anchored.bash", ordinal, {}, anchored, {});
    failures += write_script(dir, "values.bash", values, {}, {}, {});
    failures += write_script(dir, "quoted.bash", quoted, {}, {}, {});
    failures += write_script(dir, "delim.bash", delim, hostile, {}, {});
    failures += write_script(dir, "plain.bash", plain, {}, {}, {});
    return failures;
}

std::int32_t write_all(const std::filesystem::path &dir)
{
    const nucleus::expected<nucleus::cli_delimiter, std::string> star     = nucleus::cli_delimiter::parse("*");
    const nucleus::expected<nucleus::cli_delimiter, std::string> hostile  = nucleus::cli_delimiter::parse("$(touch PWN_DELIM)");
    const nucleus::expected<nucleus::key_path, std::string>      anchored = nucleus::key_path::parse("cluster/node");
    if(!star)
        return refused("delimiter", star.error());
    if(!hostile)
        return refused("delimiter", hostile.error());
    if(!anchored)
        return refused("anchor", anchored.error());
    return write_shapes(dir, *star, *hostile, *anchored);
}

}

int main(int argc, char **argv)
{
    const std::vector<std::string> args(argv, argv + argc);
    if(args.size() != 2)
    {
        std::cerr << "usage: bash_fixture_writer <output directory>\n";
        return 2;
    }
    const std::filesystem::path dir(args[1]);
    std::error_code             ec;
    std::filesystem::create_directories(dir, ec);
    if(ec)
        return refused("create " + dir.string(), ec.message());
    return write_all(dir) == 0 ? 0 : 1;
}
