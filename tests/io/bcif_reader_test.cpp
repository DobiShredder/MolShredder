#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "molshredder/io/structure_reader.hpp"
#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

std::string read_bytes(const std::filesystem::path &path) {
  std::ifstream stream{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream},
          std::istreambuf_iterator<char>{}};
}

const std::vector<molshredder::model::Vec3d> &
positions(const molshredder::model::CoordinateFrame &frame) {
  return std::get<std::vector<molshredder::model::Vec3d>>(
      frame.positions().values());
}

} // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 3) {
    std::cerr << "expected valid and old-version BCIF fixture paths\n";
    return 2;
  }

  bool passed = true;
  const auto loaded = io::read_structure_file(argv[1]);
  passed &= expect(loaded.has_value(),
                   loaded.has_value() ? "" : loaded.error().message);
  if (loaded.has_value()) {
    const auto &document = loaded.value();
    passed &= expect(document.format == io::StructureFormat::bcif &&
                         document.structures.size() == 1U,
                     "BCIF extension must select one BinaryCIF data block");
    const auto &structure = document.structures.front();
    const auto first = structure.coordinates->read_frame(0U);
    const auto second = structure.coordinates->read_frame(1U);
    const auto *charge_presence =
        structure.topology->properties().find("formal_charge_present");
    passed &= expect(
        structure.name == "BCIF_TEST" &&
            structure.topology->atom_count() == 2U &&
            structure.topology->residue_count() == 1U &&
            structure.topology->bonds().size() == 1U &&
            structure.topology->bonds()[0].order == model::BondOrder::single &&
            structure.coordinates->frame_count() == 2U,
        "BCIF atom_site and struct_conn must build multi-model topology");
    passed &= expect(
        first.has_value() && second.has_value() &&
            positions(*first.value())[1] == model::Vec3d{1.2, 0.0, 0.0} &&
            positions(*second.value())[0] == model::Vec3d{0.1, 0.0, 0.0} &&
            second.value()->metadata().source_step == 2U &&
            first.value()->metadata().unit_cell.has_value() &&
            std::abs(first.value()->metadata().unit_cell->b.y - 11.0) < 1e-9,
        "FixedPoint/Delta coordinates, RLE model and cell must decode");
    passed &= expect(
        charge_presence != nullptr &&
            std::get<model::BooleanColumn>(charge_presence->values).values ==
                std::vector<std::uint8_t>({1U, 0U}) &&
            structure.metadata.at("bcif.version") == "0.3.0" &&
            structure.metadata.at("bcif.encoder") ==
                "MolShredder synthetic fixture" &&
            structure.metadata.at("_molshredder_test.packed_300") == "300",
        "BCIF mask, encoder metadata and IntegerPacking continuation must "
        "decode");
  }

  const auto old_version =
      io::read_structure_file(argv[2], io::StructureFormat::bcif);
  passed &= expect(!old_version.has_value() &&
                       old_version.error().message.find(
                           "unsupported BinaryCIF") != std::string::npos,
                   "pre-0.3 BinaryCIF must fail with a version error");

  auto unknown_encoding = read_bytes(argv[1]);
  const auto encoding_position = unknown_encoding.find("ByteArray");
  if (encoding_position != std::string::npos)
    unknown_encoding.replace(encoding_position, 9U, "ByteArrzz");
  const auto unknown = io::read_structure(
      unknown_encoding, {io::StructureFormat::bcif, "unknown.bcif"});
  passed &= expect(!unknown.has_value() &&
                       unknown.error().message.find("unsupported encoding") !=
                           std::string::npos,
                   "unknown BinaryCIF encoding must not be ignored");

  auto truncated = read_bytes(argv[1]);
  if (!truncated.empty())
    truncated.pop_back();
  const auto truncated_result = io::read_structure(
      truncated, {io::StructureFormat::bcif, "truncated.bcif"});
  passed &= expect(!truncated_result.has_value() &&
                       truncated_result.error().message.find("MessagePack") !=
                           std::string::npos,
                   "truncated BinaryCIF must fail at the MessagePack boundary");

  const auto empty =
      io::read_structure({}, {io::StructureFormat::bcif, "empty.bcif"});
  passed &= expect(!empty.has_value() &&
                       empty.error().message.find("empty") != std::string::npos,
                   "empty BinaryCIF must return a bounded parse error");

  return passed ? 0 : 1;
}
