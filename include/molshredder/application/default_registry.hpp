#pragma once

#include <memory>

#include "molshredder/application/workspace.hpp"
#include "molshredder/command/registry.hpp"

namespace molshredder::application {

[[nodiscard]] command::Registry make_default_registry();
[[nodiscard]] command::Registry make_default_registry(
    std::shared_ptr<Workspace> workspace);

}  // namespace molshredder::application
