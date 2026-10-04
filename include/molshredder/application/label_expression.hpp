#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "molshredder/model/coordinates.hpp"
#include "molshredder/model/topology.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::application {

// PyMOL-compatible label expression: a Python subset evaluated per atom.
// Supported: string/int/float literals, None/True/False, atom fields
// (name resn resi resv chain segi alt elem type model ss text_type b q
// partial_charge formal_charge ID index rank), + - * / % (string
// %-formatting with a value or tuple), unary minus, parentheses, tuples and
// str()/int()/float()/len()/round()/abs(). Float results are formatted with
// label_digits; other values use Python str().
class LabelExpression {
public:
  [[nodiscard]] static operation::Result<LabelExpression>
  parse(std::string_view source);

  struct AtomContext {
    const model::Topology *topology{};
    std::string_view object_name;
    std::int64_t digits{1};
    // Frame-level atom properties (PDB b/occupancy) take precedence.
    const model::CoordinateFrame *frame{};
  };
  [[nodiscard]] operation::Result<std::string>
  evaluate(const AtomContext &context, std::size_t atom) const;

  [[nodiscard]] const std::string &source() const noexcept { return source_; }

  struct Node;

private:
  std::string source_;
  std::shared_ptr<const Node> root_;
};

// Python str() of a double (shortest round-trip repr with Python's
// fixed/scientific switch at exponents < -4 or >= 16).
[[nodiscard]] std::string python_float_repr(double value);

} // namespace molshredder::application
