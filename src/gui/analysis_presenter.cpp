#include "molshredder/gui/analysis_presenter.hpp"

#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::gui {
namespace {

operation::Error malformed(std::string message) {
  return operation::Error{operation::ErrorCode::internal, std::move(message),
                          "report the malformed analysis result"};
}

const command::Value* field(const command::Value::Object& fields,
                            std::string_view name) {
  const auto found = fields.find(name);
  return found == fields.end() ? nullptr : &found->second;
}

template <typename Value>
const Value* value_as(const command::Value::Object& fields,
                      std::string_view name) {
  const auto* found = field(fields, name);
  return found == nullptr ? nullptr : std::get_if<Value>(&found->data);
}

operation::Result<operation::LengthUnit> parse_unit(
    const command::Value::Object& fields) {
  const auto* unit = value_as<std::string>(fields, "unit");
  if (unit == nullptr) {
    return operation::Result<operation::LengthUnit>::failure(
        malformed("analysis result is missing a string unit"));
  }
  if (*unit == "angstrom") {
    return operation::Result<operation::LengthUnit>::success(
        operation::LengthUnit::angstrom);
  }
  if (*unit == "nanometer") {
    return operation::Result<operation::LengthUnit>::success(
        operation::LengthUnit::nanometer);
  }
  return operation::Result<operation::LengthUnit>::failure(
      malformed("analysis result contains an unknown length unit"));
}

std::string unit_symbol(operation::LengthUnit unit) {
  return unit == operation::LengthUnit::angstrom ? "A" : "nm";
}

std::string fixed(double value, std::uint64_t precision) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::fixed << std::setprecision(static_cast<int>(precision))
         << value;
  return stream.str();
}

operation::Result<AnalysisMarker> center_marker(
    const command::Value::Object& fields) {
  const auto* object_id = value_as<std::uint64_t>(fields, "object_id");
  const auto* position = value_as<command::Value::Array>(fields, "position");
  const auto* mode = value_as<std::string>(fields, "mode");
  const auto* precision = value_as<std::uint64_t>(fields, "precision");
  const auto unit = parse_unit(fields);
  if (object_id == nullptr || position == nullptr || position->size() != 3U ||
      mode == nullptr || precision == nullptr || !unit.has_value()) {
    return operation::Result<AnalysisMarker>::failure(
        unit.has_value()
            ? malformed("center result has an invalid marker schema")
            : unit.error());
  }
  const auto* x = std::get_if<double>(&(*position)[0].data);
  const auto* y = std::get_if<double>(&(*position)[1].data);
  const auto* z = std::get_if<double>(&(*position)[2].data);
  if (x == nullptr || y == nullptr || z == nullptr || *precision > 15U) {
    return operation::Result<AnalysisMarker>::failure(
        malformed("center result has invalid position or precision values"));
  }
  const auto label = *mode + " (" + fixed(*x, *precision) + ", " +
                     fixed(*y, *precision) + ", " +
                     fixed(*z, *precision) + ") " +
                     unit_symbol(unit.value());
  return operation::Result<AnalysisMarker>::success(PointMarker{
      *object_id, {*x, *y, *z}, unit.value(), std::move(label)});
}

operation::Result<AnalysisMarker> distance_marker(
    const command::Value::Object& fields) {
  const auto* measurement_id =
      value_as<std::uint64_t>(fields, "measurement_id");
  const auto* object_id = value_as<std::uint64_t>(fields, "object_id");
  const auto* first = value_as<std::uint64_t>(fields, "first_atom_index");
  const auto* second = value_as<std::uint64_t>(fields, "second_atom_index");
  const auto* distance = value_as<double>(fields, "distance");
  const auto* precision = value_as<std::uint64_t>(fields, "precision");
  const auto unit = parse_unit(fields);
  if (measurement_id == nullptr || object_id == nullptr || first == nullptr ||
      second == nullptr || distance == nullptr || precision == nullptr ||
      !unit.has_value()) {
    return operation::Result<AnalysisMarker>::failure(
        unit.has_value()
            ? malformed("distance result has an invalid marker schema")
            : unit.error());
  }
  if (*first == 0U || *second == 0U || *precision > 15U) {
    return operation::Result<AnalysisMarker>::failure(
        malformed("distance result has invalid atom indices or precision"));
  }
  auto label = fixed(*distance, *precision) + " " + unit_symbol(unit.value());
  return operation::Result<AnalysisMarker>::success(AtomDistanceMarker{
      *measurement_id, *object_id, model::AtomIndex{*first - 1U},
      model::AtomIndex{*second - 1U}, *distance, unit.value(),
      std::move(label)});
}

}  // namespace

operation::Result<AnalysisPresentation> make_analysis_presentation(
    const application::DispatchOutcome& outcome) {
  if (!outcome.canonical_invocation.has_value()) {
    return operation::Result<AnalysisPresentation>::failure(operation::Error{
        operation::ErrorCode::invalid_argument,
        "analysis presentation requires a normalized invocation",
        "present registry validation errors through the generic error view"});
  }
  const auto& command_name =
      outcome.canonical_invocation.value().canonical_name;
  const bool center_scalar = command_name == "analyze center";
  const bool distance_scalar = command_name == "measure distance";
  const bool contact_scalar = command_name == "analyze contacts";
  const bool hbond_scalar = command_name == "analyze hbonds";
  const bool secondary_scalar = command_name == "analyze secondary-structure";
  const bool center_series = command_name == "analyze trajectory center";
  const bool distance_series = command_name == "analyze trajectory distance";
  const bool rmsd_series = command_name == "analyze trajectory rmsd";
  const bool rmsd_matrix = command_name == "analyze trajectory rmsd-matrix";
  const bool rmsf_series = command_name == "analyze trajectory rmsf";
  const bool contact_series = command_name == "analyze trajectory contacts";
  const bool hbond_series = command_name == "analyze trajectory hbonds";
  if (!center_scalar && !distance_scalar && !center_series &&
      !distance_series && !rmsd_series && !rmsd_matrix && !rmsf_series && !contact_scalar &&
      !hbond_scalar && !secondary_scalar && !contact_series && !hbond_series) {
    return operation::Result<AnalysisPresentation>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        "command does not have an analysis presentation: " + command_name,
        "use the generic command result presenter"});
  }
  const auto text = command::render(outcome.envelope,
                                    operation::OutputFormat::text);
  if (!text.has_value()) {
    return operation::Result<AnalysisPresentation>::failure(text.error());
  }
  const auto json = command::render(outcome.envelope,
                                    operation::OutputFormat::json);
  if (!json.has_value()) {
    return operation::Result<AnalysisPresentation>::failure(json.error());
  }

  AnalysisPresentation presentation;
  presentation.command_name = command_name;
  presentation.succeeded = outcome.succeeded();
  if (center_series) {
    presentation.title = "Trajectory center analysis";
  } else if (distance_series) {
    presentation.title = "Trajectory distance analysis";
  } else if (rmsd_series) {
    presentation.title = "Trajectory RMSD analysis";
  } else if (rmsd_matrix) {
    presentation.title = "Trajectory RMSD matrix analysis";
  } else if (rmsf_series) {
    presentation.title = "Trajectory RMSF analysis";
  } else if (contact_series) {
    presentation.title = "Trajectory contact analysis";
  } else if (hbond_series) {
    presentation.title = "Trajectory hydrogen-bond analysis";
  } else if (contact_scalar) {
    presentation.title = "Contact analysis";
  } else if (hbond_scalar) {
    presentation.title = "Hydrogen-bond analysis";
  } else if (secondary_scalar) {
    presentation.title = "Secondary-structure assignment";
  } else {
    presentation.title = center_scalar ? "Center analysis"
                                       : "Distance measurement";
  }
  presentation.text = text.value();
  presentation.json = json.value();
  if (!outcome.succeeded()) {
    const auto& error = std::get<operation::Error>(outcome.envelope.payload);
    presentation.summary = error.message;
    return operation::Result<AnalysisPresentation>::success(
        std::move(presentation));
  }

  const auto& response =
      std::get<command::Response>(outcome.envelope.payload);
  presentation.summary = response.summary;
  presentation.fields = response.fields;
  presentation.table = response.table;
  if (response.table.has_value()) {
    const auto csv =
        command::render(outcome.envelope, operation::OutputFormat::csv);
    if (!csv.has_value()) {
      return operation::Result<AnalysisPresentation>::failure(csv.error());
    }
    presentation.csv = csv.value();
    return operation::Result<AnalysisPresentation>::success(
        std::move(presentation));
  }
  const auto marker = center_scalar ? center_marker(response.fields)
                                    : distance_marker(response.fields);
  if (!marker.has_value()) {
    return operation::Result<AnalysisPresentation>::failure(marker.error());
  }
  presentation.marker = marker.value();
  return operation::Result<AnalysisPresentation>::success(
      std::move(presentation));
}

}  // namespace molshredder::gui
