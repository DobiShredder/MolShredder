#pragma once

#include <iosfwd>

#include "molshredder/command/registry.hpp"

namespace molshredder::cli {

class Adapter {
 public:
  explicit Adapter(const command::Registry& registry) : registry_{registry} {}

  int run(int argc, char** argv, std::istream& input, std::ostream& output,
          std::ostream& error) const;

 private:
  const command::Registry& registry_;
};

}  // namespace molshredder::cli
