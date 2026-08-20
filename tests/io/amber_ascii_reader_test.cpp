#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <variant>

#include "molshredder/io/trajectory_reader.hpp"
#include "molshredder/io/trajectory_writer.hpp"
#include "molshredder/model/coordinates.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 3) {
    std::cerr << "expected MDCRD fixture and build directory\n";
    return 2;
  }
  bool passed = true;
  const auto source = io::open_amber_ascii_trajectory(argv[1], 4U, 90.0);
  passed &= expect(source.has_value() && source.value()->atom_count() == 4U &&
                       source.value()->frame_count() == 2U &&
                       source.value()->access() ==
                           model::FrameAccess::random_access &&
                       source.value()->metadata().box_value_count == 3U,
                   "MDCRD must index both fixed-width boxed frames");
  if (source.has_value()) {
    const auto second = source.value()->read_frame(1U);
    const auto first = source.value()->read_frame(0U);
    passed &= expect(second.has_value() && first.has_value(),
                     "MDCRD frames must decode out of order");
    if (second.has_value()) {
      const auto &positions = std::get<std::vector<model::Vec3d>>(
          second.value()->positions().values());
      passed &= expect(
          positions[0].x == 1.5 && positions[3].z == 6.5 &&
              second.value()->metadata().unit_cell.has_value() &&
              std::abs(second.value()->metadata().unit_cell->signed_volume() -
                       1509.375) < 1.0e-9 &&
              second.value()->metadata().fields.at("box_angle_source") ==
                  "prmtop-box-dimensions",
          "MDCRD coordinates and topology-derived box angles must be typed");
    }
    const auto outside = source.value()->read_frame(2U);
    passed &=
        expect(!outside.has_value(), "MDCRD out-of-range frame must fail");

    if (second.has_value()) {
      io::TrajectoryWriteOptions write_options;
      write_options.format = io::TrajectoryFormat::mdcrd;
      write_options.title = "MDCRD writer fixture";
      operation::TaskContext write_context;
      const auto serialized = io::serialize_trajectory_frame(
          *second.value(), write_options, write_context);
      passed &= expect(
          serialized.has_value() &&
              serialized.value().content.starts_with(
                  "MDCRD writer fixture\n   1.500") &&
              serialized.value().report.format ==
                  io::TrajectoryFormat::mdcrd &&
              !serialized.value().report.has_unit_cell &&
              std::any_of(serialized.value().report.losses.begin(),
                          serialized.value().report.losses.end(),
                          [](const io::TrajectoryFormatLoss &loss) {
                            return loss.channel == "unit_cell";
                          }),
          "coordinate-only CRD writer must emit F8.3 and report box loss");

      const auto roundtrip_path =
          std::filesystem::path{argv[2]} / "roundtrip.crd";
      std::error_code ignored;
      std::filesystem::remove(roundtrip_path, ignored);
      io::TrajectoryWriteOptions auto_options;
      auto_options.title = "MDCRD writer fixture";
      const auto written = io::write_trajectory_frame_file(
          roundtrip_path, *second.value(), auto_options, false,
          write_context);
      const auto roundtrip =
          io::open_amber_ascii_trajectory(roundtrip_path, 4U, std::nullopt);
      const auto copied =
          roundtrip.has_value()
              ? roundtrip.value()->read_frame(0U)
              : operation::Result<
                    std::shared_ptr<const model::CoordinateFrame>>::failure(
                    roundtrip.error());
      passed &= expect(
          written.has_value() && roundtrip.has_value() &&
              roundtrip.value()->frame_count() == 1U && copied.has_value() &&
              !copied.value()->metadata().unit_cell.has_value() &&
              std::get<std::vector<model::Vec3d>>(
                  copied.value()->positions().values())[3U].z == 6.5,
          "CRD file write-read must preserve the selected coordinate frame");

      const auto crdbox_path =
          std::filesystem::path{argv[2]} / "roundtrip.crdbox";
      std::filesystem::remove(crdbox_path, ignored);
      io::TrajectoryWriteOptions crdbox_options;
      crdbox_options.format = io::TrajectoryFormat::crdbox;
      crdbox_options.title = "CRDBOX writer fixture";
      const auto crdbox_written = io::write_trajectory_frame_file(
          crdbox_path, *second.value(), crdbox_options, false, write_context);
      const auto crdbox_opened = io::open_trajectory(
          crdbox_path, io::TrajectoryFormat::auto_detect, 4U, 90.0);
      const auto crdbox_frame =
          crdbox_opened.has_value()
              ? crdbox_opened.value().source->read_frame(0U)
              : operation::Result<
                    std::shared_ptr<const model::CoordinateFrame>>::failure(
                    crdbox_opened.error());
      passed &= expect(
          crdbox_written.has_value() &&
              crdbox_written.value().format == io::TrajectoryFormat::crdbox &&
              crdbox_written.value().has_unit_cell &&
              crdbox_path.extension() == ".crdbox" &&
              crdbox_opened.has_value() &&
              crdbox_opened.value().format == io::TrajectoryFormat::crdbox &&
              crdbox_frame.has_value() &&
              crdbox_frame.value()->metadata().unit_cell.has_value() &&
              std::abs(crdbox_frame.value()
                           ->metadata()
                           .unit_cell->signed_volume() -
                       second.value()->metadata().unit_cell->signed_volume()) <
                  1.0e-9 &&
              std::any_of(
                  crdbox_written.value().losses.begin(),
                  crdbox_written.value().losses.end(),
                  [](const io::TrajectoryFormatLoss &loss) {
                    return loss.channel == "unit_cell_angles";
                  }),
          "explicit CRDBOX must preserve box lengths and report external "
          "angle semantics");
      const auto crdbox_without_angle = io::open_amber_ascii_trajectory(
          crdbox_path, 4U, std::nullopt,
          io::AmberAsciiBoxLayout::three_lengths);
      passed &= expect(
          !crdbox_without_angle.has_value() &&
              crdbox_without_angle.error().message.find("BOX_DIMENSIONS") !=
                  std::string::npos,
          "explicit CRDBOX read must require the topology angle");
      if (copied.has_value()) {
        const auto missing_cell = io::serialize_trajectory_frame(
            *copied.value(), crdbox_options, write_context);
        passed &= expect(
            !missing_cell.has_value() &&
                missing_cell.error().message.find("typed unit cell") !=
                    std::string::npos,
            "CRDBOX write must reject a frame without a unit cell");
      }
      auto unequal_metadata = second.value()->metadata();
      unequal_metadata.unit_cell = model::UnitCell{
          {10.0, 0.0, 0.0}, {0.0, 11.0, 0.0}, {1.0, 2.0, 12.0}};
      const auto unequal_frame = model::CoordinateFrame::create(
          model::CoordinateBuffer{
              std::get<std::vector<model::Vec3d>>(
                  second.value()->positions().values())},
          std::nullopt, {}, std::move(unequal_metadata));
      const auto unequal_box =
          unequal_frame.has_value()
              ? io::serialize_trajectory_frame(*unequal_frame.value(),
                                               crdbox_options, write_context)
              : operation::Result<io::SerializedTrajectoryFrame>::failure(
                    unequal_frame.error());
      passed &= expect(
          !unequal_box.has_value() &&
              unequal_box.error().message.find("one shared topology angle") !=
                  std::string::npos,
          "CRDBOX write must reject cells with three unequal angles");
      const auto collision = io::write_trajectory_frame_file(
          roundtrip_path, *second.value(), auto_options, false,
          write_context);
      passed &= expect(!collision.has_value(),
                       "CRD writer must preserve an existing target");

      const auto cancelled_path =
          std::filesystem::path{argv[2]} / "cancelled.crd";
      std::filesystem::remove(cancelled_path, ignored);
      operation::TaskContext cancelled_context;
      cancelled_context.cancellation.request_cancel();
      const auto cancelled = io::write_trajectory_frame_file(
          cancelled_path, *second.value(), auto_options, false,
          cancelled_context);
      passed &= expect(
          !cancelled.has_value() &&
              cancelled.error().code == operation::ErrorCode::cancelled &&
              !std::filesystem::exists(cancelled_path),
          "cancelled CRD export must not publish a partial target");
      std::filesystem::remove(roundtrip_path, ignored);
      std::filesystem::remove(crdbox_path, ignored);
    }
  }

  const auto missing_angle =
      io::open_amber_ascii_trajectory(argv[1], 4U, std::nullopt);
  passed &=
      expect(!missing_angle.has_value() &&
                 missing_angle.error().message.find("BOX_DIMENSIONS") !=
                     std::string::npos,
             "three-value MDCRD box must not invent missing topology angles");
  const auto mismatch = io::open_amber_ascii_trajectory(argv[1], 5U, 90.0);
  passed &= expect(!mismatch.has_value(),
                   "MDCRD topology atom-count mismatch must fail");

  const auto small_path =
      std::filesystem::path{argv[2]} / "ambiguous-small.mdcrd";
  {
    std::ofstream output{small_path};
    output << "small\n"
              "   1.000   2.000   3.000\n"
              "   4.000   5.000   6.000\n";
  }
  const auto small =
      io::open_amber_ascii_trajectory(small_path, 1U, std::nullopt);
  passed &= expect(!small.has_value() &&
                       small.error().code == operation::ErrorCode::unsupported,
                   "small-system frame/box ambiguity must not be guessed");

  const auto malformed_path =
      std::filesystem::path{argv[2]} / "malformed.mdcrd";
  {
    std::ofstream output{malformed_path};
    output << "truncated\n"
              "   1.000   2.000   3.000   4.000\n";
  }
  const auto malformed =
      io::open_amber_ascii_trajectory(malformed_path, 4U, 90.0);
  passed &=
      expect(!malformed.has_value() && malformed.error().message.find(
                                           "requires 10") != std::string::npos,
             "truncated MDCRD coordinate rows must fail deterministically");
  std::error_code ignored;
  std::filesystem::remove(small_path, ignored);
  std::filesystem::remove(malformed_path, ignored);
  return passed ? 0 : 1;
}
