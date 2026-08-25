#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/render/representation.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  using namespace molshredder;
  constexpr std::size_t atom_count = 10000U;
  bool passed = true;

  model::TopologyBuilder builder;
  const auto residue =
      builder.add_residue(model::ResidueRecord{"POL", 1, "", "A", ""});
  std::vector<model::AtomIndex> atom_indices;
  atom_indices.reserve(atom_count);
  for (std::size_t index = 0; index < atom_count; ++index) {
    const auto atom = builder.add_atom(model::AtomRecord{
        "C", 6U, residue.value(), "", 0, static_cast<std::int64_t>(index)});
    if (!atom.has_value()) {
      std::cerr << "could not construct scaling topology\n";
      return 1;
    }
    atom_indices.push_back(atom.value());
    if (index > 0U) {
      const auto error = builder.add_bond(
          {atom_indices[index - 1U], atom_indices[index],
           model::BondOrder::single});
      if (error.has_value()) {
        std::cerr << "could not construct scaling bonds\n";
        return 1;
      }
    }
  }
  const auto topology = builder.build();
  std::vector<model::Vec3f> positions;
  positions.reserve(atom_count);
  for (std::size_t index = 0; index < atom_count; ++index) {
    positions.push_back(
        {static_cast<float>(index), 0.0F, 0.0F});
  }
  const auto frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(positions)});
  if (!topology.has_value() || !frame.has_value()) {
    std::cerr << "could not construct scaling fixture\n";
    return 1;
  }
  const std::vector<render::AtomVisual> visuals(
      atom_count, render::AtomVisual{{0.5F, 0.7F, 0.9F, 1.0F}, 1.5});

  render::RepresentationRequest request{
      topology.value().get(), frame.value().get(), 0U, 1U, visuals, {}, {}, {},
      nullptr, 0U, {}};
  const auto started = std::chrono::steady_clock::now();
  const auto lines = render::build_representation(request);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);
  passed &= expect(lines.has_value() &&
                       lines.value().lines.size() == 2U * (atom_count - 1U) &&
                       lines.value().pick_targets.size() == atom_count - 1U,
                   "large line packet must preserve all bonds and picks");

  request.style.kind = render::RepresentationKind::spheres;
  const auto spheres = render::build_representation(request);
  passed &= expect(spheres.has_value() &&
                       spheres.value().spheres.size() == atom_count &&
                       spheres.value().pick_targets.size() == atom_count,
                   "large sphere packet must preserve all atoms and picks");
  std::cout << "atoms=" << atom_count << " line_build_ms=" << elapsed.count()
            << '\n';
  return passed ? 0 : 1;
}
