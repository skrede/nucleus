// argv: how command-line flags map onto the keyspace, and unknown-flag policy.
//
// `--a-b-c=v` maps to the key path `a/b/c`; `-` is always the separator. A
// recognizer ties the source to the set of admissible keys. In lenient mode an
// unrecognized flag is stored with a warning; in strict mode it fails the pull.

#include "nucleus/sources/argv_source.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"

#include <vector>
#include <iostream>

int main()
{
    nucleus::argv_source args(
        std::vector<std::string>{"--service-name=edge", "--service-mode=fast"});

    // Only `service/name` is admissible; `service/mode` is unrecognized.
    args.recognize_with([](const nucleus::key_path &path) {
            return path.str() == "service/name";
        })
        .policy(nucleus::unknown_key_policy::lenient);

    auto pulled = args.pull();
    if(!pulled)
    {
        std::cerr << "pull failed: " << pulled.error() << '\n';
        return 1;
    }

    for(const nucleus::keyspace_entry &entry : pulled.value().entries)
        std::cout << entry.path << " = " << entry.value.text() << '\n';
    return 0;
}
