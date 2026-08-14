#include "nucleus/keyspace/storage_shape.h"

#include "nucleus/format.h"

#include <map>
#include <string>
#include <vector>
#include <utility>

namespace nucleus {
namespace {

struct stored_shape
{
    std::string signature;
    std::string concrete_path;
};

std::string canonical_leaf(const key_path &path)
{
    std::vector<std::string> segments;
    segments.reserve(path.size());
    for(const std::string &segment : path.segments())
        segments.emplace_back(key_path::base_name(segment));
    return key_path(std::move(segments)).str();
}

std::string indexed_signature(const key_path &path)
{
    std::string signature;
    signature.reserve(path.size());
    for(const std::string &segment : path.segments())
        signature.push_back(key_path::is_indexed_segment(segment) ? 'i' : 'p');
    return signature;
}

}

expected<void, error> validate_storage_shape(std::span<const key_path> paths)
{
    std::map<std::string, stored_shape> shapes;
    for(const key_path &path : paths)
    {
        const std::string canonical = canonical_leaf(path);
        const std::string signature = indexed_signature(path);
        auto [found, inserted]      = shapes.try_emplace(
                canonical, stored_shape{signature, path.str()});
        if(!inserted && found->second.signature != signature)
            return unexpected(error{errc::schema_violation, nucleus::format("canonical path '{}' has conflicting concrete paths '{}' and '{}'; "
                                                                            "a leaf family must use one indexed storage shape",
                                                                            canonical, found->second.concrete_path, path.str())});
    }
    return {};
}

}
