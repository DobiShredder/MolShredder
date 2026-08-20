#include "molshredder/io/trajectory_writer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <type_traits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "molshredder/operation/error.hpp"

namespace molshredder::io {
namespace {

constexpr double kAmberVelocityScale = 20.455;
constexpr std::int32_t kTrrMagic = 1993;

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return {operation::ErrorCode::invalid_argument, std::move(message),
          std::move(suggestion)};
}

operation::Error io_error(std::string message, std::string suggestion = {}) {
  return {operation::ErrorCode::internal, std::move(message),
          std::move(suggestion)};
}

TrajectoryFormat resolve_format(TrajectoryFormat requested,
                                const std::filesystem::path &path = {}) {
  if (requested != TrajectoryFormat::auto_detect)
    return requested;
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  if (extension == ".rst7" || extension == ".restrt" ||
      extension == ".inpcrd" || extension == ".inprst") {
    return TrajectoryFormat::rst7;
  }
  if (extension == ".trr")
    return TrajectoryFormat::trr;
  if (extension == ".dcd")
    return TrajectoryFormat::dcd;
  if (extension == ".mdcrd" || extension == ".crd")
    return TrajectoryFormat::mdcrd;
  if (extension == ".crdbox")
    return TrajectoryFormat::crdbox;
  if (extension == ".binpos")
    return TrajectoryFormat::binpos;
  return TrajectoryFormat::auto_detect;
}

model::Vec3d vector_at(const model::CoordinateBuffer &buffer,
                       std::size_t index) {
  return std::visit(
      [index](const auto &values) {
        const auto value = values[index];
        return model::Vec3d{static_cast<double>(value.x),
                            static_cast<double>(value.y),
                            static_cast<double>(value.z)};
      },
      buffer.values());
}

double dot(model::Vec3d left, model::Vec3d right) noexcept {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

double length(model::Vec3d value) noexcept {
  return std::sqrt(dot(value, value));
}

double angle(model::Vec3d left, model::Vec3d right) noexcept {
  constexpr double radians_to_degrees = 57.2957795130823208768;
  const auto cosine = std::clamp(dot(left, right) /
                                     (length(left) * length(right)),
                                 -1.0, 1.0);
  return std::acos(cosine) * radians_to_degrees;
}

operation::Result<std::string> fixed12(double value, std::string_view field) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::fixed << std::setprecision(7) << value;
  auto text = std::move(output).str();
  if (text.size() > 12U) {
    return operation::Result<std::string>::failure(invalid(
        "Amber RST7 " + std::string{field} +
            " exceeds the F12.7 field: " + text,
        "translate or rescale values before export"));
  }
  return operation::Result<std::string>::success(
      std::string(12U - text.size(), ' ') + text);
}

operation::Result<std::string> fixed8(double value, std::string_view field) {
  if (!std::isfinite(value)) {
    return operation::Result<std::string>::failure(
        invalid("Amber CRD " + std::string{field} + " must be finite"));
  }
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::fixed << std::setprecision(3) << value;
  auto text = std::move(output).str();
  if (text.size() > 8U) {
    return operation::Result<std::string>::failure(invalid(
        "Amber CRD " + std::string{field} +
            " exceeds the F8.3 field: " + text,
        "translate or rescale values before export"));
  }
  return operation::Result<std::string>::success(
      std::string(8U - text.size(), ' ') + text);
}

operation::Result<double> metadata_number(const model::FrameMetadata &metadata,
                                          std::string_view name) {
  const auto found = metadata.fields.find(name);
  if (found == metadata.fields.end())
    return operation::Result<double>::failure(
        invalid("missing frame metadata field: " + std::string{name}));
  double value{};
  const auto parsed = std::from_chars(found->second.data(),
                                      found->second.data() + found->second.size(),
                                      value);
  if (parsed.ec != std::errc{} ||
      parsed.ptr != found->second.data() + found->second.size() ||
      !std::isfinite(value)) {
    return operation::Result<double>::failure(
        invalid("Amber RST7 " + std::string{name} +
                " metadata must be a finite number"));
  }
  return operation::Result<double>::success(value);
}

void append_u32(std::string &output, std::uint32_t value) {
  output.push_back(static_cast<char>((value >> 24U) & 0xffU));
  output.push_back(static_cast<char>((value >> 16U) & 0xffU));
  output.push_back(static_cast<char>((value >> 8U) & 0xffU));
  output.push_back(static_cast<char>(value & 0xffU));
}

void append_i32(std::string &output, std::int32_t value) {
  append_u32(output, std::bit_cast<std::uint32_t>(value));
}

void append_le_u32(std::string &output, std::uint32_t value) {
  output.push_back(static_cast<char>(value & 0xffU));
  output.push_back(static_cast<char>((value >> 8U) & 0xffU));
  output.push_back(static_cast<char>((value >> 16U) & 0xffU));
  output.push_back(static_cast<char>((value >> 24U) & 0xffU));
}

void put_le_u32(std::string &output, std::size_t offset,
                std::uint32_t value) {
  for (unsigned int index = 0; index < 4U; ++index) {
    output[offset + index] =
        static_cast<char>((value >> (index * 8U)) & 0xffU);
  }
}

void put_le_i32(std::string &output, std::size_t offset, std::int32_t value) {
  put_le_u32(output, offset, std::bit_cast<std::uint32_t>(value));
}

void put_le_f32(std::string &output, std::size_t offset, float value) {
  put_le_u32(output, offset, std::bit_cast<std::uint32_t>(value));
}

void append_le_f32(std::string &output, float value) {
  append_le_u32(output, std::bit_cast<std::uint32_t>(value));
}

void append_le_f64(std::string &output, double value) {
  const auto bits = std::bit_cast<std::uint64_t>(value);
  append_le_u32(output, static_cast<std::uint32_t>(bits & 0xffffffffU));
  append_le_u32(output, static_cast<std::uint32_t>(bits >> 32U));
}

void append_le_record(std::string &output, std::string_view payload) {
  append_le_u32(output, static_cast<std::uint32_t>(payload.size()));
  output.append(payload);
  append_le_u32(output, static_cast<std::uint32_t>(payload.size()));
}

template <typename Scalar>
void append_real(std::string &output, double value) {
  if constexpr (std::is_same_v<Scalar, float>) {
    append_u32(output, std::bit_cast<std::uint32_t>(static_cast<float>(value)));
  } else {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    append_u32(output, static_cast<std::uint32_t>(bits >> 32U));
    append_u32(output, static_cast<std::uint32_t>(bits & 0xffffffffU));
  }
}

operation::Result<std::int32_t>
trr_step(const model::FrameMetadata &metadata) {
  const auto signed_value = metadata.fields.find("trr.signed_step");
  if (signed_value != metadata.fields.end()) {
    std::int32_t parsed{};
    const auto result = std::from_chars(
        signed_value->second.data(),
        signed_value->second.data() + signed_value->second.size(), parsed);
    if (result.ec != std::errc{} ||
        result.ptr != signed_value->second.data() + signed_value->second.size()) {
      return operation::Result<std::int32_t>::failure(
          invalid("TRR signed step metadata must be a 32-bit integer"));
    }
    return operation::Result<std::int32_t>::success(parsed);
  }
  if (!metadata.source_step.has_value()) {
    return operation::Result<std::int32_t>::failure(invalid(
        "TRR export requires a typed source step",
        "attach a TRR frame or assign source_step before export"));
  }
  if (*metadata.source_step >
      static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
    return operation::Result<std::int32_t>::failure(
        invalid("TRR source step exceeds the signed 32-bit header field"));
  }
  return operation::Result<std::int32_t>::success(
      static_cast<std::int32_t>(*metadata.source_step));
}

const model::AtomProperty *force_property(const model::FrameMetadata &metadata,
                                          std::string_view name) {
  const auto found = metadata.atom_properties.find(name);
  return found == metadata.atom_properties.end() ? nullptr : &found->second;
}

operation::Result<double> force_component(const model::AtomProperty &property,
                                          std::size_t atom) {
  if (property.metadata.unit !=
      std::optional<std::string>{"kJ mol^-1 angstrom^-1"}) {
    return operation::Result<double>::failure(invalid(
        "TRR force properties must use kJ mol^-1 angstrom^-1 units"));
  }
  return std::visit(
      [atom](const auto &values) -> operation::Result<double> {
        using Column = std::decay_t<decltype(values)>;
        if constexpr (std::is_same_v<Column, std::vector<float>> ||
                      std::is_same_v<Column, std::vector<double>>) {
          if (atom >= values.size())
            return operation::Result<double>::failure(
                invalid("TRR force property length does not match atom count"));
          const auto value = static_cast<double>(values[atom]);
          if (!std::isfinite(value))
            return operation::Result<double>::failure(
                invalid("TRR force property contains a non-finite value"));
          return operation::Result<double>::success(value);
        }
        return operation::Result<double>::failure(
            invalid("TRR force properties must be float32 or float64"));
      },
      property.values);
}

operation::Result<SerializedTrajectoryFrame>
write_dcd(const model::CoordinateFrame &frame, TrajectoryWriteOptions options,
          operation::TaskContext &context) {
  if (context.cancellation.is_cancelled()) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        {operation::ErrorCode::cancelled, "DCD export cancelled", {}});
  }
  if (frame.atom_count() == 0U ||
      frame.atom_count() >
          static_cast<std::size_t>(
              std::numeric_limits<std::uint32_t>::max() / 4U)) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        invalid("DCD atom count exceeds the 32-bit coordinate record"));
  }
  for (std::size_t atom = 0; atom < frame.atom_count(); ++atom) {
    if (!frame.atom_present(atom)) {
      return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
          "DCD cannot represent missing atom " + std::to_string(atom + 1U),
          "fill the atom or export a format with explicit presence"));
    }
  }

  const auto &metadata = frame.metadata();
  std::int32_t step{};
  bool synthesized_step = false;
  if (const auto found = metadata.fields.find("dcd.signed_step");
      found != metadata.fields.end()) {
    const auto parsed = std::from_chars(
        found->second.data(), found->second.data() + found->second.size(), step);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != found->second.data() + found->second.size()) {
      return operation::Result<SerializedTrajectoryFrame>::failure(
          invalid("DCD signed step metadata must be a 32-bit integer"));
    }
  } else if (metadata.source_step.has_value()) {
    if (*metadata.source_step >
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
      return operation::Result<SerializedTrajectoryFrame>::failure(
          invalid("DCD source step exceeds the signed 32-bit header field"));
    }
    step = static_cast<std::int32_t>(*metadata.source_step);
  } else {
    synthesized_step = true;
  }

  double delta = 1.0;
  bool synthesized_delta = true;
  if (const auto found = metadata.fields.find("dcd.raw_delta");
      found != metadata.fields.end()) {
    const auto parsed = std::from_chars(
        found->second.data(), found->second.data() + found->second.size(),
        delta);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != found->second.data() + found->second.size() ||
        !std::isfinite(delta) || delta <= 0.0) {
      return operation::Result<SerializedTrajectoryFrame>::failure(
          invalid("DCD raw delta metadata must be a positive finite number"));
    }
    synthesized_delta = false;
  }
  const auto narrowed_delta = static_cast<float>(delta);
  if (!std::isfinite(narrowed_delta)) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        invalid("DCD raw delta exceeds float32 range"));
  }

  auto title = std::move(options.title);
  if (title.empty()) {
    const auto found = metadata.fields.find("dcd.title");
    title = found == metadata.fields.end() ? "MolShredder DCD frame"
                                           : found->second;
  }
  bool title_changed = false;
  for (auto &character : title) {
    const auto code = static_cast<unsigned char>(character);
    if (code < 0x20U || code == 0x7fU) {
      character = ' ';
      title_changed = true;
    }
  }
  if (title.size() > 80U) {
    title.resize(80U);
    title_changed = true;
  }

  const auto has_cell = metadata.unit_cell.has_value();
  if (has_cell && !metadata.unit_cell->is_valid()) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        invalid("DCD unit cell is invalid"));
  }

  std::string output;
  const auto coordinate_bytes = frame.atom_count() * 4U;
  output.reserve(116U + 92U + 12U +
                 (has_cell ? 56U : 0U) + 3U * (coordinate_bytes + 8U));
  std::string header(84U, '\0');
  std::copy_n("CORD", 4U, header.begin());
  put_le_i32(header, 4U, 1);
  put_le_i32(header, 8U, step);
  put_le_i32(header, 12U, 1);
  put_le_i32(header, 16U, step);
  put_le_i32(header, 36U, 0);
  put_le_f32(header, 40U, narrowed_delta);
  put_le_i32(header, 44U, has_cell ? 1 : 0);
  put_le_i32(header, 48U, 0);
  put_le_i32(header, 80U, 24);
  append_le_record(output, header);

  std::string title_record(84U, ' ');
  put_le_i32(title_record, 0U, 1);
  std::copy(title.begin(), title.end(), title_record.begin() + 4);
  append_le_record(output, title_record);
  std::string atom_record;
  atom_record.reserve(4U);
  append_le_u32(atom_record, static_cast<std::uint32_t>(frame.atom_count()));
  append_le_record(output, atom_record);

  if (has_cell) {
    const auto &cell = *metadata.unit_cell;
    const auto a = length(cell.a);
    const auto b = length(cell.b);
    const auto c = length(cell.c);
    const auto cos_gamma = dot(cell.a, cell.b) / (a * b);
    const auto cos_beta = dot(cell.a, cell.c) / (a * c);
    const auto cos_alpha = dot(cell.b, cell.c) / (b * c);
    std::string cell_record;
    cell_record.reserve(48U);
    for (const auto value :
         {a, cos_gamma, b, cos_beta, cos_alpha, c}) {
      if (!std::isfinite(value)) {
        return operation::Result<SerializedTrajectoryFrame>::failure(
            invalid("DCD unit cell contains a non-finite value"));
      }
      append_le_f64(cell_record, value);
    }
    append_le_record(output, cell_record);
  }

  const auto coordinate_scale =
      metadata.coordinate_unit == operation::LengthUnit::nanometer ? 10.0
                                                                   : 1.0;
  for (std::size_t axis = 0; axis < 3U; ++axis) {
    std::string axis_record;
    axis_record.reserve(coordinate_bytes);
    for (std::size_t atom = 0; atom < frame.atom_count(); ++atom) {
      if ((atom & 0x3fffU) == 0U && context.cancellation.is_cancelled()) {
        return operation::Result<SerializedTrajectoryFrame>::failure(
            {operation::ErrorCode::cancelled,
             "DCD export cancelled while writing coordinates", {}});
      }
      const auto value = vector_at(frame.positions(), atom);
      const auto component =
          axis == 0U ? value.x : (axis == 1U ? value.y : value.z);
      const auto narrowed = static_cast<float>(component * coordinate_scale);
      if (!std::isfinite(narrowed)) {
        return operation::Result<SerializedTrajectoryFrame>::failure(
            invalid("DCD coordinates contain a non-finite or out-of-range "
                    "value"));
      }
      append_le_f32(axis_record, narrowed);
    }
    append_le_record(output, axis_record);
  }

  std::vector<TrajectoryFormatLoss> losses;
  losses.push_back({"coordinate_precision",
                    static_cast<std::uint64_t>(frame.atom_count()) * 3U,
                    "DCD coordinates are narrowed to float32"});
  if (synthesized_step)
    losses.push_back({"source_step", 1U,
                      "DCD header step was synthesized as zero"});
  if (synthesized_delta)
    losses.push_back({"raw_delta", 1U,
                      "DCD raw delta was synthesized as one without a time "
                      "unit"});
  if (static_cast<double>(narrowed_delta) != delta)
    losses.push_back({"raw_delta_precision", 1U,
                      "CHARMM DCD stores raw delta as float32"});
  if (title_changed)
    losses.push_back({"title", 1U,
                      "DCD title was sanitized or truncated to 80 bytes"});
  if (metadata.physical_time.has_value())
    losses.push_back({"physical_time", 1U,
                      "DCD raw delta has no typed physical-time unit"});
  if (frame.velocities().has_value())
    losses.push_back({"velocity", 1U,
                      "coordinate DCD does not encode velocity vectors"});
  if (!metadata.atom_properties.empty())
    losses.push_back({"atom_properties",
                      static_cast<std::uint64_t>(
                          metadata.atom_properties.size()),
                      "DCD does not encode per-atom property columns"});
  std::uint64_t other_fields{};
  for (const auto &[name, unused] : metadata.fields) {
    static_cast<void>(unused);
    if (name != "dcd.raw_delta" && name != "dcd.title" &&
        name != "dcd.signed_step" && name != "dcd.fixed_atom_count" &&
        name != "format")
      ++other_fields;
  }
  if (other_fields != 0U)
    losses.push_back({"frame_metadata", other_fields,
                      "DCD does not encode auxiliary frame metadata"});
  if (context.report_progress)
    context.report_progress({1.0, "write-dcd"});
  return operation::Result<SerializedTrajectoryFrame>::success(
      SerializedTrajectoryFrame{
          std::move(output),
          TrajectoryWriteReport{TrajectoryFormat::dcd,
                                frame.atom_count(),
                                0U,
                                false,
                                false,
                                false,
                                false,
                                has_cell,
                                TrrPrecision::float32,
                                std::move(losses)}});
}

operation::Result<SerializedTrajectoryFrame>
write_mdcrd(const model::CoordinateFrame &frame,
            TrajectoryWriteOptions options,
            operation::TaskContext &context) {
  const auto write_box = options.format == TrajectoryFormat::crdbox;
  const std::string format_name =
      write_box ? "Amber CRDBOX" : "Amber CRD";
  if (context.cancellation.is_cancelled()) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        {operation::ErrorCode::cancelled, format_name + " export cancelled",
         {}});
  }
  if (frame.atom_count() == 0U) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        invalid(format_name + " export requires at least one atom"));
  }
  for (std::size_t atom = 0U; atom < frame.atom_count(); ++atom) {
    if (!frame.atom_present(atom)) {
      return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
          format_name + " cannot represent missing atom " +
              std::to_string(atom + 1U),
          "fill the atom or export a format with explicit presence"));
    }
  }

  const auto &metadata = frame.metadata();
  if (write_box && !metadata.unit_cell.has_value()) {
    return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
        "Amber CRDBOX export requires a typed unit cell",
        "use CRD for a non-periodic frame or attach periodic cell metadata"));
  }
  if (write_box) {
    const auto &cell = *metadata.unit_cell;
    const auto alpha = angle(cell.b, cell.c);
    const auto beta = angle(cell.a, cell.c);
    const auto gamma = angle(cell.a, cell.b);
    constexpr double angle_tolerance = 1.0e-6;
    if (std::abs(alpha - beta) > angle_tolerance ||
        std::abs(alpha - gamma) > angle_tolerance) {
      return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
          "Amber CRDBOX can represent only three lengths and one shared "
          "topology angle",
          "use a trajectory format that stores all three cell angles"));
    }
  }
  auto title = std::move(options.title);
  if (title.empty()) {
    const auto found = metadata.fields.find("title");
    title = found == metadata.fields.end() ? "MolShredder Amber trajectory"
                                           : found->second;
  }
  bool title_changed{};
  for (auto &character : title) {
    const auto code = static_cast<unsigned char>(character);
    if (code < 0x20U || code == 0x7fU) {
      character = ' ';
      title_changed = true;
    }
  }
  if (title.size() > 80U) {
    title.resize(80U);
    title_changed = true;
  }

  const auto coordinate_scale =
      metadata.coordinate_unit == operation::LengthUnit::nanometer ? 10.0
                                                                   : 1.0;
  std::string output;
  output.reserve(title.size() + 1U + frame.atom_count() * 24U +
                 (frame.atom_count() * 3U + 9U) / 10U);
  output += title;
  output.push_back('\n');
  std::size_t field_count{};
  for (std::size_t atom = 0U; atom < frame.atom_count(); ++atom) {
    if ((atom & 0x3fffU) == 0U && context.cancellation.is_cancelled()) {
      return operation::Result<SerializedTrajectoryFrame>::failure(
          {operation::ErrorCode::cancelled,
           format_name + " export cancelled while writing coordinates", {}});
    }
    const auto coordinate = vector_at(frame.positions(), atom);
    for (const auto component : {coordinate.x, coordinate.y, coordinate.z}) {
      auto formatted = fixed8(component * coordinate_scale, "coordinate");
      if (!formatted.has_value()) {
        return operation::Result<SerializedTrajectoryFrame>::failure(
            formatted.error());
      }
      output += formatted.value();
      ++field_count;
      if (field_count % 10U == 0U)
        output.push_back('\n');
    }
  }
  if (field_count % 10U != 0U)
    output.push_back('\n');
  if (write_box) {
    const auto &cell = *metadata.unit_cell;
    for (const auto cell_length :
         {length(cell.a), length(cell.b), length(cell.c)}) {
      auto formatted =
          fixed8(cell_length * coordinate_scale, "unit-cell length");
      if (!formatted.has_value()) {
        return operation::Result<SerializedTrajectoryFrame>::failure(
            formatted.error());
      }
      output += formatted.value();
    }
    output.push_back('\n');
  }

  std::vector<TrajectoryFormatLoss> losses;
  losses.push_back(
      {"coordinate_precision",
       static_cast<std::uint64_t>(frame.atom_count()) * 3U,
       "Amber CRD coordinates use fixed F8.3 decimal fields"});
  if (title_changed) {
    losses.push_back(
        {"title", 1U,
         "Amber CRD title was sanitized or truncated to 80 characters"});
  }
  if (!write_box && metadata.unit_cell.has_value()) {
    losses.push_back(
        {"unit_cell", 1U,
         "coordinate-only CRD output omits the periodic box; use explicit "
         "CRDBOX output"});
  } else if (write_box) {
    losses.push_back(
        {"unit_cell_angles", 3U,
         "Amber CRDBOX stores three lengths; the shared cell angle remains "
         "defined by the matching topology"});
  }
  if (frame.velocities().has_value()) {
    losses.push_back(
        {"velocity", 1U,
         format_name + " does not encode velocity vectors"});
  }
  if (metadata.physical_time.has_value()) {
    losses.push_back(
        {"physical_time", 1U,
         format_name + " does not encode typed physical time"});
  }
  if (metadata.source_step.has_value()) {
    losses.push_back(
        {"source_step", 1U,
         format_name + " does not encode a source step identifier"});
  }
  if (!metadata.atom_properties.empty()) {
    losses.push_back(
        {"atom_properties",
         static_cast<std::uint64_t>(metadata.atom_properties.size()),
         format_name + " does not encode per-atom property columns"});
  }
  std::uint64_t other_fields{};
  for (const auto &[name, unused] : metadata.fields) {
    static_cast<void>(unused);
    if (name != "title" && name != "format" &&
        name != "coordinate_field" && name != "box_angle_source")
      ++other_fields;
  }
  if (other_fields != 0U) {
    losses.push_back(
        {"frame_metadata", other_fields,
         format_name + " does not encode auxiliary frame metadata"});
  }
  if (context.report_progress)
    context.report_progress({1.0, write_box ? "write-crdbox" : "write-mdcrd"});
  return operation::Result<SerializedTrajectoryFrame>::success(
      SerializedTrajectoryFrame{
          std::move(output),
          TrajectoryWriteReport{options.format,
                                frame.atom_count(),
                                0U,
                                false,
                                false,
                                false,
                                false,
                                write_box,
                                std::nullopt,
                                std::move(losses)}});
}

operation::Result<SerializedTrajectoryFrame>
write_binpos(const model::CoordinateFrame &frame,
             TrajectoryWriteOptions options,
             operation::TaskContext &context) {
  if (context.cancellation.is_cancelled()) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        {operation::ErrorCode::cancelled, "BINPOS export cancelled", {}});
  }
  if (frame.atom_count() == 0U ||
      frame.atom_count() >
          static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
      frame.atom_count() >
          (std::numeric_limits<std::size_t>::max() - 8U) / 12U) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        invalid("BINPOS atom count must fit a positive signed 32-bit field"));
  }
  for (std::size_t atom = 0U; atom < frame.atom_count(); ++atom) {
    if (!frame.atom_present(atom)) {
      return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
          "BINPOS cannot represent missing atom " + std::to_string(atom + 1U),
          "fill the atom or export a format with explicit presence"));
    }
  }

  const auto &metadata = frame.metadata();
  const auto coordinate_scale =
      metadata.coordinate_unit == operation::LengthUnit::nanometer ? 10.0
                                                                   : 1.0;
  std::string output;
  output.reserve(8U + frame.atom_count() * 12U);
  output.append("fxyz", 4U);
  append_le_u32(output, static_cast<std::uint32_t>(frame.atom_count()));
  for (std::size_t atom = 0U; atom < frame.atom_count(); ++atom) {
    if ((atom & 0x3fffU) == 0U && context.cancellation.is_cancelled()) {
      return operation::Result<SerializedTrajectoryFrame>::failure(
          {operation::ErrorCode::cancelled,
           "BINPOS export cancelled while writing coordinates", {}});
    }
    const auto coordinate = vector_at(frame.positions(), atom);
    for (const auto component :
         {coordinate.x, coordinate.y, coordinate.z}) {
      const auto scaled = component * coordinate_scale;
      const auto narrowed = static_cast<float>(scaled);
      if (!std::isfinite(scaled) || !std::isfinite(narrowed)) {
        return operation::Result<SerializedTrajectoryFrame>::failure(
            invalid("BINPOS coordinate is non-finite or exceeds float32 range"));
      }
      append_le_f32(output, narrowed);
    }
  }

  std::vector<TrajectoryFormatLoss> losses;
  if (frame.positions().precision() == model::CoordinatePrecision::float64) {
    losses.push_back(
        {"coordinate_precision",
         static_cast<std::uint64_t>(frame.atom_count()) * 3U,
         "BINPOS stores Cartesian coordinates as float32"});
  }
  if (!options.title.empty())
    losses.push_back({"title", 1U, "BINPOS does not encode a title"});
  if (metadata.unit_cell.has_value())
    losses.push_back({"unit_cell", 1U, "BINPOS does not encode a unit cell"});
  if (frame.velocities().has_value())
    losses.push_back({"velocity", 1U,
                      "BINPOS does not encode velocity vectors"});
  if (metadata.physical_time.has_value())
    losses.push_back(
        {"physical_time", 1U, "BINPOS does not encode physical time"});
  if (metadata.source_step.has_value())
    losses.push_back(
        {"source_step", 1U, "BINPOS does not encode a source step"});
  if (!metadata.atom_properties.empty()) {
    losses.push_back(
        {"atom_properties",
         static_cast<std::uint64_t>(metadata.atom_properties.size()),
         "BINPOS does not encode per-atom property columns"});
  }
  std::uint64_t metadata_fields{};
  for (const auto &[name, unused] : metadata.fields) {
    static_cast<void>(unused);
    if (name != "format" && name != "byte_order" &&
        name != "coordinate_unit_source")
      ++metadata_fields;
  }
  if (metadata_fields != 0U) {
    losses.push_back({"frame_metadata", metadata_fields,
                      "BINPOS does not encode auxiliary frame metadata"});
  }
  if (context.report_progress)
    context.report_progress({1.0, "write-binpos"});
  return operation::Result<SerializedTrajectoryFrame>::success(
      SerializedTrajectoryFrame{
          std::move(output),
          TrajectoryWriteReport{TrajectoryFormat::binpos,
                                frame.atom_count(),
                                0U,
                                false,
                                false,
                                false,
                                false,
                                false,
                                std::nullopt,
                                std::move(losses)}});
}

template <typename Scalar>
operation::Result<SerializedTrajectoryFrame>
write_trr_precision(const model::CoordinateFrame &frame,
                    operation::TaskContext &context) {
  const auto &metadata = frame.metadata();
  if (context.cancellation.is_cancelled()) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        {operation::ErrorCode::cancelled, "TRR export cancelled", {}});
  }
  if (frame.atom_count() == 0U ||
      frame.atom_count() >
          static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        invalid("TRR atom count must fit a positive signed 32-bit field"));
  }
  for (std::size_t atom = 0; atom < frame.atom_count(); ++atom) {
    if (!frame.atom_present(atom)) {
      return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
          "TRR cannot represent missing atom " + std::to_string(atom + 1U),
          "fill the atom or export a format with explicit presence"));
    }
  }
  const auto step = trr_step(metadata);
  if (!step.has_value())
    return operation::Result<SerializedTrajectoryFrame>::failure(step.error());
  if (!metadata.physical_time.has_value()) {
    return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
        "TRR export requires typed physical time",
        "attach a trajectory frame or assign physical_time before export"));
  }
  auto time_ps = metadata.physical_time->value;
  if (metadata.physical_time->unit == model::TimeUnit::femtosecond)
    time_ps /= 1000.0;
  if (!std::isfinite(time_ps)) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        invalid("TRR physical time must be finite"));
  }
  const auto lambda = metadata_number(metadata, "trr.lambda");
  if (!lambda.has_value()) {
    return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
        "TRR export requires finite trr.lambda metadata",
        "preserve the source TRR metadata before export"));
  }
  std::int32_t nre{};
  if (const auto found = metadata.fields.find("trr.nre");
      found != metadata.fields.end()) {
    const auto parsed = std::from_chars(
        found->second.data(), found->second.data() + found->second.size(), nre);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != found->second.data() + found->second.size()) {
      return operation::Result<SerializedTrajectoryFrame>::failure(
          invalid("TRR nre metadata must be a signed 32-bit integer"));
    }
  }

  const auto *force_x = force_property(metadata, "force_x");
  const auto *force_y = force_property(metadata, "force_y");
  const auto *force_z = force_property(metadata, "force_z");
  const auto force_count = static_cast<unsigned int>(force_x != nullptr) +
                           static_cast<unsigned int>(force_y != nullptr) +
                           static_cast<unsigned int>(force_z != nullptr);
  if (force_count != 0U && force_count != 3U) {
    return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
        "TRR force export requires force_x, force_y and force_z together"));
  }
  const auto has_forces = force_count == 3U;
  if (has_forces) {
    for (std::size_t atom = 0; atom < frame.atom_count(); ++atom) {
      for (const auto *property : {force_x, force_y, force_z}) {
        const auto value = force_component(*property, atom);
        if (!value.has_value())
          return operation::Result<SerializedTrajectoryFrame>::failure(
              value.error());
      }
    }
  }
  const auto has_velocities = frame.velocities().has_value();
  if (has_velocities && !metadata.velocity_time_unit.has_value()) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        invalid("TRR velocity export requires a typed time unit"));
  }

  constexpr auto real_size = sizeof(Scalar);
  const auto vector_bytes = frame.atom_count() * 3U * real_size;
  if (vector_bytes >
      static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        invalid("TRR coordinate block exceeds the signed 32-bit size field"));
  }
  const auto has_cell = metadata.unit_cell.has_value();
  const auto box_bytes = has_cell ? 9U * real_size : 0U;
  std::string output;
  output.reserve(96U + box_bytes +
                 vector_bytes *
                     (1U + static_cast<unsigned int>(has_velocities) +
                      static_cast<unsigned int>(has_forces)));
  append_i32(output, kTrrMagic);
  append_i32(output, 13);
  append_i32(output, 12);
  output.append("GMX_trn_file", 12U);
  for (const auto size : std::array<std::size_t, 10U>{
           0U, 0U, box_bytes, 0U, 0U, 0U, 0U, vector_bytes,
           has_velocities ? vector_bytes : 0U,
           has_forces ? vector_bytes : 0U}) {
    append_i32(output, static_cast<std::int32_t>(size));
  }
  append_i32(output, static_cast<std::int32_t>(frame.atom_count()));
  append_i32(output, step.value());
  append_i32(output, nre);
  append_real<Scalar>(output, time_ps);
  append_real<Scalar>(output, lambda.value());

  const auto coordinate_to_nm =
      metadata.coordinate_unit == operation::LengthUnit::angstrom ? 0.1 : 1.0;
  if (has_cell) {
    const auto &cell = *metadata.unit_cell;
    for (const auto component : {cell.a.x, cell.a.y, cell.a.z, cell.b.x,
                                 cell.b.y, cell.b.z, cell.c.x, cell.c.y,
                                 cell.c.z}) {
      if (!std::isfinite(component)) {
        return operation::Result<SerializedTrajectoryFrame>::failure(
            invalid("TRR unit cell contains a non-finite value"));
      }
      append_real<Scalar>(output, component * coordinate_to_nm);
    }
  }
  auto write_vectors = [&](const model::CoordinateBuffer &buffer, double scale,
                           std::string_view channel)
      -> std::optional<operation::Error> {
    for (std::size_t atom = 0; atom < buffer.size(); ++atom) {
      if ((atom & 0x3fffU) == 0U && context.cancellation.is_cancelled()) {
        return operation::Error{operation::ErrorCode::cancelled,
                                "TRR export cancelled while writing " +
                                    std::string{channel},
                                {}};
      }
      const auto value = vector_at(buffer, atom);
      for (const auto component : {value.x, value.y, value.z}) {
        if (!std::isfinite(component))
          return invalid("TRR " + std::string{channel} +
                         " contains a non-finite value");
        append_real<Scalar>(output, component * scale);
      }
    }
    return std::nullopt;
  };
  if (const auto error = write_vectors(frame.positions(), coordinate_to_nm,
                                       "coordinates");
      error.has_value()) {
    return operation::Result<SerializedTrajectoryFrame>::failure(*error);
  }
  if (has_velocities) {
    auto velocity_scale = coordinate_to_nm;
    if (*metadata.velocity_time_unit == model::TimeUnit::femtosecond)
      velocity_scale *= 1000.0;
    if (const auto error = write_vectors(*frame.velocities(), velocity_scale,
                                         "velocities");
        error.has_value()) {
      return operation::Result<SerializedTrajectoryFrame>::failure(*error);
    }
  }
  if (has_forces) {
    for (std::size_t atom = 0; atom < frame.atom_count(); ++atom) {
      for (const auto *property : {force_x, force_y, force_z}) {
        const auto value = force_component(*property, atom);
        append_real<Scalar>(output, value.value() * 10.0);
      }
    }
  }

  std::vector<TrajectoryFormatLoss> losses;
  if constexpr (std::is_same_v<Scalar, float>) {
    if (has_cell)
      losses.push_back({"unit_cell_precision", 9U,
                        "TRR float32 output narrows the typed unit cell"});
    losses.push_back({"physical_time_precision", 1U,
                      "TRR float32 output narrows physical time"});
    losses.push_back({"lambda_precision", 1U,
                      "TRR float32 output narrows lambda"});
  }
  std::uint64_t other_properties{};
  for (const auto &[name, unused] : metadata.atom_properties) {
    static_cast<void>(unused);
    if (name != "force_x" && name != "force_y" && name != "force_z")
      ++other_properties;
  }
  if (other_properties != 0U) {
    losses.push_back({"atom_properties", other_properties,
                      "TRR does not encode auxiliary per-atom properties"});
  }
  std::uint64_t other_fields{};
  for (const auto &[name, unused] : metadata.fields) {
    static_cast<void>(unused);
    if (name != "trr.lambda" && name != "trr.nre" &&
        name != "trr.signed_step" && name != "format" &&
        name != "velocity_source_unit" &&
        name != "velocity_scale_to_angstrom_per_ps")
      ++other_fields;
  }
  if (other_fields != 0U) {
    losses.push_back({"frame_metadata", other_fields,
                      "TRR does not encode auxiliary frame metadata"});
  }
  if (context.report_progress)
    context.report_progress({1.0, "write-trr"});
  return operation::Result<SerializedTrajectoryFrame>::success(
      SerializedTrajectoryFrame{
          std::move(output),
          TrajectoryWriteReport{
              TrajectoryFormat::trr, frame.atom_count(), 0U, true, false,
              has_velocities, has_forces, has_cell,
              std::is_same_v<Scalar, float>
                  ? std::optional<TrrPrecision>{TrrPrecision::float32}
                  : std::optional<TrrPrecision>{TrrPrecision::float64},
              std::move(losses)}});
}

operation::Result<SerializedTrajectoryFrame>
write_trr(const model::CoordinateFrame &frame,
          operation::TaskContext &context) {
  auto use_double = frame.positions().precision() ==
                    model::CoordinatePrecision::float64;
  if (frame.velocities().has_value() &&
      frame.velocities()->precision() == model::CoordinatePrecision::float64)
    use_double = true;
  for (const auto name : {"force_x", "force_y", "force_z"}) {
    if (const auto *property = force_property(frame.metadata(), name);
        property != nullptr &&
        std::holds_alternative<std::vector<double>>(property->values))
      use_double = true;
  }
  return use_double ? write_trr_precision<double>(frame, context)
                    : write_trr_precision<float>(frame, context);
}

operation::Result<SerializedTrajectoryFrame>
write_rst7(const model::CoordinateFrame &frame, TrajectoryWriteOptions options,
           operation::TaskContext &context) {
  if (context.cancellation.is_cancelled()) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        {operation::ErrorCode::cancelled, "Amber RST7 export cancelled", {}});
  }
  for (std::size_t atom = 0; atom < frame.atom_count(); ++atom) {
    if (!frame.atom_present(atom)) {
      return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
          "Amber RST7 cannot represent missing atom " +
              std::to_string(atom + 1U),
          "fill the atom or export a format with explicit presence"));
    }
  }
  if (frame.atom_count() > 999999U) {
    return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
        "Amber RST7 atom count exceeds the six-character NATOM field"));
  }
  const auto &metadata = frame.metadata();
  const auto has_time = metadata.physical_time.has_value();
  const auto temperature_field = metadata.fields.find("temperature");
  const auto has_temperature = temperature_field != metadata.fields.end();
  std::optional<double> temperature;
  if (has_temperature) {
    auto parsed = metadata_number(metadata, "temperature");
    if (!parsed.has_value())
      return operation::Result<SerializedTrajectoryFrame>::failure(
          parsed.error());
    temperature = parsed.value();
    const auto unit = metadata.fields.find("temperature_unit");
    if (unit != metadata.fields.end() && unit->second != "kelvin") {
      return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
          "Amber RST7 temperature must use kelvin metadata units"));
    }
  }
  if (has_temperature && !has_time) {
    return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
        "Amber RST7 cannot encode temperature without the preceding time "
        "header field",
        "assign physical time or omit temperature before export"));
  }
  double time{};
  if (has_time) {
    time = metadata.physical_time->value;
    if (metadata.physical_time->unit == model::TimeUnit::femtosecond)
      time /= 1000.0;
    if (!std::isfinite(time)) {
      return operation::Result<SerializedTrajectoryFrame>::failure(
          invalid("Amber RST7 physical time must be finite"));
    }
  }

  auto title = options.title;
  if (title.empty()) {
    const auto found = metadata.fields.find("title");
    title = found == metadata.fields.end() ? "MolShredder Amber restart"
                                           : found->second;
  }
  bool title_changed{};
  for (auto &character : title) {
    const auto code = static_cast<unsigned char>(character);
    if (code < 0x20U || code == 0x7fU) {
      character = ' ';
      title_changed = true;
    }
  }
  if (title.size() > 80U) {
    title.resize(80U);
    title_changed = true;
  }

  const auto coordinate_scale =
      metadata.coordinate_unit == operation::LengthUnit::nanometer ? 10.0
                                                                   : 1.0;
  const auto has_velocities = frame.velocities().has_value();
  double velocity_scale = coordinate_scale / kAmberVelocityScale;
  if (has_velocities) {
    if (!metadata.velocity_time_unit.has_value()) {
      return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
          "Amber RST7 velocity export requires a typed time unit"));
    }
    if (*metadata.velocity_time_unit == model::TimeUnit::femtosecond)
      velocity_scale *= 1000.0;
  }

  std::array<double, 6U> cell_values{};
  std::size_t box_count{};
  if (metadata.unit_cell.has_value()) {
    const auto &cell = *metadata.unit_cell;
    cell_values = {length(cell.a) * coordinate_scale,
                   length(cell.b) * coordinate_scale,
                   length(cell.c) * coordinate_scale,
                   angle(cell.b, cell.c),
                   angle(cell.a, cell.c),
                   angle(cell.a, cell.b)};
    const auto orthogonal = std::abs(cell_values[3] - 90.0) <= 1.0e-10 &&
                            std::abs(cell_values[4] - 90.0) <= 1.0e-10 &&
                            std::abs(cell_values[5] - 90.0) <= 1.0e-10;
    box_count = orthogonal ? 3U : 6U;
  }
  if (frame.atom_count() > std::numeric_limits<std::size_t>::max() / 3U) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        invalid("Amber RST7 coordinate count overflows addressable memory"));
  }
  const auto coordinate_count = frame.atom_count() * 3U;
  const auto trailing = (has_velocities ? coordinate_count : 0U) + box_count;
  if ((coordinate_count == 3U && trailing == 3U) ||
      (coordinate_count == 6U && trailing == 6U)) {
    return operation::Result<SerializedTrajectoryFrame>::failure(invalid(
        "Amber RST7 optional blocks are ambiguous for this small system",
        "use a NetCDF restart or include a distinguishable velocity/cell "
        "combination"));
  }

  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << title << '\n' << std::setw(6) << frame.atom_count();
  if (has_time)
    output << ' ' << std::scientific << std::uppercase << std::setprecision(7)
           << time;
  if (temperature.has_value())
    output << ' ' << std::scientific << std::uppercase << std::setprecision(7)
           << *temperature;
  output << '\n';

  auto write_block = [&](const model::CoordinateBuffer &buffer, double scale,
                         std::string_view field)
      -> std::optional<operation::Error> {
    std::size_t field_count{};
    for (std::size_t atom = 0; atom < buffer.size(); ++atom) {
      if ((atom & 0x3fffU) == 0U && context.cancellation.is_cancelled()) {
        return operation::Error{operation::ErrorCode::cancelled,
                                "Amber RST7 export cancelled before atom " +
                                    std::to_string(atom + 1U),
                                {}};
      }
      const auto value = vector_at(buffer, atom);
      for (const auto component : {value.x, value.y, value.z}) {
        auto formatted = fixed12(component * scale, field);
        if (!formatted.has_value())
          return formatted.error();
        output << formatted.value();
        ++field_count;
        if (field_count % 6U == 0U)
          output << '\n';
      }
    }
    if (field_count % 6U != 0U)
      output << '\n';
    return std::nullopt;
  };
  if (const auto error = write_block(frame.positions(), coordinate_scale,
                                     "coordinate");
      error.has_value()) {
    return operation::Result<SerializedTrajectoryFrame>::failure(*error);
  }
  if (has_velocities) {
    if (const auto error = write_block(*frame.velocities(), velocity_scale,
                                       "velocity");
        error.has_value()) {
      return operation::Result<SerializedTrajectoryFrame>::failure(*error);
    }
  }
  for (std::size_t index = 0; index < box_count; ++index) {
    auto formatted = fixed12(cell_values[index], "unit-cell");
    if (!formatted.has_value())
      return operation::Result<SerializedTrajectoryFrame>::failure(
          formatted.error());
    output << formatted.value();
  }
  if (box_count != 0U)
    output << '\n';
  if (!output) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        io_error("failed while writing Amber RST7 output"));
  }

  std::vector<TrajectoryFormatLoss> losses;
  losses.push_back({"coordinate_precision",
                    static_cast<std::uint64_t>(coordinate_count),
                    "Amber RST7 coordinates use fixed F12.7 decimal fields"});
  if (has_velocities) {
    losses.push_back(
        {"velocity_precision", static_cast<std::uint64_t>(coordinate_count),
         "Amber RST7 velocities use scaled fixed F12.7 decimal fields"});
  }
  if (has_time) {
    losses.push_back({"physical_time_precision", 1U,
                      "Amber RST7 physical time uses scientific decimal "
                      "text with seven fractional digits"});
  }
  if (has_temperature) {
    losses.push_back({"temperature_precision", 1U,
                      "Amber RST7 temperature uses scientific decimal text "
                      "with seven fractional digits"});
  }
  if (box_count != 0U) {
    losses.push_back({"unit_cell_precision", box_count,
                      "Amber RST7 unit-cell values use fixed F12.7 decimal "
                      "fields"});
  }
  if (title_changed) {
    losses.push_back({"title", 1U,
                      "Amber RST7 title was sanitized or truncated to 80 "
                      "characters"});
  }
  if (metadata.source_step.has_value()) {
    losses.push_back({"source_step", 1U,
                      "Amber RST7 does not encode a source step identifier"});
  }
  if (!metadata.atom_properties.empty()) {
    losses.push_back(
        {"atom_properties",
         static_cast<std::uint64_t>(metadata.atom_properties.size()),
         "Amber RST7 does not encode per-atom property columns"});
  }
  std::uint64_t other_fields{};
  for (const auto &[name, unused] : metadata.fields) {
    static_cast<void>(unused);
    if (name != "title" && name != "temperature" &&
        name != "temperature_unit" && name != "format" &&
        name != "velocity_source_unit" &&
        name != "velocity_scale_to_angstrom_per_ps") {
      ++other_fields;
    }
  }
  if (other_fields != 0U) {
    losses.push_back({"frame_metadata", other_fields,
                      "Amber RST7 does not encode auxiliary frame metadata"});
  }
  auto content = std::move(output).str();
  if (context.report_progress)
    context.report_progress({1.0, "write-rst7"});
  return operation::Result<SerializedTrajectoryFrame>::success(
      SerializedTrajectoryFrame{
          std::move(content),
          TrajectoryWriteReport{TrajectoryFormat::rst7,
                                frame.atom_count(),
                                0U,
                                has_time,
                                has_temperature,
                                has_velocities,
                                false,
                                box_count != 0U,
                                std::nullopt,
                                std::move(losses)}});
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

operation::Result<SerializedTrajectoryFrame>
serialize_trajectory_frame(const model::CoordinateFrame &frame,
                           TrajectoryWriteOptions options,
                           operation::TaskContext &context) {
  options.format = resolve_format(options.format);
  if (options.format != TrajectoryFormat::rst7 &&
      options.format != TrajectoryFormat::trr &&
      options.format != TrajectoryFormat::dcd &&
      options.format != TrajectoryFormat::mdcrd &&
      options.format != TrajectoryFormat::crdbox &&
      options.format != TrajectoryFormat::binpos) {
    return operation::Result<SerializedTrajectoryFrame>::failure(
        {operation::ErrorCode::unsupported,
         "trajectory writer does not support format: " +
             std::string{to_string(options.format)},
         "use DCD, BINPOS, Amber CRD/RST7 or GROMACS TRR output"});
  }
  auto result =
      options.format == TrajectoryFormat::dcd
          ? write_dcd(frame, std::move(options), context)
          : (options.format == TrajectoryFormat::binpos
                 ? write_binpos(frame, std::move(options), context)
                 : (options.format == TrajectoryFormat::mdcrd ||
                     options.format == TrajectoryFormat::crdbox
                 ? write_mdcrd(frame, std::move(options), context)
                 : (options.format == TrajectoryFormat::trr
                        ? write_trr(frame, context)
                        : write_rst7(frame, std::move(options), context))));
  if (result.has_value())
    result.value().report.byte_count = result.value().content.size();
  return result;
}

operation::Result<TrajectoryWriteReport>
write_trajectory_frame_file(const std::filesystem::path &path,
                            const model::CoordinateFrame &frame,
                            TrajectoryWriteOptions options, bool overwrite,
                            operation::TaskContext &context) {
  options.format = resolve_format(options.format, path);
  if (options.format == TrajectoryFormat::auto_detect) {
    return operation::Result<TrajectoryWriteReport>::failure(invalid(
        "could not infer trajectory output format from path: " + path.string(),
        "use a .dcd/.binpos/.mdcrd/.crd/.crdbox/.rst7/.restrt/.inpcrd/.inprst/.trr suffix or an "
        "explicit --file-format"));
  }
  if (path.empty() || path.filename().empty()) {
    return operation::Result<TrajectoryWriteReport>::failure(
        invalid("trajectory output path must name a file"));
  }
  std::error_code filesystem_error;
  if (!overwrite && std::filesystem::exists(path, filesystem_error)) {
    return operation::Result<TrajectoryWriteReport>::failure(
        invalid("trajectory output already exists: " + path.string(),
                "choose another path or pass --overwrite true"));
  }
  if (filesystem_error) {
    return operation::Result<TrajectoryWriteReport>::failure(io_error(
        "could not inspect trajectory output path: " + path.string()));
  }
  auto serialized = serialize_trajectory_frame(frame, std::move(options), context);
  if (!serialized.has_value())
    return operation::Result<TrajectoryWriteReport>::failure(serialized.error());
  const auto temporary = temporary_path(path);
  if (temporary.empty()) {
    return operation::Result<TrajectoryWriteReport>::failure(
        io_error("could not allocate a temporary trajectory output path"));
  }
  std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
  if (!output) {
    return operation::Result<TrajectoryWriteReport>::failure(
        {operation::ErrorCode::not_found,
         "could not create temporary trajectory output: " + temporary.string(),
         "check directory permissions and free space"});
  }
  output.write(serialized.value().content.data(),
               static_cast<std::streamsize>(serialized.value().content.size()));
  output.flush();
  const auto stream_ok = output.good();
  output.close();
  if (!stream_ok) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return operation::Result<TrajectoryWriteReport>::failure(io_error(
        "failed while flushing trajectory output: " + path.string(),
        "check free space and filesystem health"));
  }
  if (!replace_file(temporary, path, overwrite)) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return operation::Result<TrajectoryWriteReport>::failure(io_error(
        "could not atomically publish trajectory output: " + path.string(),
        overwrite ? "check target permissions"
                  : "target may have appeared; retry with another path"));
  }
  return operation::Result<TrajectoryWriteReport>::success(
      std::move(serialized.value().report));
}

} // namespace molshredder::io
