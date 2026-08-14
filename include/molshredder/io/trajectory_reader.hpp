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

enum class TrajectoryFormat {
  auto_detect,
  dcd,
  trr,
  xtc,
  rst7,
  mdcrd,
  amber_netcdf,
  h5md,
  lammps_dump,
  binpos
};
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
  [[nodiscard]] std::optional<std::size_t>
  frame_count() const noexcept override {
    return metadata_.frame_count;
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return model::FrameAccess::random_access;
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;

  [[nodiscard]] const DcdMetadata &metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  friend operation::Result<std::shared_ptr<const DcdCoordinateSource>>
  open_dcd(const std::filesystem::path &, std::optional<std::size_t>);

  DcdCoordinateSource(std::filesystem::path path, DcdMetadata metadata,
                      std::vector<std::uint64_t> frame_offsets)
      : path_{std::move(path)}, metadata_{std::move(metadata)},
        frame_offsets_{std::move(frame_offsets)} {}

  std::filesystem::path path_;
  DcdMetadata metadata_;
  std::vector<std::uint64_t> frame_offsets_;
};

[[nodiscard]] operation::Result<std::shared_ptr<const DcdCoordinateSource>>
open_dcd(const std::filesystem::path &path,
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
  [[nodiscard]] std::optional<std::size_t>
  frame_count() const noexcept override {
    return metadata_.frame_count;
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return model::FrameAccess::random_access;
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;

  [[nodiscard]] const TrrMetadata &metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  friend operation::Result<std::shared_ptr<const TrrCoordinateSource>>
  open_trr(const std::filesystem::path &, std::optional<std::size_t>);

  TrrCoordinateSource(std::filesystem::path path, TrrMetadata metadata,
                      std::vector<std::uint64_t> frame_offsets)
      : path_{std::move(path)}, metadata_{metadata},
        frame_offsets_{std::move(frame_offsets)} {}

  std::filesystem::path path_;
  TrrMetadata metadata_;
  std::vector<std::uint64_t> frame_offsets_;
};

[[nodiscard]] operation::Result<std::shared_ptr<const TrrCoordinateSource>>
open_trr(const std::filesystem::path &path,
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
  [[nodiscard]] std::optional<std::size_t>
  frame_count() const noexcept override {
    return metadata_.frame_count;
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return model::FrameAccess::random_access;
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;

  [[nodiscard]] const XtcMetadata &metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  friend operation::Result<std::shared_ptr<const XtcCoordinateSource>>
  open_xtc(const std::filesystem::path &, std::optional<std::size_t>);

  XtcCoordinateSource(std::filesystem::path path, XtcMetadata metadata,
                      std::vector<std::uint64_t> frame_offsets)
      : path_{std::move(path)}, metadata_{metadata},
        frame_offsets_{std::move(frame_offsets)} {}

  std::filesystem::path path_;
  XtcMetadata metadata_;
  std::vector<std::uint64_t> frame_offsets_;
};

[[nodiscard]] operation::Result<std::shared_ptr<const XtcCoordinateSource>>
open_xtc(const std::filesystem::path &path,
         std::optional<std::size_t> expected_atom_count = std::nullopt);

struct AmberRestartMetadata {
  std::size_t atom_count{};
  std::string title;
  bool has_time{};
  bool has_temperature{};
  bool has_velocities{};
  bool has_unit_cell{};
};

[[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateSource>>
open_amber_restart(
    const std::filesystem::path &path,
    std::optional<std::size_t> expected_atom_count = std::nullopt,
    AmberRestartMetadata *metadata = nullptr);

struct AmberAsciiTrajectoryMetadata {
  std::size_t atom_count{};
  std::size_t frame_count{};
  std::size_t box_value_count{};
  std::string title;
};

class AmberAsciiCoordinateSource final : public model::CoordinateSource {
public:
  [[nodiscard]] std::size_t atom_count() const noexcept override {
    return metadata_.atom_count;
  }
  [[nodiscard]] std::optional<std::size_t>
  frame_count() const noexcept override {
    return metadata_.frame_count;
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return model::FrameAccess::random_access;
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;

  [[nodiscard]] const AmberAsciiTrajectoryMetadata &metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  friend operation::Result<std::shared_ptr<const AmberAsciiCoordinateSource>>
  open_amber_ascii_trajectory(const std::filesystem::path &, std::size_t,
                              std::optional<double>);

  AmberAsciiCoordinateSource(std::filesystem::path path,
                             AmberAsciiTrajectoryMetadata metadata,
                             std::vector<std::uint64_t> frame_offsets,
                             std::optional<double> box_angle_degrees)
      : path_{std::move(path)}, metadata_{std::move(metadata)},
        frame_offsets_{std::move(frame_offsets)},
        box_angle_degrees_{box_angle_degrees} {}

  std::filesystem::path path_;
  AmberAsciiTrajectoryMetadata metadata_;
  std::vector<std::uint64_t> frame_offsets_;
  std::optional<double> box_angle_degrees_;
};

[[nodiscard]] operation::Result<
    std::shared_ptr<const AmberAsciiCoordinateSource>>
open_amber_ascii_trajectory(
    const std::filesystem::path &path, std::size_t expected_atom_count,
    std::optional<double> box_angle_degrees = std::nullopt);

struct AmberNetcdfMetadata {
  std::size_t atom_count{};
  std::size_t frame_count{};
  bool has_time{};
  bool has_velocities{};
  bool has_forces{};
  bool has_temperature{};
  bool has_unit_cell{};
  bool integer_compressed{};
  std::string storage_format;
  std::string title;
  std::string program;
  std::string program_version;
  std::string application;
};

[[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateSource>>
open_amber_netcdf(
    const std::filesystem::path &path,
    std::optional<std::size_t> expected_atom_count = std::nullopt,
    AmberNetcdfMetadata *metadata = nullptr);

struct H5mdMetadata {
  std::size_t atom_count{};
  std::size_t stored_particle_count{};
  std::size_t frame_count{};
  std::int64_t version_major{};
  std::int64_t version_minor{};
  bool has_time{};
  bool has_velocities{};
  bool has_forces{};
  bool has_ids{};
  bool dynamic_ids{};
  bool has_mass{};
  bool has_charge{};
  bool has_species{};
  bool has_unit_cell{};
  bool time_dependent_box{};
  std::string particle_group;
  std::string author_name;
  std::string author_email;
  std::string creator_name;
  std::string creator_version;
  std::string position_unit;
  std::string time_unit;
};

[[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateSource>>
open_h5md(
    const std::filesystem::path &path,
    std::optional<std::size_t> expected_atom_count = std::nullopt,
    const std::vector<std::int64_t> &source_atom_ids = {},
    std::optional<operation::LengthUnit> coordinate_unit = std::nullopt,
    std::optional<std::string> particle_group = std::nullopt,
    H5mdMetadata *metadata = nullptr);

enum class LammpsCoordinateConvention {
  wrapped_cartesian,
  scaled_wrapped,
  unwrapped_cartesian,
  scaled_unwrapped,
  mixed
};

struct LammpsDumpMetadata {
  std::size_t atom_count{};
  std::size_t frame_count{};
  bool has_restricted_triclinic_cell{};
  LammpsCoordinateConvention coordinate_convention{
      LammpsCoordinateConvention::wrapped_cartesian};
};

class LammpsDumpCoordinateSource final : public model::CoordinateSource {
public:
  [[nodiscard]] std::size_t atom_count() const noexcept override {
    return metadata_.atom_count;
  }
  [[nodiscard]] std::optional<std::size_t>
  frame_count() const noexcept override {
    return metadata_.frame_count;
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return model::FrameAccess::random_access;
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;

  [[nodiscard]] const LammpsDumpMetadata &metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  friend operation::Result<std::shared_ptr<const LammpsDumpCoordinateSource>>
  open_lammps_dump(const std::filesystem::path &,
                   const std::vector<std::int64_t> &,
                   operation::LengthUnit);

  LammpsDumpCoordinateSource(std::filesystem::path path,
                             LammpsDumpMetadata metadata,
                             std::vector<std::uint64_t> frame_offsets,
                             std::vector<std::int64_t> source_atom_ids,
                             operation::LengthUnit coordinate_unit)
      : path_{std::move(path)}, metadata_{metadata},
        frame_offsets_{std::move(frame_offsets)},
        source_atom_ids_{std::move(source_atom_ids)},
        coordinate_unit_{coordinate_unit} {}

  std::filesystem::path path_;
  LammpsDumpMetadata metadata_;
  std::vector<std::uint64_t> frame_offsets_;
  std::vector<std::int64_t> source_atom_ids_;
  operation::LengthUnit coordinate_unit_{operation::LengthUnit::angstrom};
};

[[nodiscard]] operation::Result<
    std::shared_ptr<const LammpsDumpCoordinateSource>>
open_lammps_dump(const std::filesystem::path &path,
                 const std::vector<std::int64_t> &source_atom_ids,
                 operation::LengthUnit coordinate_unit);

struct BinposMetadata {
  std::size_t atom_count{};
  std::size_t frame_count{};
  ByteOrder byte_order{ByteOrder::little_endian};
};

class BinposCoordinateSource final : public model::CoordinateSource {
public:
  [[nodiscard]] std::size_t atom_count() const noexcept override {
    return metadata_.atom_count;
  }
  [[nodiscard]] std::optional<std::size_t>
  frame_count() const noexcept override {
    return metadata_.frame_count;
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return model::FrameAccess::random_access;
  }
  [[nodiscard]] operation::Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override;

  [[nodiscard]] const BinposMetadata &metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  friend operation::Result<std::shared_ptr<const BinposCoordinateSource>>
  open_binpos(const std::filesystem::path &, std::size_t);

  BinposCoordinateSource(std::filesystem::path path, BinposMetadata metadata,
                         std::vector<std::uint64_t> frame_offsets)
      : path_{std::move(path)}, metadata_{metadata},
        frame_offsets_{std::move(frame_offsets)} {}

  std::filesystem::path path_;
  BinposMetadata metadata_;
  std::vector<std::uint64_t> frame_offsets_;
};

[[nodiscard]] operation::Result<std::shared_ptr<const BinposCoordinateSource>>
open_binpos(const std::filesystem::path &path,
            std::size_t expected_atom_count);

struct TrajectoryOpenContext {
  std::optional<std::size_t> expected_atom_count;
  std::vector<std::int64_t> source_atom_ids;
  std::optional<double> amber_box_angle_degrees;
  std::optional<operation::LengthUnit> coordinate_unit;
  std::optional<std::string> h5md_particle_group;
};

struct OpenedTrajectory {
  TrajectoryFormat format{TrajectoryFormat::auto_detect};
  std::shared_ptr<const model::CoordinateSource> source;
};

[[nodiscard]] std::string_view to_string(TrajectoryFormat format) noexcept;
[[nodiscard]] operation::Result<OpenedTrajectory>
open_trajectory(const std::filesystem::path &path,
                TrajectoryFormat format = TrajectoryFormat::auto_detect,
                std::optional<std::size_t> expected_atom_count = std::nullopt,
                std::optional<double> amber_box_angle_degrees = std::nullopt);

[[nodiscard]] operation::Result<OpenedTrajectory>
open_trajectory(const std::filesystem::path &path, TrajectoryFormat format,
                const TrajectoryOpenContext &context);

} // namespace molshredder::io
