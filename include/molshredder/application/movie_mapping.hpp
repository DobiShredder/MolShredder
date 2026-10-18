#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/operation/result.hpp"

namespace molshredder::application {

// Movie frame -> state mapping language (PyMOL `mset`/`madd` syntax, checked against PyMOL 3.1.0):
//   "N"        one frame showing state N (one-based)
//   "xK"       K total copies of the previous state (K when nothing precedes it: the current state)
//   "-M"       frames passing from the previous state to state M (the start state is not repeated)
//   "N x30 N -M ..." elements concatenate. Whitespace between "x"/"-" and the number is allowed.
// The result holds zero-based state indices, one per movie frame.
inline constexpr std::size_t kMaxMovieFrames = 1'000'000U;

[[nodiscard]] operation::Result<std::vector<std::size_t>>
parse_movie_state_map(std::string_view specification,
                      std::size_t current_state_zero_based);

// Shortest equivalent specification (runs become "N xK" or "N -M"); parse_movie_state_map(compress(x)) == x.
[[nodiscard]] std::string compress_movie_state_map(
    const std::vector<std::size_t> &states);

} // namespace molshredder::application
