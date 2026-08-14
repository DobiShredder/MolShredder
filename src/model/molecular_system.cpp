#include "molshredder/model/molecular_system.hpp"

#include <utility>

#include "molshredder/operation/error.hpp"

namespace molshredder::model {

operation::Result<std::shared_ptr<const MolecularSystem>>
MolecularSystem::create(
    std::uint64_t id, std::string name,
    std::shared_ptr<const Topology> topology,
    std::shared_ptr<const CoordinateSource> coordinates) {
  if (name.empty()) {
    return operation::Result<std::shared_ptr<const MolecularSystem>>::failure(
        operation::Error{operation::ErrorCode::invalid_argument,
                         "molecular system name must not be empty", {}});
  }
  if (topology == nullptr) {
    return operation::Result<std::shared_ptr<const MolecularSystem>>::failure(
        operation::Error{operation::ErrorCode::invalid_argument,
                         "molecular system requires a topology", {}});
  }
  if (coordinates == nullptr) {
    return operation::Result<std::shared_ptr<const MolecularSystem>>::failure(
        operation::Error{operation::ErrorCode::invalid_argument,
                         "molecular system requires a coordinate source", {}});
  }
  if (topology->atom_count() != coordinates->atom_count()) {
    return operation::Result<std::shared_ptr<const MolecularSystem>>::failure(
        operation::Error{
            operation::ErrorCode::invalid_argument,
            "topology and coordinate source atom counts do not match",
            "load coordinates for the same topology"});
  }

  auto system = std::shared_ptr<const MolecularSystem>(new MolecularSystem(
      id, std::move(name), std::move(topology), std::move(coordinates)));
  return operation::Result<std::shared_ptr<const MolecularSystem>>::success(
      std::move(system));
}

}  // namespace molshredder::model
