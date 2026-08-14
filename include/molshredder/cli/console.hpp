#pragma once

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/command/registry.hpp"
#include "molshredder/operation/common_types.hpp"

namespace molshredder::cli {

class Console {
 public:
  explicit Console(const command::Registry& registry) : registry_{registry} {}

  int run(std::istream& input, std::ostream& output, std::ostream& error);

  [[nodiscard]] std::vector<std::string> complete(
      std::string_view prefix) const;

  [[nodiscard]] const std::vector<command::InvocationRecord>& history() const
      noexcept {
    return history_;
  }

 private:
  void print_help(std::string_view prefix, std::ostream& output) const;

  const command::Registry& registry_;
  std::vector<command::InvocationRecord> history_;
  unsigned long long next_sequence_{1};
  operation::OutputFormat output_format_{operation::OutputFormat::text};
};

}  // namespace molshredder::cli
