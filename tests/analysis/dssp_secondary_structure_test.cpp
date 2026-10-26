// Hydrogen-bond pattern assignment on ubiquitin (1UBQ), compared with the
// helices and strands that the structure's own HELIX/SHEET records declare.
#include <iostream>
#include <set>
#include <string>

#include "molshredder/analysis/secondary_structure.hpp"
#include "molshredder/io/structure_reader.hpp"

namespace {
bool expect(bool condition, const std::string &message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}
}  // namespace

int main(int argc, char **argv) {
  using namespace molshredder;
  if (argc != 2) {
    std::cerr << "expected the 1ubq.pdb fixture path\n";
    return 1;
  }
  const auto document = io::read_structure_file(argv[1], io::StructureFormat::pdb);
  if (!document.has_value()) {
    std::cerr << document.error().message << '\n';
    return 1;
  }
  const auto &structure = document.value().structures.front();
  const auto frame = structure.coordinates->read_frame(0);
  const auto assigned = analysis::assign_secondary_structure_dssp(
      *structure.topology, *frame.value());
  if (!assigned.has_value()) {
    std::cerr << assigned.error().message << '\n';
    return 1;
  }
  std::string codes;
  for (const auto &residue : assigned.value().residues)
    codes += analysis::stride_code(residue.state);
  bool passed = true;
  passed &= expect(assigned.value().method == "molshredder-dssp-method-v1",
                   "method provenance");
  passed &= expect(codes.size() >= 76U, "ubiquitin has 76 residues");
  const auto count_in = [&](std::size_t first, std::size_t last, char code) {
    std::size_t total = 0;
    for (std::size_t residue = first; residue <= last; ++residue)
      if (codes[residue - 1U] == code) ++total;
    return total;
  };
  // The file declares strands 1-7, 10-17, 40-45, 48-50 and 64-72 and helix 23-34.
  passed &= expect(count_in(1, 7, 'E') >= 5U, "strand 1-7: " + codes);
  passed &= expect(count_in(10, 17, 'E') >= 4U, "strand 10-17: " + codes);
  passed &= expect(count_in(40, 45, 'E') >= 5U, "strand 40-45: " + codes);
  passed &= expect(count_in(48, 50, 'E') >= 2U, "strand 48-50: " + codes);
  passed &= expect(count_in(64, 72, 'E') >= 5U, "strand 64-72: " + codes);
  passed &= expect(count_in(23, 34, 'H') >= 11U, "helix 23-34: " + codes);
  passed &= expect(count_in(56, 59, 'G') >= 3U, "3-10 helix 56-59: " + codes);
  // Nothing else is a strand or alpha helix.
  std::size_t stray = 0;
  for (std::size_t residue = 1; residue <= 76; ++residue) {
    const bool declared_strand = (residue <= 8) || (residue >= 9 && residue <= 18) ||
                                 (residue >= 39 && residue <= 51) ||
                                 (residue >= 63 && residue <= 73);
    if (codes[residue - 1U] == 'E' && !declared_strand) ++stray;
    if (codes[residue - 1U] == 'H' && (residue < 22 || residue > 35)) ++stray;
  }
  passed &= expect(stray == 0U, "strands/helices outside the declared ranges: " + codes);
  return passed ? 0 : 1;
}
