#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string_view>
#include <variant>

#include "molshredder/io/trajectory_reader.hpp"
#include "molshredder/io/trajectory_writer.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

void word(std::ostream &output, std::uint32_t value,
          molshredder::io::ByteOrder order) {
  std::array<unsigned char, 4U> bytes{};
  if (order == molshredder::io::ByteOrder::little_endian) {
    for (std::size_t index = 0U; index < 4U; ++index)
      bytes[index] = static_cast<unsigned char>((value >> (index * 8U)) & 0xffU);
  } else {
    for (std::size_t index = 0U; index < 4U; ++index)
      bytes[index] =
          static_cast<unsigned char>((value >> ((3U - index) * 8U)) & 0xffU);
  }
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

void frame(std::ostream &output, std::int32_t count,
           const std::array<float, 9U> &values,
           molshredder::io::ByteOrder order) {
  word(output, std::bit_cast<std::uint32_t>(count), order);
  for (const auto value : values)
    word(output, std::bit_cast<std::uint32_t>(value), order);
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 3) {
    std::cerr << "expected BINPOS fixture and build directory\n";
    return 2;
  }
  bool passed = true;
  const auto build = std::filesystem::path{argv[2]};
  const auto source = io::open_binpos(argv[1], 3U);
  passed &= expect(source.has_value() && source.value()->atom_count() == 3U &&
                       source.value()->frame_count() == 2U &&
                       source.value()->access() ==
                           model::FrameAccess::random_access &&
                       source.value()->metadata().byte_order ==
                           io::ByteOrder::little_endian,
                   "little-endian BINPOS must expose two indexed frames");
  if (source.has_value()) {
    const auto second = source.value()->read_frame(1U);
    const auto first = source.value()->read_frame(0U);
    passed &= expect(second.has_value() && first.has_value(),
                     "BINPOS frames must decode out of order");
    if (second.has_value()) {
      const auto &positions = std::get<std::vector<model::Vec3f>>(
          second.value()->positions().values());
      passed &= expect(
          positions[0] == model::Vec3f{10.0F, 11.0F, 12.0F} &&
              positions[2] == model::Vec3f{16.0F, 17.0F, 18.0F} &&
              second.value()->metadata().coordinate_unit ==
                  operation::LengthUnit::angstrom &&
              !second.value()->metadata().unit_cell.has_value() &&
              !second.value()->metadata().source_step.has_value(),
          "BINPOS must preserve float32 Å coordinates without invented cell/time");
    }
    passed &= expect(!source.value()->read_frame(2U).has_value(),
                     "BINPOS out-of-range seek must fail");

    if (first.has_value()) {
      io::TrajectoryWriteOptions options;
      options.format = io::TrajectoryFormat::binpos;
      options.title = "not representable";
      operation::TaskContext context;
      const auto serialized =
          io::serialize_trajectory_frame(*first.value(), options, context);
      const auto title_loss =
          serialized.has_value()
              ? std::find_if(serialized.value().report.losses.begin(),
                             serialized.value().report.losses.end(),
                             [](const auto &loss) {
                               return loss.channel == "title";
                             })
              : std::vector<io::TrajectoryFormatLoss>::const_iterator{};
      passed &= expect(
          serialized.has_value() && serialized.value().content.size() == 44U &&
              serialized.value().content.substr(0U, 4U) == "fxyz" &&
              serialized.value().report.format ==
                  io::TrajectoryFormat::binpos &&
              title_loss != serialized.value().report.losses.end(),
          "BINPOS writer must emit one little-endian frame and typed losses");

      const auto roundtrip_path = build / "roundtrip.binpos";
      std::error_code ignored;
      std::filesystem::remove(roundtrip_path, ignored);
      const auto written = io::write_trajectory_frame_file(
          roundtrip_path, *first.value(), options, false, context);
      const auto roundtrip = io::open_binpos(roundtrip_path, 3U);
      const auto copied = roundtrip.has_value()
                              ? roundtrip.value()->read_frame(0U)
                              : operation::Result<std::shared_ptr<const model::CoordinateFrame>>::failure(
                                    roundtrip.error());
      const auto *copied_positions =
          copied.has_value()
              ? std::get_if<std::vector<model::Vec3f>>(
                    &copied.value()->positions().values())
              : nullptr;
      passed &= expect(
          written.has_value() && written.value().byte_count == 44U &&
              roundtrip.has_value() &&
              roundtrip.value()->metadata().byte_order ==
                  io::ByteOrder::little_endian &&
              roundtrip.value()->frame_count() == 1U &&
              copied_positions != nullptr &&
              (*copied_positions)[2] == model::Vec3f{6.0F, 7.0F, 8.0F},
          "BINPOS write-read must preserve current-frame coordinates");
      const auto collision = io::write_trajectory_frame_file(
          roundtrip_path, *first.value(), options, false, context);
      passed &= expect(!collision.has_value(),
                       "BINPOS writer must reject implicit overwrite");

      const auto cancelled_path = build / "cancelled.binpos";
      std::filesystem::remove(cancelled_path, ignored);
      operation::TaskContext cancelled_context;
      cancelled_context.cancellation.request_cancel();
      const auto cancelled = io::write_trajectory_frame_file(
          cancelled_path, *first.value(), options, false, cancelled_context);
      passed &= expect(
          !cancelled.has_value() &&
              cancelled.error().code == operation::ErrorCode::cancelled &&
              !std::filesystem::exists(cancelled_path),
          "cancelled BINPOS export must not publish a partial target");
      std::filesystem::remove(roundtrip_path, ignored);
    }
  }
  const auto auto_open = io::open_trajectory(
      argv[1], io::TrajectoryFormat::auto_detect, 3U);
  passed &= expect(auto_open.has_value() &&
                       auto_open.value().format == io::TrajectoryFormat::binpos,
                   "BINPOS suffix must dispatch through generic trajectory API");
  passed &= expect(!io::open_binpos(argv[1], 4U).has_value(),
                   "BINPOS topology atom-count mismatch must fail");

  constexpr std::array<float, 9U> values{-1.0F, -2.0F, -3.0F, 1.5F, 2.5F,
                                          3.5F, 100.0F, 200.0F, 300.0F};
  const auto big_path = build / "big-endian.binpos";
  {
    std::ofstream output{big_path, std::ios::binary};
    output.write("fxyz", 4);
    frame(output, 3, values, io::ByteOrder::big_endian);
  }
  const auto big = io::open_binpos(big_path, 3U);
  passed &= expect(big.has_value() &&
                       big.value()->metadata().byte_order ==
                           io::ByteOrder::big_endian,
                   "big-endian BINPOS must be detected from topology count");
  if (big.has_value()) {
    const auto decoded = big.value()->read_frame(0U);
    const auto &positions = std::get<std::vector<model::Vec3f>>(
        decoded.value()->positions().values());
    passed &= expect(positions[0] == model::Vec3f{-1.0F, -2.0F, -3.0F} &&
                         positions[2] == model::Vec3f{100.0F, 200.0F, 300.0F},
                     "big-endian float payload must decode exactly");
  }

  const auto varying_path = build / "varying-count.binpos";
  {
    std::ofstream output{varying_path, std::ios::binary};
    output.write("fxyz", 4);
    frame(output, 3, values, io::ByteOrder::little_endian);
    frame(output, 4, values, io::ByteOrder::little_endian);
  }
  passed &= expect(!io::open_binpos(varying_path, 3U).has_value(),
                   "varying BINPOS frame atom count must fail at open");

  const auto nonfinite_path = build / "nonfinite.binpos";
  {
    auto invalid_values = values;
    invalid_values[4] = std::numeric_limits<float>::infinity();
    std::ofstream output{nonfinite_path, std::ios::binary};
    output.write("fxyz", 4);
    frame(output, 3, invalid_values, io::ByteOrder::little_endian);
  }
  const auto nonfinite = io::open_binpos(nonfinite_path, 3U);
  passed &= expect(nonfinite.has_value() &&
                       !nonfinite.value()->read_frame(0U).has_value(),
                   "non-finite BINPOS payload must fail at decode");

  const auto truncated_path = build / "truncated.binpos";
  {
    std::ofstream output{truncated_path, std::ios::binary};
    output.write("fxyz", 4);
    word(output, 3U, io::ByteOrder::little_endian);
    word(output, std::bit_cast<std::uint32_t>(1.0F),
         io::ByteOrder::little_endian);
  }
  passed &= expect(!io::open_binpos(truncated_path, 3U).has_value(),
                   "truncated BINPOS record must fail deterministically");

  model::FrameMetadata nanometer_metadata;
  nanometer_metadata.coordinate_unit = operation::LengthUnit::nanometer;
  const auto nanometer_frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{
          std::vector<model::Vec3d>{{0.1, 0.2, 0.3}}},
      std::nullopt, {}, std::move(nanometer_metadata));
  io::TrajectoryWriteOptions binpos_options;
  binpos_options.format = io::TrajectoryFormat::binpos;
  operation::TaskContext write_context;
  const auto nanometer_path = build / "nanometer.binpos";
  std::error_code ignored;
  std::filesystem::remove(nanometer_path, ignored);
  const auto nanometer_written = io::write_trajectory_frame_file(
      nanometer_path, *nanometer_frame.value(), binpos_options, false,
      write_context);
  const auto nanometer_roundtrip = io::open_binpos(nanometer_path, 1U);
  const auto nanometer_decoded =
      nanometer_roundtrip.has_value()
          ? nanometer_roundtrip.value()->read_frame(0U)
          : operation::Result<
                std::shared_ptr<const model::CoordinateFrame>>::failure(
                nanometer_roundtrip.error());
  const auto precision_loss =
      nanometer_written.has_value()
          ? std::find_if(nanometer_written.value().losses.begin(),
                         nanometer_written.value().losses.end(),
                         [](const auto &loss) {
                           return loss.channel == "coordinate_precision";
                         })
          : std::vector<io::TrajectoryFormatLoss>::const_iterator{};
  passed &= expect(
      nanometer_decoded.has_value() &&
          std::get<std::vector<model::Vec3f>>(
              nanometer_decoded.value()->positions().values())[0] ==
              model::Vec3f{1.0F, 2.0F, 3.0F} &&
          precision_loss != nanometer_written.value().losses.end(),
      "BINPOS writer must convert nm to Å and report float64 narrowing");

  const auto missing_atom_frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{
          std::vector<model::Vec3f>{{0.0F, 0.0F, 0.0F}}},
      std::nullopt, {0U});
  passed &= expect(
      missing_atom_frame.has_value() &&
          !io::serialize_trajectory_frame(*missing_atom_frame.value(),
                                          binpos_options, write_context)
               .has_value(),
      "BINPOS writer must reject a missing atom");
  std::filesystem::remove(big_path, ignored);
  std::filesystem::remove(varying_path, ignored);
  std::filesystem::remove(nonfinite_path, ignored);
  std::filesystem::remove(truncated_path, ignored);
  std::filesystem::remove(nanometer_path, ignored);
  return passed ? 0 : 1;
}
