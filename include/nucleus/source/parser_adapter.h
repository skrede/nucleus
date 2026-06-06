#ifndef HPP_GUARD_NUCLEUS_SOURCE_PARSER_ADAPTER_H
#define HPP_GUARD_NUCLEUS_SOURCE_PARSER_ADAPTER_H

#include "nucleus/source/parser.h"
#include "nucleus/source/source.h"

#include <memory>
#include <utility>

namespace nucleus {

// Type-erases a Parser-concept struct into the runtime-virtual `source`. This is
// the single chokepoint where the compile-time authoring surface meets the
// runtime injection surface: every concept-satisfying struct reaches the engine
// through exactly the same virtual path a hand-written source does, so the seam
// has one contract, not two. The adapter owns the parser by value.
template <Parser T>
class parser_adapter final : public source
{
public:
    explicit parser_adapter(T parser) : m_parser(std::move(parser)) {}

    [[nodiscard]] capability_descriptor capabilities() const override
    {
        return m_parser.capabilities();
    }

    [[nodiscard]] source_result pull() override { return m_parser.pull(); }

private:
    T m_parser;
};

// Adapts a Parser-concept struct into an owning `source` handle ready for
// registration through the virtual seam.
template <Parser T>
[[nodiscard]] std::unique_ptr<source> adapt_parser(T parser)
{
    return std::make_unique<parser_adapter<T>>(std::move(parser));
}

}

#endif
