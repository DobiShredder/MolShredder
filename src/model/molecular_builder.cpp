#include "molshredder/model/molecular_builder.hpp"

#include <limits>
#include <utility>

#include "molshredder/scene/math.hpp"

namespace molshredder::model {
namespace {

operation::Error invalid(std::string message, std::string suggestion = {}) {
  return {operation::ErrorCode::invalid_argument, std::move(message),
          std::move(suggestion)};
}

std::size_t saturated_add(std::size_t left, std::size_t right) {
  return right > std::numeric_limits<std::size_t>::max() - left
             ? std::numeric_limits<std::size_t>::max()
             : left + right;
}

std::size_t saturated_multiply(std::size_t left, std::size_t right) {
  if (left == 0U || right == 0U) return 0U;
  return right > std::numeric_limits<std::size_t>::max() / left
             ? std::numeric_limits<std::size_t>::max()
             : left * right;
}

}  // namespace

operation::Result<MoleculeBuildResult> build_molecule(
    const MoleculeBuildRequest &request) {
  const auto cancelled = [&request]() {
    return request.cancellation_requested &&
           request.cancellation_requested();
  };
  const auto cancellation_error = [] {
    return operation::Result<MoleculeBuildResult>::failure(operation::Error{
        operation::ErrorCode::cancelled, "molecule builder cancelled", {}});
  };
  if (cancelled()) return cancellation_error();
  if (request.residues.empty())
    return operation::Result<MoleculeBuildResult>::failure(
        invalid("molecule builder requires at least one residue"));
  if (request.atoms.empty())
    return operation::Result<MoleculeBuildResult>::failure(
        invalid("molecule builder requires at least one atom"));
  if (request.memory_budget_bytes == 0U)
    return operation::Result<MoleculeBuildResult>::failure(
        invalid("molecule builder memory budget must be positive"));

  auto reserved_bytes = saturated_multiply(request.atoms.size(),
      sizeof(AtomRecord) + sizeof(Vec3d) + sizeof(AtomId));
  reserved_bytes = saturated_add(
      reserved_bytes,
      saturated_multiply(request.bonds.size(), sizeof(Bond) + sizeof(BondId)));
  reserved_bytes = saturated_add(
      reserved_bytes,
      saturated_multiply(request.residues.size(), sizeof(ResidueRecord)));
  if (reserved_bytes > request.memory_budget_bytes)
    return operation::Result<MoleculeBuildResult>::failure(operation::Error{
        operation::ErrorCode::resource_exhausted,
        "molecule builder request exceeds its memory budget",
        "increase the builder budget or reduce the fragment size",
        {{"memory_required_bytes", std::to_string(reserved_bytes)},
         {"memory_budget_bytes",
          std::to_string(request.memory_budget_bytes)}}});

  TopologyBuilder builder;
  for (const auto &residue : request.residues) {
    if (cancelled()) return cancellation_error();
    const auto added = builder.add_residue(residue);
    if (!added.has_value())
      return operation::Result<MoleculeBuildResult>::failure(added.error());
  }
  std::vector<Vec3d> positions;
  positions.reserve(request.atoms.size());
  for (const auto &entry : request.atoms) {
    if (cancelled()) return cancellation_error();
    if (entry.atom.residue.value >= request.residues.size())
      return operation::Result<MoleculeBuildResult>::failure(
          invalid("builder atom references an unknown residue ordinal"));
    if (!scene::is_finite(entry.position))
      return operation::Result<MoleculeBuildResult>::failure(
          invalid("builder atom position must be finite"));
    const auto added = builder.add_atom(entry.atom);
    if (!added.has_value())
      return operation::Result<MoleculeBuildResult>::failure(added.error());
    positions.push_back(entry.position);
  }
  for (const auto &entry : request.bonds) {
    if (cancelled()) return cancellation_error();
    if (entry.first_atom >= request.atoms.size() ||
        entry.second_atom >= request.atoms.size())
      return operation::Result<MoleculeBuildResult>::failure(
          invalid("builder bond references an unknown atom ordinal"));
    if (const auto error = builder.add_bond(
            Bond{AtomIndex{entry.first_atom}, AtomIndex{entry.second_atom},
                 entry.order, BondQuery::none, BondStereo::none,
                 ChemicalAnnotationOrigin::user_override});
        error.has_value())
      return operation::Result<MoleculeBuildResult>::failure(*error);
  }
  if (cancelled()) return cancellation_error();
  const auto topology = builder.build();
  if (!topology.has_value())
    return operation::Result<MoleculeBuildResult>::failure(topology.error());
  FrameMetadata metadata;
  metadata.coordinate_unit = request.coordinate_unit;
  metadata.fields.emplace("builder", "molshredder-molecule-builder-v1");
  const auto frame = CoordinateFrame::create(
      CoordinateBuffer{std::move(positions)}, std::nullopt, {},
      std::move(metadata));
  if (!frame.has_value())
    return operation::Result<MoleculeBuildResult>::failure(frame.error());
  if (cancelled()) return cancellation_error();
  const auto coordinates = InMemoryCoordinateSource::create(
      topology.value()->atom_count(), {frame.value()});
  if (!coordinates.has_value())
    return operation::Result<MoleculeBuildResult>::failure(coordinates.error());
  return operation::Result<MoleculeBuildResult>::success(
      {topology.value(), coordinates.value(), topology.value()->atom_ids(),
       topology.value()->bond_ids(), reserved_bytes});
}

}  // namespace molshredder::model
