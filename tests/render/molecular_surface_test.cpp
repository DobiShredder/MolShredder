#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/render/molecular_surface.hpp"
#include "molshredder/render/reference_renderer.hpp"
#include "molshredder/scene/camera.hpp"
#include "molshredder/scene/math.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

struct Fixture {
  std::shared_ptr<const molshredder::model::Topology> topology;
  std::shared_ptr<const molshredder::model::CoordinateFrame> frame;
  std::vector<molshredder::render::AtomVisual> visuals;
};

Fixture make_fixture(molshredder::operation::LengthUnit unit) {
  using namespace molshredder;
  model::TopologyBuilder builder;
  const auto residue =
      builder.add_residue(model::ResidueRecord{"LIG", 1, "", "A", ""});
  static_cast<void>(builder.add_atom(
      model::AtomRecord{"C1", 6U, residue.value(), "", 0, 1}));
  auto metadata = model::FrameMetadata{};
  metadata.coordinate_unit = unit;
  const auto frame = model::CoordinateFrame::create(
                         model::CoordinateBuffer{std::vector<model::Vec3d>{
                             {0.0, 0.0, 0.0}}},
                         std::nullopt, {}, std::move(metadata))
                         .value();
  return {builder.build().value(), frame,
          {{{0.3F, 0.6F, 0.9F, 1.0F}, 1.5}}};
}

} // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const auto fixture = make_fixture(operation::LengthUnit::angstrom);
  render::MolecularSurfaceRequest request;
  request.topology = fixture.topology.get();
  request.frame = fixture.frame.get();
  request.frame_index = 0U;
  request.scene_node_id = 91U;
  request.atom_visuals = fixture.visuals;
  request.style.kind = render::MolecularSurfaceKind::van_der_waals;
  request.style.probe_radius_angstrom = 0.0;
  request.style.grid_spacing_angstrom = 0.5;
  request.style.voxel_budget = 100'000U;
  request.style.memory_budget_bytes = 8U * 1024U * 1024U;

  operation::TaskContext context;
  double progress{};
  context.report_progress = [&progress](const operation::ProgressUpdate &update) {
    progress = update.fraction;
  };
  request.context = &context;
  const auto vdw = render::build_molecular_surface(request);
  passed &= expect(vdw.has_value() && !vdw.value().mesh_vertices.empty() &&
                       !vdw.value().mesh_triangles.empty(),
                   "single atom VDW surface must produce a closed mesh");
  if (vdw.has_value()) {
    passed &= expect(
        vdw.value().topology_version == fixture.topology->version() &&
            vdw.value().scene_node_id == 91U &&
            vdw.value().provenance.at("algorithm") ==
                "union-sphere-signed-distance" &&
            vdw.value().provenance.at("surface_kind") == "van-der-waals" &&
            vdw.value().pick_targets.size() == 1U &&
            vdw.value().pick_targets.at(1U).kind == render::PickKind::atom &&
            vdw.value().pick_targets.at(1U).atom == model::AtomIndex{0U} &&
            progress == 1.0,
        "surface must retain algorithm, topology and atom picking provenance");
    for (const auto &vertex : vdw.value().mesh_vertices) {
      const auto radius = scene::length(vertex.position);
      passed &= expect(std::abs(radius - 1.5) < 0.08,
                       "signed-distance interpolation must approximate the analytic sphere");
    }

    auto camera_parameters = scene::Camera::create().value().parameters();
    camera_parameters.target = {0.0, 0.0, 0.0};
    camera_parameters.distance = 8.0;
    camera_parameters.projection = scene::ProjectionMode::orthographic;
    camera_parameters.orthographic_height = 5.0;
    camera_parameters.aspect_ratio = 1.0;
    camera_parameters.near_clip = 0.1;
    camera_parameters.far_clip = 100.0;
    const auto camera = scene::Camera::create(camera_parameters);
    render::ReferenceRenderSettings settings;
    settings.width = 64U;
    settings.height = 64U;
    const auto first = camera.has_value()
                           ? render::render_reference(vdw.value(), camera.value(),
                                                      settings)
                           : operation::Result<render::ImageRgba8>::failure(
                                 {operation::ErrorCode::internal,
                                  "surface camera setup failed", {}});
    const auto second = camera.has_value()
                            ? render::render_reference(vdw.value(), camera.value(),
                                                       settings)
                            : operation::Result<render::ImageRgba8>::failure(
                                  {operation::ErrorCode::internal,
                                   "surface camera setup failed", {}});
    bool found_atom_pick{};
    if (first.has_value()) {
      for (const auto pick_id : first.value().pick_ids)
        found_atom_pick = found_atom_pick || pick_id == 1U;
    }
    passed &= expect(
        first.has_value() && second.has_value() &&
            render::image_checksum(first.value()) != 0U &&
            render::image_checksum(first.value()) ==
                render::image_checksum(second.value()) &&
            first.value().pixels == second.value().pixels &&
            first.value().pick_ids == second.value().pick_ids &&
            found_atom_pick,
        "fixed-camera molecular surface image and picking must be deterministic");
  }

  request.style.kind = render::MolecularSurfaceKind::solvent_accessible;
  request.style.probe_radius_angstrom = 1.4;
  const auto sas = render::build_molecular_surface(request);
  passed &= expect(sas.has_value() && !sas.value().bounds.empty &&
                       sas.value().bounds.maximum.x > 2.8 &&
                       sas.value().bounds.minimum.x < -2.8 &&
                       sas.value().provenance.at("surface_kind") ==
                           "solvent-accessible",
                   "SAS must expand the analytic atom by the probe radius");

  const auto nanometer_fixture = make_fixture(operation::LengthUnit::nanometer);
  request.topology = nanometer_fixture.topology.get();
  request.frame = nanometer_fixture.frame.get();
  request.atom_visuals = nanometer_fixture.visuals;
  const auto nanometer = render::build_molecular_surface(request);
  passed &= expect(nanometer.has_value() &&
                       nanometer.value().bounds.maximum.x > 0.28 &&
                       nanometer.value().bounds.maximum.x < 0.31,
                   "surface radius and spacing must follow frame coordinate units");

  request.topology = fixture.topology.get();
  request.frame = fixture.frame.get();
  request.atom_visuals = fixture.visuals;
  request.style.voxel_budget = 1U;
  const auto budget = render::build_molecular_surface(request);
  passed &= expect(!budget.has_value() &&
                       budget.error().code ==
                           operation::ErrorCode::resource_exhausted,
                   "voxel budget exhaustion must be explicit");
  request.style.voxel_budget = 100'000U;
  context.cancellation.request_cancel();
  const auto cancelled = render::build_molecular_surface(request);
  passed &= expect(!cancelled.has_value() &&
                       cancelled.error().code == operation::ErrorCode::cancelled,
                   "cancelled surface generation must not publish a packet");
  return passed ? 0 : 1;
}
