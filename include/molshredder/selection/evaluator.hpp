#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "molshredder/model/topology.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/selection/expression.hpp"

namespace molshredder::selection {

inline constexpr std::string_view kChemicalClassificationVersion =
    "molshredder-selection-chemical-v1";
inline constexpr std::string_view kVdwRadiusFallbackVersion =
    "molshredder-selection-vdw-radius-v1";

using Mask = std::vector<std::uint8_t>;
using NamedResolver = std::function<operation::Result<Mask>(std::string_view)>;

struct EvaluationContext {
  constexpr EvaluationContext(
      const model::CoordinateFrame* current_frame = nullptr,
      std::uint64_t current_coordinate_revision = 0U,
      const model::CoordinateSource* current_coordinate_source = nullptr,
      std::string_view current_object_name = {},
      std::optional<model::Vec3d> current_scene_center = std::nullopt,
      std::optional<model::Vec3d> current_rotation_origin = std::nullopt) noexcept
      : frame{current_frame},
        coordinate_revision{current_coordinate_revision},
        coordinate_source{current_coordinate_source},
        object_name{current_object_name},
        scene_center{current_scene_center},
        rotation_origin{current_rotation_origin} {}

  const model::CoordinateFrame* frame{};
  std::uint64_t coordinate_revision{};
  const model::CoordinateSource* coordinate_source{};
  std::string_view object_name;
  std::optional<model::Vec3d> scene_center;
  std::optional<model::Vec3d> rotation_origin;
};

[[nodiscard]] operation::Result<Mask> evaluate(
    const Expression& expression, const model::Topology& topology,
    const NamedResolver& named_resolver = {},
    const EvaluationContext& context = {});

[[nodiscard]] bool mask_is_valid(std::span<const std::uint8_t> mask,
                                 std::size_t atom_count) noexcept;

}  // namespace molshredder::selection
