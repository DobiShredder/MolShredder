#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "molshredder/analysis/basic.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::analysis {

inline constexpr auto kRdfAlgorithmVersion = "molshredder-rdf-histogram-v1";

enum class RdfNormalization { pair_count, radial_distribution };

struct RdfBin {
  double lower{};
  double upper{};
  double center{};
  std::uint64_t pair_count{};
  double value{};
};

struct RdfResult {
  std::vector<RdfBin> bins;
  std::uint64_t eligible_pair_count{};
  std::uint64_t evaluated_pair_count{};
  std::size_t ignored_missing_atoms{};
  double maximum_radius{};
  double bin_width{};
  DistanceBoundary boundary{DistanceBoundary::raw};
  RdfNormalization normalization{RdfNormalization::pair_count};
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
};

struct RdfRequest {
  const model::CoordinateFrame *frame{};
  std::span<const std::uint8_t> first;
  std::span<const std::uint8_t> second;
  double maximum_radius{};
  double bin_width{};
  DistanceBoundary boundary{DistanceBoundary::raw};
  RdfNormalization normalization{RdfNormalization::pair_count};
  bool same_selection{};
  std::uint64_t evaluation_budget{100'000'000U};
  operation::TaskContext *context{};
};

[[nodiscard]] operation::Result<RdfResult>
radial_distribution_function(const RdfRequest &request);

} // namespace molshredder::analysis
