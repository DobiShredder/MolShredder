#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/operation/result.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/render/representation.hpp"

namespace molshredder::application {

inline constexpr unsigned int kRepresentationVisibilitySchemaVersion = 1U;
inline constexpr unsigned int kRepresentationVisibilitySessionSchemaVersion =
    2U;
inline constexpr std::size_t kRepresentationKindCount = 5U;

enum class RepresentationVisibilityMutation { show, hide, exclusive, toggle };

struct RepresentationVisibilitySnapshot {
  unsigned int schema_version{kRepresentationVisibilitySchemaVersion};
  std::size_t atom_count{};
  std::array<std::vector<std::uint64_t>, kRepresentationKindCount> masks;

  friend bool operator==(const RepresentationVisibilitySnapshot &,
                         const RepresentationVisibilitySnapshot &) = default;
};

struct RepresentationVisibilitySessionSnapshot {
  unsigned int schema_version{kRepresentationVisibilitySessionSchemaVersion};
  std::uint64_t object_id{};
  std::uint64_t topology_version{};
  std::vector<std::uint64_t> atom_ids;
  RepresentationVisibilitySnapshot visibility;

  friend bool operator==(const RepresentationVisibilitySessionSnapshot &,
                         const RepresentationVisibilitySessionSnapshot &) =
      default;
};

class RepresentationVisibilityState {
public:
  RepresentationVisibilityState() = default;

  [[nodiscard]] static operation::Result<RepresentationVisibilityState>
  create(std::size_t atom_count);
  [[nodiscard]] static operation::Result<RepresentationVisibilityState>
  restore(const RepresentationVisibilitySnapshot &snapshot);
  [[nodiscard]] operation::Result<RepresentationVisibilityState>
  remap(const model::TopologyRemap &remap) const;

  [[nodiscard]] std::size_t atom_count() const noexcept { return atom_count_; }
  [[nodiscard]] operation::Result<std::size_t>
  visible_count(render::RepresentationKind kind) const;
  [[nodiscard]] operation::Result<bool>
  visible(render::RepresentationKind kind, std::size_t atom_index) const;
  [[nodiscard]] operation::Result<bool>
  effectively_visible(render::RepresentationKind kind, std::size_t atom_index,
                      bool object_enabled) const;
  [[nodiscard]] operation::Result<std::vector<std::uint8_t>>
  selection_mask(render::RepresentationKind kind) const;

  [[nodiscard]] std::optional<operation::Error>
  apply(render::RepresentationKind kind, std::span<const std::uint8_t> selection,
        RepresentationVisibilityMutation mutation);

  [[nodiscard]] RepresentationVisibilitySnapshot snapshot() const;

private:
  explicit RepresentationVisibilityState(std::size_t atom_count);

  std::size_t atom_count_{};
  std::array<std::vector<std::uint64_t>, kRepresentationKindCount> masks_;
};

[[nodiscard]] operation::Result<std::string>
serialize_representation_visibility(
    const RepresentationVisibilitySnapshot &snapshot);
[[nodiscard]] operation::Result<RepresentationVisibilitySnapshot>
parse_representation_visibility(std::string_view text);
[[nodiscard]] operation::Result<std::string>
serialize_representation_visibility_session(
    const RepresentationVisibilitySessionSnapshot &snapshot);
[[nodiscard]] operation::Result<RepresentationVisibilitySessionSnapshot>
parse_representation_visibility_session(std::string_view text);

}  // namespace molshredder::application
