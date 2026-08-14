#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::model {

class MolecularSystem {
 public:
  [[nodiscard]] static operation::Result<std::shared_ptr<const MolecularSystem>>
  create(std::uint64_t id, std::string name,
         std::shared_ptr<const Topology> topology,
         std::shared_ptr<const CoordinateSource> coordinates);

  [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  [[nodiscard]] const std::shared_ptr<const Topology>& topology() const
      noexcept {
    return topology_;
  }
  [[nodiscard]] const std::shared_ptr<const CoordinateSource>& coordinates()
      const noexcept {
    return coordinates_;
  }

 private:
  MolecularSystem(std::uint64_t id, std::string name,
                  std::shared_ptr<const Topology> topology,
                  std::shared_ptr<const CoordinateSource> coordinates)
      : id_{id},
        name_{std::move(name)},
        topology_{std::move(topology)},
        coordinates_{std::move(coordinates)} {}

  std::uint64_t id_{};
  std::string name_;
  std::shared_ptr<const Topology> topology_;
  std::shared_ptr<const CoordinateSource> coordinates_;
};

}  // namespace molshredder::model
