#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "molshredder/command/registry.hpp"
#include "molshredder/command/result.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/render/packet.hpp"

namespace molshredder::application {

inline constexpr unsigned int kAnalysisResultSchemaVersion = 1U;

enum class AnalysisResultKind {
  center,
  distance,
  rmsd_series,
  contacts,
  contact_series
};

enum class AnalysisSourceStatus { current, topology_changed, object_deleted };

struct AnalysisResultProvenance {
  model::TopologySnapshotReference source;
  std::string object_name;
  std::string canonical_command;
  command::Arguments canonical_arguments;
  std::string algorithm;
  std::string algorithm_version;
  std::string coordinate_unit;
  std::string pbc_policy;
  std::string missing_data_policy;
  std::optional<std::size_t> frame_first;
  std::optional<std::size_t> frame_last;
  std::optional<std::size_t> frame_stride;
  std::string created_at_utc;
};

struct PointAnalysisOverlay {
  model::Vec3d position;
  std::string label;
};

struct DistanceAnalysisOverlay {
  model::AtomReference first;
  model::AtomReference second;
  model::Vec3d first_position;
  model::Vec3d second_position;
  std::string label;
};

using AnalysisOverlay =
    std::variant<std::monostate, PointAnalysisOverlay,
                 DistanceAnalysisOverlay>;

struct PersistentAnalysisResult {
  unsigned int schema_version{kAnalysisResultSchemaVersion};
  std::uint64_t result_id{};
  std::string name;
  AnalysisResultKind kind{AnalysisResultKind::center};
  AnalysisResultProvenance provenance;
  command::Response response;
  command::Table export_table;
  AnalysisOverlay overlay;
  bool overlay_visible{};
};

struct AnalysisResultDraft {
  std::optional<std::string> name;
  AnalysisResultKind kind{AnalysisResultKind::center};
  AnalysisResultProvenance provenance;
  command::Response response;
  command::Table export_table;
  AnalysisOverlay overlay;
  bool overlay_visible{true};
};

struct AnalysisResultStoreSnapshot {
  unsigned int schema_version{kAnalysisResultSchemaVersion};
  std::uint64_t next_result_id{1U};
  std::vector<PersistentAnalysisResult> records;
};

struct AnalysisResultExportReport {
  std::uint64_t result_id{};
  std::filesystem::path path;
  std::string format;
  std::uint64_t byte_count{};
};

class AnalysisResultStore {
 public:
  using TimestampProvider = std::function<std::string()>;

  explicit AnalysisResultStore(TimestampProvider timestamp_provider = {});

  [[nodiscard]] std::optional<operation::Error> validate_name(
      const std::optional<std::string>& name) const;

  [[nodiscard]] operation::Result<PersistentAnalysisResult> add(
      AnalysisResultDraft draft);
  [[nodiscard]] operation::Result<PersistentAnalysisResult> get(
      std::uint64_t result_id) const;
  [[nodiscard]] std::span<const PersistentAnalysisResult> records() const
      noexcept {
    return records_;
  }
  [[nodiscard]] operation::Result<PersistentAnalysisResult> erase(
      std::uint64_t result_id);
  [[nodiscard]] operation::Result<PersistentAnalysisResult>
  set_overlay_visible(std::uint64_t result_id, bool visible);

  [[nodiscard]] AnalysisResultStoreSnapshot snapshot() const;
  [[nodiscard]] std::optional<operation::Error> restore(
      const AnalysisResultStoreSnapshot& snapshot);

 private:
  TimestampProvider timestamp_provider_;
  std::uint64_t next_result_id_{1U};
  std::vector<PersistentAnalysisResult> records_;
};

[[nodiscard]] std::string_view to_string(AnalysisResultKind kind) noexcept;
[[nodiscard]] std::string_view to_string(AnalysisSourceStatus status) noexcept;

[[nodiscard]] command::Response analysis_result_response(
    const PersistentAnalysisResult& record, AnalysisSourceStatus status);

[[nodiscard]] operation::Result<AnalysisResultExportReport>
export_analysis_result(const PersistentAnalysisResult& record,
                       AnalysisSourceStatus status,
                       const std::filesystem::path& path,
                       operation::OutputFormat format, bool overwrite);

[[nodiscard]] render::RenderPacket build_analysis_overlay_packet(
    std::span<const PersistentAnalysisResult> records);

}  // namespace molshredder::application
