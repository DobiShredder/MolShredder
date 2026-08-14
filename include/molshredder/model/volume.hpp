#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/common_types.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::model {

enum class VolumePrecision { float32, float64 };

class VolumeScalarBuffer {
public:
  explicit VolumeScalarBuffer(std::vector<float> values)
      : values_{std::move(values)} {}
  explicit VolumeScalarBuffer(std::vector<double> values)
      : values_{std::move(values)} {}

  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] VolumePrecision precision() const noexcept;
  [[nodiscard]] bool all_finite() const noexcept;
  [[nodiscard]] double value(std::size_t index) const noexcept;
  [[nodiscard]] std::pair<double, double> range() const noexcept;
  [[nodiscard]] const std::variant<std::vector<float>, std::vector<double>> &
  values() const noexcept {
    return values_;
  }

private:
  std::variant<std::vector<float>, std::vector<double>> values_;
};

struct VolumeShape {
  std::size_t x{};
  std::size_t y{};
  std::size_t z{};

  friend bool operator==(const VolumeShape &, const VolumeShape &) = default;
};

struct VolumeMetadata {
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::optional<std::string> scalar_unit;
  std::map<std::string, std::string, std::less<>> fields;
};

class VolumeGrid {
public:
  [[nodiscard]] static operation::Result<std::shared_ptr<const VolumeGrid>>
  create(VolumeShape shape, Vec3d origin, std::array<Vec3d, 3U> deltas,
         VolumeScalarBuffer scalars, VolumeMetadata metadata = {});

  [[nodiscard]] const VolumeShape &shape() const noexcept { return shape_; }
  [[nodiscard]] std::size_t value_count() const noexcept {
    return scalars_.size();
  }
  [[nodiscard]] const Vec3d &origin() const noexcept { return origin_; }
  [[nodiscard]] const std::array<Vec3d, 3U> &deltas() const noexcept {
    return deltas_;
  }
  [[nodiscard]] const VolumeScalarBuffer &scalars() const noexcept {
    return scalars_;
  }
  [[nodiscard]] const VolumeMetadata &metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] std::size_t linear_index(std::size_t x, std::size_t y,
                                         std::size_t z) const noexcept;
  [[nodiscard]] double value(std::size_t x, std::size_t y,
                             std::size_t z) const noexcept;
  [[nodiscard]] Vec3d position(std::size_t x, std::size_t y,
                               std::size_t z) const noexcept;

private:
  VolumeGrid(VolumeShape shape, Vec3d origin, std::array<Vec3d, 3U> deltas,
             VolumeScalarBuffer scalars, VolumeMetadata metadata)
      : shape_{shape}, origin_{origin}, deltas_{deltas},
        scalars_{std::move(scalars)}, metadata_{std::move(metadata)} {}

  VolumeShape shape_;
  Vec3d origin_;
  std::array<Vec3d, 3U> deltas_;
  VolumeScalarBuffer scalars_;
  VolumeMetadata metadata_;
};

} // namespace molshredder::model
