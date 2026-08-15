#include "structure_writer_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "structure_reader_internal.hpp"

namespace molshredder::io::detail {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

operation::Error io_error(std::string message) {
  return operation::Error{
      operation::ErrorCode::internal, std::move(message), {}};
}

class LossCollector {
public:
  void add(std::string channel, std::uint64_t count, std::string message) {
    if (count == 0U)
      return;
    const auto found = indices_.find(channel);
    if (found == indices_.end()) {
      indices_.emplace(channel, losses_.size());
      losses_.push_back(
          FormatLoss{std::move(channel), count, std::move(message)});
      return;
    }
    losses_[found->second].count += count;
  }

  [[nodiscard]] std::vector<FormatLoss> take() { return std::move(losses_); }

private:
  std::map<std::string, std::size_t, std::less<>> indices_;
  std::vector<FormatLoss> losses_;
};

std::vector<std::size_t>
selected_frames(const model::CoordinateSource &coordinates,
                const StructureWriteOptions &options,
                std::optional<operation::Error> &error) {
  const auto count = coordinates.frame_count();
  if (!count.has_value() || count.value() == 0U) {
    error = invalid("mmCIF export requires a known non-zero frame count");
    return {};
  }
  if (options.frame_indices.empty()) {
    std::vector<std::size_t> frames(count.value());
    std::iota(frames.begin(), frames.end(), std::size_t{});
    return frames;
  }
  for (const auto frame : options.frame_indices) {
    if (frame >= count.value()) {
      error = invalid("mmCIF export frame is outside the available range");
      return {};
    }
  }
  return options.frame_indices;
}

model::Vec3d position_at(const model::CoordinateFrame &frame,
                         std::size_t index) {
  return std::visit(
      [index](const auto &values) {
        const auto &value = values[index];
        return model::Vec3d{static_cast<double>(value.x),
                            static_cast<double>(value.y),
                            static_cast<double>(value.z)};
      },
      frame.positions().values());
}

bool valid_cif_text(std::string_view value) {
  return std::none_of(value.begin(), value.end(), [](unsigned char character) {
    return character == '\r' || (character < 0x20U && character != '\n') ||
           character == 0x7fU;
  });
}

std::string lowercase(std::string_view value) {
  std::string result{value};
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return result;
}

bool bare_cif_token(std::string_view value) {
  if (value.empty() || value == "." || value == "?" || value.front() == '_' ||
      value.front() == '#' || value.front() == ';' || value.front() == '\'' ||
      value.front() == '"') {
    return false;
  }
  if (std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0 || character == '#';
      })) {
    return false;
  }
  const auto normalized = lowercase(value);
  return normalized != "loop_" && normalized != "stop_" &&
         normalized != "global_" && !normalized.starts_with("data_") &&
         !normalized.starts_with("save_");
}

struct CifToken {
  std::string value;
  bool text_field{};
};

operation::Result<CifToken> cif_token(std::string_view value,
                                      bool missing_marker = false) {
  if (!valid_cif_text(value)) {
    return operation::Result<CifToken>::failure(
        invalid("mmCIF text contains a prohibited control character"));
  }
  if (missing_marker && (value == "." || value == "?")) {
    return operation::Result<CifToken>::success(
        CifToken{std::string{value}, false});
  }
  if (bare_cif_token(value)) {
    return operation::Result<CifToken>::success(
        CifToken{std::string{value}, false});
  }
  if (value.find('\n') == std::string_view::npos &&
      value.find('\'') == std::string_view::npos) {
    return operation::Result<CifToken>::success(
        CifToken{"'" + std::string{value} + "'", false});
  }
  if (value.find('\n') == std::string_view::npos &&
      value.find('"') == std::string_view::npos) {
    return operation::Result<CifToken>::success(
        CifToken{"\"" + std::string{value} + "\"", false});
  }
  if (value.starts_with(';') || value.find("\n;") != std::string_view::npos) {
    return operation::Result<CifToken>::failure(invalid(
        "mmCIF text cannot be quoted safely with CIF 1.1 syntax",
        "remove a semicolon that appears at the start of a text-field line"));
  }
  return operation::Result<CifToken>::success(
      CifToken{std::string{value}, true});
}

operation::Result<bool> write_scalar(std::ostream &output,
                                     std::string_view name,
                                     std::string_view value) {
  const auto token = cif_token(value);
  if (!token.has_value()) {
    return operation::Result<bool>::failure(token.error());
  }
  if (token.value().text_field) {
    output << name << "\n;" << token.value().value << "\n;\n";
  } else {
    output << name << ' ' << token.value().value << '\n';
  }
  return operation::Result<bool>::success(true);
}

struct CifValue {
  CifValue(std::string value_in, bool missing_marker_in = false)
      : value(std::move(value_in)), missing_marker(missing_marker_in) {}
  CifValue(const char *value_in) : value(value_in) {}

  static CifValue missing(std::string marker) {
    return CifValue{std::move(marker), true};
  }

  std::string value;
  bool missing_marker{};
};

operation::Result<bool> write_loop_row(std::ostream &output,
                                       const std::vector<CifValue> &values) {
  bool line_has_value = false;
  for (const auto &value : values) {
    const auto token = cif_token(value.value, value.missing_marker);
    if (!token.has_value()) {
      return operation::Result<bool>::failure(token.error());
    }
    if (token.value().text_field) {
      if (line_has_value)
        output << '\n';
      output << ';' << token.value().value << "\n;\n";
      line_has_value = false;
    } else {
      if (line_has_value)
        output << ' ';
      output << token.value().value;
      line_has_value = true;
    }
  }
  if (line_has_value)
    output << '\n';
  return operation::Result<bool>::success(true);
}

std::string data_block_name(const model::Topology &topology, bool &normalized) {
  const auto found = topology.source_metadata().find("data_block");
  auto value = found == topology.source_metadata().end()
                   ? std::string{"molshredder"}
                   : found->second;
  normalized = found == topology.source_metadata().end();
  for (auto &character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isalnum(byte) == 0 && character != '_' && character != '-' &&
        character != '.') {
      character = '_';
      normalized = true;
    }
  }
  if (value.empty()) {
    value = "molshredder";
    normalized = true;
  }
  return value;
}

const std::vector<std::string> *
text_property(const model::Topology &topology, std::string_view name,
              std::optional<operation::Error> &error) {
  const auto *property = topology.properties().find(name);
  if (property == nullptr)
    return nullptr;
  const auto *values = std::get_if<std::vector<std::string>>(&property->values);
  if (values == nullptr) {
    error = invalid(std::string{name} + " must be a text property column");
  }
  return values;
}

const model::BooleanColumn *
boolean_property(const model::Topology &topology, std::string_view name,
                 std::optional<operation::Error> &error) {
  const auto *property = topology.properties().find(name);
  if (property == nullptr)
    return nullptr;
  const auto *values = std::get_if<model::BooleanColumn>(&property->values);
  if (values == nullptr) {
    error = invalid(std::string{name} + " must be a boolean property column");
  }
  return values;
}

std::optional<double>
frame_numeric_property(const model::CoordinateFrame &frame,
                       std::string_view name, std::string_view presence_name,
                       std::size_t atom_index,
                       std::optional<operation::Error> &error) {
  const auto found = frame.metadata().atom_properties.find(name);
  const auto presence = frame.metadata().atom_properties.find(presence_name);
  if (found == frame.metadata().atom_properties.end()) {
    if (presence != frame.metadata().atom_properties.end()) {
      error = invalid("mmCIF frame property " + std::string{presence_name} +
                      " exists without " + std::string{name});
    }
    return std::nullopt;
  }
  if (presence != frame.metadata().atom_properties.end()) {
    const auto *values =
        std::get_if<model::BooleanColumn>(&presence->second.values);
    if (values == nullptr) {
      error = invalid("mmCIF frame property " + std::string{presence_name} +
                      " must be a boolean column");
      return std::nullopt;
    }
    if (values->values[atom_index] == 0U)
      return std::nullopt;
  }
  if (const auto *values =
          std::get_if<std::vector<double>>(&found->second.values)) {
    return (*values)[atom_index];
  }
  if (const auto *values =
          std::get_if<std::vector<float>>(&found->second.values)) {
    return static_cast<double>((*values)[atom_index]);
  }
  error = invalid("mmCIF frame property " + std::string{name} +
                  " must be a float32 or float64 column");
  return std::nullopt;
}

std::string decimal(double value, unsigned int precision) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::fixed << std::setprecision(static_cast<int>(precision))
         << value;
  return output.str();
}

double vector_length(const model::Vec3d &value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

double vector_dot(const model::Vec3d &first, const model::Vec3d &second) {
  return first.x * second.x + first.y * second.y + first.z * second.z;
}

struct CellParameters {
  double a{};
  double b{};
  double c{};
  double alpha{};
  double beta{};
  double gamma{};
};

CellParameters cell_parameters(const model::UnitCell &cell, double scale) {
  const auto a = vector_length(cell.a);
  const auto b = vector_length(cell.b);
  const auto c = vector_length(cell.c);
  const auto angle = [](const model::Vec3d &first, const model::Vec3d &second,
                        double first_length, double second_length) {
    const auto cosine = std::clamp(
        vector_dot(first, second) / (first_length * second_length), -1.0, 1.0);
    return std::acos(cosine) * 180.0 / std::numbers::pi;
  };
  return {a * scale,
          b * scale,
          c * scale,
          angle(cell.b, cell.c, b, c),
          angle(cell.a, cell.c, a, c),
          angle(cell.a, cell.b, a, b)};
}

bool cells_match(const std::optional<CellParameters> &first,
                 const std::optional<CellParameters> &second) {
  if (first.has_value() != second.has_value())
    return false;
  if (!first.has_value())
    return true;
  const auto close = [](double left, double right) {
    return std::abs(left - right) <=
           1.0e-9 * std::max({1.0, std::abs(left), std::abs(right)});
  };
  return close(first->a, second->a) && close(first->b, second->b) &&
         close(first->c, second->c) && close(first->alpha, second->alpha) &&
         close(first->beta, second->beta) && close(first->gamma, second->gamma);
}

std::string bond_order(model::BondOrder order) {
  switch (order) {
  case model::BondOrder::single:
    return "sing";
  case model::BondOrder::double_bond:
    return "doub";
  case model::BondOrder::triple:
    return "trip";
  case model::BondOrder::aromatic:
    return "arom";
  case model::BondOrder::amide:
  case model::BondOrder::unknown:
    return "?";
  }
  return "?";
}

struct AtomIdentity {
  std::string label_atom;
  std::string label_component;
  std::string label_asym;
  std::string label_entity;
  std::string label_sequence;
  std::string author_atom;
  std::string author_component;
  std::string author_asym;
  std::string author_sequence;
  std::string alternate;
  std::string insertion;
  bool label_entity_missing{};
  bool alternate_missing{};
  bool insertion_missing{};
  bool author_asym_missing{};
};

} // namespace

operation::Result<StructureWriteReport>
write_mmcif(std::ostream &output, const model::Topology &topology,
            const model::CoordinateSource &coordinates,
            const StructureWriteOptions &options,
            operation::TaskContext &context) {
  if (options.decimal_places > 15U) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("mmCIF decimal precision must be between 0 and 15"));
  }
  if (coordinates.atom_count() != topology.atom_count()) {
    return operation::Result<StructureWriteReport>::failure(invalid(
        "topology and coordinate source atom counts differ during export"));
  }
  if (topology.atom_count() == 0U) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("mmCIF export requires at least one atom"));
  }
  std::optional<operation::Error> frame_error;
  const auto frames = selected_frames(coordinates, options, frame_error);
  if (frame_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*frame_error);
  }

  std::optional<operation::Error> property_error;
  const auto *label_asym =
      text_property(topology, "mmcif.label_asym_id", property_error);
  const auto *label_sequence =
      text_property(topology, "mmcif.label_seq_id", property_error);
  const auto *label_entity =
      text_property(topology, "mmcif.label_entity_id", property_error);
  const auto *hetero =
      boolean_property(topology, "mmcif.is_hetero", property_error);
  if (hetero == nullptr) {
    hetero = boolean_property(topology, "pdb.is_hetero", property_error);
  }
  const auto *charge_present =
      boolean_property(topology, "formal_charge_present", property_error);
  if (property_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*property_error);
  }

  std::vector<AtomIdentity> identities;
  identities.reserve(topology.atom_count());
  std::uint64_t synthesized_label_asym{};
  std::uint64_t missing_label_entity{};
  std::set<std::tuple<std::string, std::string, std::string, std::string,
                      std::string>>
      unique_label_identity;
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    const auto &atom = topology.atoms()[index];
    if (atom.residue.value >= topology.residue_count()) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("mmCIF atom references an invalid residue"));
    }
    const auto &residue = topology.residues()[atom.residue.value];
    if (atom.name.empty() || residue.name.empty() ||
        !valid_cif_text(atom.name) || !valid_cif_text(residue.name) ||
        !valid_cif_text(residue.chain_id) ||
        !valid_cif_text(residue.segment_id) ||
        !valid_cif_text(residue.insertion_code) ||
        !valid_cif_text(atom.alternate_location)) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("mmCIF atom/residue identity contains an empty required "
                  "field or prohibited control character at atom " +
                  std::to_string(index + 1U)));
    }
    if (element_symbol(atom.atomic_number).empty()) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("mmCIF export requires a known element at atom " +
                  std::to_string(index + 1U)));
    }
    AtomIdentity identity;
    identity.author_atom = atom.name;
    identity.author_component = residue.name;
    identity.author_asym = residue.chain_id;
    identity.author_sequence = std::to_string(residue.sequence_number);
    identity.label_atom = atom.name;
    identity.label_component = residue.name;
    identity.label_asym =
        label_asym != nullptr ? (*label_asym)[index] : residue.segment_id;
    if (identity.label_asym.empty()) {
      identity.label_asym = residue.chain_id.empty() ? "A" : residue.chain_id;
      ++synthesized_label_asym;
    }
    identity.label_sequence =
        label_sequence != nullptr && !(*label_sequence)[index].empty()
            ? (*label_sequence)[index]
            : identity.author_sequence;
    identity.label_entity =
        label_entity != nullptr ? (*label_entity)[index] : std::string{};
    if (identity.label_entity.empty()) {
      identity.label_entity = "?";
      identity.label_entity_missing = true;
      ++missing_label_entity;
    }
    identity.alternate_missing = atom.alternate_location.empty();
    identity.alternate =
        identity.alternate_missing ? "." : atom.alternate_location;
    identity.insertion_missing = residue.insertion_code.empty();
    identity.insertion =
        identity.insertion_missing ? "?" : residue.insertion_code;
    identity.author_asym_missing = residue.chain_id.empty();
    if (!unique_label_identity
             .emplace(identity.label_asym, identity.label_sequence,
                      identity.label_component, identity.label_atom,
                      identity.alternate)
             .second) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("mmCIF label identity is duplicated at atom " +
                      std::to_string(index + 1U),
                  "assign unique atom/alternate identifiers before export"));
    }
    identities.push_back(std::move(identity));
  }

  if (context.cancellation.is_cancelled()) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::cancelled, "mmCIF export cancelled", {}});
  }
  const auto first_frame = coordinates.read_frame(frames.front());
  if (!first_frame.has_value()) {
    return operation::Result<StructureWriteReport>::failure(
        first_frame.error());
  }
  for (std::size_t atom = 0; atom < topology.atom_count(); ++atom) {
    if (!first_frame.value()->atom_present(atom)) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("mmCIF first output model is missing atom " +
                      std::to_string(atom + 1U),
                  "select a complete frame first so later models retain "
                  "topology identity"));
    }
  }

  const auto frame_cell =
      [](const model::CoordinateFrame &frame) -> std::optional<CellParameters> {
    if (!frame.metadata().unit_cell.has_value())
      return std::nullopt;
    const auto scale =
        frame.metadata().coordinate_unit == operation::LengthUnit::nanometer
            ? 10.0
            : 1.0;
    return cell_parameters(*frame.metadata().unit_cell, scale);
  };
  const auto reference_cell = frame_cell(*first_frame.value());

  bool block_normalized = false;
  const auto block = data_block_name(topology, block_normalized);
  output.imbue(std::locale::classic());
  output << "data_" << block << '\n';
  auto scalar_result = write_scalar(output, "_entry.id", block);
  if (!scalar_result.has_value()) {
    return operation::Result<StructureWriteReport>::failure(
        scalar_result.error());
  }
  if (!options.comment.empty()) {
    scalar_result = write_scalar(output, "_struct.title", options.comment);
    if (!scalar_result.has_value()) {
      return operation::Result<StructureWriteReport>::failure(
          scalar_result.error());
    }
  }
  if (reference_cell.has_value()) {
    output << "_cell.length_a "
           << decimal(reference_cell->a, options.decimal_places) << '\n'
           << "_cell.length_b "
           << decimal(reference_cell->b, options.decimal_places) << '\n'
           << "_cell.length_c "
           << decimal(reference_cell->c, options.decimal_places) << '\n'
           << "_cell.angle_alpha "
           << decimal(reference_cell->alpha, options.decimal_places) << '\n'
           << "_cell.angle_beta "
           << decimal(reference_cell->beta, options.decimal_places) << '\n'
           << "_cell.angle_gamma "
           << decimal(reference_cell->gamma, options.decimal_places) << '\n';
  }
  output << "#\nloop_\n"
         << "_atom_site.group_PDB\n"
         << "_atom_site.id\n"
         << "_atom_site.type_symbol\n"
         << "_atom_site.label_atom_id\n"
         << "_atom_site.label_alt_id\n"
         << "_atom_site.label_comp_id\n"
         << "_atom_site.label_asym_id\n"
         << "_atom_site.label_entity_id\n"
         << "_atom_site.label_seq_id\n"
         << "_atom_site.pdbx_PDB_ins_code\n"
         << "_atom_site.Cartn_x\n"
         << "_atom_site.Cartn_y\n"
         << "_atom_site.Cartn_z\n"
         << "_atom_site.occupancy\n"
         << "_atom_site.B_iso_or_equiv\n"
         << "_atom_site.pdbx_formal_charge\n"
         << "_atom_site.auth_seq_id\n"
         << "_atom_site.auth_comp_id\n"
         << "_atom_site.auth_asym_id\n"
         << "_atom_site.auth_atom_id\n"
         << "_atom_site.pdbx_PDB_model_num\n";

  std::set<std::uint64_t> model_numbers;
  std::uint64_t normalized_model_numbers{};
  std::uint64_t site_id{1U};
  std::uint64_t present_coordinate_values{};
  std::uint64_t velocity_frames{};
  std::uint64_t time_frames{};
  std::uint64_t other_frame_properties{};
  std::uint64_t other_frame_fields{};
  for (std::size_t output_index = 0; output_index < frames.size();
       ++output_index) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<StructureWriteReport>::failure(
          operation::Error{operation::ErrorCode::cancelled,
                           "mmCIF export cancelled before model " +
                               std::to_string(output_index + 1U),
                           {}});
    }
    const auto frame_result =
        output_index == 0U ? first_frame
                           : coordinates.read_frame(frames[output_index]);
    if (!frame_result.has_value()) {
      return operation::Result<StructureWriteReport>::failure(
          frame_result.error());
    }
    const auto &frame = *frame_result.value();
    if (!cells_match(reference_cell, frame_cell(frame))) {
      return operation::Result<StructureWriteReport>::failure(
          operation::Error{operation::ErrorCode::unsupported,
                           "one mmCIF data block cannot represent a unit cell "
                           "that changes between selected models",
                           "export each cell as a separate data block or use a "
                           "trajectory format"});
    }
    std::uint64_t model_number = output_index + 1U;
    if (frame.metadata().source_step.has_value() &&
        model_numbers.insert(*frame.metadata().source_step).second) {
      model_number = *frame.metadata().source_step;
    } else {
      ++normalized_model_numbers;
      while (model_numbers.contains(model_number))
        ++model_number;
      model_numbers.insert(model_number);
    }
    const auto coordinate_scale =
        frame.metadata().coordinate_unit == operation::LengthUnit::nanometer
            ? 10.0
            : 1.0;
    for (std::size_t index = 0; index < topology.atom_count(); ++index) {
      if (!frame.atom_present(index))
        continue;
      const auto &atom = topology.atoms()[index];
      const auto &identity = identities[index];
      const auto position = position_at(frame, index);
      if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
          !std::isfinite(position.z)) {
        return operation::Result<StructureWriteReport>::failure(
            invalid("mmCIF coordinate is non-finite at atom " +
                    std::to_string(index + 1U)));
      }
      std::optional<operation::Error> frame_property_error;
      const auto occupancy = frame_numeric_property(
          frame, "occupancy", "occupancy_present", index, frame_property_error);
      if (frame_property_error.has_value()) {
        return operation::Result<StructureWriteReport>::failure(
            *frame_property_error);
      }
      const auto b_factor = frame_numeric_property(frame, "b_iso_or_equiv",
                                                   "b_iso_or_equiv_present",
                                                   index, frame_property_error);
      if (frame_property_error.has_value()) {
        return operation::Result<StructureWriteReport>::failure(
            *frame_property_error);
      }
      if ((occupancy.has_value() && !std::isfinite(*occupancy)) ||
          (b_factor.has_value() && !std::isfinite(*b_factor))) {
        return operation::Result<StructureWriteReport>::failure(
            invalid("mmCIF occupancy or B-factor is non-finite at atom " +
                    std::to_string(index + 1U)));
      }
      const auto formal_charge =
          charge_present != nullptr && charge_present->values[index] != 0U
              ? std::to_string(atom.formal_charge)
          : atom.formal_charge == 0 ? "?"
                                    : std::to_string(atom.formal_charge);
      const bool formal_charge_missing =
          charge_present != nullptr && charge_present->values[index] != 0U
              ? false
              : atom.formal_charge == 0;
      const std::vector<CifValue> row{
          hetero != nullptr && hetero->values[index] != 0U ? "HETATM" : "ATOM",
          std::to_string(site_id++),
          std::string{element_symbol(atom.atomic_number)},
          identity.label_atom,
          CifValue{identity.alternate, identity.alternate_missing},
          identity.label_component,
          identity.label_asym,
          CifValue{identity.label_entity, identity.label_entity_missing},
          identity.label_sequence,
          CifValue{identity.insertion, identity.insertion_missing},
          decimal(position.x * coordinate_scale, options.decimal_places),
          decimal(position.y * coordinate_scale, options.decimal_places),
          decimal(position.z * coordinate_scale, options.decimal_places),
          occupancy.has_value()
              ? CifValue{decimal(*occupancy, options.decimal_places)}
              : CifValue::missing("."),
          b_factor.has_value()
              ? CifValue{decimal(*b_factor, options.decimal_places)}
              : CifValue::missing("."),
          CifValue{formal_charge, formal_charge_missing},
          identity.author_sequence,
          identity.author_component,
          identity.author_asym_missing ? CifValue::missing("?")
                                       : CifValue{identity.author_asym},
          identity.author_atom,
          std::to_string(model_number)};
      const auto written = write_loop_row(output, row);
      if (!written.has_value()) {
        return operation::Result<StructureWriteReport>::failure(
            written.error());
      }
      present_coordinate_values += 3U;
    }
    if (frame.velocities().has_value())
      ++velocity_frames;
    if (frame.metadata().physical_time.has_value())
      ++time_frames;
    for (const auto &[name, unused] : frame.metadata().atom_properties) {
      static_cast<void>(unused);
      if (name != "occupancy" && name != "occupancy_present" &&
          name != "b_iso_or_equiv" && name != "b_iso_or_equiv_present") {
        ++other_frame_properties;
      }
    }
    other_frame_fields += frame.metadata().fields.size();
    if (context.report_progress) {
      context.report_progress({static_cast<double>(output_index + 1U) /
                                   static_cast<double>(frames.size()),
                               "write-mmcif"});
    }
  }
  output << "#\n";

  std::uint64_t amide_bonds{};
  if (!topology.bonds().empty()) {
    output << "loop_\n"
           << "_struct_conn.id\n"
           << "_struct_conn.conn_type_id\n"
           << "_struct_conn.ptnr1_label_asym_id\n"
           << "_struct_conn.ptnr1_label_seq_id\n"
           << "_struct_conn.ptnr1_label_comp_id\n"
           << "_struct_conn.ptnr1_label_atom_id\n"
           << "_struct_conn.pdbx_ptnr1_label_alt_id\n"
           << "_struct_conn.ptnr2_label_asym_id\n"
           << "_struct_conn.ptnr2_label_seq_id\n"
           << "_struct_conn.ptnr2_label_comp_id\n"
           << "_struct_conn.ptnr2_label_atom_id\n"
           << "_struct_conn.pdbx_ptnr2_label_alt_id\n"
           << "_struct_conn.pdbx_value_order\n"
           << "_struct_conn.ptnr1_auth_asym_id\n"
           << "_struct_conn.ptnr1_auth_seq_id\n"
           << "_struct_conn.ptnr1_auth_comp_id\n"
           << "_struct_conn.ptnr1_auth_atom_id\n"
           << "_struct_conn.ptnr2_auth_asym_id\n"
           << "_struct_conn.ptnr2_auth_seq_id\n"
           << "_struct_conn.ptnr2_auth_comp_id\n"
           << "_struct_conn.ptnr2_auth_atom_id\n";
    for (std::size_t index = 0; index < topology.bonds().size(); ++index) {
      const auto &bond = topology.bonds()[index];
      const auto &first = identities[bond.first.value];
      const auto &second = identities[bond.second.value];
      const auto order = bond_order(bond.order);
      const std::vector<CifValue> row{
          "conn" + std::to_string(index + 1U),
          "covale",
          first.label_asym,
          first.label_sequence,
          first.label_component,
          first.label_atom,
          CifValue{first.alternate, first.alternate_missing},
          second.label_asym,
          second.label_sequence,
          second.label_component,
          second.label_atom,
          CifValue{second.alternate, second.alternate_missing},
          CifValue{order, order == "?"},
          first.author_asym_missing ? CifValue::missing("?")
                                    : CifValue{first.author_asym},
          first.author_sequence,
          first.author_component,
          first.author_atom,
          second.author_asym_missing ? CifValue::missing("?")
                                     : CifValue{second.author_asym},
          second.author_sequence,
          second.author_component,
          second.author_atom};
      const auto written = write_loop_row(output, row);
      if (!written.has_value()) {
        return operation::Result<StructureWriteReport>::failure(
            written.error());
      }
      if (bond.order == model::BondOrder::amide)
        ++amide_bonds;
    }
    output << "#\n";
  }
  if (!output) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("failed while writing mmCIF output"));
  }

  LossCollector losses;
  losses.add("data_block", block_normalized ? 1U : 0U,
             "mmCIF export generated or sanitized the data-block identifier");
  losses.add(
      "label_asym_id", synthesized_label_asym,
      "mmCIF export synthesized missing label_asym_id from author chain or A");
  losses.add("label_entity_id", missing_label_entity,
             "mmCIF export writes unknown label_entity_id when entity "
             "relationships are not modeled");
  losses.add("atom_site_id", site_id - 1U,
             "mmCIF export assigns globally unique sequential atom_site IDs");
  losses.add("model_number", normalized_model_numbers,
             "mmCIF export normalized missing or duplicate model numbers");
  losses.add(
      "coordinate_precision", present_coordinate_values,
      "mmCIF coordinates are decimal text rounded to the requested precision");
  losses.add("crystal_symmetry", reference_cell.has_value() ? 1U : 0U,
             "mmCIF export preserves cell geometry but does not invent missing "
             "space-group relationships");
  losses.add("amide_bond_order", amide_bonds,
             "PDBx struct_conn has no native amide value-order code; endpoint "
             "is preserved with unknown order");
  losses.add("higher_connectivity",
             static_cast<std::uint64_t>(topology.angles().size() +
                                        topology.dihedrals().size() +
                                        topology.impropers().size() +
                                        topology.cmap_terms().size()),
             "mmCIF coordinate output does not preserve force-field angle, "
             "dihedral or improper terms");
  losses.add("velocity", velocity_frames,
             "mmCIF coordinate output does not store velocity vectors");
  losses.add("physical_time", time_frames,
             "mmCIF coordinate output does not store typed physical time");
  losses.add("frame_atom_properties", other_frame_properties,
             "mmCIF output preserves occupancy and isotropic B-factor frame "
             "columns only");
  losses.add("frame_metadata", other_frame_fields,
             "mmCIF output does not preserve arbitrary typed frame metadata");
  std::uint64_t other_atom_properties{};
  for (const auto &name : topology.properties().names()) {
    if (name != "mmcif.atom_site_id" && name != "mmcif.label_asym_id" &&
        name != "mmcif.label_seq_id" && name != "mmcif.label_entity_id" &&
        name != "mmcif.is_hetero" && name != "pdb.is_hetero" &&
        name != "formal_charge_present") {
      ++other_atom_properties;
    }
  }
  losses.add(
      "atom_properties", other_atom_properties,
      "mmCIF output preserves implemented atom-site identity columns only");
  std::uint64_t other_source_metadata{};
  for (const auto &[name, unused] : topology.source_metadata()) {
    static_cast<void>(unused);
    if (name != "format" && name != "syntax" && name != "data_block") {
      ++other_source_metadata;
    }
  }
  losses.add("source_metadata", other_source_metadata,
             "mmCIF output does not preserve the full source dictionary or "
             "scalar categories");
  const auto position = output.tellp();
  if (position < std::streampos{}) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("could not determine mmCIF output size"));
  }
  return operation::Result<StructureWriteReport>::success(StructureWriteReport{
      StructureFormat::mmcif, topology.atom_count(), frames.size(),
      static_cast<std::uint64_t>(position), losses.take()});
}

} // namespace molshredder::io::detail
