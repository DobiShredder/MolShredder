#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "molshredder/application/dispatcher.hpp"
#include "molshredder/command/result.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/common_types.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::gui {

struct PointMarker {
  std::uint64_t object_id{};
  model::Vec3d position;
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::string label;
};

struct AtomDistanceMarker {
  std::uint64_t measurement_id{};
  std::uint64_t object_id{};
  model::AtomIndex first;
  model::AtomIndex second;
  double distance{};
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::string label;
};

using AnalysisMarker = std::variant<PointMarker, AtomDistanceMarker>;

struct AnalysisPresentation {
  std::string command_name;
  bool succeeded{};
  std::string title;
  std::string summary;
  command::Value::Object fields;
  std::optional<command::Table> table;
  std::string text;
  std::string json;
  std::string csv;
  std::optional<AnalysisMarker> marker;
};

[[nodiscard]] operation::Result<AnalysisPresentation>
make_analysis_presentation(const application::DispatchOutcome& outcome);

}  // namespace molshredder::gui
