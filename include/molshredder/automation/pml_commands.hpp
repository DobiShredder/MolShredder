#pragma once

#include <memory>
#include <optional>

#include "molshredder/command/registry.hpp"
#include "molshredder/operation/error.hpp"

namespace molshredder::application {
class Workspace;
}

namespace molshredder::automation {

// Registers `pml translate`/`pml run` (PyMOL command scripts) and
// `tcl translate`/`tcl run` (a VMD Tcl subset). Neither language needs Python
// trust: statements that would execute code are refused with a diagnostic.
[[nodiscard]] std::optional<operation::Error> register_pml_commands(
    command::Registry& registry,
    std::shared_ptr<application::Workspace> workspace = {});

}  // namespace molshredder::automation
