#include "molshredder/application/movie_mapping.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>

namespace molshredder::application {
namespace {

using operation::Error;
using operation::ErrorCode;
using Result = operation::Result<std::vector<std::size_t>>;

Error invalid(std::string message) {
  return Error{ErrorCode::invalid_argument, std::move(message),
               "use PyMOL mset syntax such as '1 x30 1 -15 15 x30 15 -1'"};
}

// Parses a decimal number of at most 7 digits starting at `position`; advances past it.
std::optional<std::int64_t> read_number(std::string_view text,
                                        std::size_t &position) {
  const auto start = position;
  std::int64_t value = 0;
  while (position < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[position])) != 0) {
    value = value * 10 + (text[position] - '0');
    ++position;
    if (position - start > 7U) return std::nullopt;
  }
  if (position == start) return std::nullopt;
  return value;
}

void skip_space(std::string_view text, std::size_t &position) {
  while (position < text.size() &&
         std::isspace(static_cast<unsigned char>(text[position])) != 0)
    ++position;
}

} // namespace

Result parse_movie_state_map(std::string_view specification,
                             std::size_t current_state_zero_based) {
  std::vector<std::size_t> output;
  std::int64_t last = -1;
  const auto room = [&](std::size_t count) {
    return output.size() + count <= kMaxMovieFrames;
  };
  std::size_t position = 0;
  while (true) {
    skip_space(specification, position);
    if (position >= specification.size()) break;
    const auto c = specification[position];
    if (c == 'x' || c == 'X') {
      ++position;
      skip_space(specification, position);
      const auto count = read_number(specification, position);
      if (!count) return Result::failure(invalid("'x' needs a repeat count of 1 to 9999999"));
      std::int64_t copies;
      if (last < 0) {
        last = static_cast<std::int64_t>(current_state_zero_based);
        copies = *count;
      } else {
        copies = *count - 1;
      }
      if (copies > 0) {
        if (!room(static_cast<std::size_t>(copies)))
          return Result::failure(invalid("movie mapping exceeds 1000000 frames"));
        output.insert(output.end(), static_cast<std::size_t>(copies),
                      static_cast<std::size_t>(last));
      }
    } else if (c == '-') {
      ++position;
      skip_space(specification, position);
      const auto target = read_number(specification, position);
      if (!target || *target < 1)
        return Result::failure(invalid("'-' needs a state number of 1 to 9999999"));
      const std::int64_t end = *target - 1;
      std::int64_t cursor = last;
      const std::int64_t direction = end < cursor ? -1 : 1;
      const auto length = static_cast<std::size_t>((end - cursor) * direction);
      if (!room(length))
        return Result::failure(invalid("movie mapping exceeds 1000000 frames"));
      while (cursor != end) {
        cursor += direction;
        output.push_back(static_cast<std::size_t>(cursor));
      }
      last = end;
    } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
      const auto state = read_number(specification, position);
      if (!state) return Result::failure(invalid("state number must be 1 to 9999999"));
      if (*state < 1)
        return Result::failure(invalid("states are numbered from 1"));
      if (!room(1U)) return Result::failure(invalid("movie mapping exceeds 1000000 frames"));
      last = *state - 1;
      output.push_back(static_cast<std::size_t>(last));
    } else {
      return Result::failure(invalid(std::string{"unexpected character '"} + c + "' in movie mapping"));
    }
  }
  return Result::success(std::move(output));
}

std::string compress_movie_state_map(const std::vector<std::size_t> &states) {
  std::string out;
  const auto append = [&](const std::string &token) {
    if (!out.empty()) out.push_back(' ');
    out += token;
  };
  std::size_t i = 0;
  bool have_previous = false;
  while (i < states.size()) {
    // run of equal states
    std::size_t j = i;
    while (j + 1U < states.size() && states[j + 1U] == states[i]) ++j;
    if (j > i) {
      append(std::to_string(states[i] + 1U));
      append("x" + std::to_string(j - i + 1U));
      i = j + 1U;
      have_previous = true;
      continue;
    }
    // +1/-1 run of length >= 3 (including the start)
    if (i + 2U < states.size()) {
      const auto step = static_cast<std::int64_t>(states[i + 1U]) - static_cast<std::int64_t>(states[i]);
      if (step == 1 || step == -1) {
        std::size_t k = i + 1U;
        while (k + 1U < states.size() &&
               static_cast<std::int64_t>(states[k + 1U]) - static_cast<std::int64_t>(states[k]) == step)
          ++k;
        if (k - i >= 2U) {
          append(std::to_string(states[i] + 1U));
          append("-" + std::to_string(states[k] + 1U));
          i = k + 1U;
          have_previous = true;
          continue;
        }
      }
    }
    append(std::to_string(states[i] + 1U));
    ++i;
    have_previous = true;
  }
  (void)have_previous;
  return out;
}

} // namespace molshredder::application
