#include "molshredder/io/structure_writer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include "molshredder/core/parse_number.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "structure_reader_internal.hpp"
#include "structure_writer_internal.hpp"

namespace molshredder::io {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument,
                          std::move(message), std::move(suggestion)};
}

operation::Error io_error(std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::internal, std::move(message),
                          std::move(suggestion)};
}

StructureFormat resolve_format(StructureFormat requested,
                               const std::filesystem::path &path = {}) {
  if (requested != StructureFormat::auto_detect)
    return requested;
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  if (extension == ".xyz" || extension == ".xmol")
    return StructureFormat::xyz;
  if (extension == ".pdb" || extension == ".ent") {
    return StructureFormat::pdb;
  }
  if (extension == ".cif" || extension == ".mmcif") {
    return StructureFormat::mmcif;
  }
  if (extension == ".pqr")
    return StructureFormat::pqr;
  if (extension == ".mol")
    return StructureFormat::mol;
  if (extension == ".sdf" || extension == ".sd") {
    return StructureFormat::sdf;
  }
  if (extension == ".mol2")
    return StructureFormat::mol2;
  if (extension == ".psf")
    return StructureFormat::psf;
  if (extension == ".gro")
    return StructureFormat::gro;
  if (extension == ".g96")
    return StructureFormat::g96;
  return StructureFormat::auto_detect;
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

void add_residue_semantics_loss(const model::Topology &topology,
                                LossCollector &losses,
                                std::string_view format) {
  const auto count = static_cast<std::uint64_t>(std::ranges::count_if(
      topology.residues(), [](const model::ResidueRecord &residue) {
        return residue.chemical_origin !=
               model::ChemicalAnnotationOrigin::unspecified;
      }));
  losses.add("residue_semantics", count,
             std::string{format} +
                 " output does not preserve normalized residue/polymer classification");
}

void add_unrepresented_chemical_semantics(
    const model::Topology &topology, LossCollector &losses,
    std::string_view format, bool formal_charge_is_unrepresented) {
  std::uint64_t explicit_zero_charges{};
  std::uint64_t isotopes{};
  std::uint64_t radicals{};
  std::uint64_t atom_stereo{};
  for (const auto &atom : topology.atoms()) {
    explicit_zero_charges +=
        formal_charge_is_unrepresented && atom.formal_charge_present &&
                atom.formal_charge == 0
            ? 1U
            : 0U;
    isotopes += atom.isotope_mass_number.has_value() ? 1U : 0U;
    radicals += atom.radical != model::RadicalState::none ? 1U : 0U;
    atom_stereo +=
        atom.stereo_parity != model::AtomStereoParity::unspecified ? 1U : 0U;
  }
  std::uint64_t query_bonds{};
  std::uint64_t bond_stereo{};
  for (const auto &bond : topology.bonds()) {
    query_bonds += bond.order == model::BondOrder::query ? 1U : 0U;
    bond_stereo += bond.stereo != model::BondStereo::none ? 1U : 0U;
  }
  const auto prefix = std::string{format} + " output does not preserve ";
  losses.add("formal_charge", explicit_zero_charges,
             prefix + "explicit zero formal-charge presence");
  losses.add("isotope", isotopes, prefix + "isotope mass number");
  losses.add("radical", radicals, prefix + "radical state");
  losses.add("atom_stereo", atom_stereo, prefix + "atom stereo parity");
  losses.add("query_bond", query_bonds, prefix + "query bond constraints");
  losses.add("bond_stereo", bond_stereo, prefix + "bond stereo");
  add_residue_semantics_loss(topology, losses, format);
}

std::vector<std::size_t>
selected_frames(const model::CoordinateSource &coordinates,
                const StructureWriteOptions &options,
                std::optional<operation::Error> &error) {
  const auto count = coordinates.frame_count();
  if (!count.has_value() || count.value() == 0U) {
    error = invalid("structure export requires a known non-zero frame count");
    return {};
  }
  if (options.frame_indices.empty()) {
    std::vector<std::size_t> frames(count.value());
    std::iota(frames.begin(), frames.end(), std::size_t{});
    return frames;
  }
  for (const auto frame : options.frame_indices) {
    if (frame >= count.value()) {
      error = invalid("export frame " + std::to_string(frame) +
                          " is outside the available frame count",
                      "use a zero-based frame less than " +
                          std::to_string(count.value()));
      return {};
    }
  }
  return options.frame_indices;
}

std::string sanitized_comment(std::string value) {
  std::replace(value.begin(), value.end(), '\n', ' ');
  std::replace(value.begin(), value.end(), '\r', ' ');
  return value;
}

operation::Result<std::string>
gro_title_with_time(std::string title,
                    const std::optional<model::PhysicalTime> &physical_time) {
  const auto marker = title.find("t=");
  std::optional<std::pair<std::size_t, std::size_t>> existing_time;
  std::optional<double> existing_time_value;
  if (marker != std::string::npos) {
    auto begin = marker + 2U;
    while (begin < title.size() &&
           std::isspace(static_cast<unsigned char>(title[begin])) != 0) {
      ++begin;
    }
    double parsed{};
    const auto result =
        molshredder::core::from_chars(title.data() + begin, title.data() + title.size(), parsed);
    if (result.ec == std::errc{} && result.ptr != title.data() + begin &&
        std::isfinite(parsed)) {
      existing_time = std::pair{
          begin, static_cast<std::size_t>(result.ptr - title.data())};
      existing_time_value = parsed;
    }
  }
  if (!physical_time.has_value()) {
    if (existing_time.has_value()) {
      return operation::Result<std::string>::failure(invalid(
          "GRO title contains a parseable t= value but the frame has no "
          "typed physical time",
          "assign physical_time or remove the t= value from the title"));
    }
    return operation::Result<std::string>::success(std::move(title));
  }
  auto picoseconds = physical_time->value;
  if (physical_time->unit == model::TimeUnit::femtosecond)
    picoseconds /= 1000.0;
  if (!std::isfinite(picoseconds)) {
    return operation::Result<std::string>::failure(
        invalid("GRO physical time must be finite"));
  }
  std::ostringstream time_text;
  time_text.imbue(std::locale::classic());
  time_text << std::setprecision(15) << picoseconds;
  if (existing_time.has_value()) {
    if (*existing_time_value != picoseconds) {
      title.replace(existing_time->first,
                    existing_time->second - existing_time->first,
                    time_text.str());
    }
  } else {
    title += ", t= " + time_text.str();
  }
  return operation::Result<std::string>::success(std::move(title));
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

void collect_topology_losses(const model::Topology &topology,
                             LossCollector &losses) {
  losses.add(
      "residue_identity", static_cast<std::uint64_t>(topology.residue_count()),
      "XYZ does not store residue, chain, segment or insertion identity");
  const auto connectivity = topology.bonds().size() + topology.angles().size() +
                            topology.dihedrals().size() +
                            topology.impropers().size() +
                            topology.cmap_terms().size();
  losses.add("connectivity", static_cast<std::uint64_t>(connectivity),
             "XYZ does not store bonds, bond order or higher connectivity");
  losses.add("atom_properties",
             static_cast<std::uint64_t>(topology.properties().names().size()),
             "XYZ does not store typed topology property columns");
  losses.add("source_metadata",
             static_cast<std::uint64_t>(topology.source_metadata().size()),
             "XYZ does not preserve topology source metadata");
  std::uint64_t charges{};
  std::uint64_t names{};
  for (const auto &atom : topology.atoms()) {
    if (atom.formal_charge != 0)
      ++charges;
    const auto symbol = atom.atomic_number == 0U
                            ? std::string_view{"X"}
                            : detail::element_symbol(atom.atomic_number);
    if (atom.name != symbol)
      ++names;
  }
  losses.add("formal_charge", charges, "XYZ does not store atom formal charge");
  losses.add(
      "atom_name", names,
      "XYZ writes element symbols and does not preserve distinct atom names");
  add_unrepresented_chemical_semantics(topology, losses, "XYZ", true);
}

operation::Result<StructureWriteReport>
write_xyz(std::ostream &output, const model::Topology &topology,
          const model::CoordinateSource &coordinates,
          const StructureWriteOptions &options,
          operation::TaskContext &context) {
  if (options.decimal_places > 15U) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("XYZ decimal precision must be between 0 and 15"));
  }
  if (coordinates.atom_count() != topology.atom_count()) {
    return operation::Result<StructureWriteReport>::failure(invalid(
        "topology and coordinate source atom counts differ during export"));
  }
  std::optional<operation::Error> frame_error;
  const auto frames = selected_frames(coordinates, options, frame_error);
  if (frame_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*frame_error);
  }

  LossCollector losses;
  collect_topology_losses(topology, losses);
  output.imbue(std::locale::classic());
  output << std::fixed
         << std::setprecision(static_cast<int>(options.decimal_places));
  for (std::size_t output_index = 0; output_index < frames.size();
       ++output_index) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<StructureWriteReport>::failure(
          operation::Error{operation::ErrorCode::cancelled,
                           "XYZ export cancelled before frame " +
                               std::to_string(frames[output_index]),
                           {}});
    }
    const auto frame_result = coordinates.read_frame(frames[output_index]);
    if (!frame_result.has_value()) {
      return operation::Result<StructureWriteReport>::failure(
          frame_result.error());
    }
    const auto &frame = *frame_result.value();
    for (std::size_t atom = 0; atom < topology.atom_count(); ++atom) {
      if (!frame.atom_present(atom)) {
        return operation::Result<StructureWriteReport>::failure(invalid(
            "XYZ cannot represent missing atom " + std::to_string(atom + 1U) +
                " in frame " + std::to_string(frames[output_index]),
            "export a format with explicit atom presence or fill the atom "
            "first"));
      }
    }
    output << topology.atom_count() << '\n';
    auto comment = options.comment;
    if (comment.empty()) {
      const auto found = frame.metadata().fields.find("xyz.comment");
      comment =
          found == frame.metadata().fields.end()
              ? "MolShredder frame=" + std::to_string(frames[output_index])
              : found->second;
    }
    output << sanitized_comment(std::move(comment)) << '\n';
    const auto scale =
        frame.metadata().coordinate_unit == operation::LengthUnit::nanometer
            ? 10.0
            : 1.0;
    for (std::size_t atom_index = 0; atom_index < topology.atom_count();
         ++atom_index) {
      const auto number = topology.atoms()[atom_index].atomic_number;
      const auto symbol =
          number == 0U ? std::string_view{"X"} : detail::element_symbol(number);
      if (symbol.empty()) {
        return operation::Result<StructureWriteReport>::failure(
            invalid("atom " + std::to_string(atom_index + 1U) +
                    " has an invalid atomic number for XYZ export"));
      }
      const auto position = position_at(frame, atom_index);
      output << symbol << ' ' << position.x * scale << ' ' << position.y * scale
             << ' ' << position.z * scale << '\n';
    }
    losses.add(
        "coordinate_precision",
        static_cast<std::uint64_t>(topology.atom_count()) * 3U,
        "XYZ coordinates are decimal text rounded to the requested precision");
    losses.add("velocity", frame.velocities().has_value() ? 1U : 0U,
               "plain XYZ does not store velocity vectors");
    losses.add("unit_cell", frame.metadata().unit_cell.has_value() ? 1U : 0U,
               "plain XYZ does not store periodic unit cells");
    losses.add("physical_time",
               frame.metadata().physical_time.has_value() ? 1U : 0U,
               "plain XYZ does not store typed physical time");
    losses.add("source_step",
               frame.metadata().source_step.has_value() ? 1U : 0U,
               "plain XYZ does not store typed source step identity");
    losses.add(
        "frame_atom_properties",
        static_cast<std::uint64_t>(frame.metadata().atom_properties.size()),
        "plain XYZ does not store per-frame atom property columns");
    std::uint64_t unrepresented_fields{};
    for (const auto &[name, unused] : frame.metadata().fields) {
      static_cast<void>(unused);
      if (name != "xyz.comment")
        ++unrepresented_fields;
    }
    losses.add("frame_metadata", unrepresented_fields,
               "plain XYZ preserves only one untyped comment line per frame");
    if (!output) {
      return operation::Result<StructureWriteReport>::failure(
          io_error("failed while writing XYZ output"));
    }
    if (context.report_progress) {
      context.report_progress(
          operation::ProgressUpdate{static_cast<double>(output_index + 1U) /
                                        static_cast<double>(frames.size()),
                                    "write-xyz"});
    }
  }
  const auto position = output.tellp();
  if (position < std::streampos{}) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("could not determine XYZ output size"));
  }
  return operation::Result<StructureWriteReport>::success(StructureWriteReport{
      StructureFormat::xyz, topology.atom_count(), frames.size(),
      static_cast<std::uint64_t>(position), losses.take()});
}

const model::AtomProperty *
required_numeric_property(const model::Topology &topology,
                          std::string_view name, std::string_view purpose,
                          std::optional<operation::Error> &error) {
  const auto *property = topology.properties().find(name);
  if (property == nullptr) {
    error = invalid("PQR export requires atom property " + std::string{name},
                    "load a PQR file or assign " + std::string{purpose} +
                        " values before export");
    return nullptr;
  }
  if (!std::holds_alternative<std::vector<double>>(property->values) &&
      !std::holds_alternative<std::vector<float>>(property->values)) {
    error = invalid("PQR property " + std::string{name} +
                    " must be a float32 or float64 column");
    return nullptr;
  }
  return property;
}

double numeric_value(const model::AtomProperty &property, std::size_t index) {
  if (const auto *values = std::get_if<std::vector<double>>(&property.values)) {
    return (*values)[index];
  }
  return static_cast<double>(
      std::get<std::vector<float>>(property.values)[index]);
}

const model::BooleanColumn *hetero_column(const model::Topology &topology) {
  for (const auto name : {"pqr.is_hetero", "pdb.is_hetero"}) {
    const auto *property = topology.properties().find(name);
    if (property != nullptr) {
      if (const auto *values =
              std::get_if<model::BooleanColumn>(&property->values)) {
        return values;
      }
    }
  }
  return nullptr;
}

bool token_safe(std::string_view value) {
  return !value.empty() &&
         std::none_of(value.begin(), value.end(), [](unsigned char character) {
           return std::isspace(character) != 0;
         });
}

double pdb_vector_length(const model::Vec3d &value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

double pdb_vector_dot(const model::Vec3d &first, const model::Vec3d &second) {
  return first.x * second.x + first.y * second.y + first.z * second.z;
}

struct PdbCellParameters {
  double a{};
  double b{};
  double c{};
  double alpha{};
  double beta{};
  double gamma{};
};

PdbCellParameters pdb_cell_parameters(const model::UnitCell &cell,
                                      double scale) {
  const auto a = pdb_vector_length(cell.a);
  const auto b = pdb_vector_length(cell.b);
  const auto c = pdb_vector_length(cell.c);
  const auto angle = [](const model::Vec3d &first, const model::Vec3d &second,
                        double first_length, double second_length) {
    const auto cosine = std::clamp(pdb_vector_dot(first, second) /
                                       (first_length * second_length),
                                   -1.0, 1.0);
    return std::acos(cosine) * 180.0 / std::numbers::pi;
  };
  return {a * scale,
          b * scale,
          c * scale,
          angle(cell.b, cell.c, b, c),
          angle(cell.a, cell.c, a, c),
          angle(cell.a, cell.b, a, b)};
}

std::optional<std::string> fixed_decimal_field(double value,
                                               std::size_t width,
                                               unsigned int precision);

operation::Result<StructureWriteReport>
write_pqr(std::ostream &output, const model::Topology &topology,
          const model::CoordinateSource &coordinates,
          const StructureWriteOptions &options,
          operation::TaskContext &context) {
  if (options.decimal_places > 15U) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("PQR decimal precision must be between 0 and 15"));
  }
  if (coordinates.atom_count() != topology.atom_count()) {
    return operation::Result<StructureWriteReport>::failure(invalid(
        "topology and coordinate source atom counts differ during export"));
  }
  std::optional<operation::Error> frame_error;
  const auto frames = selected_frames(coordinates, options, frame_error);
  if (frame_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*frame_error);
  }
  if (frames.size() != 1U) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        "PQR export requires exactly one selected frame",
        "use --frames current or export each frame to a separate PQR file"});
  }
  std::optional<operation::Error> property_error;
  const auto *charges = required_numeric_property(
      topology, "partial_charge", "partial charge", property_error);
  if (property_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*property_error);
  }
  const auto *radii = topology.properties().find("pqr.radius");
  auto radius_property_name = std::string_view{"pqr.radius"};
  if (radii != nullptr &&
      !std::holds_alternative<std::vector<double>>(radii->values) &&
      !std::holds_alternative<std::vector<float>>(radii->values)) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("PQR property pqr.radius must be a float32 or float64 column"));
  }
  if (radii == nullptr) {
    radius_property_name = "vdw_radius";
    radii = required_numeric_property(topology, radius_property_name,
                                      "atomic radius", property_error);
  }
  if (property_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*property_error);
  }
  const auto charge_unit = charges->metadata.unit.value_or("elementary_charge");
  if (charge_unit != "elementary_charge" && charge_unit != "e") {
    return operation::Result<StructureWriteReport>::failure(
        invalid("PQR partial_charge unit must be elementary_charge or e"));
  }
  const auto radius_unit = radii->metadata.unit.value_or("angstrom");
  if (radius_unit != "angstrom" && radius_unit != "nanometer") {
    return operation::Result<StructureWriteReport>::failure(
        invalid("PQR radius property unit must be angstrom or nanometer"));
  }
  if (context.cancellation.is_cancelled()) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::cancelled, "PQR export cancelled", {}});
  }
  const auto frame_result = coordinates.read_frame(frames.front());
  if (!frame_result.has_value()) {
    return operation::Result<StructureWriteReport>::failure(
        frame_result.error());
  }
  const auto &frame = *frame_result.value();
  const auto *hetero = hetero_column(topology);
  std::set<std::int64_t> serials;
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    if (!frame.atom_present(index)) {
      return operation::Result<StructureWriteReport>::failure(invalid(
          "PQR cannot represent missing atom " + std::to_string(index + 1U) +
          " in frame " + std::to_string(frames.front())));
    }
    const auto &atom = topology.atoms()[index];
    const auto &residue = topology.residues()[atom.residue.value];
    if (!token_safe(atom.name) || !token_safe(residue.name) ||
        (!residue.chain_id.empty() && !token_safe(residue.chain_id))) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("PQR atom, residue and optional chain names must be "
                  "non-empty whitespace-free tokens"));
    }
    const auto serial =
        atom.source_serial.value_or(static_cast<std::int64_t>(index + 1U));
    if (!serials.insert(serial).second) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("PQR export encountered duplicate atom serial " +
                  std::to_string(serial)));
    }
    const auto charge = numeric_value(*charges, index);
    auto radius = numeric_value(*radii, index);
    if (radius_unit == "nanometer")
      radius *= 10.0;
    if (!std::isfinite(charge) || !std::isfinite(radius) || radius <= 0.0) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("PQR charge must be finite and radius must be finite and "
                  "positive at atom " +
                  std::to_string(index + 1U)));
    }
  }

  output.imbue(std::locale::classic());
  output << std::fixed
         << std::setprecision(static_cast<int>(options.decimal_places));
  if (!options.comment.empty()) {
    output << "REMARK " << sanitized_comment(options.comment) << '\n';
  }
  const auto coordinate_scale =
      frame.metadata().coordinate_unit == operation::LengthUnit::nanometer
          ? 10.0
          : 1.0;
  if (frame.metadata().unit_cell.has_value()) {
    const auto cell = pdb_cell_parameters(*frame.metadata().unit_cell,
                                          coordinate_scale);
    const auto a = fixed_decimal_field(cell.a, 9U, 3U);
    const auto b = fixed_decimal_field(cell.b, 9U, 3U);
    const auto c = fixed_decimal_field(cell.c, 9U, 3U);
    const auto alpha = fixed_decimal_field(cell.alpha, 7U, 2U);
    const auto beta = fixed_decimal_field(cell.beta, 7U, 2U);
    const auto gamma = fixed_decimal_field(cell.gamma, 7U, 2U);
    if (!a.has_value() || !b.has_value() || !c.has_value() ||
        !alpha.has_value() || !beta.has_value() || !gamma.has_value()) {
      return operation::Result<StructureWriteReport>::failure(invalid(
          "PQR unit-cell parameters do not fit CRYST1 fixed fields"));
    }
    output << "CRYST1" << *a << *b << *c << *alpha << *beta << *gamma
           << " P 1           1" << '\n';
  }
  const auto radius_scale = radius_unit == "nanometer" ? 10.0 : 1.0;
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    const auto &atom = topology.atoms()[index];
    const auto &residue = topology.residues()[atom.residue.value];
    const auto position = position_at(frame, index);
    output << ((hetero != nullptr && hetero->values[index] != 0U) ? "HETATM"
                                                                  : "ATOM")
           << ' '
           << atom.source_serial.value_or(static_cast<std::int64_t>(index + 1U))
           << ' ' << atom.name << ' ' << residue.name << ' ';
    if (!residue.chain_id.empty())
      output << residue.chain_id << ' ';
    output << residue.sequence_number << ' ' << position.x * coordinate_scale
           << ' ' << position.y * coordinate_scale << ' '
           << position.z * coordinate_scale << ' '
           << numeric_value(*charges, index) << ' '
           << numeric_value(*radii, index) * radius_scale << '\n';
  }
  if (!output) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("failed while writing PQR output"));
  }
  if (context.report_progress) {
    context.report_progress(operation::ProgressUpdate{1.0, "write-pqr"});
  }
  LossCollector losses;
  const auto connectivity = topology.bonds().size() + topology.angles().size() +
                            topology.dihedrals().size() +
                            topology.impropers().size() +
                            topology.cmap_terms().size();
  losses.add("connectivity", static_cast<std::uint64_t>(connectivity),
             "PQR does not store bonds, bond order or higher connectivity");
  std::uint64_t formal_charges{};
  std::uint64_t alternate_locations{};
  std::uint64_t extended_residue_identity{};
  for (const auto &atom : topology.atoms()) {
    if (atom.formal_charge != 0)
      ++formal_charges;
    if (!atom.alternate_location.empty())
      ++alternate_locations;
  }
  for (const auto &residue : topology.residues()) {
    if (!residue.insertion_code.empty() || !residue.segment_id.empty()) {
      ++extended_residue_identity;
    }
  }
  losses.add("explicit_element",
             static_cast<std::uint64_t>(topology.atom_count()),
             "PQR stores atom names but not an explicit element column");
  losses.add("formal_charge", formal_charges,
             "PQR partial charge does not preserve integer formal charge");
  losses.add("alternate_location", alternate_locations,
             "PQR does not store alternate-location identifiers");
  losses.add("extended_residue_identity", extended_residue_identity,
             "PQR does not store insertion codes or segment identifiers");
  std::uint64_t other_properties{};
  for (const auto &name : topology.properties().names()) {
    if (name != "partial_charge" && name != radius_property_name &&
        name != "pqr.is_hetero" && name != "pdb.is_hetero") {
      ++other_properties;
    }
  }
  losses.add("atom_properties", other_properties,
             "PQR preserves partial_charge and the selected radius property "
             "but not other typed columns");
  losses.add("source_metadata",
             static_cast<std::uint64_t>(topology.source_metadata().size()),
             "PQR does not preserve topology source metadata");
  add_unrepresented_chemical_semantics(topology, losses, "PQR", true);
  losses.add(
      "coordinate_precision",
      static_cast<std::uint64_t>(topology.atom_count()) * 3U,
      "PQR coordinates are decimal text rounded to the requested precision");
  losses.add("velocity", frame.velocities().has_value() ? 1U : 0U,
             "PQR does not store velocity vectors");
  if (frame.metadata().unit_cell.has_value()) {
    losses.add("crystal_symmetry", 1U,
               "PQR CRYST1 preserves unit-cell geometry but emits unknown "
               "symmetry as P 1 and Z=1");
  }
  losses.add("physical_time",
             frame.metadata().physical_time.has_value() ? 1U : 0U,
             "PQR does not store typed physical time");
  losses.add("source_step", frame.metadata().source_step.has_value() ? 1U : 0U,
             "PQR does not store typed source step identity");
  losses.add(
      "frame_atom_properties",
      static_cast<std::uint64_t>(frame.metadata().atom_properties.size()),
      "PQR does not store per-frame atom property columns");
  losses.add("frame_metadata",
             static_cast<std::uint64_t>(frame.metadata().fields.size()),
             "PQR does not preserve typed frame metadata");
  const auto position = output.tellp();
  if (position < std::streampos{}) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("could not determine PQR output size"));
  }
  return operation::Result<StructureWriteReport>::success(StructureWriteReport{
      StructureFormat::pqr, topology.atom_count(), 1U,
      static_cast<std::uint64_t>(position), losses.take()});
}

const std::vector<std::int64_t> *
integer_property(const model::Topology &topology, std::string_view name,
                 std::optional<operation::Error> &error) {
  const auto *property = topology.properties().find(name);
  if (property == nullptr)
    return nullptr;
  const auto *values =
      std::get_if<std::vector<std::int64_t>>(&property->values);
  if (values == nullptr) {
    error = invalid(std::string{name} + " must be an int64 property column");
  }
  return values;
}

std::optional<std::string> source_value(const model::Topology &topology,
                                        std::string_view name) {
  const auto found = topology.source_metadata().find(name);
  return found == topology.source_metadata().end()
             ? std::nullopt
             : std::optional<std::string>{found->second};
}

std::string coordinate_field(double value) {
  std::ostringstream field;
  field.imbue(std::locale::classic());
  field << std::fixed << std::setprecision(4) << std::setw(10) << value;
  auto result = std::move(field).str();
  return result.size() == 10U ? result : std::string{};
}

std::optional<std::string> fixed_decimal_field(double value, std::size_t width,
                                               unsigned int precision) {
  if (!std::isfinite(value))
    return std::nullopt;
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::fixed << std::setprecision(static_cast<int>(precision))
         << value;
  auto result = stream.str();
  if (result.size() > width)
    return std::nullopt;
  result.insert(result.begin(), width - result.size(), ' ');
  return result;
}

bool printable_ascii(std::string_view value) {
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return character >= 0x20U && character <= 0x7eU;
  });
}

std::string padded_right(std::string_view value, std::size_t width) {
  std::string result{value};
  result.append(width - result.size(), ' ');
  return result;
}

std::string pdb_atom_name(std::string_view name, std::string_view element) {
  if (name.size() == 4U ||
      (!name.empty() &&
       std::isdigit(static_cast<unsigned char>(name.front())) != 0)) {
    return padded_right(name, 4U);
  }
  if (element.size() == 1U) {
    return " " + padded_right(name, 3U);
  }
  return padded_right(name, 4U);
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
      error = invalid("PDB frame property " + std::string{presence_name} +
                      " exists without " + std::string{name});
    }
    return std::nullopt;
  }
  if (presence != frame.metadata().atom_properties.end()) {
    const auto *values =
        std::get_if<model::BooleanColumn>(&presence->second.values);
    if (values == nullptr) {
      error = invalid("PDB frame property " + std::string{presence_name} +
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
  error = invalid("PDB frame property " + std::string{name} +
                  " must be a float32 or float64 column");
  return std::nullopt;
}

bool cells_match(const std::optional<PdbCellParameters> &first,
                 const std::optional<PdbCellParameters> &second) {
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

operation::Result<StructureWriteReport>
write_pdb(std::ostream &output, const model::Topology &topology,
          const model::CoordinateSource &coordinates,
          const StructureWriteOptions &options,
          operation::TaskContext &context) {
  if (coordinates.atom_count() != topology.atom_count()) {
    return operation::Result<StructureWriteReport>::failure(invalid(
        "topology and coordinate source atom counts differ during export"));
  }
  if (topology.atom_count() == 0U || topology.atom_count() > 99999U) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        "PDB 3.3 requires 1..99999 atoms per model",
        "use mmCIF for structures beyond the fixed-column PDB limit"});
  }
  std::optional<operation::Error> frame_error;
  const auto frames = selected_frames(coordinates, options, frame_error);
  if (frame_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*frame_error);
  }

  const auto *hetero = hetero_column(topology);
  std::vector<std::int64_t> serials;
  serials.reserve(topology.atom_count());
  std::set<std::int64_t> unique_serials;
  bool preserve_serials = true;
  for (const auto &atom : topology.atoms()) {
    if (!atom.source_serial.has_value() || *atom.source_serial < 1 ||
        *atom.source_serial > 99999 ||
        !unique_serials.insert(*atom.source_serial).second) {
      preserve_serials = false;
      break;
    }
    serials.push_back(*atom.source_serial);
  }
  if (!preserve_serials) {
    serials.clear();
    for (std::size_t index = 0; index < topology.atom_count(); ++index) {
      serials.push_back(static_cast<std::int64_t>(index + 1U));
    }
  }

  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    const auto &atom = topology.atoms()[index];
    if (atom.residue.value >= topology.residue_count()) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("PDB atom references an invalid residue"));
    }
    const auto &residue = topology.residues()[atom.residue.value];
    const auto element = detail::element_symbol(atom.atomic_number);
    if (element.empty()) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("PDB export requires a known element at atom " +
                  std::to_string(index + 1U)));
    }
    if (atom.name.empty() || atom.name.size() > 4U || residue.name.empty() ||
        residue.name.size() > 3U || residue.chain_id.size() > 1U ||
        residue.insertion_code.size() > 1U || residue.segment_id.size() > 4U ||
        atom.alternate_location.size() > 1U || !printable_ascii(atom.name) ||
        !printable_ascii(residue.name) || !printable_ascii(residue.chain_id) ||
        !printable_ascii(residue.insertion_code) ||
        !printable_ascii(residue.segment_id) ||
        !printable_ascii(atom.alternate_location)) {
      return operation::Result<StructureWriteReport>::failure(invalid(
          "PDB fixed-column atom/residue identity does not fit at atom " +
              std::to_string(index + 1U),
          "shorten the field or use mmCIF"));
    }
    if (residue.sequence_number < -999 || residue.sequence_number > 9999) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("PDB residue sequence number does not fit I4 at atom " +
                      std::to_string(index + 1U),
                  "use mmCIF for extended residue identifiers"));
    }
    if (atom.formal_charge < -9 || atom.formal_charge > 9) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("PDB formal charge must fit one magnitude digit at atom " +
                  std::to_string(index + 1U)));
    }
  }

  if (context.cancellation.is_cancelled()) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::cancelled, "PDB export cancelled", {}});
  }
  const auto first_frame = coordinates.read_frame(frames.front());
  if (!first_frame.has_value()) {
    return operation::Result<StructureWriteReport>::failure(
        first_frame.error());
  }
  for (std::size_t atom = 0; atom < topology.atom_count(); ++atom) {
    if (!first_frame.value()->atom_present(atom)) {
      return operation::Result<StructureWriteReport>::failure(invalid(
          "PDB first output model is missing atom " + std::to_string(atom + 1U),
          "select a complete frame first so later MODEL records retain "
          "topology identity"));
    }
  }

  const auto frame_cell = [](const model::CoordinateFrame &frame)
      -> std::optional<PdbCellParameters> {
    if (!frame.metadata().unit_cell.has_value())
      return std::nullopt;
    const auto scale =
        frame.metadata().coordinate_unit == operation::LengthUnit::nanometer
            ? 10.0
            : 1.0;
    return pdb_cell_parameters(*frame.metadata().unit_cell, scale);
  };
  const auto reference_cell = frame_cell(*first_frame.value());

  LossCollector losses;
  add_residue_semantics_loss(topology, losses, "PDB");
  if (!preserve_serials) {
    losses.add("atom_serial", static_cast<std::uint64_t>(topology.atom_count()),
               "PDB export normalized invalid or duplicate atom serials to "
               "coordinate order");
  }
  if (options.decimal_places != 3U) {
    losses.add("requested_precision", 1U,
               "PDB 3.3 coordinates use fixed F8.3 precision");
  }
  if (reference_cell.has_value()) {
    losses.add("crystal_symmetry", 1U,
               "PDB export preserves unit-cell geometry but emits unknown "
               "symmetry as P 1 and Z=1");
  }

  const auto entry_id = source_value(topology, "entry_id");
  const auto entry_id_preserved = entry_id.has_value() && !entry_id->empty() &&
                                  entry_id->size() <= 4U &&
                                  printable_ascii(*entry_id);
  if (entry_id_preserved) {
    std::string header(80U, ' ');
    header.replace(0U, 6U, "HEADER");
    header.replace(62U, entry_id->size(), *entry_id);
    output << header << '\n';
  }
  if (!options.comment.empty()) {
    const auto comment = sanitized_comment(options.comment);
    if (!printable_ascii(comment)) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("PDB comment must contain printable ASCII characters only"));
    }
    for (std::size_t offset = 0; offset < comment.size(); offset += 68U) {
      output << "REMARK 999 " << comment.substr(offset, 68U) << '\n';
    }
  }
  if (reference_cell.has_value()) {
    const auto a = fixed_decimal_field(reference_cell->a, 9U, 3U);
    const auto b = fixed_decimal_field(reference_cell->b, 9U, 3U);
    const auto c = fixed_decimal_field(reference_cell->c, 9U, 3U);
    const auto alpha = fixed_decimal_field(reference_cell->alpha, 7U, 2U);
    const auto beta = fixed_decimal_field(reference_cell->beta, 7U, 2U);
    const auto gamma = fixed_decimal_field(reference_cell->gamma, 7U, 2U);
    if (!a.has_value() || !b.has_value() || !c.has_value() ||
        !alpha.has_value() || !beta.has_value() || !gamma.has_value()) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("PDB unit-cell parameters do not fit CRYST1 fixed fields",
                  "use mmCIF for this unit cell"));
    }
    output << "CRYST1" << *a << *b << *c << *alpha << *beta << *gamma
           << " P 1           1" << '\n';
  }

  std::set<std::uint64_t> model_numbers;
  std::uint64_t normalized_model_numbers{};
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
                           "PDB export cancelled before model " +
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
      return operation::Result<StructureWriteReport>::failure(operation::Error{
          operation::ErrorCode::unsupported,
          "PDB cannot represent a unit cell that changes between selected "
          "models",
          "export each cell separately or use a trajectory format"});
    }
    std::uint64_t model_number = output_index + 1U;
    if (frame.metadata().source_step.has_value() &&
        *frame.metadata().source_step >= 1U &&
        *frame.metadata().source_step <= 9999U &&
        model_numbers.insert(*frame.metadata().source_step).second) {
      model_number = *frame.metadata().source_step;
    } else {
      ++normalized_model_numbers;
      while (model_numbers.contains(model_number))
        ++model_number;
      if (model_number > 9999U) {
        return operation::Result<StructureWriteReport>::failure(
            operation::Error{operation::ErrorCode::unsupported,
                             "PDB MODEL serial exceeds the I4 field",
                             "export fewer frames or use a trajectory format"});
      }
      model_numbers.insert(model_number);
    }
    if (frames.size() > 1U) {
      output << "MODEL     " << std::setw(4) << model_number << '\n';
    }
    const auto coordinate_scale =
        frame.metadata().coordinate_unit == operation::LengthUnit::nanometer
            ? 10.0
            : 1.0;
    for (std::size_t index = 0; index < topology.atom_count(); ++index) {
      if (!frame.atom_present(index))
        continue;
      const auto &atom = topology.atoms()[index];
      const auto &residue = topology.residues()[atom.residue.value];
      const auto element = detail::element_symbol(atom.atomic_number);
      const auto position = position_at(frame, index);
      const auto x = fixed_decimal_field(position.x * coordinate_scale, 8U, 3U);
      const auto y = fixed_decimal_field(position.y * coordinate_scale, 8U, 3U);
      const auto z = fixed_decimal_field(position.z * coordinate_scale, 8U, 3U);
      if (!x.has_value() || !y.has_value() || !z.has_value()) {
        return operation::Result<StructureWriteReport>::failure(
            invalid("PDB coordinate does not fit F8.3 at atom " +
                        std::to_string(index + 1U) + " in model " +
                        std::to_string(output_index + 1U),
                    "translate coordinates or use mmCIF"));
      }
      std::optional<operation::Error> property_error;
      const auto occupancy = frame_numeric_property(
          frame, "occupancy", "occupancy_present", index, property_error);
      if (property_error.has_value()) {
        return operation::Result<StructureWriteReport>::failure(
            *property_error);
      }
      const auto b_factor = frame_numeric_property(frame, "b_iso_or_equiv",
                                                   "b_iso_or_equiv_present",
                                                   index, property_error);
      if (property_error.has_value()) {
        return operation::Result<StructureWriteReport>::failure(
            *property_error);
      }
      const auto occupancy_field =
          occupancy.has_value()
              ? fixed_decimal_field(*occupancy, 6U, 2U)
              : std::optional<std::string>{std::string(6U, ' ')};
      const auto b_factor_field =
          b_factor.has_value()
              ? fixed_decimal_field(*b_factor, 6U, 2U)
              : std::optional<std::string>{std::string(6U, ' ')};
      if (!occupancy_field.has_value() || !b_factor_field.has_value()) {
        return operation::Result<StructureWriteReport>::failure(
            invalid("PDB occupancy or B-factor does not fit F6.2 at atom " +
                    std::to_string(index + 1U)));
      }
      const auto charge = atom.formal_charge == 0
                              ? std::string{"  "}
                              : std::to_string(std::abs(atom.formal_charge)) +
                                    (atom.formal_charge > 0 ? "+" : "-");
      output << ((hetero != nullptr && hetero->values[index] != 0U) ? "HETATM"
                                                                    : "ATOM  ")
             << std::setw(5) << serials[index] << ' '
             << pdb_atom_name(atom.name, element)
             << (atom.alternate_location.empty() ? ' '
                                                 : atom.alternate_location[0])
             << padded_right(residue.name, 3U) << ' '
             << (residue.chain_id.empty() ? ' ' : residue.chain_id[0])
             << std::setw(4) << residue.sequence_number
             << (residue.insertion_code.empty() ? ' '
                                                : residue.insertion_code[0])
             << "   " << *x << *y << *z << *occupancy_field << *b_factor_field
             << "      " << padded_right(residue.segment_id, 4U) << std::setw(2)
             << element << charge << '\n';
      present_coordinate_values += 3U;
    }
    if (frames.size() > 1U)
      output << "ENDMDL\n";
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
                               "write-pdb"});
    }
  }

  std::map<std::int64_t, std::vector<std::int64_t>> adjacency;
  std::uint64_t bond_order_losses{};
  std::uint64_t bond_stereo_losses{};
  for (const auto &bond : topology.bonds()) {
    adjacency[serials[bond.first.value]].push_back(serials[bond.second.value]);
    if (bond.order != model::BondOrder::unknown)
      ++bond_order_losses;
    if (bond.stereo != model::BondStereo::none)
      ++bond_stereo_losses;
  }
  for (auto &[source, targets] : adjacency) {
    std::sort(targets.begin(), targets.end());
    for (std::size_t first = 0; first < targets.size(); first += 4U) {
      output << "CONECT" << std::setw(5) << source;
      const auto count = std::min<std::size_t>(4U, targets.size() - first);
      for (std::size_t index = 0; index < count; ++index) {
        output << std::setw(5) << targets[first + index];
      }
      output << '\n';
    }
  }
  output << "END\n";
  if (!output) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("failed while writing PDB output"));
  }

  losses.add(
      "model_number", normalized_model_numbers,
      "PDB export normalized missing, duplicate or out-of-range MODEL serials");
  losses.add("coordinate_precision", present_coordinate_values,
             "PDB coordinates are rounded to fixed F8.3 fields");
  losses.add(
      "bond_order", bond_order_losses,
      "PDB CONECT output preserves endpoints but not explicit bond order");
  losses.add("bond_stereo", bond_stereo_losses,
             "PDB CONECT output does not preserve bond stereo");
  losses.add("higher_connectivity",
             static_cast<std::uint64_t>(topology.angles().size() +
                                        topology.dihedrals().size() +
                                        topology.impropers().size() +
                                        topology.cmap_terms().size()),
             "PDB output does not preserve angle, dihedral or improper terms");
  losses.add("velocity", velocity_frames,
             "PDB output does not store velocity vectors");
  losses.add("physical_time", time_frames,
             "PDB output does not store typed physical time");
  losses.add("frame_atom_properties", other_frame_properties,
             "PDB output preserves occupancy and isotropic B-factor frame "
             "columns only");
  losses.add("frame_metadata", other_frame_fields,
             "PDB output does not preserve arbitrary typed frame metadata");
  std::uint64_t other_atom_properties{};
  std::uint64_t isotope_losses{};
  std::uint64_t radical_losses{};
  std::uint64_t atom_stereo_losses{};
  for (const auto &atom : topology.atoms()) {
    isotope_losses += atom.isotope_mass_number.has_value() ? 1U : 0U;
    radical_losses += atom.radical != model::RadicalState::none ? 1U : 0U;
    atom_stereo_losses +=
        atom.stereo_parity != model::AtomStereoParity::unspecified ? 1U : 0U;
  }
  losses.add("isotope", isotope_losses,
             "PDB ATOM/HETATM output does not preserve isotope mass number");
  losses.add("radical", radical_losses,
             "PDB ATOM/HETATM output does not preserve radical state");
  losses.add("atom_stereo", atom_stereo_losses,
             "PDB ATOM/HETATM output does not preserve atom stereo parity");
  for (const auto &name : topology.properties().names()) {
    if (name != "pdb.is_hetero" && name != "pqr.is_hetero") {
      ++other_atom_properties;
    }
  }
  losses.add("atom_properties", other_atom_properties,
             "PDB output preserves only ATOM/HETATM origin from static "
             "property columns");
  std::uint64_t other_source_metadata{};
  for (const auto &[name, unused] : topology.source_metadata()) {
    static_cast<void>(unused);
    if ((name != "entry_id" || !entry_id_preserved) && name != "format" &&
        name != "format_version") {
      ++other_source_metadata;
    }
  }
  losses.add(
      "source_metadata", other_source_metadata,
      "PDB output preserves a valid entry ID but not other source metadata");
  const auto position = output.tellp();
  if (position < std::streampos{}) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("could not determine PDB output size"));
  }
  return operation::Result<StructureWriteReport>::success(StructureWriteReport{
      StructureFormat::pdb, topology.atom_count(), frames.size(),
      static_cast<std::uint64_t>(position), losses.take()});
}

std::uint64_t mol_bond_type(const model::Bond &bond) {
  switch (bond.order) {
  case model::BondOrder::single:
    return 1U;
  case model::BondOrder::double_bond:
    return 2U;
  case model::BondOrder::triple:
    return 3U;
  case model::BondOrder::aromatic:
    return 4U;
  case model::BondOrder::query:
    switch (bond.query) {
    case model::BondQuery::single_or_double: return 5U;
    case model::BondQuery::single_or_aromatic: return 6U;
    case model::BondQuery::double_or_aromatic: return 7U;
    case model::BondQuery::any: return 8U;
    case model::BondQuery::none: return 0U;
    }
    return 0U;
  case model::BondOrder::amide:
  case model::BondOrder::zero:
  case model::BondOrder::unknown:
    return 0U;
  }
  return 0U;
}

std::uint64_t mol_bond_stereo(model::BondStereo stereo) {
  switch (stereo) {
  case model::BondStereo::none: return 0U;
  case model::BondStereo::up: return 1U;
  case model::BondStereo::cis_or_trans: return 3U;
  case model::BondStereo::either: return 4U;
  case model::BondStereo::down: return 6U;
  }
  return 0U;
}

void write_m_property(
    std::ostream &output, std::string_view keyword,
    const std::vector<std::pair<std::size_t, std::int64_t>> &values) {
  constexpr std::size_t kPairsPerLine = 8U;
  for (std::size_t first = 0; first < values.size(); first += kPairsPerLine) {
    const auto count = std::min(kPairsPerLine, values.size() - first);
    output << "M  " << keyword << std::setw(3) << count;
    for (std::size_t index = 0; index < count; ++index) {
      output << std::setw(4) << values[first + index].first << std::setw(4)
             << values[first + index].second;
    }
    output << '\n';
  }
}

operation::Result<StructureWriteReport>
write_molfile(std::ostream &output, const model::Topology &topology,
              const model::CoordinateSource &coordinates,
              const StructureWriteOptions &options,
              operation::TaskContext &context) {
  if (coordinates.atom_count() != topology.atom_count()) {
    return operation::Result<StructureWriteReport>::failure(invalid(
        "topology and coordinate source atom counts differ during export"));
  }
  if (topology.atom_count() == 0U || topology.atom_count() > 999U ||
      topology.bonds().size() > 999U) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        "MOL V2000 requires 1..999 atoms and at most 999 bonds",
        "use a future MOL V3000 writer for larger structures"});
  }
  std::optional<operation::Error> frame_error;
  const auto frames = selected_frames(coordinates, options, frame_error);
  if (frame_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*frame_error);
  }
  if (frames.size() != 1U) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        "MOL/SDF export requires exactly one selected frame",
        "use --frames current or export frames to separate files"});
  }
  if (context.cancellation.is_cancelled()) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::cancelled, "MOL/SDF export cancelled", {}});
  }
  const auto frame_result = coordinates.read_frame(frames.front());
  if (!frame_result.has_value()) {
    return operation::Result<StructureWriteReport>::failure(
        frame_result.error());
  }
  const auto &frame = *frame_result.value();
  std::optional<operation::Error> property_error;
  const auto *mass_difference =
      integer_property(topology, "sdf.mass_difference", property_error);
  const auto *stereo_parity =
      integer_property(topology, "sdf.atom_stereo_parity", property_error);
  const auto *isotope_mass =
      integer_property(topology, "sdf.isotope_mass", property_error);
  const auto *radical =
      integer_property(topology, "sdf.radical", property_error);
  if (property_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*property_error);
  }
  const auto coordinate_scale =
      frame.metadata().coordinate_unit == operation::LengthUnit::nanometer
          ? 10.0
          : 1.0;
  std::vector<std::string> coordinate_fields;
  coordinate_fields.reserve(topology.atom_count() * 3U);
  std::vector<std::int64_t> normalized_stereo(topology.atom_count());
  std::vector<std::int64_t> normalized_isotope(topology.atom_count());
  std::vector<std::int64_t> normalized_radical(topology.atom_count());
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    if (!frame.atom_present(index)) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("MOL/SDF cannot represent missing atom " +
                  std::to_string(index + 1U)));
    }
    const auto position = position_at(frame, index);
    for (const auto value : {position.x, position.y, position.z}) {
      auto formatted = coordinate_field(value * coordinate_scale);
      if (formatted.empty()) {
        return operation::Result<StructureWriteReport>::failure(invalid(
            "MOL V2000 coordinate does not fit the fixed F10.4 field at atom " +
                std::to_string(index + 1U),
            "translate/scale coordinates or use a future V3000 writer"));
      }
      coordinate_fields.push_back(std::move(formatted));
    }
    const auto number = topology.atoms()[index].atomic_number;
    if (number != 0U && detail::element_symbol(number).empty()) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("atom has an invalid atomic number for MOL/SDF export"));
    }
    const auto mass =
        mass_difference == nullptr ? 0 : (*mass_difference)[index];
    const auto &atom = topology.atoms()[index];
    const auto legacy_parity =
        stereo_parity == nullptr ? 0 : (*stereo_parity)[index];
    const auto legacy_isotope =
        isotope_mass == nullptr ? 0 : (*isotope_mass)[index];
    const auto legacy_radical = radical == nullptr ? 0 : (*radical)[index];
    const auto core_parity = static_cast<std::int64_t>(atom.stereo_parity);
    const auto core_isotope = static_cast<std::int64_t>(
        atom.isotope_mass_number.value_or(0U));
    const auto core_radical = static_cast<std::int64_t>(atom.radical);
    if ((core_parity != 0 && legacy_parity != 0 &&
         core_parity != legacy_parity) ||
        (core_isotope != 0 && legacy_isotope != 0 &&
         core_isotope != legacy_isotope) ||
        (core_radical != 0 && legacy_radical != 0 &&
         core_radical != legacy_radical)) {
      return operation::Result<StructureWriteReport>::failure(invalid(
          "core and legacy MOL/SDF chemical annotations disagree at atom " +
          std::to_string(index + 1U)));
    }
    const auto parity = core_parity != 0 ? core_parity : legacy_parity;
    const auto isotope = core_isotope != 0 ? core_isotope : legacy_isotope;
    const auto radical_value =
        core_radical != 0 ? core_radical : legacy_radical;
    normalized_stereo[index] = parity;
    normalized_isotope[index] = isotope;
    normalized_radical[index] = radical_value;
    if (mass < -3 || mass > 4 || parity < 0 || parity > 3 || isotope < 0 ||
        (radical_value < 0 || radical_value > 3)) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("MOL/SDF isotope, radical, mass-difference or stereo "
                  "property is out of range"));
    }
  }
  for (const auto &bond : topology.bonds()) {
    if (mol_bond_type(bond) == 0U) {
      return operation::Result<StructureWriteReport>::failure(invalid(
          "MOL/SDF cannot export an unknown or format-specific bond order",
          "assign a V2000 concrete or query bond type first"));
    }
  }

  output.imbue(std::locale::classic());
  output << sanitized_comment(
                source_value(topology, "molfile.name").value_or("MolShredder"))
         << '\n';
  output << sanitized_comment(source_value(topology, "molfile.program")
                                  .value_or("  MolShredder"))
         << '\n';
  output << sanitized_comment(
                options.comment.empty()
                    ? source_value(topology, "molfile.comment").value_or("")
                    : options.comment)
         << '\n';
  output << std::right << std::setw(3) << topology.atom_count() << std::setw(3)
         << topology.bonds().size() << "  0  0  0  0  0  0  0  0999 V2000\n";
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<StructureWriteReport>::failure(
          operation::Error{operation::ErrorCode::cancelled,
                           "MOL/SDF export cancelled while writing atoms",
                           {}});
    }
    const auto atomic_number = topology.atoms()[index].atomic_number;
    const auto symbol =
        atomic_number == 0U
            ? std::string{"*"}
            : std::string{detail::element_symbol(atomic_number)};
    const auto mass =
        mass_difference == nullptr ? 0 : (*mass_difference)[index];
    const auto parity = normalized_stereo[index];
    output << coordinate_fields[index * 3U]
           << coordinate_fields[index * 3U + 1U]
           << coordinate_fields[index * 3U + 2U] << ' ' << std::left
           << std::setw(3) << symbol << std::right << std::setw(2) << mass
           << std::setw(3) << 0 << std::setw(3) << parity
           << "  0  0  0  0  0  0  0  0  0\n";
  }
  for (const auto &bond : topology.bonds()) {
    output << std::setw(3) << bond.first.value + 1U << std::setw(3)
           << bond.second.value + 1U << std::setw(3)
           << mol_bond_type(bond) << std::setw(3)
           << mol_bond_stereo(bond.stereo) << "  0  0  0\n";
  }
  std::vector<std::pair<std::size_t, std::int64_t>> charges;
  std::vector<std::pair<std::size_t, std::int64_t>> isotopes;
  std::vector<std::pair<std::size_t, std::int64_t>> radicals;
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    if (topology.atoms()[index].formal_charge_present ||
        topology.atoms()[index].formal_charge != 0) {
      charges.emplace_back(index + 1U, topology.atoms()[index].formal_charge);
    }
    if (normalized_isotope[index] != 0) {
      isotopes.emplace_back(index + 1U, normalized_isotope[index]);
    }
    if (normalized_radical[index] != 0) {
      radicals.emplace_back(index + 1U, normalized_radical[index]);
    }
  }
  write_m_property(output, "CHG", charges);
  write_m_property(output, "ISO", isotopes);
  write_m_property(output, "RAD", radicals);
  if (const auto raw =
          source_value(topology, "molfile.unparsed_property_lines");
      raw.has_value()) {
    std::istringstream raw_lines{*raw};
    std::string line;
    while (std::getline(raw_lines, line)) {
      const auto cleaned = detail::trim(line);
      if (cleaned == "M  END" || cleaned == "$$$$") {
        return operation::Result<StructureWriteReport>::failure(invalid(
            "preserved MOL property line contains a record terminator"));
      }
      output << line << '\n';
    }
  }
  output << "M  END\n";
  std::uint64_t sdf_field_count{};
  for (const auto &[name, value] : topology.source_metadata()) {
    constexpr std::string_view prefix{"sdf.data."};
    if (!name.starts_with(prefix))
      continue;
    ++sdf_field_count;
    if (options.format != StructureFormat::sdf)
      continue;
    const auto field_name = std::string_view{name}.substr(prefix.size());
    if (field_name.empty() ||
        field_name.find_first_of("<>\r\n") != std::string_view::npos) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("invalid preserved SDF data-field name"));
    }
    output << ">  <" << field_name << ">\n";
    std::istringstream field_lines{value};
    std::string line;
    while (std::getline(field_lines, line)) {
      if (detail::trim(line) == "$$$$") {
        return operation::Result<StructureWriteReport>::failure(
            invalid("SDF data value contains a record delimiter line"));
      }
      output << line << '\n';
    }
    output << '\n';
  }
  if (options.format == StructureFormat::sdf)
    output << "$$$$\n";
  if (!output) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("failed while writing MOL/SDF output"));
  }
  if (context.report_progress) {
    context.report_progress(operation::ProgressUpdate{1.0, "write-mol-v2000"});
  }

  LossCollector losses;
  add_residue_semantics_loss(topology, losses, "MOL/SDF");
  losses.add("residue_identity",
             static_cast<std::uint64_t>(topology.residue_count()),
             "MOL V2000 does not store residue, chain or segment identity");
  std::uint64_t distinct_names{};
  std::uint64_t alternate_locations{};
  for (const auto &atom : topology.atoms()) {
    const auto symbol = atom.atomic_number == 0U
                            ? std::string_view{"X"}
                            : detail::element_symbol(atom.atomic_number);
    if (atom.name != symbol)
      ++distinct_names;
    if (!atom.alternate_location.empty())
      ++alternate_locations;
  }
  losses.add("atom_name", distinct_names,
             "MOL V2000 stores element/query symbols but not atom names");
  losses.add("alternate_location", alternate_locations,
             "MOL V2000 does not store alternate-location identifiers");
  std::uint64_t other_properties{};
  for (const auto &name : topology.properties().names()) {
    if (name != "sdf.mass_difference" && name != "sdf.atom_stereo_parity" &&
        name != "sdf.isotope_mass" && name != "sdf.radical") {
      ++other_properties;
    }
  }
  losses.add("atom_properties", other_properties,
             "MOL/SDF writer preserves only implemented V2000 atom properties");
  losses.add("sdf_data_fields",
             options.format == StructureFormat::mol ? sdf_field_count : 0U,
             "MOL output omits SDF data fields");
  losses.add("coordinate_precision",
             static_cast<std::uint64_t>(topology.atom_count()) * 3U,
             "MOL V2000 coordinates use fixed F10.4 decimal fields");
  losses.add("velocity", frame.velocities().has_value() ? 1U : 0U,
             "MOL/SDF does not store velocity vectors");
  losses.add("unit_cell", frame.metadata().unit_cell.has_value() ? 1U : 0U,
             "MOL/SDF does not store periodic unit cells");
  losses.add("physical_time",
             frame.metadata().physical_time.has_value() ? 1U : 0U,
             "MOL/SDF does not store typed physical time");
  losses.add("source_step", frame.metadata().source_step.has_value() ? 1U : 0U,
             "MOL/SDF does not store typed source step identity");
  losses.add(
      "frame_atom_properties",
      static_cast<std::uint64_t>(frame.metadata().atom_properties.size()),
      "MOL/SDF does not store per-frame atom property columns");
  losses.add("frame_metadata",
             static_cast<std::uint64_t>(frame.metadata().fields.size()),
             "MOL/SDF does not preserve typed frame metadata");
  const auto position = output.tellp();
  if (position < std::streampos{}) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("could not determine MOL/SDF output size"));
  }
  return operation::Result<StructureWriteReport>::success(StructureWriteReport{
      options.format, topology.atom_count(), 1U,
      static_cast<std::uint64_t>(position), losses.take()});
}

std::string_view mol2_bond_type(model::BondOrder order) {
  switch (order) {
  case model::BondOrder::single:
    return "1";
  case model::BondOrder::double_bond:
    return "2";
  case model::BondOrder::triple:
    return "3";
  case model::BondOrder::aromatic:
    return "ar";
  case model::BondOrder::amide:
    return "am";
  case model::BondOrder::zero:
  case model::BondOrder::unknown:
  case model::BondOrder::query:
    return {};
  }
  return {};
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

operation::Result<std::vector<double>>
optional_numeric_property(const model::Topology &topology,
                          std::string_view name) {
  const auto *property = topology.properties().find(name);
  if (property == nullptr) {
    return operation::Result<std::vector<double>>::success({});
  }
  std::vector<double> result;
  result.reserve(topology.atom_count());
  if (const auto *double_values =
          std::get_if<std::vector<double>>(&property->values)) {
    result = *double_values;
  } else if (const auto *float_values =
                 std::get_if<std::vector<float>>(&property->values)) {
    for (const auto value : *float_values)
      result.push_back(value);
  } else {
    return operation::Result<std::vector<double>>::failure(invalid(
        std::string{name} + " must be float32 or float64 for MOL2 export"));
  }
  if (property->metadata.unit.has_value() &&
      property->metadata.unit != "elementary_charge" &&
      property->metadata.unit != "e") {
    return operation::Result<std::vector<double>>::failure(
        invalid(std::string{name} +
                " requires elementary_charge or e units for MOL2 export"));
  }
  if (std::any_of(result.begin(), result.end(),
                  [](double value) { return !std::isfinite(value); })) {
    return operation::Result<std::vector<double>>::failure(
        invalid(std::string{name} + " values must be finite"));
  }
  return operation::Result<std::vector<double>>::success(std::move(result));
}

double vector_length(const model::Vec3d &value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

double cell_angle(const model::Vec3d &first, const model::Vec3d &second) {
  auto cosine = (first.x * second.x + first.y * second.y + first.z * second.z) /
                (vector_length(first) * vector_length(second));
  cosine = std::clamp(cosine, -1.0, 1.0);
  return std::acos(cosine) * 180.0 / std::numbers::pi;
}

model::Vec3d buffer_value_at(const model::CoordinateBuffer &buffer,
                             std::size_t index) {
  return std::visit(
      [index](const auto &values) {
        const auto &value = values[index];
        return model::Vec3d{static_cast<double>(value.x),
                            static_cast<double>(value.y),
                            static_cast<double>(value.z)};
      },
      buffer.values());
}

operation::Result<StructureWriteReport>
write_g96(std::ostream &output, const model::Topology &topology,
          const model::CoordinateSource &coordinates,
          const StructureWriteOptions &options,
          operation::TaskContext &context) {
  if (options.decimal_places > 15U) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("G96 requested decimal precision must be between 0 and 15"));
  }
  if (coordinates.atom_count() != topology.atom_count() ||
      topology.atom_count() == 0U || topology.atom_count() > 9999999U) {
    return operation::Result<StructureWriteReport>::failure(invalid(
        "G96 export requires a matching topology with 1..9999999 atoms"));
  }
  std::vector<std::int64_t> atom_numbers;
  atom_numbers.reserve(topology.atom_count());
  std::set<std::int64_t> unique_atom_numbers;
  bool preserve_atom_numbers = true;
  for (const auto &atom : topology.atoms()) {
    const auto &residue = topology.residues()[atom.residue.value];
    if (residue.sequence_number < 0 || residue.sequence_number > 99999 ||
        residue.name.empty() || residue.name.size() > 5U || atom.name.empty() ||
        atom.name.size() > 5U) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("G96 residue number must fit I5 and residue/atom names must "
                  "fit five characters",
                  "rename or renumber the topology before G96 export"));
    }
    if (atom.source_serial.has_value() && *atom.source_serial >= 0 &&
        *atom.source_serial <= 9999999 &&
        unique_atom_numbers.insert(*atom.source_serial).second) {
      atom_numbers.push_back(*atom.source_serial);
    } else {
      preserve_atom_numbers = false;
    }
  }
  if (!preserve_atom_numbers) {
    atom_numbers.clear();
    for (std::size_t index = 0; index < topology.atom_count(); ++index) {
      atom_numbers.push_back(static_cast<std::int64_t>(index + 1U));
    }
  }
  std::optional<operation::Error> frame_error;
  const auto frames = selected_frames(coordinates, options, frame_error);
  if (frame_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*frame_error);
  }

  LossCollector losses;
  output.imbue(std::locale::classic());
  auto title = options.comment.empty() ? source_value(topology, "g96.title")
                                             .value_or("MolShredder G96 export")
                                       : options.comment;
  output << "TITLE\n" << sanitized_comment(std::move(title)) << "\nEND\n";
  for (std::size_t frame_output_index = 0; frame_output_index < frames.size();
       ++frame_output_index) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<StructureWriteReport>::failure(operation::Error{
          operation::ErrorCode::cancelled, "G96 export cancelled", {}});
    }
    const auto loaded = coordinates.read_frame(frames[frame_output_index]);
    if (!loaded.has_value()) {
      return operation::Result<StructureWriteReport>::failure(loaded.error());
    }
    const auto &frame = *loaded.value();
    if (frame.metadata().physical_time.has_value()) {
      const auto &physical_time = *frame.metadata().physical_time;
      const auto time_ps = physical_time.unit == model::TimeUnit::picosecond
                               ? physical_time.value
                               : physical_time.value / 1000.0;
      if (!std::isfinite(time_ps)) {
        return operation::Result<StructureWriteReport>::failure(
            invalid("G96 physical time must be finite"));
      }
      const auto step = frame.metadata().source_step.value_or(
          static_cast<std::uint64_t>(frames[frame_output_index]));
      const auto step_text = std::to_string(step);
      const auto time_text = fixed_decimal_field(time_ps, 15U, 6U);
      if (step_text.size() > 15U || !time_text.has_value()) {
        return operation::Result<StructureWriteReport>::failure(
            invalid("G96 TIMESTEP values do not fit the fixed-width fields"));
      }
      output << "TIMESTEP\n"
             << std::right << std::setw(15) << step << *time_text << "\nEND\n";
      losses.add(
          "source_step", frame.metadata().source_step.has_value() ? 0U : 1U,
          "G96 TIMESTEP synthesized a frame-index step for physical time");
    } else {
      losses.add(
          "source_step", frame.metadata().source_step.has_value() ? 1U : 0U,
          "G96 TIMESTEP was omitted because source step had no physical time");
    }

    output << "POSITION\n";
    const auto coordinate_scale =
        frame.metadata().coordinate_unit == operation::LengthUnit::angstrom
            ? 0.1
            : 1.0;
    double velocity_scale{};
    if (frame.velocities().has_value()) {
      if (!frame.metadata().velocity_time_unit.has_value()) {
        return operation::Result<StructureWriteReport>::failure(invalid(
            "G96 velocity export requires an explicit velocity time unit"));
      }
      velocity_scale =
          coordinate_scale *
          (*frame.metadata().velocity_time_unit == model::TimeUnit::femtosecond
               ? 1000.0
               : 1.0);
    }
    for (std::size_t index = 0; index < topology.atom_count(); ++index) {
      if (context.cancellation.is_cancelled()) {
        return operation::Result<StructureWriteReport>::failure(
            operation::Error{operation::ErrorCode::cancelled,
                             "G96 export cancelled while writing positions",
                             {}});
      }
      if (!frame.atom_present(index)) {
        return operation::Result<StructureWriteReport>::failure(invalid(
            "G96 cannot represent missing atom " + std::to_string(index + 1U)));
      }
      const auto &atom = topology.atoms()[index];
      const auto &residue = topology.residues()[atom.residue.value];
      const auto position = position_at(frame, index);
      std::array<std::optional<std::string>, 3> values{
          fixed_decimal_field(position.x * coordinate_scale, 15U, 9U),
          fixed_decimal_field(position.y * coordinate_scale, 15U, 9U),
          fixed_decimal_field(position.z * coordinate_scale, 15U, 9U)};
      if (std::any_of(values.begin(), values.end(),
                      [](const auto &value) { return !value.has_value(); })) {
        return operation::Result<StructureWriteReport>::failure(
            invalid("G96 coordinate does not fit the fixed F15.9 field",
                    "translate or scale coordinates before export"));
      }
      output << std::right << std::setw(5) << residue.sequence_number << ' '
             << std::left << std::setw(5) << residue.name << ' ' << std::setw(5)
             << atom.name << std::right << std::setw(7) << atom_numbers[index]
             << *values[0] << *values[1] << *values[2] << '\n';
    }
    output << "END\n";
    if (frame.velocities().has_value()) {
      output << "VELOCITY\n";
      for (std::size_t index = 0; index < topology.atom_count(); ++index) {
        if (context.cancellation.is_cancelled()) {
          return operation::Result<StructureWriteReport>::failure(
              operation::Error{operation::ErrorCode::cancelled,
                               "G96 export cancelled while writing velocities",
                               {}});
        }
        const auto &atom = topology.atoms()[index];
        const auto &residue = topology.residues()[atom.residue.value];
        const auto velocity = buffer_value_at(*frame.velocities(), index);
        std::array<std::optional<std::string>, 3> values{
            fixed_decimal_field(velocity.x * velocity_scale, 15U, 9U),
            fixed_decimal_field(velocity.y * velocity_scale, 15U, 9U),
            fixed_decimal_field(velocity.z * velocity_scale, 15U, 9U)};
        if (std::any_of(values.begin(), values.end(),
                        [](const auto &value) { return !value.has_value(); })) {
          return operation::Result<StructureWriteReport>::failure(
              invalid("G96 velocity does not fit the fixed F15.9 field"));
        }
        output << std::right << std::setw(5) << residue.sequence_number << ' '
               << std::left << std::setw(5) << residue.name << ' '
               << std::setw(5) << atom.name << std::right << std::setw(7)
               << atom_numbers[index] << *values[0] << *values[1] << *values[2]
               << '\n';
      }
      output << "END\n";
    }
    if (frame.metadata().unit_cell.has_value()) {
      const auto &cell = *frame.metadata().unit_cell;
      const std::array<double, 9> raw_values{
          cell.a.x * coordinate_scale, cell.b.y * coordinate_scale,
          cell.c.z * coordinate_scale, cell.a.y * coordinate_scale,
          cell.a.z * coordinate_scale, cell.b.x * coordinate_scale,
          cell.b.z * coordinate_scale, cell.c.x * coordinate_scale,
          cell.c.y * coordinate_scale};
      output << "BOX\n";
      for (const auto value : raw_values) {
        const auto text = fixed_decimal_field(value, 15U, 9U);
        if (!text.has_value()) {
          return operation::Result<StructureWriteReport>::failure(invalid(
              "G96 unit-cell value does not fit the fixed F15.9 field"));
        }
        output << *text;
      }
      output << "\nEND\n";
    }
    losses.add("coordinate_precision",
               static_cast<std::uint64_t>(topology.atom_count()) * 3U,
               "G96 coordinates use fixed F15.9 decimal fields");
    losses.add("requested_precision", options.decimal_places == 9U ? 0U : 1U,
               "G96 uses fixed 9-decimal precision regardless of --precision");
    std::uint64_t arbitrary_fields{};
    for (const auto &[name, value] : frame.metadata().fields) {
      static_cast<void>(value);
      if (name != "g96.position_kind" && name != "g96.velocity_kind") {
        ++arbitrary_fields;
      }
    }
    losses.add(
        "frame_metadata", arbitrary_fields,
        "G96 preserves time/cell/velocity but not arbitrary frame fields");
    losses.add(
        "frame_atom_properties",
        static_cast<std::uint64_t>(frame.metadata().atom_properties.size()),
        "G96 does not store per-frame atom property columns");
    if (!output) {
      return operation::Result<StructureWriteReport>::failure(
          io_error("failed while writing G96 output"));
    }
    if (context.report_progress) {
      context.report_progress(operation::ProgressUpdate{
          static_cast<double>(frame_output_index + 1U) /
              static_cast<double>(frames.size()),
          "write-g96"});
    }
  }
  losses.add("connectivity",
             static_cast<std::uint64_t>(topology.bonds().size()),
             "G96 does not store explicit bonds or bond order");
  losses.add("higher_connectivity",
             static_cast<std::uint64_t>(topology.angles().size() +
                                        topology.dihedrals().size() +
                                        topology.impropers().size() +
                                        topology.cmap_terms().size()),
             "G96 does not store angles, dihedrals or impropers");
  const auto had_source_atom_number = std::any_of(
      topology.atoms().begin(), topology.atoms().end(),
      [](const auto &atom) { return atom.source_serial.has_value(); });
  losses.add("atom_identifier",
             !preserve_atom_numbers && had_source_atom_number
                 ? topology.atom_count()
                 : 0U,
             "source atom identifiers outside G96 I7 were renumbered");
  std::uint64_t formal_charges{};
  std::uint64_t extended_identity{};
  for (const auto &atom : topology.atoms()) {
    if (atom.formal_charge != 0)
      ++formal_charges;
    if (!atom.alternate_location.empty())
      ++extended_identity;
  }
  for (const auto &residue : topology.residues()) {
    if (!residue.insertion_code.empty() || !residue.chain_id.empty() ||
        !residue.segment_id.empty()) {
      ++extended_identity;
    }
  }
  losses.add("formal_charge", formal_charges,
             "G96 does not store formal charge");
  losses.add("extended_identity", extended_identity,
             "G96 does not store chain, segment, insertion or "
             "alternate-location identity");
  std::uint64_t other_properties{};
  for (const auto &name : topology.properties().names()) {
    if (name != "g96.element_inferred")
      ++other_properties;
  }
  losses.add("atom_properties", other_properties,
             "G96 does not store arbitrary static atom properties");
  add_unrepresented_chemical_semantics(topology, losses, "G96", true);
  const auto position = output.tellp();
  if (position < std::streampos{}) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("could not determine G96 output size"));
  }
  return operation::Result<StructureWriteReport>::success(StructureWriteReport{
      StructureFormat::g96, topology.atom_count(), frames.size(),
      static_cast<std::uint64_t>(position), losses.take()});
}

operation::Result<StructureWriteReport>
write_gro(std::ostream &output, const model::Topology &topology,
          const model::CoordinateSource &coordinates,
          const StructureWriteOptions &options,
          operation::TaskContext &context) {
  if (options.decimal_places == 0U || options.decimal_places > 15U) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("GRO decimal precision must be between 1 and 15"));
  }
  if (coordinates.atom_count() != topology.atom_count() ||
      topology.atom_count() == 0U || topology.atom_count() > 99999U) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("GRO export requires a matching topology with 1..99999 atoms"));
  }
  const auto width = static_cast<std::size_t>(options.decimal_places) + 5U;
  std::vector<std::int64_t> atom_numbers;
  atom_numbers.reserve(topology.atom_count());
  std::set<std::int64_t> unique_atom_numbers;
  bool preserve_atom_numbers = true;
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    const auto &atom = topology.atoms()[index];
    const auto &residue = topology.residues()[atom.residue.value];
    if (residue.sequence_number < 0 || residue.sequence_number > 99999 ||
        residue.name.empty() || residue.name.size() > 5U || atom.name.empty() ||
        atom.name.size() > 5U) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("GRO residue number must fit I5 and residue/atom names must "
                  "fit five characters",
                  "rename or renumber the topology before GRO export"));
    }
    if (atom.source_serial.has_value() && *atom.source_serial >= 0 &&
        *atom.source_serial <= 99999 &&
        unique_atom_numbers.insert(*atom.source_serial).second) {
      atom_numbers.push_back(*atom.source_serial);
    } else {
      preserve_atom_numbers = false;
    }
  }
  if (!preserve_atom_numbers) {
    atom_numbers.clear();
    for (std::size_t index = 0; index < topology.atom_count(); ++index) {
      atom_numbers.push_back(static_cast<std::int64_t>(index + 1U));
    }
  }
  std::optional<operation::Error> frame_error;
  const auto frames = selected_frames(coordinates, options, frame_error);
  if (frame_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*frame_error);
  }
  LossCollector losses;
  for (std::size_t frame_output_index = 0; frame_output_index < frames.size();
       ++frame_output_index) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<StructureWriteReport>::failure(operation::Error{
          operation::ErrorCode::cancelled, "GRO export cancelled", {}});
    }
    const auto loaded = coordinates.read_frame(frames[frame_output_index]);
    if (!loaded.has_value()) {
      return operation::Result<StructureWriteReport>::failure(loaded.error());
    }
    const auto &frame = *loaded.value();
    std::string title;
    if (!options.comment.empty()) {
      title = sanitized_comment(options.comment);
    } else if (const auto found = frame.metadata().fields.find("gro.title");
               found != frame.metadata().fields.end()) {
      title = sanitized_comment(found->second);
    } else {
      title = sanitized_comment(
          source_value(topology, "gro.title").value_or("MolShredder"));
    }
    const auto timed_title =
        gro_title_with_time(std::move(title), frame.metadata().physical_time);
    if (!timed_title.has_value()) {
      return operation::Result<StructureWriteReport>::failure(
          timed_title.error());
    }
    title = timed_title.value();
    output << title << '\n' << topology.atom_count() << '\n';
    const auto coordinate_scale =
        frame.metadata().coordinate_unit == operation::LengthUnit::angstrom
            ? 0.1
            : 1.0;
    double velocity_scale{};
    if (frame.velocities().has_value()) {
      if (!frame.metadata().velocity_time_unit.has_value()) {
        return operation::Result<StructureWriteReport>::failure(invalid(
            "GRO velocity export requires an explicit velocity time unit"));
      }
      velocity_scale =
          coordinate_scale *
          (*frame.metadata().velocity_time_unit == model::TimeUnit::femtosecond
               ? 1000.0
               : 1.0);
    }
    for (std::size_t index = 0; index < topology.atom_count(); ++index) {
      if (context.cancellation.is_cancelled()) {
        return operation::Result<StructureWriteReport>::failure(
            operation::Error{operation::ErrorCode::cancelled,
                             "GRO export cancelled while writing atoms",
                             {}});
      }
      if (!frame.atom_present(index)) {
        return operation::Result<StructureWriteReport>::failure(invalid(
            "GRO cannot represent missing atom " + std::to_string(index + 1U)));
      }
      const auto &atom = topology.atoms()[index];
      const auto &residue = topology.residues()[atom.residue.value];
      const auto position = position_at(frame, index);
      std::array<std::optional<std::string>, 3> coordinates_text{
          fixed_decimal_field(position.x * coordinate_scale, width,
                              options.decimal_places),
          fixed_decimal_field(position.y * coordinate_scale, width,
                              options.decimal_places),
          fixed_decimal_field(position.z * coordinate_scale, width,
                              options.decimal_places)};
      if (std::any_of(coordinates_text.begin(), coordinates_text.end(),
                      [](const auto &value) { return !value.has_value(); })) {
        return operation::Result<StructureWriteReport>::failure(invalid(
            "GRO coordinate does not fit the requested fixed-width precision",
            "lower --precision or translate/scale coordinates"));
      }
      output << std::right << std::setw(5) << residue.sequence_number
             << std::left << std::setw(5) << residue.name << std::right
             << std::setw(5) << atom.name << std::setw(5) << atom_numbers[index]
             << *coordinates_text[0] << *coordinates_text[1]
             << *coordinates_text[2];
      if (frame.velocities().has_value()) {
        const auto velocity = buffer_value_at(*frame.velocities(), index);
        const auto velocity_precision = options.decimal_places + 1U;
        std::array<std::optional<std::string>, 3> velocity_text{
            fixed_decimal_field(velocity.x * velocity_scale, width,
                                velocity_precision),
            fixed_decimal_field(velocity.y * velocity_scale, width,
                                velocity_precision),
            fixed_decimal_field(velocity.z * velocity_scale, width,
                                velocity_precision)};
        if (std::any_of(velocity_text.begin(), velocity_text.end(),
                        [](const auto &value) { return !value.has_value(); })) {
          return operation::Result<StructureWriteReport>::failure(invalid(
              "GRO velocity does not fit the requested fixed-width precision",
              "lower --precision or convert the velocity scale"));
        }
        output << *velocity_text[0] << *velocity_text[1] << *velocity_text[2];
      }
      output << '\n';
    }
    output << std::fixed
           << std::setprecision(static_cast<int>(options.decimal_places));
    if (!frame.metadata().unit_cell.has_value()) {
      output << 0.0 << ' ' << 0.0 << ' ' << 0.0 << '\n';
    } else {
      const auto &cell = *frame.metadata().unit_cell;
      const auto scale = coordinate_scale;
      output << cell.a.x * scale << ' ' << cell.b.y * scale << ' '
             << cell.c.z * scale << ' ' << cell.a.y * scale << ' '
             << cell.a.z * scale << ' ' << cell.b.x * scale << ' '
             << cell.b.z * scale << ' ' << cell.c.x * scale << ' '
             << cell.c.y * scale << '\n';
    }
    losses.add(
        "coordinate_precision",
        static_cast<std::uint64_t>(topology.atom_count()) * 3U,
        "GRO coordinates are decimal text rounded to requested precision");
    std::uint64_t arbitrary_fields{};
    for (const auto &[name, value] : frame.metadata().fields) {
      static_cast<void>(value);
      if (name != "gro.title")
        ++arbitrary_fields;
    }
    losses.add("frame_metadata", arbitrary_fields,
               "GRO preserves title/time/cell/velocity but not arbitrary frame "
               "fields");
    losses.add(
        "frame_atom_properties",
        static_cast<std::uint64_t>(frame.metadata().atom_properties.size()),
        "GRO does not store per-frame atom property columns");
    if (!output) {
      return operation::Result<StructureWriteReport>::failure(
          io_error("failed while writing GRO output"));
    }
    if (context.report_progress) {
      context.report_progress(operation::ProgressUpdate{
          static_cast<double>(frame_output_index + 1U) /
              static_cast<double>(frames.size()),
          "write-gro"});
    }
  }
  losses.add("connectivity",
             static_cast<std::uint64_t>(topology.bonds().size()),
             "GRO does not store explicit bonds or bond order");
  losses.add("higher_connectivity",
             static_cast<std::uint64_t>(topology.angles().size() +
                                        topology.dihedrals().size() +
                                        topology.impropers().size() +
                                        topology.cmap_terms().size()),
             "GRO does not store angles, dihedrals or impropers");
  const auto had_source_atom_number = std::any_of(
      topology.atoms().begin(), topology.atoms().end(),
      [](const auto &atom) { return atom.source_serial.has_value(); });
  losses.add("atom_identifier",
             !preserve_atom_numbers && had_source_atom_number
                 ? topology.atom_count()
                 : 0U,
             "source atom identifiers outside GRO I5 were renumbered");
  std::uint64_t formal_charges{};
  std::uint64_t extended_identity{};
  for (const auto &atom : topology.atoms()) {
    if (atom.formal_charge != 0)
      ++formal_charges;
    if (!atom.alternate_location.empty())
      ++extended_identity;
  }
  for (const auto &residue : topology.residues()) {
    if (!residue.insertion_code.empty() || !residue.chain_id.empty() ||
        !residue.segment_id.empty()) {
      ++extended_identity;
    }
  }
  losses.add("formal_charge", formal_charges,
             "GRO does not store formal charge");
  losses.add("extended_identity", extended_identity,
             "GRO does not store chain, segment, insertion or "
             "alternate-location identity");
  std::uint64_t other_properties{};
  for (const auto &name : topology.properties().names()) {
    if (name != "gro.element_inferred")
      ++other_properties;
  }
  losses.add("atom_properties", other_properties,
             "GRO does not store arbitrary static atom properties");
  add_unrepresented_chemical_semantics(topology, losses, "GRO", true);
  const auto position = output.tellp();
  if (position < std::streampos{}) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("could not determine GRO output size"));
  }
  return operation::Result<StructureWriteReport>::success(StructureWriteReport{
      StructureFormat::gro, topology.atom_count(), frames.size(),
      static_cast<std::uint64_t>(position), losses.take()});
}

operation::Result<StructureWriteReport>
write_mol2(std::ostream &output, const model::Topology &topology,
           const model::CoordinateSource &coordinates,
           const StructureWriteOptions &options,
           operation::TaskContext &context) {
  if (options.decimal_places > 15U) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("MOL2 decimal precision must be between 0 and 15"));
  }
  if (coordinates.atom_count() != topology.atom_count() ||
      topology.atom_count() == 0U) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("MOL2 export requires a non-empty matching topology/coordinate "
                "source"));
  }
  std::optional<operation::Error> frame_error;
  const auto frames = selected_frames(coordinates, options, frame_error);
  if (frame_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*frame_error);
  }
  if (frames.size() != 1U) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        "MOL2 export requires exactly one selected frame",
        "use --frames current or export separate molecule records"});
  }
  if (context.cancellation.is_cancelled()) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::cancelled, "MOL2 export cancelled", {}});
  }
  const auto frame_result = coordinates.read_frame(frames.front());
  if (!frame_result.has_value()) {
    return operation::Result<StructureWriteReport>::failure(
        frame_result.error());
  }
  const auto &frame = *frame_result.value();
  std::optional<operation::Error> property_error;
  const auto *atom_types =
      text_property(topology, "mol2.atom_type", property_error);
  const auto *substructure_names =
      text_property(topology, "mol2.substructure_name", property_error);
  const auto *status_bits =
      text_property(topology, "mol2.status_bits", property_error);
  const auto *substructure_ids =
      integer_property(topology, "mol2.substructure_id", property_error);
  if (property_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*property_error);
  }
  if (atom_types == nullptr) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::unsupported,
        "MOL2 export requires explicit mol2.atom_type for every atom",
        "load a MOL2 source or run a future versioned SYBYL atom-typing "
        "operation"});
  }
  const auto charges = optional_numeric_property(topology, "partial_charge");
  if (!charges.has_value()) {
    return operation::Result<StructureWriteReport>::failure(charges.error());
  }
  const auto *charge_presence_property =
      topology.properties().find("partial_charge_present");
  const model::BooleanColumn *charge_presence{};
  if (charge_presence_property != nullptr) {
    charge_presence =
        std::get_if<model::BooleanColumn>(&charge_presence_property->values);
    if (charge_presence == nullptr) {
      return operation::Result<StructureWriteReport>::failure(invalid(
          "partial_charge_present must be a boolean property for MOL2 export"));
    }
  }
  const auto has_charges =
      !charges.value().empty() &&
      (charge_presence == nullptr ||
       std::any_of(charge_presence->values.begin(),
                   charge_presence->values.end(),
                   [](std::uint8_t value) { return value != 0U; }));
  if (has_charges && charge_presence != nullptr &&
      std::any_of(charge_presence->values.begin(),
                  charge_presence->values.end(),
                  [](std::uint8_t value) { return value == 0U; })) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("MOL2 cannot export a partially missing partial-charge column",
                "fill all charges or remove the property and use NO_CHARGES"));
  }
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    if (!frame.atom_present(index)) {
      return operation::Result<StructureWriteReport>::failure(invalid(
          "MOL2 cannot represent missing atom " + std::to_string(index + 1U)));
    }
    if ((*atom_types)[index].empty() ||
        (*atom_types)[index].find_first_of(" \t\r\n") != std::string::npos) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("MOL2 atom type must be a non-empty token at atom " +
                  std::to_string(index + 1U)));
    }
  }
  for (const auto &bond : topology.bonds()) {
    if (mol2_bond_type(bond.order).empty()) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("MOL2 cannot export a bond with unknown kind/order"));
    }
    if (bond.stereo != model::BondStereo::none) {
      return operation::Result<StructureWriteReport>::failure(invalid(
          "MOL2 writer cannot represent core bond stereo without loss"));
    }
  }

  std::vector<std::int64_t> output_atom_ids;
  output_atom_ids.reserve(topology.atom_count());
  std::set<std::int64_t> unique_atom_ids;
  bool preserve_atom_ids = true;
  for (const auto &atom : topology.atoms()) {
    if (!atom.source_serial.has_value() || *atom.source_serial <= 0 ||
        !unique_atom_ids.insert(*atom.source_serial).second) {
      preserve_atom_ids = false;
      break;
    }
    output_atom_ids.push_back(*atom.source_serial);
  }
  if (!preserve_atom_ids) {
    output_atom_ids.clear();
    for (std::size_t index = 0; index < topology.atom_count(); ++index) {
      output_atom_ids.push_back(static_cast<std::int64_t>(index + 1U));
    }
  }

  std::vector<std::int64_t> output_bond_ids;
  output_bond_ids.reserve(topology.bonds().size());
  std::set<std::int64_t> unique_bond_ids;
  bool preserve_bond_ids = true;
  for (std::size_t index = 0; index < topology.bonds().size(); ++index) {
    const auto source_id =
        source_value(topology, "mol2.bond_source_id." + std::to_string(index));
    std::int64_t parsed{};
    if (!source_id.has_value()) {
      preserve_bond_ids = false;
      break;
    }
    const auto [end, error] = molshredder::core::from_chars(
        source_id->data(), source_id->data() + source_id->size(), parsed);
    if (error != std::errc{} || end != source_id->data() + source_id->size() ||
        parsed <= 0 || !unique_bond_ids.insert(parsed).second) {
      preserve_bond_ids = false;
      break;
    }
    output_bond_ids.push_back(parsed);
  }
  if (!preserve_bond_ids) {
    output_bond_ids.clear();
    for (std::size_t index = 0; index < topology.bonds().size(); ++index) {
      output_bond_ids.push_back(static_cast<std::int64_t>(index + 1U));
    }
  }

  struct NotConnectedRecord {
    std::int64_t id{};
    std::int64_t first{};
    std::int64_t second{};
    std::string status;
  };
  std::vector<NotConnectedRecord> not_connected;
  if (const auto count_text =
          source_value(topology, "mol2.not_connected_count");
      count_text.has_value()) {
    std::size_t count{};
    const auto [end, error] = molshredder::core::from_chars(
        count_text->data(), count_text->data() + count_text->size(), count);
    if (error != std::errc{} || end != count_text->data() + count_text->size()) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("MOL2 retained not-connected count is invalid"));
    }
    std::set<std::int64_t> atom_ids(output_atom_ids.begin(),
                                    output_atom_ids.end());
    std::set<std::int64_t> bond_ids(output_bond_ids.begin(),
                                    output_bond_ids.end());
    not_connected.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      const auto prefix =
          "mol2.not_connected." + std::to_string(index) + ".";
      NotConnectedRecord record;
      for (const auto &[field, target] :
           {std::pair<std::string_view, std::int64_t *>{"id", &record.id},
            {"first", &record.first}, {"second", &record.second}}) {
        const auto value = source_value(topology, prefix + std::string{field});
        if (!value.has_value()) {
          return operation::Result<StructureWriteReport>::failure(invalid(
              "MOL2 retained not-connected record is missing " +
              std::string{field}));
        }
        const auto [value_end, value_error] = molshredder::core::from_chars(
            value->data(), value->data() + value->size(), *target);
        if (value_error != std::errc{} ||
            value_end != value->data() + value->size() || *target <= 0) {
          return operation::Result<StructureWriteReport>::failure(invalid(
              "MOL2 retained not-connected " + std::string{field} +
              " is invalid"));
        }
      }
      if (!bond_ids.insert(record.id).second ||
          !atom_ids.contains(record.first) ||
          !atom_ids.contains(record.second) || record.first == record.second) {
        return operation::Result<StructureWriteReport>::failure(invalid(
            "MOL2 retained not-connected record has duplicate or stale IDs"));
      }
      record.status =
          source_value(topology, prefix + "status").value_or("");
      if (record.status.find_first_of(" \t\r\n") != std::string::npos) {
        return operation::Result<StructureWriteReport>::failure(
            invalid("MOL2 retained not-connected status must be one token"));
      }
      not_connected.push_back(std::move(record));
    }
  }

  struct OutputSubstructure {
    std::int64_t id{};
    std::string name;
    std::int64_t root{};
    std::string chain;
  };
  std::map<std::int64_t, OutputSubstructure> output_substructures;
  std::vector<std::int64_t> atom_substructure_ids;
  std::vector<std::string> atom_substructure_names;
  atom_substructure_ids.reserve(topology.atom_count());
  atom_substructure_names.reserve(topology.atom_count());
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    const auto &atom = topology.atoms()[index];
    const auto &residue = topology.residues()[atom.residue.value];
    const auto id = substructure_ids == nullptr
                        ? static_cast<std::int64_t>(atom.residue.value + 1U)
                        : (*substructure_ids)[index];
    const auto name = substructure_names == nullptr
                          ? residue.name
                          : (*substructure_names)[index];
    if (id <= 0 || name.empty() ||
        name.find_first_of(" \t\r\n") != std::string::npos) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("MOL2 substructure id/name is invalid at atom " +
                  std::to_string(index + 1U)));
    }
    const auto found = output_substructures.find(id);
    if (found == output_substructures.end()) {
      output_substructures.emplace(
          id, OutputSubstructure{id, name, output_atom_ids[index],
                                 residue.chain_id});
    } else if (found->second.name != name) {
      return operation::Result<StructureWriteReport>::failure(invalid(
          "MOL2 atoms assign conflicting names to one substructure id"));
    }
    atom_substructure_ids.push_back(id);
    atom_substructure_names.push_back(name);
  }

  output.imbue(std::locale::classic());
  output << "@<TRIPOS>MOLECULE\n";
  output << sanitized_comment(
                source_value(topology, "mol2.name").value_or("MolShredder"))
         << '\n';
  output << topology.atom_count() << ' '
         << topology.bonds().size() + not_connected.size() << ' '
         << output_substructures.size() << " 0 0\n";
  output << source_value(topology, "mol2.molecule_type").value_or("SMALL")
         << '\n';
  output << (has_charges ? source_value(topology, "mol2.charge_type")
                               .value_or("USER_CHARGES")
                         : std::string{"NO_CHARGES"})
         << '\n';
  if (const auto extra = source_value(topology, "mol2.molecule_extra");
      extra.has_value()) {
    output << *extra << '\n';
  }
  output << "@<TRIPOS>ATOM\n"
         << std::fixed
         << std::setprecision(static_cast<int>(options.decimal_places));
  const auto scale =
      frame.metadata().coordinate_unit == operation::LengthUnit::nanometer
          ? 10.0
          : 1.0;
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<StructureWriteReport>::failure(
          operation::Error{operation::ErrorCode::cancelled,
                           "MOL2 export cancelled while writing atoms",
                           {}});
    }
    const auto position = position_at(frame, index);
    output << output_atom_ids[index] << ' ' << topology.atoms()[index].name
           << ' ' << position.x * scale << ' ' << position.y * scale << ' '
           << position.z * scale << ' ' << (*atom_types)[index] << ' '
           << atom_substructure_ids[index] << ' '
           << atom_substructure_names[index];
    const auto status =
        status_bits == nullptr ? std::string_view{} : (*status_bits)[index];
    if (has_charges || !status.empty()) {
      output << ' ' << (has_charges ? charges.value()[index] : 0.0);
    }
    if (!status.empty()) {
      if (status.find_first_of(" \t\r\n") != std::string_view::npos) {
        return operation::Result<StructureWriteReport>::failure(
            invalid("MOL2 atom status must be a single token"));
      }
      output << ' ' << status;
    }
    output << '\n';
  }
  output << "@<TRIPOS>BOND\n";
  for (std::size_t index = 0; index < topology.bonds().size(); ++index) {
    if (context.cancellation.is_cancelled()) {
      return operation::Result<StructureWriteReport>::failure(
          operation::Error{operation::ErrorCode::cancelled,
                           "MOL2 export cancelled while writing bonds",
                           {}});
    }
    const auto &bond = topology.bonds()[index];
    auto first_id = output_atom_ids[bond.first.value];
    auto second_id = output_atom_ids[bond.second.value];
    const auto source_first = source_value(topology, "mol2.bond_source_first." +
                                                         std::to_string(index));
    const auto source_second = source_value(
        topology, "mol2.bond_source_second." + std::to_string(index));
    if (source_first.has_value() && source_second.has_value()) {
      std::int64_t parsed_first{};
      std::int64_t parsed_second{};
      const auto [first_end, first_error] = molshredder::core::from_chars(
          source_first->data(), source_first->data() + source_first->size(),
          parsed_first);
      const auto [second_end, second_error] = molshredder::core::from_chars(
          source_second->data(), source_second->data() + source_second->size(),
          parsed_second);
      const auto endpoints_match =
          (parsed_first == first_id && parsed_second == second_id) ||
          (parsed_first == second_id && parsed_second == first_id);
      if (first_error == std::errc{} && second_error == std::errc{} &&
          first_end == source_first->data() + source_first->size() &&
          second_end == source_second->data() + source_second->size() &&
          endpoints_match) {
        first_id = parsed_first;
        second_id = parsed_second;
      }
    }
    output << output_bond_ids[index] << ' ' << first_id << ' ' << second_id
           << ' ' << mol2_bond_type(bond.order);
    if (const auto status =
            source_value(topology, "mol2.bond_status." + std::to_string(index));
        status.has_value()) {
      if (status->find_first_of(" \t\r\n") != std::string::npos) {
        return operation::Result<StructureWriteReport>::failure(
            invalid("MOL2 bond status must be a single token"));
      }
      output << ' ' << *status;
    }
    output << '\n';
  }
  for (const auto &record : not_connected) {
    output << record.id << ' ' << record.first << ' ' << record.second
           << " nc";
    if (!record.status.empty())
      output << ' ' << record.status;
    output << '\n';
  }
  output << "@<TRIPOS>SUBSTRUCTURE\n";
  for (const auto &[id, substructure] : output_substructures) {
    static_cast<void>(id);
    output << substructure.id << ' ' << substructure.name << ' '
           << substructure.root << " GROUP 0 "
           << (substructure.chain.empty() ? "****" : substructure.chain)
           << " **** 0 ROOT\n";
  }
  if (frame.metadata().unit_cell.has_value()) {
    const auto &cell = *frame.metadata().unit_cell;
    output << "@<TRIPOS>CRYSIN\n"
           << vector_length(cell.a) << ' ' << vector_length(cell.b) << ' '
           << vector_length(cell.c) << ' ' << cell_angle(cell.b, cell.c) << ' '
           << cell_angle(cell.a, cell.c) << ' ' << cell_angle(cell.a, cell.b)
           << ' '
           << source_value(topology, "mol2.crystal_space_group").value_or("1")
           << ' '
           << source_value(topology, "mol2.crystal_setting").value_or("1")
           << '\n';
  }
  if (!output) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("failed while writing MOL2 output"));
  }
  if (context.report_progress) {
    context.report_progress(operation::ProgressUpdate{1.0, "write-mol2"});
  }

  LossCollector losses;
  add_residue_semantics_loss(topology, losses, "MOL2");
  std::uint64_t formal_charges{};
  std::uint64_t alternate_locations{};
  std::uint64_t isotope_losses{};
  std::uint64_t radical_losses{};
  std::uint64_t atom_stereo_losses{};
  for (const auto &atom : topology.atoms()) {
    if (atom.formal_charge_present || atom.formal_charge != 0)
      ++formal_charges;
    if (!atom.alternate_location.empty())
      ++alternate_locations;
    isotope_losses += atom.isotope_mass_number.has_value() ? 1U : 0U;
    radical_losses += atom.radical != model::RadicalState::none ? 1U : 0U;
    atom_stereo_losses +=
        atom.stereo_parity != model::AtomStereoParity::unspecified ? 1U : 0U;
  }
  losses.add(
      "formal_charge", formal_charges,
      "MOL2 writer currently preserves partial rather than formal charge");
  losses.add("alternate_location", alternate_locations,
             "MOL2 does not preserve alternate-location identity");
  losses.add("isotope", isotope_losses,
             "MOL2 writer does not preserve isotope mass number");
  losses.add("radical", radical_losses,
             "MOL2 writer does not preserve radical state");
  losses.add("atom_stereo", atom_stereo_losses,
             "MOL2 writer does not preserve atom stereo parity");
  losses.add("higher_connectivity",
             static_cast<std::uint64_t>(topology.angles().size() +
                                        topology.dihedrals().size() +
                                        topology.impropers().size() +
                                        topology.cmap_terms().size()),
             "MOL2 writer emits atom/bond/substructure sections only");
  std::uint64_t other_properties{};
  for (const auto &name : topology.properties().names()) {
    if (name != "mol2.atom_type" && name != "mol2.substructure_id" &&
        name != "mol2.substructure_name" && name != "mol2.status_bits" &&
        name != "partial_charge" && name != "partial_charge_present") {
      ++other_properties;
    }
  }
  losses.add("atom_properties", other_properties,
             "MOL2 writer emits only implemented atom/type/charge fields");
  std::uint64_t omitted_sections{};
  for (const auto &[name, value] : topology.source_metadata()) {
    static_cast<void>(value);
    if (name.starts_with("mol2.section."))
      ++omitted_sections;
  }
  losses.add("optional_sections", omitted_sections,
             "MOL2 optional FEATURE/SET/UNITY sections are not emitted");
  losses.add("substructure_metadata",
             source_value(topology, "mol2.substructure_lines").has_value()
                 ? static_cast<std::uint64_t>(output_substructures.size())
                 : 0U,
             "MOL2 SUBSTRUCTURE rows are normalized from current residues");
  std::uint64_t atom_id_normalization{};
  if (!preserve_atom_ids &&
      std::any_of(
          topology.atoms().begin(), topology.atoms().end(),
          [](const auto &atom) { return atom.source_serial.has_value(); })) {
    atom_id_normalization = topology.atom_count();
  }
  losses.add("atom_identifier", atom_id_normalization,
             "invalid, duplicate or incomplete source atom identifiers were "
             "normalized");
  std::uint64_t bond_id_normalization{};
  if (!preserve_bond_ids) {
    for (std::size_t index = 0; index < topology.bonds().size(); ++index) {
      if (source_value(topology, "mol2.bond_source_id." + std::to_string(index))
              .has_value()) {
        bond_id_normalization = topology.bonds().size();
        break;
      }
    }
  }
  losses.add("bond_identifier", bond_id_normalization,
             "invalid, duplicate or incomplete source bond identifiers were "
             "normalized");
  losses.add("velocity", frame.velocities().has_value() ? 1U : 0U,
             "MOL2 does not store velocity vectors");
  losses.add("frame_metadata",
             static_cast<std::uint64_t>(frame.metadata().fields.size()),
             "MOL2 does not preserve arbitrary frame metadata");
  const auto position = output.tellp();
  if (position < std::streampos{}) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("could not determine MOL2 output size"));
  }
  return operation::Result<StructureWriteReport>::success(StructureWriteReport{
      StructureFormat::mol2, topology.atom_count(), 1U,
      static_cast<std::uint64_t>(position), losses.take()});
}

operation::Result<StructureWriteReport>
write_psf(std::ostream &output, const model::Topology &topology,
          const model::CoordinateSource &coordinates,
          const StructureWriteOptions &options,
          operation::TaskContext &context) {
  if (coordinates.atom_count() != topology.atom_count() ||
      topology.atom_count() == 0U) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("PSF export requires a non-empty topology and matching "
                "coordinate source"));
  }
  if (context.cancellation.is_cancelled()) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::cancelled, "PSF export cancelled", {}});
  }
  std::optional<operation::Error> property_error;
  const auto *atom_types =
      text_property(topology, "psf.atom_type", property_error);
  if (property_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*property_error);
  }
  const auto *charges = topology.properties().find("partial_charge");
  const auto *masses = topology.properties().find("mass");
  const auto *unused = integer_property(topology, "psf.unused", property_error);
  if (property_error.has_value()) {
    return operation::Result<StructureWriteReport>::failure(*property_error);
  }
  if (atom_types == nullptr || charges == nullptr || masses == nullptr) {
    return operation::Result<StructureWriteReport>::failure(
        operation::Error{operation::ErrorCode::unsupported,
                         "PSF export requires psf.atom_type, partial_charge "
                         "and mass for every atom",
                         "load a PSF source or run explicit force-field atom "
                         "typing before export"});
  }
  const auto numeric = [](const model::AtomProperty *property) {
    return property != nullptr &&
           (std::holds_alternative<std::vector<double>>(property->values) ||
            std::holds_alternative<std::vector<float>>(property->values));
  };
  if (!numeric(charges) || !numeric(masses)) {
    return operation::Result<StructureWriteReport>::failure(invalid(
        "PSF partial_charge and mass must be float32 or float64 properties"));
  }
  const auto charge_unit = charges->metadata.unit.value_or("elementary_charge");
  const auto mass_unit = masses->metadata.unit.value_or("dalton");
  if ((charge_unit != "elementary_charge" && charge_unit != "e") ||
      (mass_unit != "dalton" && mass_unit != "Da")) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("PSF partial_charge must use elementary_charge/e and mass must "
                "use dalton/Da"));
  }
  for (std::size_t index = 0U; index < topology.atom_count(); ++index) {
    const auto &atom = topology.atoms()[index];
    if (atom.residue.value >= topology.residue_count()) {
      return operation::Result<StructureWriteReport>::failure(
          invalid("PSF atom references an invalid residue"));
    }
    const auto &residue = topology.residues()[atom.residue.value];
    const auto residue_id =
        std::to_string(residue.sequence_number) + residue.insertion_code;
    const auto valid_field = [](std::string_view field, std::size_t maximum) {
      return token_safe(field) && field.size() <= maximum;
    };
    if (!valid_field(residue.segment_id, 8U) || !valid_field(residue_id, 8U) ||
        !valid_field(residue.name, 8U) || !valid_field(atom.name, 8U) ||
        !valid_field((*atom_types)[index], 6U)) {
      return operation::Result<StructureWriteReport>::failure(operation::Error{
          operation::ErrorCode::unsupported,
          "PSF EXT requires non-empty whitespace-free segment/residue/atom "
          "fields up to 8 characters and atom types up to 6 characters",
          "shorten the identity fields or use a future NAMD free-field "
          "writer"});
    }
    const auto charge = numeric_value(*charges, index);
    const auto mass = numeric_value(*masses, index);
    if (!std::isfinite(charge) || !std::isfinite(mass) || mass < 0.0) {
      return operation::Result<StructureWriteReport>::failure(invalid(
          "PSF charge and mass values must be finite and mass non-negative"));
    }
  }

  auto title = sanitized_comment(options.comment);
  if (title.empty()) {
    title = source_value(topology, "psf.title")
                .value_or("MolShredder-generated X-PLOR PSF topology");
  }
  output.imbue(std::locale::classic());
  output << "PSF EXT XPLOR";
  if (!topology.cmap_terms().empty())
    output << " CMAP";
  output << "\n\n"
         << std::setw(10) << 1 << " !NTITLE\n REMARKS " << title << "\n\n"
         << std::setw(10) << topology.atom_count() << " !NATOM\n";
  LossCollector losses;
  std::uint64_t normalized_serials{};
  for (std::size_t index = 0U; index < topology.atom_count(); ++index) {
    const auto &atom = topology.atoms()[index];
    const auto &residue = topology.residues()[atom.residue.value];
    const auto residue_id =
        std::to_string(residue.sequence_number) + residue.insertion_code;
    if (!atom.source_serial.has_value() ||
        atom.source_serial.value() != static_cast<std::int64_t>(index + 1U)) {
      ++normalized_serials;
    }
    output << std::right << std::setw(10) << index + 1U << ' ' << std::left
           << std::setw(8) << residue.segment_id << ' ' << std::setw(8)
           << residue_id << ' ' << std::setw(8) << residue.name << ' '
           << std::setw(8) << atom.name << ' ' << std::setw(6)
           << (*atom_types)[index] << std::right << std::setprecision(6)
           << std::defaultfloat << std::setw(15)
           << numeric_value(*charges, index) << std::setw(14)
           << numeric_value(*masses, index) << std::setw(8)
           << (unused == nullptr ? 0 : (*unused)[index]) << '\n';
  }
  output << '\n';

  const auto write_header = [&](std::size_t count, std::string_view name) {
    output << std::right << std::setw(10) << count << " !" << name << '\n';
  };
  const auto write_values = [&](const std::vector<std::size_t> &values,
                                std::size_t values_per_line) {
    for (std::size_t index = 0U; index < values.size(); ++index) {
      output << std::right << std::setw(10) << values[index] + 1U;
      if ((index + 1U) % values_per_line == 0U)
        output << '\n';
    }
    if (!values.empty() && values.size() % values_per_line != 0U)
      output << '\n';
    output << '\n';
  };
  std::vector<std::size_t> values;
  values.reserve(topology.bonds().size() * 2U);
  for (const auto &bond : topology.bonds()) {
    values.push_back(bond.first.value);
    values.push_back(bond.second.value);
  }
  write_header(topology.bonds().size(), "NBOND: bonds");
  write_values(values, 8U);
  values.clear();
  for (const auto &angle : topology.angles()) {
    values.insert(values.end(),
                  {angle.first.value, angle.center.value, angle.third.value});
  }
  write_header(topology.angles().size(), "NTHETA: angles");
  write_values(values, 9U);
  values.clear();
  for (const auto &dihedral : topology.dihedrals()) {
    values.insert(values.end(), {dihedral.first.value, dihedral.second.value,
                                 dihedral.third.value, dihedral.fourth.value});
  }
  write_header(topology.dihedrals().size(), "NPHI: dihedrals");
  write_values(values, 8U);
  values.clear();
  for (const auto &improper : topology.impropers()) {
    values.insert(values.end(), {improper.center.value, improper.first.value,
                                 improper.second.value, improper.third.value});
  }
  write_header(topology.impropers().size(), "NIMPHI: impropers");
  write_values(values, 8U);
  write_header(0U, "NDON: donors");
  output << '\n';
  write_header(0U, "NACC: acceptors");
  output << '\n';
  write_header(0U, "NNB");
  for (std::size_t index = 0U; index < topology.atom_count(); ++index) {
    output << std::setw(10) << 0;
    if ((index + 1U) % 8U == 0U)
      output << '\n';
  }
  if (topology.atom_count() % 8U != 0U)
    output << '\n';
  output << '\n'
         << std::setw(8) << 1 << ' ' << std::setw(7) << 0 << " !NGRP\n"
         << std::setw(10) << 0 << std::setw(10) << 0 << std::setw(10) << 0
         << "\n\n";
  values.clear();
  for (const auto &term : topology.cmap_terms()) {
    for (const auto atom : term.atoms)
      values.push_back(atom.value);
  }
  write_header(topology.cmap_terms().size(), "NCRTERM: cross-terms");
  write_values(values, 8U);
  if (!output) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("failed while writing PSF output"));
  }
  losses.add("atom_serial", normalized_serials,
             "PSF export normalized atom identifiers to coordinate order");
  std::uint64_t bond_orders{};
  for (const auto &bond : topology.bonds()) {
    if (bond.order != model::BondOrder::unknown)
      ++bond_orders;
  }
  losses.add("bond_order", bond_orders, "PSF does not store bond order");
  std::uint64_t extended_identity{};
  std::uint64_t formal_charges{};
  for (const auto &atom : topology.atoms()) {
    const auto &residue = topology.residues()[atom.residue.value];
    if (!residue.chain_id.empty() || !atom.alternate_location.empty()) {
      ++extended_identity;
    }
    if (atom.formal_charge != 0)
      ++formal_charges;
  }
  losses.add("extended_identity", extended_identity,
             "PSF does not preserve chain or alternate-location identity");
  losses.add("formal_charge", formal_charges,
             "PSF stores partial charge but not formal charge");
  const auto frame_count = coordinates.frame_count().value_or(0U);
  losses.add("coordinates", static_cast<std::uint64_t>(frame_count),
             "PSF is topology-only and does not store coordinate frames");
  std::uint64_t other_properties{};
  for (const auto &name : topology.properties().names()) {
    if (name != "psf.atom_type" && name != "partial_charge" && name != "mass" &&
        name != "psf.unused" && name != "psf.element_inferred") {
      ++other_properties;
    }
  }
  losses.add("atom_properties", other_properties,
             "PSF preserves atom type, charge, mass and unused field only");
  add_unrepresented_chemical_semantics(topology, losses, "PSF", true);
  std::uint64_t unmodeled_sections{};
  for (const auto &[name, unused_value] : topology.source_metadata()) {
    static_cast<void>(unused_value);
    if (name.starts_with("psf.unmodeled."))
      ++unmodeled_sections;
  }
  losses.add(
      "psf_auxiliary_sections", unmodeled_sections,
      "PSF auxiliary donor/acceptor/exclusion/group data is not re-emitted");
  if (options.decimal_places != 6U) {
    losses.add("requested_precision", 1U,
               "PSF EXT uses fixed six-significant-digit numeric fields");
  }
  if (context.report_progress) {
    context.report_progress({1.0, "write-psf"});
  }
  const auto position = output.tellp();
  if (position < std::streampos{}) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("could not determine PSF output size"));
  }
  return operation::Result<StructureWriteReport>::success(StructureWriteReport{
      StructureFormat::psf, topology.atom_count(), 0U,
      static_cast<std::uint64_t>(position), losses.take()});
}

operation::Result<StructureWriteReport>
write_to_stream(std::ostream &output, const model::Topology &topology,
                const model::CoordinateSource &coordinates,
                StructureWriteOptions options,
                operation::TaskContext &context) {
  if (options.format == StructureFormat::pdb) {
    return write_pdb(output, topology, coordinates, options, context);
  }
  if (options.format == StructureFormat::mmcif) {
    return detail::write_mmcif(output, topology, coordinates, options, context);
  }
  if (options.format == StructureFormat::pqr) {
    return write_pqr(output, topology, coordinates, options, context);
  }
  if (options.format == StructureFormat::mol ||
      options.format == StructureFormat::sdf) {
    return write_molfile(output, topology, coordinates, options, context);
  }
  if (options.format == StructureFormat::mol2) {
    return write_mol2(output, topology, coordinates, options, context);
  }
  if (options.format == StructureFormat::psf) {
    return write_psf(output, topology, coordinates, options, context);
  }
  if (options.format == StructureFormat::gro) {
    return write_gro(output, topology, coordinates, options, context);
  }
  if (options.format == StructureFormat::g96) {
    return write_g96(output, topology, coordinates, options, context);
  }
  if (options.format != StructureFormat::xyz) {
    return operation::Result<StructureWriteReport>::failure(
        operation::Error{operation::ErrorCode::unsupported,
                         "structure writer does not support format: " +
                             std::string{to_string(options.format)},
                         "use --file-format pdb, mmcif, g96, gro, mol, mol2, "
                         "psf, sdf, pqr or xyz"});
  }
  return write_xyz(output, topology, coordinates, options, context);
}

std::filesystem::path temporary_path(const std::filesystem::path &target) {
  static std::atomic_uint64_t counter{};
  for (std::size_t attempt = 0; attempt < 1024U; ++attempt) {
    auto candidate = target.parent_path() /
                     (target.filename().string() + ".molshredder.tmp." +
                      std::to_string(counter.fetch_add(1U)));
    std::error_code error;
    if (!std::filesystem::exists(candidate, error) && !error)
      return candidate;
  }
  return {};
}

bool replace_file(const std::filesystem::path &source,
                  const std::filesystem::path &target, bool overwrite) {
#ifdef _WIN32
  const auto flags = overwrite
                         ? MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
                         : MOVEFILE_WRITE_THROUGH;
  return MoveFileExW(source.c_str(), target.c_str(), flags) != 0;
#else
  if (overwrite)
    return std::rename(source.c_str(), target.c_str()) == 0;
  std::error_code error;
  std::filesystem::create_hard_link(source, target, error);
  if (error)
    return false;
  std::filesystem::remove(source, error);
  return true;
#endif
}

} // namespace

operation::Result<SerializedStructure> serialize_structure(
    const model::Topology &topology, const model::CoordinateSource &coordinates,
    StructureWriteOptions options, operation::TaskContext &context) {
  options.format = resolve_format(options.format);
  std::ostringstream output;
  const auto report = write_to_stream(output, topology, coordinates,
                                      std::move(options), context);
  if (!report.has_value()) {
    return operation::Result<SerializedStructure>::failure(report.error());
  }
  return operation::Result<SerializedStructure>::success(
      SerializedStructure{std::move(output).str(), report.value()});
}

operation::Result<StructureWriteReport> write_structure_file(
    const std::filesystem::path &path, const model::Topology &topology,
    const model::CoordinateSource &coordinates, StructureWriteOptions options,
    bool overwrite, operation::TaskContext &context) {
  options.format = resolve_format(options.format, path);
  if (options.format == StructureFormat::auto_detect) {
    return operation::Result<StructureWriteReport>::failure(invalid(
        "could not infer structure output format from path: " + path.string(),
        "use a .cif/.mmcif/.g96/.gro/.mol/.psf/.sdf/.pqr/.xyz/.xmol suffix "
        "or an "
        "explicit "
        "--file-format"));
  }
  if (path.empty() || path.filename().empty()) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("structure output path must name a file"));
  }
  std::error_code filesystem_error;
  if (!std::filesystem::exists(path.parent_path().empty()
                                   ? std::filesystem::path{"."}
                                   : path.parent_path(),
                               filesystem_error) ||
      filesystem_error) {
    return operation::Result<StructureWriteReport>::failure(
        operation::Error{operation::ErrorCode::not_found,
                         "structure output directory does not exist: " +
                             path.parent_path().string(),
                         "create the directory before exporting"});
  }
  if (!overwrite && std::filesystem::exists(path, filesystem_error)) {
    return operation::Result<StructureWriteReport>::failure(
        invalid("structure output already exists: " + path.string(),
                "choose another path or pass --overwrite true"));
  }
  if (filesystem_error) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("could not inspect structure output path: " + path.string()));
  }
  const auto temporary = temporary_path(path);
  if (temporary.empty()) {
    return operation::Result<StructureWriteReport>::failure(
        io_error("could not allocate a temporary structure output path"));
  }
  std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
  if (!output) {
    return operation::Result<StructureWriteReport>::failure(operation::Error{
        operation::ErrorCode::not_found,
        "could not create temporary structure output: " + temporary.string(),
        "check directory permissions and free space"});
  }
  const auto report = write_to_stream(output, topology, coordinates,
                                      std::move(options), context);
  output.flush();
  const auto stream_ok = output.good();
  output.close();
  if (!report.has_value() || !stream_ok) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    if (!report.has_value()) {
      return operation::Result<StructureWriteReport>::failure(report.error());
    }
    return operation::Result<StructureWriteReport>::failure(
        io_error("failed while flushing structure output: " + path.string(),
                 "check free space and filesystem health"));
  }
  if (!replace_file(temporary, path, overwrite)) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return operation::Result<StructureWriteReport>::failure(io_error(
        "could not atomically publish structure output: " + path.string(),
        overwrite ? "check target permissions"
                  : "target may have appeared; retry with another path"));
  }
  return report;
}

} // namespace molshredder::io
