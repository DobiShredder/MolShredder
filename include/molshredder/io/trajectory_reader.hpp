#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::io {

enum class TrajectoryFormat { auto_detect, dcd, trr, xtc };
enum class ByteOrder { little_endian, big_endian };
enum class DcdDialect { charmm, xplor };
enum class TrrPrecision { float32, float64, mixed };
enum class XtcVariant { legacy_1995, long_2023, mixed };

struct DcdMetadata {
  std::size_t atom_count{};
  std::size_t frame_count{};
  std::int32_t start_step{};
  std::int32_t save_interval{};
  double raw_delta{};
  bool has_unit_cell{};
  ByteOrder byte_order{ByteOrder::little_endian};
  DcdDialect dialect{DcdDialect::charmm};
  std::string title;
};

class DcdCoordinateSource final : public model::CoordinateSource {
 public:
  [[nodiscard]] std::size_t atom_count() const noexcept override {
    return metadata_.atom_count;
  }
  [[nodiscard]] std::optional<std::size_t> frame_count() const noexcept override {
    return metadata_.frame_count;
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return model::FrameAccess::random_access;
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;

  [[nodiscard]] const DcdMetadata& metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  friend operation::Result<std::shared_ptr<const DcdCoordinateSource>>
  open_dcd(const std::filesystem::path&, std::optional<std::size_t>);

  DcdCoordinateSource(std::filesystem::path path, DcdMetadata metadata,
                      std::vector<std::uint64_t> frame_offsets)
      : path_{std::move(path)},
        metadata_{std::move(metadata)},
        frame_offsets_{std::move(frame_offsets)} {}

  std::filesystem::path path_;
  DcdMetadata metadata_;
  std::vector<std::uint64_t> frame_offsets_;
};

[[nodiscard]] operation::Result<std::shared_ptr<const DcdCoordinateSource>>
open_dcd(const std::filesystem::path& path,
         std::optional<std::size_t> expected_atom_count = std::nullopt);

struct TrrMetadata {
  std::size_t atom_count{};
  std::size_t frame_count{};
  bool has_unit_cell{};
  bool has_velocities{};
  bool has_forces{};
  TrrPrecision precision{TrrPrecision::float32};
};

class TrrCoordinateSource final : public model::CoordinateSource {
 public:
  [[nodiscard]] std::size_t atom_count() const noexcept override {
    return metadata_.atom_count;
  }
  [[nodiscard]] std::optional<std::size_t> frame_count() const noexcept override {
    return metadata_.frame_count;
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return model::FrameAccess::random_access;
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;

  [[nodiscard]] const TrrMetadata& metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  friend operation::Result<std::shared_ptr<const TrrCoordinateSource>>
  open_trr(const std::filesystem::path&, std::optional<std::size_t>);

  TrrCoordinateSource(std::filesystem::path path, TrrMetadata metadata,
                      std::vector<std::uint64_t> frame_offsets)
      : path_{std::move(path)},
        metadata_{metadata},
        frame_offsets_{std::move(frame_offsets)} {}

  std::filesystem::path path_;
  TrrMetadata metadata_;
  std::vector<std::uint64_t> frame_offsets_;
};

[[nodiscard]] operation::Result<std::shared_ptr<const TrrCoordinateSource>>
open_trr(const std::filesystem::path& path,
         std::optional<std::size_t> expected_atom_count = std::nullopt);

struct XtcMetadata {
  std::size_t atom_count{};
  std::size_t frame_count{};
  bool compressed{};
  XtcVariant variant{XtcVariant::legacy_1995};
};

class XtcCoordinateSource final : public model::CoordinateSource {
 public:
  [[nodiscard]] std::size_t atom_count() const noexcept override {
    return metadata_.atom_count;
  }
  [[nodiscard]] std::optional<std::size_t> frame_count() const noexcept override {
    return metadata_.frame_count;
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return model::FrameAccess::random_access;
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;

  [[nodiscard]] const XtcMetadata& metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  friend operation::Result<std::shared_ptr<const XtcCoordinateSource>>
  open_xtc(const std::filesystem::path&, std::optional<std::size_t>);

  XtcCoordinateSource(std::filesystem::path path, XtcMetadata metadata,
                      std::vector<std::uint64_t> frame_offsets)
      : path_{std::move(path)},
        metadata_{metadata},
        frame_offsets_{std::move(frame_offsets)} {}

  std::filesystem::path path_;
  XtcMetadata metadata_;
  std::vector<std::uint64_t> frame_offsets_;
};

[[nodiscard]] operation::Result<std::shared_ptr<const XtcCoordinateSource>>
open_xtc(const std::filesystem::path& path,
         std::optional<std::size_t> expected_atom_count = std::nullopt);

struct OpenedTrajectory {
  TrajectoryFormat format{TrajectoryFormat::auto_detect};
  std::shared_ptr<const model::CoordinateSource> source;
};

[[nodiscard]] std::string_view to_string(TrajectoryFormat format) noexcept;
[[nodiscard]] operation::Result<OpenedTrajectory> open_trajectory(
    const std::filesystem::path& path,
    TrajectoryFormat format = TrajectoryFormat::auto_detect,
    std::optional<std::size_t> expected_atom_count = std::nullopt);

}  // namespace molshredder::io
