#pragma once

#include <memory>

#include "molshredder/application/runtime_diagnostics.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/command/registry.hpp"

namespace molshredder::application {

[[nodiscard]] command::Registry make_default_registry();
[[nodiscard]] command::Registry make_default_registry(
    std::shared_ptr<Workspace> workspace);

[[nodiscard]] command::Registry make_default_registry(
    std::shared_ptr<Workspace> workspace,
    std::shared_ptr<RuntimeDiagnostics> diagnostics);

}  // namespace molshredder::application
