#pragma once

#include <cstddef>
#include <string>

namespace molshredder::operation {

struct SelectionExpression {
  std::string text;
};

enum class FrameScope { current, all, range };

struct FrameRange {
  FrameScope scope{FrameScope::current};
  std::size_t first{};
  std::size_t last{};
  std::size_t stride{1};

  [[nodiscard]] bool is_valid() const noexcept {
    if (stride == 0) {
      return false;
    }
    return scope != FrameScope::range || first <= last;
  }
};

enum class PbcMode { raw, minimum_image, unwrap };
enum class LengthUnit { angstrom, nanometer };
enum class OutputFormat { text, json, csv };

struct NumericPrecision {
  unsigned int decimal_places{6};

  [[nodiscard]] bool is_valid() const noexcept {
    return decimal_places <= 15;
  }
};

}  // namespace molshredder::operation
