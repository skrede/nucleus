#ifndef HPP_GUARD_NUCLEUS_TESTS_COMPLETION_BASH_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_COMPLETION_BASH_TEST_SUPPORT_H

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace nucleus::bash_test {

// The smoke tests run only when the bash first on PATH is 5.0 or later, the version the generated script
// registers its completion on; below it they would drive functions bash never registers. Windows skips:
// handing a Git-Bash a native temp-file path crosses an unverified path-translation boundary.
inline bool bash_available()
{
#ifdef _WIN32
    return false;
#else
    return std::system("bash -c '((BASH_VERSINFO[0] >= 5))' >/dev/null 2>&1") == 0;
#endif
}

// Two executables drive bash at once under a parallel CTest, so each scratch tree is created
// exclusively under a name unique to its guard, and neither can overwrite or remove the other's.
class scratch_tree
{
public:
    explicit scratch_tree(const std::string &stem)
            : m_path(created(stem))
    {
    }

    ~scratch_tree()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    scratch_tree(const scratch_tree &)            = delete;
    scratch_tree &operator=(const scratch_tree &) = delete;

    const std::filesystem::path &path() const { return m_path; }

private:
    std::filesystem::path m_path;

    static std::filesystem::path created(const std::string &stem)
    {
        std::random_device random;
        for(;;)
        {
            std::filesystem::path dir = std::filesystem::temp_directory_path() / (stem + "_" + std::to_string(random()));
            if(std::filesystem::create_directory(dir))
                return dir;
        }
    }
};

inline std::string shell_quoted(const std::string &text)
{
    std::string quoted = "'";
    for(const char c : text)
    {
        if(c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    return quoted + "'";
}

#ifdef _MSC_VER
inline std::int32_t close_pipe(std::FILE *pipe) { return _pclose(pipe); }
inline std::FILE   *open_pipe(const std::string &command) { return _popen(command.c_str(), "r"); }
#else
inline std::int32_t close_pipe(std::FILE *pipe) { return pclose(pipe); }
inline std::FILE   *open_pipe(const std::string &command) { return popen(command.c_str(), "r"); }
#endif

using pipe_handle = std::unique_ptr<std::FILE, decltype(&close_pipe)>;

inline pipe_handle opened_pipe(const std::string &command) { return pipe_handle(open_pipe(command), &close_pipe); }

inline std::string run_bash(const std::string &script)
{
    const scratch_tree          scratch("nucleus_completion_smoke");
    const std::filesystem::path file = scratch.path() / "script.sh";
    std::ofstream(file) << script;
    std::string  captured;
    std::int32_t status = -1;
    if(pipe_handle pipe = opened_pipe("bash " + shell_quoted(file.string()) + " 2>/dev/null"))
    {
        std::array<char, 256> buf{};
        while(std::fgets(buf.data(), static_cast<int>(buf.size()), pipe.get()))
            captured += buf.data();
        status = close_pipe(pipe.release());
    }
    INFO(captured);
    REQUIRE(status == 0);
    return captured;
}

inline constexpr std::string_view argv_printer  = "myapp() { for a in \"$@\"; do printf '<%s>' \"$a\"; done; echo; }\n";
inline constexpr std::string_view payload_check = "for f in PWN_*; do if [ -e \"$f\" ]; then echo \"PAYLOAD $f\"; fi; done\n";

// Opens a loop holding each line readline builds from a reply: `prefix`, the reply verbatim, then `open`
// -- the quote open at the cursor, named by the caller and never read off the typed word -- unless the
// reply already ends with it.
inline std::string inserted_lines(const std::vector<std::string> &words, const std::string &prefix, const std::string &open)
{
    std::string out = "\nCOMP_WORDS=(myapp";
    for(const std::string &word : words)
        out += " " + shell_quoted(word);
    out += ")\nCOMP_CWORD=$((${#COMP_WORDS[@]} - 1))\n_myapp_complete || exit $?\n";
    out += "p=" + shell_quoted(prefix) + "; q=" + shell_quoted(open) + "\n";
    return out + R"(for c in "${COMPREPLY[@]}"; do case "$c" in *"$q") l="$p$c" ;; *) l="$p$c$q" ;; esac; )";
}

inline std::string insert_and_parse(const std::vector<std::string> &words, const std::string &prefix, const std::string &open)
{
    const std::string parse = "eval \"myapp $l\"; done\n";
    return "\n" + std::string(argv_printer) + inserted_lines(words, prefix, open) + parse + std::string(payload_check);
}

inline std::string print_inserted(const std::vector<std::string> &words, const std::string &prefix, const std::string &open)
{
    return inserted_lines(words, prefix, open) + "printf 'myapp %s\\n' \"$l\"; done\n";
}

// Drives the generated completer with the working directory set to a scratch tree holding
// `decoys`, so a candidate the shell would glob has something in reach to be replaced by.
inline std::string run_seeded(const std::string &completion, const std::string &drive,
                              const std::vector<std::string> &decoys)
{
    const scratch_tree dir("nucleus_completion_glob");
    for(const std::string &decoy : decoys)
        std::ofstream(dir.path() / decoy);
    return run_bash("cd " + shell_quoted(dir.path().string()) + " || exit 1\n" + completion + drive);
}

// An interactive shell history-expands each line it reads before parsing it, and so does a script once it
// turns history on; `eval` never does, so the lines are run as script input.
inline std::string run_history_expanded(const std::string &lines)
{
    const scratch_tree dir("nucleus_completion_history");
    const std::string  script = "set -o history -H\ncd " + shell_quoted(dir.path().string()) + " || exit 1\n";
    return run_bash(script + std::string(argv_printer) + lines + std::string(payload_check));
}

inline schema_registry delimiter_registry()
{
    const anchor    cluster = anchor::keyspace(key_path::parse("cluster").value());
    const anchor    node    = anchor::keyspace(key_path::parse("cluster/node").value());
    schema_registry reg;
    REQUIRE(reg.attach(element("cluster", anchor::root())));
    REQUIRE(reg.attach(element("port", cluster)));
    REQUIRE(reg.attach(repeated_element("node", cluster)));
    REQUIRE(reg.attach(element("port", node)));
    return reg;
}

struct insertion_row
{
    std::string              script;
    std::vector<std::string> words;
    std::string              prefix;
    std::string              open;
    std::string              expected;
};

inline void check_rows(const std::vector<insertion_row> &rows)
{
    for(const insertion_row &row : rows)
    {
        DYNAMIC_SECTION(row.prefix << row.words.back())
        {
            REQUIRE(run_seeded(row.script, insert_and_parse(row.words, row.prefix, row.open), {"--cluster-node-7-level"}) == row.expected);
        }
    }
}
}

#endif
