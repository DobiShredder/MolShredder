#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <utility>

#include "molshredder/operation/common_types.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/model/topology.hpp"

namespace molshredder::model {

template <typename Scalar>
struct Vec3 {
  Scalar x{};
  Scalar y{};
  Scalar z{};

  friend bool operator==(const Vec3&, const Vec3&) = default;
};

using Vec3f = Vec3<float>;
using Vec3d = Vec3<double>;

enum class CoordinatePrecision { float32, float64 };

class CoordinateBuffer {
 public:
  CoordinateBuffer(std::vector<Vec3f> values) : values_{std::move(values)} {}
  CoordinateBuffer(std::vector<Vec3d> values) : values_{std::move(values)} {}

  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] CoordinatePrecision precision() const noexcept;
  [[nodiscard]] bool all_finite() const noexcept;

  [[nodiscard]] const std::variant<std::vector<Vec3f>, std::vector<Vec3d>>&
  values() const noexcept {
    return values_;
  }

 private:
  std::variant<std::vector<Vec3f>, std::vector<Vec3d>> values_;
};

struct UnitCell {
  Vec3d a;
  Vec3d b;
  Vec3d c;

  [[nodiscard]] double signed_volume() const noexcept;
  [[nodiscard]] bool is_valid() const noexcept;
};

enum class TimeUnit { femtosecond, picosecond };

struct PhysicalTime {
  double value{};
  TimeUnit unit{TimeUnit::picosecond};
};

struct FrameMetadata {
  std::optional<std::uint64_t> source_step;
  std::optional<PhysicalTime> physical_time;
  std::optional<UnitCell> unit_cell;
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::optional<TimeUnit> velocity_time_unit;
  std::map<std::string, AtomProperty, std::less<>> atom_properties;
  std::map<std::string, std::string, std::less<>> fields;
};

class CoordinateFrame {
 public:
  [[nodiscard]] static operation::Result<std::shared_ptr<const CoordinateFrame>>
  create(CoordinateBuffer positions,
         std::optional<CoordinateBuffer> velocities = std::nullopt,
         std::vector<std::uint8_t> presence = {},
         FrameMetadata metadata = {});

  [[nodiscard]] std::size_t atom_count() const noexcept {
    return positions_.size();
  }
  [[nodiscard]] const CoordinateBuffer& positions() const noexcept {
    return positions_;
  }
  [[nodiscard]] const std::optional<CoordinateBuffer>& velocities() const
      noexcept {
    return velocities_;
  }
  [[nodiscard]] const std::vector<std::uint8_t>& presence() const noexcept {
    return presence_;
  }
  [[nodiscard]] bool atom_present(std::size_t atom_index) const noexcept {
    return atom_index < presence_.size() && presence_[atom_index] != 0U;
  }
  [[nodiscard]] const FrameMetadata& metadata() const noexcept {
    return metadata_;
  }

 private:
  CoordinateFrame(CoordinateBuffer positions,
                  std::optional<CoordinateBuffer> velocities,
                  std::vector<std::uint8_t> presence, FrameMetadata metadata)
      : positions_{std::move(positions)},
        velocities_{std::move(velocities)},
        presence_{std::move(presence)},
        metadata_{std::move(metadata)} {}

  CoordinateBuffer positions_;
  std::optional<CoordinateBuffer> velocities_;
  std::vector<std::uint8_t> presence_;
  FrameMetadata metadata_;
};

enum class FrameAccess { sequential, random_access };

class CoordinateSource {
 public:
  virtual ~CoordinateSource() = default;

  [[nodiscard]] virtual std::size_t atom_count() const noexcept = 0;
  [[nodiscard]] virtual std::optional<std::size_t> frame_count() const
      noexcept = 0;
  [[nodiscard]] virtual FrameAccess access() const noexcept = 0;
  [[nodiscard]] virtual operation::Result<
      std::shared_ptr<const CoordinateFrame>>
  read_frame(std::size_t frame_index) const = 0;
};

class InMemoryCoordinateSource final : public CoordinateSource {
 public:
  [[nodiscard]] static operation::Result<
      std::shared_ptr<const InMemoryCoordinateSource>>
  create(std::size_t atom_count,
         std::vector<std::shared_ptr<const CoordinateFrame>> frames);

  [[nodiscard]] std::size_t atom_count() const noexcept override {
    return atom_count_;
  }
  [[nodiscard]] std::optional<std::size_t> frame_count() const
      noexcept override {
    return frames_.size();
  }
  [[nodiscard]] FrameAccess access() const noexcept override {
    return FrameAccess::random_access;
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;

 private:
  InMemoryCoordinateSource(
      std::size_t atom_count,
      std::vector<std::shared_ptr<const CoordinateFrame>> frames)
      : atom_count_{atom_count}, frames_{std::move(frames)} {}

  std::size_t atom_count_{};
  std::vector<std::shared_ptr<const CoordinateFrame>> frames_;
};

}  // namespace molshredder::model
