#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/selection/expression.hpp"

namespace molshredder::selection {

using Mask = std::vector<std::uint8_t>;
using NamedResolver = std::function<operation::Result<Mask>(std::string_view)>;

[[nodiscard]] operation::Result<Mask> evaluate(
    const Expression& expression, const model::Topology& topology,
    const NamedResolver& named_resolver = {});

[[nodiscard]] bool mask_is_valid(std::span<const std::uint8_t> mask,
                                 std::size_t atom_count) noexcept;

}  // namespace molshredder::selection
