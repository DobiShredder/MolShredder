#include "molshredder/model/coordinates.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <set>
#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::model {
namespace {

using operation::Error;
using operation::ErrorCode;

Error invalid(std::string message, std::string suggestion = {}) {
  return Error{ErrorCode::invalid_argument, std::move(message),
               std::move(suggestion)};
}

template <typename Scalar>
bool finite_vector(const Vec3<Scalar>& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

}  // namespace

std::size_t CoordinateBuffer::size() const noexcept {
  return std::visit([](const auto& values) { return values.size(); }, values_);
}

CoordinatePrecision CoordinateBuffer::precision() const noexcept {
  return std::holds_alternative<std::vector<Vec3f>>(values_)
             ? CoordinatePrecision::float32
             : CoordinatePrecision::float64;
}

bool CoordinateBuffer::all_finite() const noexcept {
  return std::visit(
      [](const auto& values) {
        return std::all_of(values.begin(), values.end(),
                           [](const auto& value) {
                             return finite_vector(value);
                           });
      },
      values_);
}

double UnitCell::signed_volume() const noexcept {
  return a.x * (b.y * c.z - b.z * c.y) -
         a.y * (b.x * c.z - b.z * c.x) +
         a.z * (b.x * c.y - b.y * c.x);
}

bool UnitCell::is_valid() const noexcept {
  return finite_vector(a) && finite_vector(b) && finite_vector(c) &&
         signed_volume() > 1.0e-12;
}

operation::Result<std::shared_ptr<const CoordinateFrame>>
CoordinateFrame::create(CoordinateBuffer positions,
                        std::optional<CoordinateBuffer> velocities,
                        std::vector<std::uint8_t> presence,
                        FrameMetadata metadata) {
  if (!positions.all_finite()) {
    return operation::Result<std::shared_ptr<const CoordinateFrame>>::failure(
        invalid("coordinate frame contains a non-finite position",
                "use a presence mask and finite placeholder coordinates for "
                "missing atoms"));
  }
  if (velocities.has_value()) {
    if (velocities->size() != positions.size()) {
      return operation::Result<std::shared_ptr<const CoordinateFrame>>::failure(
          invalid("velocity count does not match position count"));
    }
    if (!velocities->all_finite()) {
      return operation::Result<std::shared_ptr<const CoordinateFrame>>::failure(
          invalid("coordinate frame contains a non-finite velocity"));
    }
  }
  if (presence.empty()) {
    presence.assign(positions.size(), 1U);
  }
  if (presence.size() != positions.size()) {
    return operation::Result<std::shared_ptr<const CoordinateFrame>>::failure(
        invalid("presence-mask count does not match position count"));
  }
  if (std::any_of(presence.begin(), presence.end(),
                  [](std::uint8_t value) { return value > 1U; })) {
    return operation::Result<std::shared_ptr<const CoordinateFrame>>::failure(
        invalid("presence mask contains a value other than 0 or 1"));
  }
  if (metadata.physical_time.has_value() &&
      !std::isfinite(metadata.physical_time->value)) {
    return operation::Result<std::shared_ptr<const CoordinateFrame>>::failure(
        invalid("frame physical time must be finite"));
  }
  if (metadata.unit_cell.has_value() && !metadata.unit_cell->is_valid()) {
    return operation::Result<std::shared_ptr<const CoordinateFrame>>::failure(
        invalid("unit cell must contain finite, non-coplanar vectors"));
  }
  for (const auto& [name, property] : metadata.atom_properties) {
    if (name.empty()) {
      return operation::Result<std::shared_ptr<const CoordinateFrame>>::failure(
          invalid("frame atom property name must not be empty"));
    }
    if (column_size(property.values) != positions.size()) {
      return operation::Result<std::shared_ptr<const CoordinateFrame>>::failure(
          invalid("frame atom property row count does not match positions: " +
                  name));
    }
    if (const auto* boolean = std::get_if<BooleanColumn>(&property.values);
        boolean != nullptr &&
        std::any_of(boolean->values.begin(), boolean->values.end(),
                    [](std::uint8_t value) { return value > 1U; })) {
      return operation::Result<std::shared_ptr<const CoordinateFrame>>::failure(
          invalid("frame boolean property contains a value other than 0 or 1: " +
                  name));
    }
  }

  auto frame = std::shared_ptr<const CoordinateFrame>(new CoordinateFrame(
      std::move(positions), std::move(velocities), std::move(presence),
      std::move(metadata)));
  return operation::Result<std::shared_ptr<const CoordinateFrame>>::success(
      std::move(frame));
}

operation::Result<std::shared_ptr<const InMemoryCoordinateSource>>
InMemoryCoordinateSource::create(
    std::size_t atom_count,
    std::vector<std::shared_ptr<const CoordinateFrame>> frames) {
  for (const auto& frame : frames) {
    if (frame == nullptr) {
      return operation::Result<
          std::shared_ptr<const InMemoryCoordinateSource>>::failure(
          invalid("coordinate source contains a null frame"));
    }
    if (frame->atom_count() != atom_count) {
      return operation::Result<
          std::shared_ptr<const InMemoryCoordinateSource>>::failure(
          invalid("coordinate frame atom count does not match source topology"));
    }
  }
  auto source = std::shared_ptr<const InMemoryCoordinateSource>(
      new InMemoryCoordinateSource(atom_count, std::move(frames)));
  return operation::Result<
      std::shared_ptr<const InMemoryCoordinateSource>>::success(
      std::move(source));
}

operation::Result<std::shared_ptr<const CoordinateFrame>>
InMemoryCoordinateSource::read_frame(std::size_t frame_index) const {
  if (frame_index >= frames_.size()) {
    return operation::Result<std::shared_ptr<const CoordinateFrame>>::failure(
        Error{ErrorCode::not_found,
              "coordinate frame index is out of range: " +
                  std::to_string(frame_index),
              "request an index smaller than the frame count"});
  }
  return operation::Result<std::shared_ptr<const CoordinateFrame>>::success(
      frames_[frame_index]);
}

operation::Result<std::shared_ptr<const RemappedCoordinateSource>>
RemappedCoordinateSource::create(
    std::shared_ptr<const CoordinateSource> source,
    std::vector<std::optional<std::size_t>> target_to_source) {
  if (source == nullptr)
    return operation::Result<
        std::shared_ptr<const RemappedCoordinateSource>>::failure(
        invalid("remapped coordinate source must not be null"));
  std::set<std::size_t> used;
  for (const auto index : target_to_source) {
    if (!index.has_value())
      continue;
    if (*index >= source->atom_count())
      return operation::Result<
          std::shared_ptr<const RemappedCoordinateSource>>::failure(
          invalid("coordinate remap source index is out of range"));
    if (!used.insert(*index).second)
      return operation::Result<
          std::shared_ptr<const RemappedCoordinateSource>>::failure(
          invalid("coordinate remap source indices must be unique"));
  }
  return operation::Result<
      std::shared_ptr<const RemappedCoordinateSource>>::success(
      std::shared_ptr<const RemappedCoordinateSource>(
          new RemappedCoordinateSource(std::move(source),
                                       std::move(target_to_source))));
}

operation::Result<std::shared_ptr<const CoordinateFrame>>
RemappedCoordinateSource::read_frame(std::size_t frame_index) const {
  const auto source_frame = source_->read_frame(frame_index);
  if (!source_frame.has_value())
    return operation::Result<std::shared_ptr<const CoordinateFrame>>::failure(
        source_frame.error());
  const auto remap_buffer = [this](const CoordinateBuffer &buffer) {
    return std::visit(
        [this](const auto &values) -> CoordinateBuffer {
          using Values = std::decay_t<decltype(values)>;
          Values result;
          result.reserve(target_to_source_.size());
          for (const auto source : target_to_source_)
            result.push_back(source.has_value()
                                 ? values[*source]
                                 : typename Values::value_type{});
          return CoordinateBuffer{std::move(result)};
        },
        buffer.values());
  };
  const auto &source = *source_frame.value();
  auto metadata = source.metadata();
  for (auto &[name, property] : metadata.atom_properties) {
    static_cast<void>(name);
    property.values = std::visit(
        [this](const auto &values) -> AtomPropertyColumn {
          using Values = std::decay_t<decltype(values)>;
          if constexpr (std::is_same_v<Values, BooleanColumn>) {
            BooleanColumn result;
            result.values.reserve(target_to_source_.size());
            for (const auto source : target_to_source_)
              result.values.push_back(source.has_value()
                                          ? values.values[*source]
                                          : 0U);
            return result;
          } else {
            Values result;
            result.reserve(target_to_source_.size());
            for (const auto source : target_to_source_)
              result.push_back(source.has_value()
                                   ? values[*source]
                                   : typename Values::value_type{});
            return result;
          }
        },
        property.values);
  }
  metadata.fields.insert_or_assign("molshredder.coordinate_remap",
                                   "stable-identity-v1");
  std::vector<std::uint8_t> presence;
  presence.reserve(target_to_source_.size());
  for (const auto index : target_to_source_)
    presence.push_back(index.has_value() && source.atom_present(*index) ? 1U
                                                                       : 0U);
  std::optional<CoordinateBuffer> velocities;
  if (source.velocities().has_value())
    velocities = remap_buffer(*source.velocities());
  return CoordinateFrame::create(remap_buffer(source.positions()),
                                 std::move(velocities), std::move(presence),
                                 std::move(metadata));
}

}  // namespace molshredder::model
