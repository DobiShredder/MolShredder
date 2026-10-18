#include "molshredder/analysis/sequence_alignment.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>

namespace molshredder::analysis {
namespace {

// BLOSUM62 (public matrix, Henikoff & Henikoff 1992) in the order below.
constexpr std::string_view kOrder = "ARNDCQEGHILKMFPSTWYVBZX";
constexpr int kBlosum62[23][23] = {
    {4, -1, -2, -2, 0, -1, -1, 0, -2, -1, -1, -1, -1, -2, -1, 1, 0, -3, -2, 0, -2, -1, 0},
    {-1, 5, 0, -2, -3, 1, 0, -2, 0, -3, -2, 2, -1, -3, -2, -1, -1, -3, -2, -3, -1, 0, -1},
    {-2, 0, 6, 1, -3, 0, 0, 0, 1, -3, -3, 0, -2, -3, -2, 1, 0, -4, -2, -3, 3, 0, -1},
    {-2, -2, 1, 6, -3, 0, 2, -1, -1, -3, -4, -1, -3, -3, -1, 0, -1, -4, -3, -3, 4, 1, -1},
    {0, -3, -3, -3, 9, -3, -4, -3, -3, -1, -1, -3, -1, -2, -3, -1, -1, -2, -2, -1, -3, -3, -2},
    {-1, 1, 0, 0, -3, 5, 2, -2, 0, -3, -2, 1, 0, -3, -1, 0, -1, -2, -1, -2, 0, 3, -1},
    {-1, 0, 0, 2, -4, 2, 5, -2, 0, -3, -3, 1, -2, -3, -1, 0, -1, -3, -2, -2, 1, 4, -1},
    {0, -2, 0, -1, -3, -2, -2, 6, -2, -4, -4, -2, -3, -3, -2, 0, -2, -2, -3, -3, -1, -2, -1},
    {-2, 0, 1, -1, -3, 0, 0, -2, 8, -3, -3, -1, -2, -1, -2, -1, -2, -2, 2, -3, 0, 0, -1},
    {-1, -3, -3, -3, -1, -3, -3, -4, -3, 4, 2, -3, 1, 0, -3, -2, -1, -3, -1, 3, -3, -3, -1},
    {-1, -2, -3, -4, -1, -2, -3, -4, -3, 2, 4, -2, 2, 0, -3, -2, -1, -2, -1, 1, -4, -3, -1},
    {-1, 2, 0, -1, -3, 1, 1, -2, -1, -3, -2, 5, -1, -3, -1, 0, -1, -3, -2, -2, 0, 1, -1},
    {-1, -1, -2, -3, -1, 0, -2, -3, -2, 1, 2, -1, 5, 0, -2, -1, -1, -1, -1, 1, -3, -1, -1},
    {-2, -3, -3, -3, -2, -3, -3, -3, -1, 0, 0, -3, 0, 6, -4, -2, -2, 1, 3, -1, -3, -3, -1},
    {-1, -2, -2, -1, -3, -1, -1, -2, -2, -3, -3, -1, -2, -4, 7, -1, -1, -4, -3, -2, -2, -1, -2},
    {1, -1, 1, 0, -1, 0, 0, 0, -1, -2, -2, 0, -1, -2, -1, 4, 1, -3, -2, -2, 0, 0, 0},
    {0, -1, 0, -1, -1, -1, -1, -2, -2, -1, -1, -1, -1, -2, -1, 1, 5, -2, -2, 0, -1, -1, 0},
    {-3, -3, -4, -4, -2, -2, -3, -2, -2, -3, -2, -3, -1, 1, -4, -3, -2, 11, 2, -3, -4, -3, -2},
    {-2, -2, -2, -3, -2, -1, -2, -3, 2, -1, -1, -2, -1, 3, -3, -2, -2, 2, 7, -1, -3, -2, -1},
    {0, -3, -3, -3, -1, -2, -2, -3, -3, 3, 1, -2, 1, -1, -2, -2, 0, -3, -1, 4, -3, -2, -1},
    {-2, -1, 3, 4, -3, 0, 1, -1, 0, -3, -4, 0, -3, -3, -2, 0, -1, -4, -3, -3, 4, 1, -1},
    {-1, 0, 0, 1, -3, 3, 4, -2, 0, -3, -3, 1, -1, -3, -1, 0, -1, -3, -2, -2, 1, 4, -1},
    {0, -1, -1, -1, -2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, 0, 0, -2, -1, -1, -1, -1, -1},
};

int score_of(char a, char b) {
  const auto ia = kOrder.find(static_cast<char>(std::toupper(static_cast<unsigned char>(a))));
  const auto ib = kOrder.find(static_cast<char>(std::toupper(static_cast<unsigned char>(b))));
  const auto x = kOrder.find('X');
  return kBlosum62[ia == std::string_view::npos ? x : ia][ib == std::string_view::npos ? x : ib];
}

} // namespace

char residue_letter(std::string_view name) noexcept {
  static const std::map<std::string, char, std::less<>> table{
      {"ALA", 'A'}, {"ARG", 'R'}, {"ASN", 'N'}, {"ASP", 'D'}, {"CYS", 'C'}, {"GLN", 'Q'}, {"GLU", 'E'}, {"GLY", 'G'},
      {"HIS", 'H'}, {"ILE", 'I'}, {"LEU", 'L'}, {"LYS", 'K'}, {"MET", 'M'}, {"PHE", 'F'}, {"PRO", 'P'}, {"SER", 'S'},
      {"THR", 'T'}, {"TRP", 'W'}, {"TYR", 'Y'}, {"VAL", 'V'}, {"MSE", 'M'}, {"SEC", 'C'}, {"HID", 'H'}, {"HIE", 'H'},
      {"HIP", 'H'}, {"CYX", 'C'}, {"CYM", 'C'}, {"ASH", 'D'}, {"GLH", 'E'}, {"LYN", 'K'}, {"ASX", 'B'}, {"GLX", 'Z'}};
  std::string upper;
  for (const auto c : name) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  const auto found = table.find(upper);
  return found == table.end() ? 'X' : found->second;
}

operation::Result<SequenceAlignment> align_sequences(const SequenceAlignmentRequest &request) {
  using R = operation::Result<SequenceAlignment>;
  const auto &a = request.first;
  const auto &b = request.second;
  constexpr std::size_t kLimit = 20000U;
  if (a.empty() || b.empty())
    return R::failure(operation::Error{operation::ErrorCode::invalid_argument, "sequence alignment needs two non-empty sequences", {}});
  if (a.size() > kLimit || b.size() > kLimit)
    return R::failure(operation::Error{operation::ErrorCode::resource_exhausted, "sequences are limited to 20000 residues", {}});
  if (request.gap_open > 0.0 || request.gap_extend > 0.0)
    return R::failure(operation::Error{operation::ErrorCode::invalid_argument, "gap penalties must not be positive", {}});
  const std::size_t n = a.size(), m = b.size();
  const double negative = -std::numeric_limits<double>::infinity();
  // M: ends in a match; X: ends in a gap in b (a residue against '-'); Y: ends in a gap in a.
  const std::size_t width = m + 1U;
  std::vector<double> mm((n + 1U) * width, negative), xx((n + 1U) * width, negative), yy((n + 1U) * width, negative);
  // 0 = M, 1 = X, 2 = Y for tracebacks
  std::vector<std::uint8_t> from_m((n + 1U) * width, 0U), from_x((n + 1U) * width, 0U), from_y((n + 1U) * width, 0U);
  const auto open_i = [&](std::size_t) { return request.free_end_gaps ? 0.0 : request.gap_open; };
  const auto at = [&](std::size_t i, std::size_t j) { return i * width + j; };
  mm[at(0, 0)] = 0.0;
  for (std::size_t i = 1; i <= n; ++i) {
    xx[at(i, 0)] = request.free_end_gaps ? 0.0 : request.gap_open + static_cast<double>(i - 1U) * request.gap_extend;
    from_x[at(i, 0)] = 1U;
  }
  for (std::size_t j = 1; j <= m; ++j) {
    yy[at(0, j)] = request.free_end_gaps ? 0.0 : request.gap_open + static_cast<double>(j - 1U) * request.gap_extend;
    from_y[at(0, j)] = 2U;
  }
  (void)open_i;
  for (std::size_t i = 1; i <= n; ++i) {
    for (std::size_t j = 1; j <= m; ++j) {
      // match
      double best = mm[at(i - 1, j - 1)];
      std::uint8_t source = 0U;
      if (xx[at(i - 1, j - 1)] > best) { best = xx[at(i - 1, j - 1)]; source = 1U; }
      if (yy[at(i - 1, j - 1)] > best) { best = yy[at(i - 1, j - 1)]; source = 2U; }
      mm[at(i, j)] = best + score_of(a[i - 1U], b[j - 1U]);
      from_m[at(i, j)] = source;
      // gap in b: consume a[i-1]
      double open_value = std::max(mm[at(i - 1, j)], yy[at(i - 1, j)]) + request.gap_open;
      const double extend_value = xx[at(i - 1, j)] + request.gap_extend;
      if (request.free_end_gaps && j == m) {  // trailing gap in b is free
        open_value = std::max(mm[at(i - 1, j)], yy[at(i - 1, j)]);
        xx[at(i, j)] = std::max(open_value, xx[at(i - 1, j)]);
        from_x[at(i, j)] = xx[at(i - 1, j)] > open_value ? 1U : (mm[at(i - 1, j)] >= yy[at(i - 1, j)] ? 0U : 2U);
      } else {
        xx[at(i, j)] = std::max(open_value, extend_value);
        from_x[at(i, j)] = extend_value > open_value ? 1U : (mm[at(i - 1, j)] >= yy[at(i - 1, j)] ? 0U : 2U);
      }
      // gap in a: consume b[j-1]
      open_value = std::max(mm[at(i, j - 1)], xx[at(i, j - 1)]) + request.gap_open;
      const double extend_y = yy[at(i, j - 1)] + request.gap_extend;
      if (request.free_end_gaps && i == n) {
        open_value = std::max(mm[at(i, j - 1)], xx[at(i, j - 1)]);
        yy[at(i, j)] = std::max(open_value, yy[at(i, j - 1)]);
        from_y[at(i, j)] = yy[at(i, j - 1)] > open_value ? 2U : (mm[at(i, j - 1)] >= xx[at(i, j - 1)] ? 0U : 1U);
      } else {
        yy[at(i, j)] = std::max(open_value, extend_y);
        from_y[at(i, j)] = extend_y > open_value ? 2U : (mm[at(i, j - 1)] >= xx[at(i, j - 1)] ? 0U : 1U);
      }
    }
  }
  // Best final state.
  std::uint8_t state = 0U;
  double best = mm[at(n, m)];
  if (xx[at(n, m)] > best) { best = xx[at(n, m)]; state = 1U; }
  if (yy[at(n, m)] > best) { best = yy[at(n, m)]; state = 2U; }
  SequenceAlignment result;
  result.score = best;
  std::size_t i = n, j = m;
  while (i > 0U || j > 0U) {
    if (state == 0U) {
      if (i == 0U || j == 0U) break;
      result.pairs.emplace_back(i - 1U, j - 1U);
      if (std::toupper(static_cast<unsigned char>(a[i - 1U])) == std::toupper(static_cast<unsigned char>(b[j - 1U]))) ++result.identical;
      state = from_m[at(i, j)];
      --i;
      --j;
    } else if (state == 1U) {
      if (i == 0U) break;
      state = from_x[at(i, j)];
      --i;
    } else {
      if (j == 0U) break;
      state = from_y[at(i, j)];
      --j;
    }
  }
  std::reverse(result.pairs.begin(), result.pairs.end());
  return R::success(std::move(result));
}

} // namespace molshredder::analysis
