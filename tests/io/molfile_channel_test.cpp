#include <cmath>
#include <cstdint>
#include <future>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/command/result.hpp"
#include "molshredder/io/molfile_provider.hpp"

namespace {

bool expect(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAILED: " << message << '\n';
  return condition;
}

void touch(const std::filesystem::path& path) {
  std::ofstream output{path};
  output << "synthetic molfile input\n";
}

bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

std::string loss_of(const molshredder::io::StructureDocument& document) {
  const auto& metadata =
      document.structures.front().topology->source_metadata();
  const auto found = metadata.find("molfile.loss");
  return found == metadata.end() ? std::string{} : found->second;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder;
  if (argc != 3) {
    std::cerr << "usage: molfile_channel_test plugin-list working-directory\n";
    return 2;
  }
  std::map<std::string, std::filesystem::path> plugins;
  {
    std::ifstream list{argv[1]};
    for (std::string line; std::getline(list, line);) {
      const auto separator = line.find('=');
      if (separator != std::string::npos) {
        plugins.emplace(line.substr(0U, separator),
                        line.substr(separator + 1U));
      }
    }
  }
  const auto plugin = [&](const char* name) {
    return plugins.count(name) == 1U ? plugins.at(name)
                                     : std::filesystem::path{};
  };
  const auto psf_plugin = plugin("psf");
  const auto pdb_plugin = plugin("pdb");
  const auto xyz_plugin = plugin("xyz");
  const auto unprofiled_plugin = plugin("hoomd");
  const auto dcd_plugin = plugin("dcd");
  const auto crd_plugin = plugin("crd");
  const auto dx_plugin = plugin("dx");
  const auto ccp4_plugin = plugin("ccp4");
  if (psf_plugin.empty() || dcd_plugin.empty() || dx_plugin.empty()) {
    std::cerr << "plugin list is incomplete\n";
    return 2;
  }
  const auto root = std::filesystem::path{argv[2]} / "molfile-channel-fixtures";
  std::filesystem::create_directories(root);
  for (const auto* name :
       {"traj.dcd", "traj_empty.dcd", "traj_mismatch.dcd", "traj_nan.dcd",
        "traj_nocell.dcd", "traj_replay_fail.dcd", "traj_cancel.dcd",
        "traj.crd", "traj_shared.dcd", "traj_budget.dcd", "traj_action.dcd",
        "vol.dx", "vol.map", "vol_meta_fail.dx", "vol_data_fail.dx",
        "vol_multi.dx", "vol_single_axis.dx", "vol_oversized.dx",
        "vol_nan.dx", "vol_nan_axis.dx", "vol_cancel.dx", "vol_action.dx",
        "vol_named.dx", "topology.psf", "model.pdb", "rich_no_order.pdb", "bond_fail.pdb",
        "bond_zero_based.pdb", "bond_negative.pdb", "molecule.xyz",
        "trajectory.dcd", "bond_budget.psf", "action.psf", "oversized.pdb"}) {
    touch(root / name);
  }

  bool passed = true;

  {
    io::MolfileProviderRegistry registry;
    passed &= expect(registry.discover({{}, {psf_plugin}}).has_value(),
                     "psf-like plugin must register without a timestep callback");
    const auto document =
        registry.read_structure(root / "topology.psf", "molfile:psf");
    passed &= expect(document.has_value(), "psf channel must read");
    if (document.has_value()) {
      const auto& structure = document.value().structures.front();
      const auto& topology = *structure.topology;
      passed &= expect(document.value().format == io::StructureFormat::psf &&
                           topology.atom_count() == 3U,
                       "psf channel must report the psf format and 3 atoms");
      passed &= expect(structure.coordinates->frame_count() == 0U &&
                           structure.metadata.at("coordinate_state") == "absent",
                       "structure-only channel must not invent coordinates");
      passed &= expect(topology.bonds().size() == 3U,
                       "reverse duplicate bond must be dropped once");
      passed &= expect(topology.bonds()[0].order == model::BondOrder::single &&
                           topology.bonds()[1].order ==
                               model::BondOrder::double_bond &&
                           topology.bonds()[2].order == model::BondOrder::unknown,
                       "bond orders 1/2 map and unmapped order stays unknown");
      passed &= expect(topology.properties().find("partial_charge") != nullptr &&
                           topology.properties().find("mass") != nullptr &&
                           topology.properties().find("vdw_radius") == nullptr,
                       "only supplied atom fields become properties");
      const auto loss = loss_of(document.value());
      passed &= expect(contains(loss, "duplicate_bonds_dropped:1") &&
                           contains(loss, "bond_order_unmapped:1") &&
                           contains(loss, "velocity") &&
                           contains(loss, "source_atom_serial") &&
                           contains(loss, "atomic_number_inferred_from_name"),
                       "semantic loss must be reported: " + loss);
    }
    const auto budget = registry.read_structure(
        root / "bond_budget.psf", "molfile:psf",
        io::MolfileReadLimits{5'000'000U, 1024U * 1024U * 1024U, 2U});
    passed &= expect(!budget.has_value(),
                     "bond count above the configured budget must fail");
  }

  {
    io::MolfileProviderRegistry registry;
    passed &= expect(registry.discover({{}, {pdb_plugin}}).has_value(),
                     "pdb-like plugin must register");
    const auto document =
        registry.read_structure(root / "model.pdb", "molfile:pdb");
    passed &= expect(document.has_value(), "pdb channel must read");
    if (document.has_value()) {
      const auto& structure = document.value().structures.front();
      const auto& topology = *structure.topology;
      passed &= expect(document.value().format == io::StructureFormat::pdb &&
                           topology.atom_count() == 3U &&
                           topology.residue_count() == 2U,
                       "insertion code must split residues");
      passed &= expect(topology.atoms()[0].atomic_number == 7U &&
                           topology.atoms()[1].atomic_number == 6U &&
                           topology.atoms()[1].alternate_location == "B",
                       "atomic number and altloc must follow the plugin");
      passed &= expect(topology.properties().find("vdw_radius") != nullptr &&
                           topology.properties().find("pqr.radius") == nullptr,
                       "non-PQR radius uses the vdw_radius property");
      const auto frame = structure.coordinates->read_frame(0U);
      passed &= expect(frame.has_value() &&
                           frame.value()->metadata().unit_cell.has_value() &&
                           frame.value()->metadata().atom_properties.count(
                               "occupancy") == 1U &&
                           frame.value()->metadata().atom_properties.count(
                               "b_iso_or_equiv_present") == 1U,
                       "cell, occupancy and B-factor must reach the frame");
      passed &= expect(!contains(loss_of(document.value()),
                                 "atomic_number_inferred_from_name") &&
                           contains(loss_of(document.value()), "physical_time"),
                       "supplied atomic number is not reported as inferred");
    }
    const auto unordered =
        registry.read_structure(root / "rich_no_order.pdb", "molfile:pdb");
    passed &= expect(unordered.has_value() &&
                         contains(loss_of(unordered.value()),
                                  "bond_order_not_provided") &&
                         unordered.value().structures.front().topology->bonds()
                                 .front()
                                 .order == model::BondOrder::unknown,
                     "missing bond order must not be invented");
    for (const auto* name :
         {"bond_fail.pdb", "bond_zero_based.pdb", "bond_negative.pdb"}) {
      const auto result = registry.read_structure(root / name, "molfile:pdb");
      passed &= expect(!result.has_value(),
                       std::string{"invalid bond callback must fail: "} + name);
      passed &= expect(std::filesystem::exists(root / (std::string{name} + ".closed")),
                       std::string{"failed read must close the handle: "} + name);
    }
    const auto oversized =
        registry.read_structure(root / "oversized.pdb", "molfile:pdb");
    passed &= expect(!oversized.has_value(),
                     "oversized atom count must fail before bond callbacks");
  }

  {
    io::MolfileProviderRegistry registry;
    passed &= expect(registry.discover({{}, {xyz_plugin}}).has_value(),
                     "xyz-like plugin must register");
    const auto document =
        registry.read_structure(root / "molecule.xyz", "molfile:xyz");
    passed &= expect(document.has_value(), "xyz channel must read");
    if (document.has_value()) {
      const auto& topology = *document.value().structures.front().topology;
      passed &= expect(topology.residue_count() == 1U &&
                           topology.residues().front().name == "MOL" &&
                           topology.bonds().empty(),
                       "xyz channel gets one synthetic MOL residue and no bonds");
      const auto loss = loss_of(document.value());
      passed &= expect(contains(loss, "residue_identity_synthetic") &&
                           contains(loss, "connectivity_not_provided") &&
                           contains(loss, "atomic_number_inferred_from_name"),
                       "xyz loss report: " + loss);
    }
  }

  {
    io::MolfileProviderRegistry registry;
    passed &= expect(registry.discover({{}, {unprofiled_plugin}}).has_value(),
                     "unprofiled plugin still registers");
    const auto result =
        registry.read_structure(root / "trajectory.dcd", "molfile:hoomd");
    passed &= expect(!result.has_value() &&
                         result.error().code == operation::ErrorCode::unsupported &&
                         !std::filesystem::exists(root / "trajectory.dcd.closed"),
                     "plugin without a structure channel fails before opening input");
  }

  {
    auto workspace = std::make_shared<application::Workspace>();
    auto registry = application::make_default_registry(workspace);
    operation::TaskContext context;
    const auto loaded = registry.invoke(
        "load", {{"file-format", "psf"},
                 {"name", "action-psf"},
                 {"path", (root / "action.psf").string()},
                 {"plugin-path", psf_plugin.string()},
                 {"provider", "molfile:psf"}},
        context);
    passed &= expect(loaded.has_value() && workspace->object_count() == 1U,
                     "shared load action must dispatch molfile:psf");
    operation::TaskContext mismatch_context;
    const auto mismatch = registry.invoke(
        "load", {{"file-format", "gro"},
                 {"name", "mismatch"},
                 {"path", (root / "action.psf").string()},
                 {"plugin-path", psf_plugin.string()},
                 {"provider", "molfile:psf"}},
        mismatch_context);
    passed &= expect(!mismatch.has_value() && workspace->object_count() == 1U,
                     "mismatched --file-format must fail without a Workspace change");
  }

  // Trajectory channels (S2).
  const auto coordinate = [](const model::CoordinateFrame& frame,
                             std::size_t atom, std::size_t axis) {
    const auto& values =
        std::get<std::vector<model::Vec3f>>(frame.positions().values());
    const auto& point = values.at(atom);
    return static_cast<double>(axis == 0U   ? point.x
                               : axis == 1U ? point.y
                                            : point.z);
  };
  const auto open_count = [](const std::filesystem::path& path) {
    std::ifstream input{path.string() + ".opens"};
    std::size_t count{};
    for (std::string line; std::getline(input, line);) ++count;
    return count;
  };
  for (const auto* name :
       {"traj.dcd", "traj_empty.dcd", "traj_mismatch.dcd", "traj_nan.dcd",
        "traj_nocell.dcd", "traj_replay_fail.dcd", "traj_cancel.dcd",
        "traj.crd", "traj_shared.dcd", "traj_budget.dcd", "traj_action.dcd"}) {
    std::error_code ignored;
    std::filesystem::remove(root / (std::string{name} + ".opens"), ignored);
    std::filesystem::remove(root / (std::string{name} + ".fini"), ignored);
  }
  {
    io::MolfileProviderRegistry registry;
    passed &= expect(registry.discover({{}, {dcd_plugin}}).has_value(),
                     "dcd-like plugin without a structure callback must register");
    operation::TaskContext context;
    const auto source = registry.open_trajectory(
        root / "traj.dcd", "molfile:dcd", 3U, context);
    passed &= expect(source.has_value(), "dcd trajectory must open");
    if (source.has_value()) {
      const auto& trajectory = *source.value();
      passed &= expect(trajectory.frame_count() == 5U &&
                           trajectory.atom_count() == 3U &&
                           trajectory.access() == model::FrameAccess::sequential,
                       "scan must count 5 frames and report sequential access");
      const auto frame2 = trajectory.read_frame(2U);
      passed &= expect(frame2.has_value() &&
                           coordinate(*frame2.value(), 0U, 0U) == 200.0 &&
                           coordinate(*frame2.value(), 2U, 2U) == 208.0,
                       "frame 2 must hold the plugin's third timestep");
      if (frame2.has_value()) {
        const auto& metadata = frame2.value()->metadata();
        passed &= expect(metadata.unit_cell.has_value() &&
                             !metadata.physical_time.has_value() &&
                             !frame2.value()->velocities().has_value(),
                         "cell is imported; time and velocity are not invented");
        const auto raw = metadata.fields.find("molfile.physical_time_raw");
        passed &= expect(raw != metadata.fields.end() && raw->second == "1",
                         "raw plugin time must be kept as an unverified field");
      }
      passed &= expect(trajectory.read_frame(4U).has_value(),
                       "forward access must continue the same handle");
      passed &= expect(open_count(root / "traj.dcd") == 2U,
                       "scan plus the first read are the only opens so far");
      const auto back = trajectory.read_frame(1U);
      passed &= expect(back.has_value() &&
                           coordinate(*back.value(), 1U, 1U) == 104.0 &&
                           open_count(root / "traj.dcd") == 3U,
                       "backward access must reopen and skip forward");
      passed &= expect(!trajectory.read_frame(5U).has_value(),
                       "frame index beyond the scan must fail");
      bool has_velocity{};
      bool has_time{};
      for (const auto& entry : source.value()->loss()) {
        has_velocity |= entry == "velocity";
        has_time |= entry == "physical_time_not_imported_unit_unverified";
      }
      passed &= expect(has_velocity && has_time,
                       "trajectory loss must name velocity and time");
      auto first = std::async(std::launch::async,
                              [&] { return trajectory.read_frame(3U); });
      auto second = std::async(std::launch::async,
                               [&] { return trajectory.read_frame(0U); });
      const auto a = first.get();
      const auto b = second.get();
      passed &= expect(a.has_value() && b.has_value() &&
                           coordinate(*a.value(), 0U, 0U) == 300.0 &&
                           coordinate(*b.value(), 0U, 0U) == 0.0,
                       "concurrent reads must serialize on the handle");
    }
    const auto nocell = registry.open_trajectory(
        root / "traj_nocell.dcd", "molfile:dcd", 3U, context);
    const auto nocell_frame =
        nocell.has_value() ? nocell.value()->read_frame(0U)
                           : decltype(nocell.value()->read_frame(0U))::failure(
                                 operation::Error{});
    passed &= expect(nocell_frame.has_value() &&
                         !nocell_frame.value()->metadata().unit_cell.has_value(),
                     "a plugin that never writes a cell must not get one");
    const auto mismatch = registry.open_trajectory(
        root / "traj_mismatch.dcd", "molfile:dcd", 3U, context);
    passed &= expect(!mismatch.has_value() &&
                         mismatch.error().message.find("does not match") !=
                             std::string::npos,
                     "topology atom count mismatch must fail");
    const auto empty = registry.open_trajectory(
        root / "traj_empty.dcd", "molfile:dcd", 3U, context);
    passed &= expect(!empty.has_value(), "a trajectory without frames must fail");
    const auto nan = registry.open_trajectory(
        root / "traj_nan.dcd", "molfile:dcd", 3U, context);
    passed &= expect(nan.has_value() && nan.value()->read_frame(0U).has_value() &&
                         !nan.value()->read_frame(1U).has_value(),
                     "non-finite coordinates must fail only that frame");
    const auto replay = registry.open_trajectory(
        root / "traj_replay_fail.dcd", "molfile:dcd", 3U, context);
    passed &= expect(
        replay.has_value() && replay.value()->read_frame(0U).has_value() &&
            !replay.value()->read_frame(1U).has_value() &&
            replay.value()->read_frame(0U).has_value(),
        "a callback failure must close the handle and allow a clean reopen");
    const auto limited = registry.open_trajectory(
        root / "traj_budget.dcd", "molfile:dcd", 3U, context,
        io::MolfileTrajectoryLimits{5'000'000U, 2U, 1024U * 1024U * 1024U});
    passed &= expect(!limited.has_value(),
                     "frame limit must stop the scan");
    const auto atom_limited = registry.open_trajectory(
        root / "traj_budget.dcd", "molfile:dcd", 3U, context,
        io::MolfileTrajectoryLimits{2U, 100U, 1024U * 1024U * 1024U});
    passed &= expect(!atom_limited.has_value(),
                     "atom limit must reject before opening input");
    operation::TaskContext cancelled;
    cancelled.cancellation.request_cancel();
    const auto pre_cancelled = registry.open_trajectory(
        root / "traj_cancel.dcd", "molfile:dcd", 3U, cancelled);
    passed &= expect(!pre_cancelled.has_value() &&
                         pre_cancelled.error().code ==
                             operation::ErrorCode::cancelled &&
                         open_count(root / "traj_cancel.dcd") == 0U,
                     "pre-cancelled open must not touch the input");
  }
  {
    io::MolfileProviderRegistry registry;
    passed &= expect(registry.discover({{}, {crd_plugin}}).has_value(),
                     "crd-like plugin must register");
    operation::TaskContext context;
    const auto source = registry.open_trajectory(root / "traj.crd",
                                                 "molfile:crd", 3U, context);
    passed &= expect(source.has_value() && source.value()->frame_count() == 5U,
                     "host-supplied atom count must reach the plugin");
    const auto frame = source.has_value() ? source.value()->read_frame(0U)
                                          : decltype(source.value()->read_frame(0U))::failure(
                                                operation::Error{});
    passed &= expect(frame.has_value() &&
                         !frame.value()->metadata().unit_cell.has_value() &&
                         frame.value()->metadata().fields.count(
                             "molfile.physical_time_raw") == 0U,
                     "crd frames have no cell and no raw time");
    passed &= expect(!registry.open_trajectory(root / "traj.crd", "molfile:crd",
                                               0U, context)
                          .has_value(),
                     "zero topology atoms must be rejected");
  }
  {
    io::MolfileProviderRegistry registry;
    passed &= expect(registry.discover({{}, {xyz_plugin}}).has_value(),
                     "xyz-like plugin registers for the profile check");
    operation::TaskContext context;
    const auto result = registry.open_trajectory(
        root / "molecule.xyz", "molfile:xyz", 3U, context);
    passed &= expect(!result.has_value() &&
                         result.error().code == operation::ErrorCode::unsupported,
                     "a name outside the trajectory profiles must be unsupported");
    const auto unknown = registry.open_trajectory(
        root / "molecule.xyz", "molfile:missing", 3U, context);
    passed &= expect(!unknown.has_value(), "unregistered provider must fail");
  }
  {
    std::shared_ptr<const io::MolfileTrajectorySource> kept;
    {
      io::MolfileProviderRegistry registry;
      operation::TaskContext context;
      passed &= expect(registry.discover({{}, {dcd_plugin}}).has_value(),
                       "dcd plugin registers for the lifetime check");
      const auto source = registry.open_trajectory(
          root / "traj_shared.dcd", "molfile:dcd", 3U, context);
      passed &= expect(source.has_value(), "shared trajectory must open");
      if (source.has_value()) kept = source.value();
    }
    passed &= expect(kept != nullptr && kept->read_frame(1U).has_value(),
                     "a live source must keep the plugin library loaded");
    kept.reset();
    passed &= expect(std::filesystem::exists(root / "traj_shared.dcd.fini"),
                     "plugin fini must run after the last source is released");
  }
  {
    auto workspace = std::make_shared<application::Workspace>();
    auto registry = application::make_default_registry(workspace);
    operation::TaskContext context;
    passed &= expect(
        registry
            .invoke("load", {{"file-format", "psf"},
                             {"name", "traj-topology"},
                             {"path", (root / "action.psf").string()},
                             {"plugin-path", psf_plugin.string()},
                             {"provider", "molfile:psf"}},
                    context)
            .has_value(),
        "topology for the trajectory action must load");
    const command::Arguments attach{
        {"file-format", "dcd"},
        {"mapping", "index"},
        {"path", (root / "traj_action.dcd").string()},
        {"plugin-path", dcd_plugin.string()},
        {"provider", "molfile:dcd"}};
    operation::TaskContext unit_context;
    auto unit_arguments = attach;
    unit_arguments["coordinate-unit"] = "nanometer";
    const auto unit_rejected =
        registry.invoke("traj load", unit_arguments, unit_context);
    passed &= expect(!unit_rejected.has_value(),
                     "molfile trajectories must reject a unit override");
    operation::TaskContext format_context;
    auto format_arguments = attach;
    format_arguments["file-format"] = "xtc";
    passed &= expect(
        !registry.invoke("traj load", format_arguments, format_context)
             .has_value(),
        "mismatched --file-format must be rejected for molfile:dcd");
    operation::TaskContext attach_context;
    const auto attached = registry.invoke("traj load", attach, attach_context);
    passed &= expect(attached.has_value(), "traj load must dispatch molfile:dcd");
    if (attached.has_value()) {
      const auto& fields = attached.value().fields;
      passed &= expect(fields.count("molfile_loss") == 1U &&
                           fields.count("provider") == 1U &&
                           std::get<std::uint64_t>(fields.at("frame_count")
                                                       .data) == 5U,
                       "traj load result must report provider, frames and loss");
    }
  }

  // Volume channels (S3).
  for (const auto* name :
       {"vol.dx", "vol.map", "vol_meta_fail.dx", "vol_data_fail.dx",
        "vol_multi.dx", "vol_single_axis.dx", "vol_oversized.dx",
        "vol_nan.dx", "vol_nan_axis.dx", "vol_cancel.dx", "vol_action.dx",
        "vol_named.dx"}) {
    std::error_code ignored;
    std::filesystem::remove(root / (std::string{name} + ".closed"), ignored);
  }
  const auto closed = [&](const char* name) {
    return std::filesystem::exists(root / (std::string{name} + ".closed"));
  };
  {
    io::MolfileProviderRegistry registry;
    passed &= expect(registry.discover({{}, {dx_plugin, ccp4_plugin}}).has_value(),
                     "volume-only plugins must register");
    operation::TaskContext context;
    const auto document = registry.read_volume(root / "vol.dx", "molfile:dx",
                                               context);
    passed &= expect(document.has_value() &&
                         document.value().format == io::VolumeFormat::opendx &&
                         document.value().volumes.size() == 1U &&
                         document.value().volumes.front().name == "vol",
                     "dx channel must read one volume named after the file");
    if (document.has_value()) {
      const auto& grid = *document.value().volumes.front().grid;
      passed &= expect(grid.shape() == model::VolumeShape{2U, 3U, 4U} &&
                           grid.origin().x == 1.0 && grid.origin().y == 2.0 &&
                           grid.origin().z == 3.0,
                       "shape and origin must follow the ABI metadata");
      passed &= expect(grid.deltas()[0].x == 2.0 && grid.deltas()[1].x == 0.25 &&
                           grid.deltas()[1].y == 1.5 && grid.deltas()[2].z == 2.0,
                       "deltas must be axis extent / (count - 1)");
      bool values_match = true;
      for (std::size_t x = 0; x < 2U; ++x) {
        for (std::size_t y = 0; y < 3U; ++y) {
          for (std::size_t z = 0; z < 4U; ++z) {
            values_match &= grid.value(x, y, z) ==
                            static_cast<double>(x + 10U * y + 100U * z);
          }
        }
      }
      passed &= expect(values_match &&
                           grid.scalars().precision() ==
                               model::VolumePrecision::float32,
                       "x-fastest plugin block must map to the z-fastest grid");
      passed &= expect(
          grid.metadata().coordinate_unit == operation::LengthUnit::angstrom &&
              grid.metadata().fields.count("molfile.dataname") == 1U &&
              !grid.metadata().scalar_unit.has_value(),
          "no scalar unit is invented; dataset name is kept");
      const auto position = grid.position(1U, 2U, 3U);
      passed &= expect(position.x == 1.0 + 2.0 + 0.5 &&
                           position.y == 2.0 + 3.0 && position.z == 3.0 + 6.0,
                       "skewed basis must place the last sample at origin + extents");
    }
    const auto ccp4 = registry.read_volume(
        root / "vol.map", "molfile:ccp4", context,
        operation::LengthUnit::nanometer, std::string{"density-map"});
    passed &= expect(ccp4.has_value() &&
                         ccp4.value().format == io::VolumeFormat::mrc &&
                         ccp4.value().volumes.front().name == "density-map" &&
                         ccp4.value().volumes.front().grid->metadata()
                                 .coordinate_unit ==
                             operation::LengthUnit::nanometer,
                     "ccp4 channel maps to mrc and honors name and declared unit");
    passed &= expect(
        !registry.read_volume(root / "vol_named.dx", "molfile:dx", context,
                              operation::LengthUnit::angstrom, std::string{})
             .has_value(),
        "an empty explicit volume name must be rejected");
    for (const auto* name :
         {"vol_meta_fail.dx", "vol_data_fail.dx", "vol_multi.dx",
          "vol_single_axis.dx", "vol_oversized.dx", "vol_nan.dx",
          "vol_nan_axis.dx"}) {
      const auto result =
          registry.read_volume(root / name, "molfile:dx", context);
      passed &= expect(!result.has_value(),
                       std::string{"invalid volume must fail: "} + name);
      passed &= expect(closed(name),
                       std::string{"failed volume read must close: "} + name);
    }
    const auto multi =
        registry.read_volume(root / "vol_multi.dx", "molfile:dx", context);
    const auto single_axis =
        registry.read_volume(root / "vol_single_axis.dx", "molfile:dx", context);
    passed &= expect(!multi.has_value() &&
                         multi.error().code == operation::ErrorCode::unsupported &&
                         !single_axis.has_value() &&
                         single_axis.error().code ==
                             operation::ErrorCode::unsupported,
                     "multi-dataset and singleton-axis grids are explicit unsupported");
    const auto limited = registry.read_volume(
        root / "vol.dx", "molfile:dx", context, operation::LengthUnit::angstrom,
        std::nullopt, io::MolfileVolumeLimits{10U, 1024U * 1024U * 1024U});
    passed &= expect(!limited.has_value(), "value budget must reject the grid");
    operation::TaskContext cancelled;
    cancelled.cancellation.request_cancel();
    const auto pre_cancelled =
        registry.read_volume(root / "vol_cancel.dx", "molfile:dx", cancelled);
    passed &= expect(!pre_cancelled.has_value() &&
                         pre_cancelled.error().code ==
                             operation::ErrorCode::cancelled &&
                         !closed("vol_cancel.dx"),
                     "pre-cancelled volume read must not open the input");
  }
  {
    io::MolfileProviderRegistry registry;
    passed &= expect(registry.discover({{}, {psf_plugin, unprofiled_plugin}})
                         .has_value(),
                     "non-volume plugins register for the profile check");
    operation::TaskContext context;
    const auto structure_only =
        registry.read_volume(root / "vol.dx", "molfile:psf", context);
    const auto unprofiled =
        registry.read_volume(root / "vol.dx", "molfile:hoomd", context);
    passed &= expect(!structure_only.has_value() && !unprofiled.has_value() &&
                         structure_only.error().code ==
                             operation::ErrorCode::unsupported &&
                         unprofiled.error().code ==
                             operation::ErrorCode::unsupported,
                     "plugins outside the volume profiles must be unsupported");
  }
  {
    auto workspace = std::make_shared<application::Workspace>();
    auto registry = application::make_default_registry(workspace);
    operation::TaskContext mismatch_context;
    const auto mismatch = registry.invoke(
        "volume load", {{"file-format", "ccp4"},
                        {"path", (root / "vol_action.dx").string()},
                        {"plugin-path", dx_plugin.string()},
                        {"provider", "molfile:dx"}},
        mismatch_context);
    passed &= expect(!mismatch.has_value() && workspace->list_volumes().empty(),
                     "mismatched --file-format must fail without a Workspace change");
    operation::TaskContext context;
    const auto loaded = registry.invoke(
        "volume load", {{"file-format", "dx"},
                        {"name", "action-volume"},
                        {"path", (root / "vol_action.dx").string()},
                        {"plugin-path", dx_plugin.string()},
                        {"provider", "molfile:dx"}},
        context);
    passed &= expect(loaded.has_value() && workspace->list_volumes().size() == 1U,
                     "volume load must dispatch molfile:dx");
    if (loaded.has_value()) {
      const auto& fields = loaded.value().fields;
      passed &= expect(fields.count("provider") == 1U &&
                           std::get<std::uint64_t>(fields.at("value_count").data) ==
                               24U &&
                           std::get<std::string>(fields.at("format").data) ==
                               "opendx",
                       "volume load result must report provider, count and format");
    }
  }

  // MOL-03 P0 read-channel table: every registration is exercised through its
  // intended channel and rejected through the others.
  {
    struct StructureCase {
      const char* name;
      io::StructureFormat format;
      const char* format_name;
      bool coordinates;
    };
    const StructureCase structure_cases[] = {
        {"pdb", io::StructureFormat::pdb, "pdb", true},
        {"mmcif", io::StructureFormat::mmcif, "mmcif", true},
        {"pdbx", io::StructureFormat::mmcif, "mmcif", true},
        {"pqr", io::StructureFormat::pqr, "pqr", true},
        {"mol2", io::StructureFormat::mol2, "mol2", true},
        {"psf", io::StructureFormat::psf, "psf", false},
        {"parm7", io::StructureFormat::prmtop, "prmtop", false},
        {"gro", io::StructureFormat::gro, "gro", true},
        {"g96", io::StructureFormat::g96, "g96", true},
        {"vtf", io::StructureFormat::vtf, "vtf", true},
        {"vsf", io::StructureFormat::vtf, "vtf", false},
        {"xyz", io::StructureFormat::xyz, "xyz", true},
    };
    const char* trajectory_names[] = {"dcd",   "trr",       "xtc",
                                      "crd",   "crdbox",    "lammpstrj",
                                      "netcdf", "binpos",   "rst7",
                                      "vcf"};
    const char* volume_names[] = {"dx", "ccp4"};
    std::size_t exercised = 0U;
    const auto unsupported = [](const auto& result) {
      return !result.has_value() &&
             result.error().code == operation::ErrorCode::unsupported;
    };
    for (const auto& channel : structure_cases) {
      const auto input = root / (std::string{"p0-"} + channel.name + ".in");
      touch(input);
      io::MolfileProviderRegistry registry;
      const auto provider = std::string{"molfile:"} + channel.name;
      passed &= expect(registry.discover({{}, {plugin(channel.name)}}).has_value(),
                       std::string{"P0 plugin must register: "} + channel.name);
      const auto document = registry.read_structure(input, provider);
      passed &= expect(
          document.has_value() && document.value().format == channel.format &&
              document.value().structures.front().metadata.at("format") ==
                  channel.format_name &&
              document.value().structures.front().coordinates->frame_count() ==
                  (channel.coordinates ? 1U : 0U),
          std::string{"structure channel must follow its profile: "} +
              channel.name);
      operation::TaskContext context;
      passed &= expect(
          unsupported(registry.open_trajectory(input, provider, 3U, context)) &&
              unsupported(registry.read_volume(input, provider, context)),
          std::string{"structure channel must reject other channels: "} +
              channel.name);
      ++exercised;
    }
    for (const auto* name : trajectory_names) {
      const auto input = root / (std::string{"p0-"} + name + ".in");
      touch(input);
      io::MolfileProviderRegistry registry;
      const auto provider = std::string{"molfile:"} + name;
      passed &= expect(registry.discover({{}, {plugin(name)}}).has_value(),
                       std::string{"P0 plugin must register: "} + name);
      operation::TaskContext context;
      const auto source = registry.open_trajectory(input, provider, 3U, context);
      passed &= expect(
          source.has_value() && source.value()->frame_count() == 5U &&
              source.value()->read_frame(4U).has_value() &&
              coordinate(*source.value()->read_frame(4U).value(), 0U, 0U) ==
                  400.0,
          std::string{"trajectory channel must read five frames: "} + name);
      passed &= expect(
          unsupported(registry.read_structure(input, provider)) &&
              unsupported(registry.read_volume(input, provider, context)),
          std::string{"trajectory channel must reject other channels: "} + name);
      ++exercised;
    }
    for (const auto* name : volume_names) {
      const auto input = root / (std::string{"p0-"} + name + ".in");
      touch(input);
      io::MolfileProviderRegistry registry;
      const auto provider = std::string{"molfile:"} + name;
      passed &= expect(registry.discover({{}, {plugin(name)}}).has_value(),
                       std::string{"P0 plugin must register: "} + name);
      operation::TaskContext context;
      const auto document = registry.read_volume(input, provider, context);
      passed &= expect(document.has_value() &&
                           document.value().volumes.front().grid->value_count() ==
                               24U,
                       std::string{"volume channel must read: "} + name);
      passed &= expect(
          unsupported(registry.read_structure(input, provider)) &&
              unsupported(registry.open_trajectory(input, provider, 3U, context)),
          std::string{"volume channel must reject other channels: "} + name);
      ++exercised;
    }
    passed &= expect(exercised == 24U,
                     "MOL-03 scope is 24 P0 read registrations (13 structure/"
                     "mixed incl. vcf as trajectory, 9 trajectory, 2 volume)");
  }

  // MOL-04 disposition (Q-029 a): molfile writers are not adapted. An explicit
  // molfile provider on any save action must fail without fallback or output.
  {
    auto workspace = std::make_shared<application::Workspace>();
    auto registry = application::make_default_registry(workspace);
    operation::TaskContext context;
    passed &= expect(
        registry
            .invoke("load", {{"file-format", "psf"},
                             {"name", "writer-topology"},
                             {"path", (root / "action.psf").string()},
                             {"plugin-path", psf_plugin.string()},
                             {"provider", "molfile:psf"}},
                    context)
            .has_value(),
        "topology for the writer rejection check must load");
    for (const auto& request :
         {std::pair<std::string, std::string>{"save", "writer-out.pdb"},
          {"traj save", "writer-out.dcd"},
          {"volume save", "writer-out.dx"}}) {
      const auto output = root / request.second;
      std::error_code ignored;
      std::filesystem::remove(output, ignored);
      operation::TaskContext save_context;
      const auto result = registry.invoke(
          request.first, {{"path", output.string()},
                          {"provider", "molfile:" + output.extension().string().substr(1)}},
          save_context);
      passed &= expect(!result.has_value() &&
                           result.error().code ==
                               operation::ErrorCode::unsupported &&
                           !std::filesystem::exists(output),
                       "explicit molfile writer provider must be unsupported "
                       "without output: " + request.first);
    }
  }

  return passed ? 0 : 1;
}
