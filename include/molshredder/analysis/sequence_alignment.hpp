#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/operation/result.hpp"

namespace molshredder::analysis {

// One-letter code of a residue name (standard and common modified amino acids); 'X' when unknown.
[[nodiscard]] char residue_letter(std::string_view residue_name) noexcept;

struct SequenceAlignmentRequest {
  std::string first;
  std::string second;
  // Affine gap costs (negative): the first gapped position costs gap_open, every further one gap_extend.
  double gap_open{-10.0};
  double gap_extend{-0.5};
  // Terminal gaps are not penalised so a fragment can align inside a longer sequence.
  bool free_end_gaps{true};
};

struct SequenceAlignment {
  // Aligned residue positions (zero-based index into first, index into second), in increasing order.
  std::vector<std::pair<std::size_t, std::size_t>> pairs;
  double score{};
  std::size_t identical{};
};

// Global alignment (Gotoh) with the BLOSUM62 substitution matrix. Inputs are limited to 20000 residues each.
[[nodiscard]] operation::Result<SequenceAlignment> align_sequences(const SequenceAlignmentRequest &request);

} // namespace molshredder::analysis
