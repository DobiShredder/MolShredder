#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/session.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/command/result.hpp"
#include "molshredder/gui/action_adapter.hpp"
#include "molshredder/gui/analysis_presenter.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/version.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

double numeric(const molshredder::command::Value &value) {
  if (const auto *plain = std::get_if<double>(&value.data))
    return *plain;
  return std::get<molshredder::command::Number>(value.data).value;
}

void append_u32(std::vector<unsigned char> &output, std::uint32_t value) {
  for (unsigned int index = 0; index < 4U; ++index) {
    output.push_back(
        static_cast<unsigned char>((value >> (index * 8U)) & 0xffU));
  }
}

void put_i32(std::vector<unsigned char> &output, std::size_t offset,
             std::int32_t value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  for (unsigned int index = 0; index < 4U; ++index) {
    output[offset + index] =
        static_cast<unsigned char>((bits >> (index * 8U)) & 0xffU);
  }
}

void put_f32(std::vector<unsigned char> &output, std::size_t offset,
             float value) {
  put_i32(output, offset,
          std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value)));
}

void append_record(std::vector<unsigned char> &output,
                   const std::vector<unsigned char> &payload) {
  append_u32(output, static_cast<std::uint32_t>(payload.size()));
  output.insert(output.end(), payload.begin(), payload.end());
  append_u32(output, static_cast<std::uint32_t>(payload.size()));
}

void append_f32(std::vector<unsigned char> &output, float value) {
  append_u32(output, std::bit_cast<std::uint32_t>(value));
}

void write_dcd(const std::filesystem::path &path, std::int32_t atom_count,
               std::int32_t frame_count) {
  std::vector<unsigned char> file;
  std::vector<unsigned char> header(84U, 0U);
  std::memcpy(header.data(), "CORD", 4U);
  put_i32(header, 4U, frame_count);
  put_i32(header, 8U, 100);
  put_i32(header, 12U, 10);
  put_f32(header, 40U, 0.5F);
  put_i32(header, 80U, 24);
  append_record(file, header);
  std::vector<unsigned char> title(84U, static_cast<unsigned char>(' '));
  put_i32(title, 0U, 1);
  constexpr std::string_view text{"MolShredder application trajectory"};
  std::copy(text.begin(), text.end(), title.begin() + 4);
  append_record(file, title);
  std::vector<unsigned char> atoms;
  append_u32(atoms, static_cast<std::uint32_t>(atom_count));
  append_record(file, atoms);
  for (std::int32_t frame = 0; frame < frame_count; ++frame) {
    for (std::int32_t axis = 0; axis < 3; ++axis) {
      std::vector<unsigned char> coordinates;
      for (std::int32_t atom = 0; atom < atom_count; ++atom) {
        append_f32(coordinates,
                   static_cast<float>(frame * 100 + axis * 10 + atom));
      }
      append_record(file, coordinates);
    }
  }
  std::ofstream stream{path, std::ios::binary};
  stream.write(reinterpret_cast<const char *>(file.data()),
               static_cast<std::streamsize>(file.size()));
}

molshredder::application::DispatchOutcome
trigger(const molshredder::gui::ActionAdapter &gui, std::string command,
        molshredder::command::Arguments arguments = {}) {
  molshredder::operation::TaskContext context;
  return gui.trigger({std::move(command), std::move(arguments)}, context);
}

bool wait_for_prefetch(
    const std::shared_ptr<molshredder::trajectory::PrefetchScheduler>
        &scheduler,
    molshredder::trajectory::PrefetchState state) {
  for (std::size_t attempt = 0; attempt < 200U; ++attempt) {
    if (scheduler->snapshot().state == state)
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return false;
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 4) {
    std::cerr
        << "expected PDB fixture, temporary directory, and H-bond fixture\n";
    return 2;
  }
  bool passed = true;
  const std::filesystem::path pdb{argv[1]};
  const std::filesystem::path directory{argv[2]};
  const std::filesystem::path hbond_pdb{argv[3]};
  const auto trajectory_path = directory / "application_trajectory.dcd";
  const auto mismatch_path = directory / "application_mismatch.dcd";
  write_dcd(trajectory_path, 3, 3);
  write_dcd(mismatch_path, 2, 2);

  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  const gui::ActionAdapter gui{dispatcher};
  passed &=
      expect(!trigger(gui, "traj load",
                      {{"path", trajectory_path.string()}, {"cache-mib", "1"}})
                  .succeeded(),
             "trajectory attach must require an active topology");
  passed &=
      expect(trigger(gui, "load", {{"path", pdb.string()}, {"name", "protein"}})
                     .succeeded() &&
                 trigger(gui, "show",
                         {{"representation", "spheres"}, {"selection", "all"}})
                     .succeeded(),
             "structure and representation must load before trajectory attach");
  const auto original_system = workspace->active_object()->system;
  const auto original_cache = workspace->active_object()->trajectory->cache;
  passed &= expect(
      !trigger(gui, "traj load",
               {{"path", mismatch_path.string()}, {"cache-mib", "1"}})
              .succeeded() &&
          workspace->active_object()->trajectory.has_value() &&
          workspace->active_object()->trajectory->cache == original_cache &&
          workspace->active_object()->system == original_system,
      "atom-count mismatch must leave the active object unchanged");

  const auto attached = trigger(gui, "traj load",
                                {{"path", trajectory_path.string()},
                                 {"file-format", "auto"},
                                 {"cache-mib", "1"}});
  const auto *object = workspace->active_object();
  const auto *scene_node = workspace->scene()->find(object->scene_node);
  passed &=
      expect(attached.succeeded() && object->trajectory.has_value() &&
                 object->trajectory->format == io::TrajectoryFormat::dcd &&
                 object->trajectory->timeline.snapshot().frame == 0U &&
                 object->system != original_system && scene_node != nullptr &&
                 scene_node->system() == object->system &&
                 object->representations[0].packet.frame_index == 0U &&
                 object->representations[0].packet.spheres[0].center.x == 0.0,
             "attach must install cache/system/scene and rebuild frame zero");
  passed &= expect(
      object->trajectory->prefetch_frame_count == 4U &&
          wait_for_prefetch(object->trajectory->prefetch,
                            trajectory::PrefetchState::succeeded) &&
          object->trajectory->prefetch->snapshot().frame_indices ==
              std::vector<std::size_t>{1U, 2U},
      "attach must schedule direction-aware read-ahead within the frame count");

  const auto selected = trigger(gui, "traj frame", {{"frame", "2"}});
  object = workspace->active_object();
  passed &= expect(
      selected.succeeded() &&
          object->trajectory->timeline.snapshot().frame == 2U &&
          object->representations[0].packet.frame_index == 2U &&
          object->representations[0].packet.spheres[0].center.x == 200.0,
      "frame seek must decode and transactionally rebuild representations");
  const auto center = trigger(gui, "analyze center",
                              {{"selection", "all"},
                               {"mode", "centroid"},
                               {"precision", "6"},
                               {"unit", "angstrom"}});
  const auto *center_response =
      center.succeeded()
          ? std::get_if<command::Response>(&center.envelope.payload)
          : nullptr;
  const auto *center_position =
      center_response != nullptr
          ? std::get_if<command::Value::Array>(
                &center_response->fields.at("position").data)
          : nullptr;
  passed &=
      expect(center_position != nullptr && center_position->size() == 3U &&
                 std::get<double>((*center_position)[0].data) == 201.0 &&
                 std::get<double>((*center_position)[1].data) == 211.0 &&
                 std::get<double>((*center_position)[2].data) == 221.0,
             "analysis must consume the selected trajectory frame");

  const auto center_series = trigger(gui, "analyze trajectory center",
                                     {{"selection", "all"},
                                      {"mode", "centroid"},
                                      {"first", "0"},
                                      {"last", "2"},
                                      {"stride", "2"},
                                      {"missing", "error"},
                                      {"precision", "3"},
                                      {"unit", "angstrom"}});
  const auto *center_series_response =
      center_series.succeeded()
          ? std::get_if<command::Response>(&center_series.envelope.payload)
          : nullptr;
  passed &= expect(
      center_series_response != nullptr &&
          center_series_response->table.has_value() &&
          center_series_response->table->rows.size() == 2U &&
          std::get<std::uint64_t>(
              center_series_response->table->rows[0][0].data) == 0U &&
          numeric(center_series_response->table->rows[0][4]) == 1.0 &&
          std::get<std::uint64_t>(
              center_series_response->table->rows[1][0].data) == 2U &&
          numeric(center_series_response->table->rows[1][4]) == 201.0 &&
          workspace->active_object()->trajectory->timeline.snapshot().frame ==
              2U,
      "center series must produce a typed strided table without seeking the "
      "viewport");
  const auto center_csv =
      center_series_response != nullptr
          ? command::render(center_series.envelope,
                            operation::OutputFormat::csv)
          : operation::Result<std::string>::failure(operation::Error{});
  passed &= expect(
      center_csv.has_value() &&
          center_csv.value().starts_with(
              "frame,source_step,physical_time,physical_time_unit,x,y,z,") &&
          center_csv.value().find("0,100,,,1,11,21") != std::string::npos,
      "center series must export frame metadata and values as CSV");
  const auto center_presentation =
      gui::make_analysis_presentation(center_series);
  passed &= expect(center_presentation.has_value() &&
                       center_presentation.value().table.has_value() &&
                       center_presentation.value().table->rows.size() == 2U &&
                       center_csv.has_value() &&
                       center_presentation.value().csv == center_csv.value() &&
                       !center_presentation.value().marker.has_value(),
                   "GUI analysis presentation must expose the same table and "
                   "CSV without a scalar marker");

  const auto distance_series = trigger(gui, "analyze trajectory distance",
                                       {{"from", "index 1"},
                                        {"to", "index 3"},
                                        {"pbc", "raw"},
                                        {"first", "0"},
                                        {"last", "2"},
                                        {"stride", "1"},
                                        {"precision", "6"},
                                        {"unit", "nanometer"}});
  const auto *distance_series_response =
      distance_series.succeeded()
          ? std::get_if<command::Response>(&distance_series.envelope.payload)
          : nullptr;
  passed &= expect(distance_series_response != nullptr &&
                       distance_series_response->table.has_value() &&
                       distance_series_response->table->rows.size() == 3U &&
                       numeric(distance_series_response->table->rows[0][9]) ==
                           0.34641 &&
                       workspace->measurements().empty(),
                   "distance series must use the shared kernel without "
                   "creating persistent measurements");
  const auto contact_occupancy = trigger(gui, "analyze trajectory contacts",
                                         {{"selection1", "index 1"},
                                          {"selection2", "index 3"},
                                          {"cutoff", "4"},
                                          {"pbc", "raw"},
                                          {"exclude-bonded", "false"},
                                          {"report", "occupancy"},
                                          {"first", "0"},
                                          {"last", "2"},
                                          {"stride", "1"},
                                          {"precision", "6"},
                                          {"unit", "angstrom"}});
  const auto *contact_occupancy_response =
      contact_occupancy.succeeded()
          ? std::get_if<command::Response>(&contact_occupancy.envelope.payload)
          : nullptr;
  passed &= expect(
      contact_occupancy_response != nullptr &&
          contact_occupancy_response->table.has_value() &&
          contact_occupancy_response->table->rows.size() == 1U &&
          std::get<std::uint64_t>(
              contact_occupancy_response->table->rows[0][2].data) == 3U &&
          numeric(contact_occupancy_response->table->rows[0][3]) == 1.0 &&
          numeric(contact_occupancy_response->table->rows[0][4]) == 3.464102,
      "trajectory contact command must expose deterministic pair occupancy");
  const auto contact_presentation =
      gui::make_analysis_presentation(contact_occupancy);
  passed &= expect(contact_presentation.has_value() &&
                       contact_presentation.value().title ==
                           "Trajectory contact analysis" &&
                       contact_presentation.value().table.has_value(),
                   "GUI presenter must expose trajectory contact occupancy");

  const auto rmsd_series = trigger(gui, "analyze trajectory rmsd",
                                   {{"selection", "all"},
                                    {"reference", "0"},
                                    {"first", "0"},
                                    {"last", "2"},
                                    {"stride", "1"},
                                    {"fit", "none"},
                                    {"weight", "uniform"},
                                    {"missing", "error"},
                                    {"precision", "6"},
                                    {"unit", "angstrom"}});
  const auto *rmsd_response =
      rmsd_series.succeeded()
          ? std::get_if<command::Response>(&rmsd_series.envelope.payload)
          : nullptr;
  passed &= expect(
      rmsd_response != nullptr && rmsd_response->table.has_value() &&
          rmsd_response->table->rows.size() == 3U &&
          numeric(rmsd_response->table->rows[0][5]) == 0.0 &&
          numeric(rmsd_response->table->rows[1][5]) == 173.205081 &&
          std::get<std::string>(
              rmsd_response->fields.at("fit_selection").data) == "all" &&
          workspace->active_object()->trajectory->timeline.snapshot().frame ==
              2U,
      "RMSD command must score raw frames against a reference without moving "
      "the viewport");
  const auto rmsd_presentation = gui::make_analysis_presentation(rmsd_series);
  passed &=
      expect(rmsd_presentation.has_value() &&
                 rmsd_presentation.value().table.has_value() &&
                 rmsd_presentation.value().title == "Trajectory RMSD analysis",
             "GUI presenter must expose the shared RMSD table");
  const auto mass_rmsd = trigger(gui, "analyze trajectory rmsd",
                                 {{"selection", "all"},
                                  {"reference", "0"},
                                  {"first", "0"},
                                  {"last", "0"},
                                  {"fit", "none"},
                                  {"weight", "mass"}});
  const auto *mass_rmsd_response =
      mass_rmsd.succeeded()
          ? std::get_if<command::Response>(&mass_rmsd.envelope.payload)
          : nullptr;
  passed &= expect(
      mass_rmsd_response != nullptr &&
          std::get<std::string>(mass_rmsd_response->fields.at("weight").data) ==
              "mass" &&
          std::get<bool>(
              mass_rmsd_response->fields.at("weight_estimated").data) &&
          std::get<std::string>(
              mass_rmsd_response->fields.at("weight_unit").data) == "dalton",
      "mass-weighted RMSD must expose estimated mass provenance");

  const auto rmsf_series = trigger(gui, "analyze trajectory rmsf",
                                   {{"selection", "all"},
                                    {"reference", "0"},
                                    {"first", "0"},
                                    {"last", "2"},
                                    {"stride", "1"},
                                    {"fit", "none"},
                                    {"weight", "uniform"},
                                    {"missing", "error"},
                                    {"precision", "6"},
                                    {"unit", "angstrom"}});
  const auto *rmsf_response =
      rmsf_series.succeeded()
          ? std::get_if<command::Response>(&rmsf_series.envelope.payload)
          : nullptr;
  passed &= expect(
      rmsf_response != nullptr && rmsf_response->table.has_value() &&
          rmsf_response->table->rows.size() == 3U &&
          numeric(rmsf_response->table->rows[0][9]) == 141.421356 &&
          std::get<std::uint64_t>(rmsf_response->table->rows[0][8].data) == 3U,
      "RMSF command must return per-atom population fluctuation and "
      "observations");
  passed &= expect(
      !trigger(gui, "analyze trajectory rmsd",
               {{"selection", "all"}, {"fit", "rigid"}})
              .succeeded() &&
          workspace->active_object()->trajectory->timeline.snapshot().frame ==
              2U,
      "rigid RMSD must reject the collinear fixture without mutating playback");

  auto hbond_workspace = std::make_shared<application::Workspace>();
  auto hbond_registry = application::make_default_registry(hbond_workspace);
  const application::Dispatcher hbond_dispatcher{hbond_registry};
  const gui::ActionAdapter hbond_gui{hbond_dispatcher};
  passed &= expect(
      trigger(hbond_gui, "load",
              {{"path", hbond_pdb.string()}, {"name", "trajectory_hbond"}})
              .succeeded() &&
          trigger(hbond_gui, "traj load",
                  {{"path", trajectory_path.string()},
                   {"file-format", "dcd"},
                   {"cache-mib", "1"},
                   {"prefetch-frames", "0"}})
              .succeeded(),
      "trajectory H-bond fixture must load");
  const auto hbond_occupancy = trigger(hbond_gui, "analyze trajectory hbonds",
                                       {{"donors", "chain A"},
                                        {"acceptors", "chain B"},
                                        {"cutoff", "4"},
                                        {"angle", "30"},
                                        {"pbc", "raw"},
                                        {"report", "occupancy"},
                                        {"first", "0"},
                                        {"last", "2"},
                                        {"stride", "1"},
                                        {"precision", "6"},
                                        {"unit", "angstrom"}});
  const auto *hbond_occupancy_response =
      hbond_occupancy.succeeded()
          ? std::get_if<command::Response>(&hbond_occupancy.envelope.payload)
          : nullptr;
  passed &=
      expect(hbond_occupancy_response != nullptr &&
                 hbond_occupancy_response->table.has_value() &&
                 hbond_occupancy_response->table->rows.size() == 1U &&
                 std::get<std::uint64_t>(
                     hbond_occupancy_response->table->rows[0][3].data) == 3U &&
                 numeric(hbond_occupancy_response->table->rows[0][4]) == 1.0 &&
                 std::get<std::string>(
                     hbond_occupancy_response->fields.at("donor_typing_source")
                         .data) == "element-bond-v1",
             "trajectory H-bond command must expose triple occupancy and "
             "typing provenance");
  const auto hbond_presentation =
      gui::make_analysis_presentation(hbond_occupancy);
  passed &= expect(hbond_presentation.has_value() &&
                       hbond_presentation.value().title ==
                           "Trajectory hydrogen-bond analysis" &&
                       hbond_presentation.value().table.has_value(),
                   "GUI presenter must expose trajectory H-bond occupancy");

  const auto invalid = trigger(gui, "traj frame", {{"frame", "99"}});
  passed &= expect(
      !invalid.succeeded() &&
          workspace->active_object()->trajectory->timeline.snapshot().frame ==
              2U &&
          workspace->active_object()->representations[0].packet.frame_index ==
              2U,
      "failed seek must preserve timeline and representation state");
  const auto played =
      trigger(gui, "traj play",
              {{"mode", "rock"}, {"direction", "reverse"}, {"steps", "1"}});
  object = workspace->active_object();
  passed &= expect(
      played.succeeded() &&
          object->trajectory->timeline.snapshot().frame == 1U &&
          object->trajectory->timeline.snapshot().playing &&
          object->representations[0].packet.frame_index == 1U &&
          object->representations[0].packet.spheres[0].center.x == 100.0,
      "play command must advance timeline and visible representation together");

  const auto ranged = trigger(gui, "traj range",
                              {{"first", "0"},
                               {"last", "2"},
                               {"stride", "2"},
                               {"mode", "loop"},
                               {"direction", "reverse"}});
  object = workspace->active_object();
  passed &= expect(
      ranged.succeeded() &&
          object->trajectory->timeline.sequence() ==
              std::vector<std::size_t>{0U, 2U} &&
          object->trajectory->timeline.snapshot().frame == 2U &&
          !object->trajectory->timeline.snapshot().playing &&
          object->representations[0].packet.frame_index == 2U,
      "range must normalize stride and start paused at direction endpoint");
  passed &= expect(object->trajectory->prefetch->snapshot().frame_indices ==
                       std::vector<std::size_t>{0U, 2U},
                   "range direction change must supersede prefetch with the "
                   "new visit order");
  const auto speed = trigger(gui, "traj speed", {{"fps", "4"}});
  const auto started =
      trigger(gui, "traj play",
              {{"mode", "loop"}, {"direction", "reverse"}, {"steps", "0"}});
  const auto half_tick = trigger(gui, "traj tick", {{"elapsed-ms", "125"}});
  passed &= expect(
      speed.succeeded() && started.succeeded() && half_tick.succeeded() &&
          workspace->active_object()->trajectory->timeline.snapshot().frame ==
              2U &&
          workspace->active_object()->trajectory->clock.pending_transitions() ==
              0.5,
      "wall clock must preserve a sub-frame fractional tick");
  const auto full_tick = trigger(gui, "traj tick", {{"elapsed-ms", "125"}});
  object = workspace->active_object();
  passed &= expect(
      full_tick.succeeded() &&
          object->trajectory->timeline.snapshot().frame == 0U &&
          object->trajectory->clock.pending_transitions() == 0.0 &&
          object->representations[0].packet.frame_index == 0U &&
          object->representations[0].packet.spheres[0].center.x == 0.0,
      "accumulated wall time must advance timeline and representation once");
  const auto paused = trigger(gui, "traj pause");
  const auto paused_tick = trigger(gui, "traj tick", {{"elapsed-ms", "1000"}});
  passed &= expect(
      paused.succeeded() && paused_tick.succeeded() &&
          !workspace->active_object()
               ->trajectory->timeline.snapshot()
               .playing &&
          workspace->active_object()->trajectory->timeline.snapshot().frame ==
              0U &&
          workspace->active_object()->trajectory->clock.pending_transitions() ==
              0.0,
      "pause must reset fractional time and ignore subsequent valid elapsed "
      "time");
  const auto before_invalid_sequence =
      workspace->active_object()->trajectory->timeline.sequence();
  passed &= expect(
      !trigger(gui, "traj range", {{"stride", "0"}}).succeeded() &&
          !trigger(gui, "traj speed", {{"fps", "0"}}).succeeded() &&
          !trigger(gui, "traj tick", {{"elapsed-ms", "-1"}}).succeeded() &&
          workspace->active_object()->trajectory->timeline.sequence() ==
              before_invalid_sequence,
      "invalid range/speed/tick values must preserve playback state");

  passed &= expect(
      trigger(gui, "traj range",
              {{"first", "0"},
               {"last", "2"},
               {"stride", "1"},
               {"mode", "loop"},
               {"direction", "forward"}})
              .succeeded() &&
          trigger(gui, "traj speed", {{"fps", "1000"}}).succeeded() &&
          trigger(gui, "traj play",
                  {{"mode", "loop"}, {"direction", "forward"}, {"steps", "0"}})
              .succeeded(),
      "catch-up test playback must configure");
  const auto catch_up = trigger(gui, "traj tick", {{"elapsed-ms", "1000"}});
  passed &= expect(
      catch_up.succeeded() &&
          workspace->active_object()->trajectory->timeline.snapshot().frame ==
              0U &&
          workspace->active_object()->trajectory->clock.pending_transitions() ==
              880.0,
      "wall-clock catch-up must cap one tick at 120 transitions and retain "
      "backlog");

  passed &= expect(
      trigger(gui, "traj range",
              {{"first", "0"},
               {"last", "2"},
               {"stride", "1"},
               {"mode", "once"},
               {"direction", "forward"}})
              .succeeded() &&
          trigger(gui, "traj speed", {{"fps", "30"}}).succeeded() &&
          trigger(gui, "traj play",
                  {{"mode", "once"}, {"direction", "forward"}, {"steps", "0"}})
              .succeeded(),
      "decode-failure test playback must configure");
  workspace->active_object()->trajectory->cache->clear();
  const auto missing_path = trajectory_path.string() + ".temporarily-missing";
  std::filesystem::rename(trajectory_path, missing_path);
  const auto failed_tick = trigger(gui, "traj tick", {{"elapsed-ms", "34"}});
  std::filesystem::rename(missing_path, trajectory_path);
  passed &= expect(
      !failed_tick.succeeded() &&
          workspace->active_object()->trajectory->timeline.snapshot().frame ==
              0U &&
          workspace->active_object()->trajectory->clock.pending_transitions() ==
              0.0 &&
          workspace->active_object()->representations[0].packet.frame_index ==
              0U,
      "tick decode failure must preserve clock/timeline/representation state");
  passed &= expect(
      !trigger(gui, "traj play", {{"steps", "-1"}}).succeeded() &&
          !trigger(gui, "traj load",
                   {{"path", trajectory_path.string()}, {"cache-mib", "0"}})
               .succeeded(),
      "negative steps and zero cache budget must fail validation");

  application::SessionDocument session;
  session.generator_version = std::string{version()};
  session.invocations = {
      {"load",
       {{"file-format", "pdb"}, {"name", "replayed"}, {"path", pdb.string()}}},
      {"show", {{"representation", "spheres"}, {"selection", "all"}}},
      {"traj load",
       {{"cache-mib", "1"},
        {"file-format", "dcd"},
        {"path", trajectory_path.string()}}},
      {"traj range",
       {{"direction", "reverse"},
        {"first", "0"},
        {"last", "2"},
        {"mode", "loop"},
        {"stride", "2"}}},
      {"traj speed", {{"fps", "4"}}},
      {"traj play",
       {{"direction", "reverse"}, {"mode", "loop"}, {"steps", "0"}}},
      {"traj tick", {{"elapsed-ms", "250"}}},
      {"analyze trajectory distance",
       {{"first", "0"},
        {"from", "index 1"},
        {"last", "2"},
        {"pbc", "raw"},
        {"precision", "6"},
        {"stride", "1"},
        {"to", "index 3"},
        {"unit", "angstrom"}}},
      {"analyze trajectory rmsd",
       {{"first", "0"},
        {"fit", "none"},
        {"last", "2"},
        {"missing", "error"},
        {"precision", "6"},
        {"reference", "0"},
        {"selection", "all"},
        {"stride", "1"},
        {"unit", "angstrom"},
        {"weight", "uniform"}}},
      {"traj pause", {}}};
  const auto serialized = application::serialize_session(session);
  const auto parsed =
      serialized.has_value()
          ? application::parse_session(serialized.value())
          : operation::Result<application::SessionDocument>::failure(
                serialized.error());
  auto replayed_workspace = std::make_shared<application::Workspace>();
  auto replayed_registry =
      application::make_default_registry(replayed_workspace);
  const application::Dispatcher replayed_dispatcher{replayed_registry};
  operation::TaskContext replay_context;
  const auto replayed =
      parsed.has_value()
          ? application::replay_session(parsed.value(), replayed_dispatcher,
                                        replay_context)
          : operation::Result<application::SessionReplayResult>::failure(
                parsed.error());
  passed &= expect(
      serialized.has_value() && parsed.has_value() && replayed.has_value() &&
          replayed.value().applied_count == 10U &&
          replayed_workspace->active_object()
                  ->trajectory->timeline.snapshot()
                  .frame == 0U &&
          !replayed_workspace->active_object()
               ->trajectory->timeline.snapshot()
               .playing &&
          replayed_workspace->active_object()
                  ->representations[0]
                  .packet.spheres[0]
                  .center.x == 0.0,
      "canonical session replay must reconstruct range/clock/frame state");

  return passed ? 0 : 1;
}
