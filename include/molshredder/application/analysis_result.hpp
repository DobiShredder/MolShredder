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

inline constexpr unsigned int kAnalysisResultSchemaVersion = 2U;
inline constexpr unsigned int kScientificResultContractSchemaVersion = 1U;

enum class AnalysisResultKind {
  center,
  distance,
  angle,
  dihedral,
  sasa,
  rdf,
  rmsd_matrix,
  rmsd_series,
  rmsf_series,
  contacts,
  contact_series
};

enum class AnalysisSourceStatus {
  current,
  coordinate_changed,
  topology_changed,
  method_changed,
  object_deleted
};

struct NumericalToleranceContract {
  double absolute{};
  double relative{};
  std::string unit;
};

struct ScientificResultContract {
  unsigned int schema_version{kScientificResultContractSchemaVersion};
  model::TopologySnapshotReference topology;
  std::uint64_t coordinate_source_revision{};
  std::uint64_t coordinate_revision{};
  bool coordinate_revision_known{true};
  std::string coordinate_scope;
  std::string algorithm;
  std::string algorithm_version;
  std::string input_coordinate_unit;
  std::string output_unit;
  std::string calculation_precision;
  unsigned int presentation_precision{};
  std::string pbc_policy;
  bool pbc_cell_required{};
  std::string missing_data_policy;
  NumericalToleranceContract tolerance;
  bool tolerance_known{true};
};

struct AnalysisResultProvenance {
  ScientificResultContract scientific;
  std::string object_name;
  std::string canonical_command;
  command::Arguments canonical_arguments;
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

struct GeometryAnalysisOverlay {
  std::vector<model::AtomReference> atoms;
  std::vector<model::Vec3d> positions;
  std::string label;
};

using AnalysisOverlay =
    std::variant<std::monostate, PointAnalysisOverlay,
                 DistanceAnalysisOverlay, GeometryAnalysisOverlay>;

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

struct LegacyAnalysisResultProvenanceV1 {
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

struct LegacyPersistentAnalysisResultV1 {
  unsigned int schema_version{1U};
  std::uint64_t result_id{};
  std::string name;
  AnalysisResultKind kind{AnalysisResultKind::center};
  LegacyAnalysisResultProvenanceV1 provenance;
  command::Response response;
  command::Table export_table;
  AnalysisOverlay overlay;
  bool overlay_visible{};
};

struct LegacyAnalysisResultStoreSnapshotV1 {
  unsigned int schema_version{1U};
  std::uint64_t next_result_id{1U};
  std::vector<LegacyPersistentAnalysisResultV1> records;
};

struct AnalysisResultExportReport {
  std::uint64_t result_id{};
  std::filesystem::path path;
  std::string format;
  std::uint64_t byte_count{};
  std::vector<std::string> losses;
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
  [[nodiscard]] std::optional<operation::Error> restore(
      const LegacyAnalysisResultStoreSnapshotV1& snapshot);

 private:
  TimestampProvider timestamp_provider_;
  std::uint64_t next_result_id_{1U};
  std::vector<PersistentAnalysisResult> records_;
};

[[nodiscard]] std::string_view to_string(AnalysisResultKind kind) noexcept;
[[nodiscard]] std::string_view to_string(AnalysisSourceStatus status) noexcept;
[[nodiscard]] std::string_view current_analysis_algorithm_version(
    AnalysisResultKind kind) noexcept;

[[nodiscard]] std::optional<command::Value::Object>
analysis_plot_projection(AnalysisResultKind kind,const command::Table& table);

[[nodiscard]] AnalysisSourceStatus assess_analysis_result(
    const PersistentAnalysisResult& record,
    const std::optional<model::TopologySnapshotReference>& current_topology,
    std::optional<std::uint64_t> current_coordinate_source_revision,
    std::optional<std::uint64_t> current_coordinate_revision,
    std::string_view current_algorithm_version) noexcept;

[[nodiscard]] operation::Result<command::Value::Object>
scientific_contract_fields(const ScientificResultContract& contract);

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
