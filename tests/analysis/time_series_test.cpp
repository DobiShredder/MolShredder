#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "molshredder/analysis/time_series.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/operation/error.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

std::shared_ptr<const molshredder::model::CoordinateFrame> frame(
    double offset, std::uint64_t step, bool second_present = true) {
  using namespace molshredder;
  model::FrameMetadata metadata;
  metadata.source_step = step;
  metadata.physical_time =
      model::PhysicalTime{offset * 0.5, model::TimeUnit::picosecond};
  const auto created = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::vector<model::Vec3d>{
          {offset, offset, offset}, {offset + 2.0, offset, offset}}},
      std::nullopt,
      {1U, static_cast<std::uint8_t>(second_present ? 1U : 0U)}, metadata);
  return created.value();
}

std::shared_ptr<const molshredder::model::CoordinateFrame> interaction_frame(
    double acceptor_x, std::uint64_t step,
    molshredder::operation::LengthUnit unit =
        molshredder::operation::LengthUnit::angstrom,
    double hydrogen_x = 1.0) {
  using namespace molshredder;
  model::FrameMetadata metadata; metadata.source_step=step; metadata.coordinate_unit=unit;
  return model::CoordinateFrame::create(model::CoordinateBuffer{
      std::vector<model::Vec3d>{{0,0,0},{hydrogen_x,0,0},{acceptor_x,0,0}}},
      std::nullopt,{},metadata).value();
}

std::shared_ptr<const molshredder::model::Topology> interaction_topology() {
  using namespace molshredder;
  model::TopologyBuilder builder;
  const auto residue=builder.add_residue({"MOL",1,"","A",""}).value();
  (void)builder.add_atom({"N",7,residue,"",0,1});
  (void)builder.add_atom({"H",1,residue,"",0,2});
  (void)builder.add_atom({"O",8,residue,"",0,3});
  (void)builder.add_bond({model::AtomIndex{0},model::AtomIndex{1},model::BondOrder::single});
  return builder.build().value();
}

}  // namespace

int main() {
  using namespace molshredder;
  bool passed = true;
  const auto source = model::InMemoryCoordinateSource::create(
      2U, {frame(0.0, 10U), frame(1.0, 20U), frame(2.0, 30U, false)});
  passed &= expect(source.has_value(), "time-series fixture must build");

  std::vector<std::uint8_t> selected{1U, 1U};
  operation::TaskContext context;
  double last_progress{};
  std::size_t progress_count{};
  context.report_progress = [&](const operation::ProgressUpdate& update) {
    last_progress = update.fraction;
    ++progress_count;
  };
  analysis::CenterSeriesRequest centers;
  centers.source = source.value();
  centers.range = {0U, 2U, 2U};
  centers.selected = selected;
  centers.missing_atom_policy = analysis::MissingAtomPolicy::skip;
  const auto center_rows = analysis::center_series(centers, context);
  passed &= expect(
      center_rows.has_value() && center_rows.value().size() == 2U &&
          center_rows.value()[0].frame.frame_index == 0U &&
          center_rows.value()[0].frame.source_step == 10U &&
          center_rows.value()[0].frame.physical_time == 0.0 &&
          center_rows.value()[0].center.position.x == 1.0 &&
          center_rows.value()[1].frame.frame_index == 2U &&
          center_rows.value()[1].center.position.x == 2.0 &&
          center_rows.value()[1].center.skipped_missing_atom_count == 1U &&
          progress_count == 2U && last_progress == 1.0,
      "center series must honor inclusive stride, metadata, skip, and progress");

  std::vector<double> masses{1.0, 3.0};
  analysis::CenterSeriesRequest com;
  com.source = source.value();
  com.range = {0U, 0U, 1U};
  com.selected = selected;
  com.mode = analysis::CenterMode::center_of_mass;
  com.masses = masses;
  com.mass_unit = "dalton";
  com.mass_source = "fixture";
  const auto com_rows = analysis::center_series(com, context);
  passed &= expect(com_rows.has_value() && com_rows.value().size() == 1U &&
                       com_rows.value()[0].center.position.x == 1.5 &&
                       com_rows.value()[0].center.total_mass == 4.0 &&
                       com_rows.value()[0].center.mass_source == "fixture",
                   "COM series must reuse mass-weighted center provenance");

  operation::TaskContext distance_context;
  const auto distances = analysis::distance_series(
      analysis::DistanceSeriesRequest{source.value(), {0U, 1U, 1U},
                                      model::AtomIndex{0U},
                                      model::AtomIndex{1U},
                                      analysis::DistanceBoundary::raw},
      distance_context);
  passed &= expect(distances.has_value() && distances.value().size() == 2U &&
                       distances.value()[0].distance.distance == 2.0 &&
                       distances.value()[1].distance.distance == 2.0,
                   "distance series must calculate every requested frame");

  operation::TaskContext cancelled_context;
  cancelled_context.cancellation.request_cancel();
  const auto cancelled = analysis::center_series(centers, cancelled_context);
  passed &= expect(!cancelled.has_value() &&
                       cancelled.error().code ==
                           operation::ErrorCode::cancelled,
                   "pre-cancelled series must not return a partial success");

  auto invalid = centers;
  invalid.range.stride = 0U;
  const auto invalid_result = analysis::center_series(invalid, context);
  passed &= expect(!invalid_result.has_value() &&
                       invalid_result.error().code ==
                           operation::ErrorCode::invalid_argument,
                   "zero-stride series must fail deterministically");

  const auto alignment_source = model::InMemoryCoordinateSource::create(
      4U,
      {model::CoordinateFrame::create(model::CoordinateBuffer{
           std::vector<model::Vec3d>{{5.0, -2.0, 1.0}, {5.0, -1.0, 1.0},
                                     {3.0, -2.0, 1.0}, {5.0, -2.0, 4.0}}})
           .value(),
       model::CoordinateFrame::create(model::CoordinateBuffer{
           std::vector<model::Vec3d>{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
                                     {0.0, 2.0, 0.0}, {0.0, 0.0, 3.0}}})
           .value(),
       model::CoordinateFrame::create(model::CoordinateBuffer{
           std::vector<model::Vec3d>{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
                                     {0.0, 2.0, 0.0}, {0.5, 0.0, 3.0}}})
           .value()});
  const std::vector<std::uint8_t> alignment_all{1U, 1U, 1U, 1U};
  const std::vector<std::uint8_t> fit_three{1U, 1U, 1U, 0U};
  operation::TaskContext rmsd_context;
  const auto rmsd = analysis::rmsd_series(
      analysis::RmsdSeriesRequest{
          alignment_source.value(), 0U, {0U, 2U, 1U}, alignment_all,
          fit_three, {}, analysis::FitMode::rigid,
          analysis::MissingAtomPolicy::error},
      rmsd_context);
  passed &= expect(
      rmsd.has_value() && rmsd.value().size() == 3U &&
          rmsd.value()[0].rmsd.rmsd < 1.0e-10 &&
          rmsd.value()[1].rmsd.rmsd < 1.0e-10 &&
          std::abs(rmsd.value()[2].rmsd.rmsd - 0.25) < 1.0e-10 &&
          rmsd.value()[2].rmsd_before_fit > 4.0 &&
          rmsd.value()[2].fit_paired_atom_count == 3U,
      "RMSD series must fit on one selection and score another selection");

  operation::TaskContext rmsf_context;
  const auto rmsf = analysis::rmsf_series(
      analysis::RmsfSeriesRequest{
          alignment_source.value(), 0U, {0U, 2U, 1U}, alignment_all,
          fit_three, {}, analysis::FitMode::rigid,
          analysis::MissingAtomPolicy::error},
      rmsf_context);
  passed &= expect(
      rmsf.has_value() && rmsf.value().frame_count == 3U &&
          rmsf.value().atoms.size() == 4U &&
          rmsf.value().atoms[0].rmsf.value_or(1.0) < 1.0e-10 &&
          std::abs(rmsf.value().atoms[3].rmsf.value_or(0.0) -
                   std::sqrt(1.0 / 18.0)) < 1.0e-10 &&
          rmsf.value().atoms[3].observation_count == 3U,
      "RMSF must use aligned online per-atom population variance");

  operation::TaskContext skipped_rmsf_context;
  const auto skipped_rmsf = analysis::rmsf_series(
      analysis::RmsfSeriesRequest{
          source.value(), 0U, {0U, 2U, 1U}, selected, selected, {},
          analysis::FitMode::none, analysis::MissingAtomPolicy::skip},
      skipped_rmsf_context);
  passed &= expect(
      skipped_rmsf.has_value() && skipped_rmsf.value().atoms.size() == 2U &&
          skipped_rmsf.value().atoms[0].observation_count == 3U &&
          skipped_rmsf.value().atoms[1].observation_count == 2U &&
          std::abs(skipped_rmsf.value().atoms[1].rmsf.value_or(0.0) -
                   std::sqrt(0.75)) < 1.0e-10,
      "RMSF skip policy must retain per-atom observation counts");

  operation::TaskContext cancelled_rmsd_context;
  cancelled_rmsd_context.cancellation.request_cancel();
  const auto cancelled_rmsd = analysis::rmsd_series(
      analysis::RmsdSeriesRequest{
          alignment_source.value(), 0U, {0U, 2U, 1U}, alignment_all,
          fit_three, {}, analysis::FitMode::rigid,
          analysis::MissingAtomPolicy::error},
      cancelled_rmsd_context);
  passed &= expect(!cancelled_rmsd.has_value() &&
                       cancelled_rmsd.error().code ==
                           operation::ErrorCode::cancelled,
                   "pre-cancelled RMSD must not return a partial table");

  const auto interaction_source=model::InMemoryCoordinateSource::create(3U,
      {interaction_frame(2.8,100),interaction_frame(4.0,200),interaction_frame(3.0,300)});
  const auto interaction_topology_value=interaction_topology();
  const std::vector<std::uint8_t> donor_selection{1,0,0};
  const std::vector<std::uint8_t> acceptor_selection{0,0,1};
  operation::TaskContext contact_context;
  const auto contacts=analysis::contact_series({interaction_source.value(),
      interaction_topology_value.get(),{0,2,1},donor_selection,acceptor_selection,
      3.5,operation::LengthUnit::angstrom,analysis::DistanceBoundary::raw,false,false},
      contact_context);
  passed &= expect(contacts.has_value() && contacts.value().frame_count==3U &&
      contacts.value().frames[0].interaction_count==1U &&
      contacts.value().frames[1].interaction_count==0U &&
      contacts.value().occupancy.size()==1U &&
      contacts.value().occupancy[0].observation_count==2U &&
      std::abs(contacts.value().occupancy[0].occupancy-2.0/3.0)<1e-12 &&
      std::abs(contacts.value().occupancy[0].mean_distance-2.9)<1e-12,
      "contact series must return frame counts and pair occupancy");
  operation::TaskContext counts_only_context;
  const auto counts_only=analysis::contact_series({interaction_source.value(),
      interaction_topology_value.get(),{0,2,1},donor_selection,acceptor_selection,
      3.5,operation::LengthUnit::angstrom,analysis::DistanceBoundary::raw,false,false,false},
      counts_only_context);
  passed &= expect(counts_only.has_value() && counts_only.value().frames.size()==3U &&
      counts_only.value().occupancy.empty(),
      "frame-count report must avoid retaining occupancy aggregates");
  const auto typing=analysis::resolve_hydrogen_bond_typing(*interaction_topology_value);
  operation::TaskContext hbond_context;
  const auto hbonds=analysis::hydrogen_bond_series({interaction_source.value(),
      interaction_topology_value.get(),{0,2,1},donor_selection,acceptor_selection,
      typing.value().donors,typing.value().acceptors,3.5,
      operation::LengthUnit::angstrom,30.0,analysis::DistanceBoundary::raw,false},
      hbond_context);
  passed &= expect(hbonds.has_value() && hbonds.value().frames.size()==3U &&
      hbonds.value().frames[1].interaction_count==0U &&
      hbonds.value().occupancy.size()==1U &&
      hbonds.value().occupancy[0].observation_count==2U &&
      std::abs(hbonds.value().occupancy[0].mean_angle_deviation_degrees)<1e-12,
      "H-bond series must return frame counts and triple occupancy");
  operation::TaskContext cancelled_contacts;
  cancelled_contacts.cancellation.request_cancel();
  const auto cancelled_contact=analysis::contact_series({interaction_source.value(),
      interaction_topology_value.get(),{0,2,1},donor_selection,acceptor_selection,
      3.5,operation::LengthUnit::angstrom,analysis::DistanceBoundary::raw,false,false},
      cancelled_contacts);
  passed &= expect(!cancelled_contact.has_value() &&
      cancelled_contact.error().code==operation::ErrorCode::cancelled,
      "pre-cancelled contact series must not return partial results");
  const auto nanometer_source=model::InMemoryCoordinateSource::create(3U,
      {interaction_frame(0.28,400,operation::LengthUnit::nanometer,0.1)});
  operation::TaskContext unit_context;
  const auto unit_contacts=analysis::contact_series({nanometer_source.value(),
      interaction_topology_value.get(),{0,0,1},donor_selection,acceptor_selection,
      3.5,operation::LengthUnit::angstrom,analysis::DistanceBoundary::raw,false,false},
      unit_context);
  passed &= expect(unit_contacts.has_value() && unit_contacts.value().occupancy.size()==1U &&
      std::abs(unit_contacts.value().occupancy[0].mean_distance-2.8)<1e-12,
      "contact series must convert cutoff and distance units per frame");
  operation::TaskContext cancelled_hbonds;
  cancelled_hbonds.cancellation.request_cancel();
  const auto cancelled_hbond=analysis::hydrogen_bond_series({interaction_source.value(),
      interaction_topology_value.get(),{0,2,1},donor_selection,acceptor_selection,
      typing.value().donors,typing.value().acceptors,3.5,
      operation::LengthUnit::angstrom,30.0,analysis::DistanceBoundary::raw,false},
      cancelled_hbonds);
  passed &= expect(!cancelled_hbond.has_value() &&
      cancelled_hbond.error().code==operation::ErrorCode::cancelled,
      "pre-cancelled H-bond series must not return partial results");

  return passed ? 0 : 1;
}
