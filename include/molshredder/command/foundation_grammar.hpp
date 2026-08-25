#pragma once

#include <vector>

#include "molshredder/command/registry.hpp"

namespace molshredder::command {

inline constexpr unsigned int kFoundationGrammarVersion = 1;

[[nodiscard]] std::vector<Descriptor> foundation_command_descriptors();

[[nodiscard]] std::vector<AliasSpec> foundation_command_aliases();
[[nodiscard]] std::vector<AliasSpec> view_command_aliases();

[[nodiscard]] std::vector<Descriptor> object_command_descriptors();

[[nodiscard]] std::vector<Descriptor> view_command_descriptors();

[[nodiscard]] std::vector<Descriptor> file_command_descriptors();

[[nodiscard]] std::vector<Descriptor> trajectory_command_descriptors();

[[nodiscard]] std::vector<Descriptor> analysis_result_command_descriptors();

}  // namespace molshredder::command
