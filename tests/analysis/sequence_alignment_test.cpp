#include <cmath>
#include <iostream>
#include <string_view>

#include "molshredder/analysis/sequence_alignment.hpp"

namespace {

namespace analysis = molshredder::analysis;

bool expect(bool condition, std::string_view message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

using Pairs = std::vector<std::pair<std::size_t, std::size_t>>;

} // namespace

int main() {
  bool passed = true;
  passed &= expect(analysis::residue_letter("ala") == 'A' && analysis::residue_letter("MSE") == 'M' &&
                       analysis::residue_letter("HOH") == 'X',
                   "residue letters");
  {
    const auto result = analysis::align_sequences({"ACDEFG", "ACDEFG"});
    passed &= expect(result.has_value() && result.value().pairs.size() == 6U && result.value().identical == 6U &&
                         std::abs(result.value().score - 36.0) < 1e-9,
                     "identical sequences align one to one with the BLOSUM62 self score");
  }
  {
    const auto result = analysis::align_sequences({"ACDEFG", "ACFG"});
    passed &= expect(result.has_value() && result.value().pairs == Pairs({{0, 0}, {1, 1}, {4, 2}, {5, 3}}) &&
                         std::abs(result.value().score - (25.0 - 10.5)) < 1e-9,
                     "an internal two-residue gap costs open + extend");
  }
  {
    const auto result = analysis::align_sequences({"MKTAYIAKQRQISFVKSHFSRQ", "AKQRQISFVK"});
    Pairs expected;
    for (std::size_t i = 0; i < 10U; ++i) expected.emplace_back(6U + i, i);
    passed &= expect(result.has_value() && result.value().pairs == expected && result.value().identical == 10U,
                     "a fragment aligns inside a longer sequence without end-gap penalties");
    analysis::SequenceAlignmentRequest strict{"MKTAYIAKQRQISFVKSHFSRQ", "AKQRQISFVK"};
    strict.free_end_gaps = false;
    const auto penalised = analysis::align_sequences(strict);
    passed &= expect(penalised.has_value() && penalised.value().score < result.value().score && penalised.value().pairs == expected,
                     "penalised end gaps lower the score but keep the same pairs");
  }
  {
    const auto result = analysis::align_sequences({"AAAWWWAAA", "AAAAAA"});
    passed &= expect(result.has_value() && result.value().pairs.size() == 6U, "an inserted block is skipped");
    passed &= expect(!analysis::align_sequences({"", "A"}).has_value(), "empty sequence is rejected");
    analysis::SequenceAlignmentRequest bad{"A", "A"};
    bad.gap_open = 1.0;
    passed &= expect(!analysis::align_sequences(bad).has_value(), "positive gap penalties are rejected");
    passed &= expect(!analysis::align_sequences({std::string(20001, 'A'), "A"}).has_value(), "length limit");
  }
  {
    // pairs are strictly increasing in both sequences
    const auto result = analysis::align_sequences({"MKVLAAGIVGLLLAQPWDEFHKCY", "MKVAGIVGLLAQPDEFKCY"});
    bool monotone = result.has_value();
    if (monotone)
      for (std::size_t i = 1; i < result.value().pairs.size(); ++i)
        monotone &= result.value().pairs[i].first > result.value().pairs[i - 1].first &&
                    result.value().pairs[i].second > result.value().pairs[i - 1].second;
    passed &= expect(monotone, "alignment pairs are monotone");
  }
  return passed ? 0 : 1;
}
