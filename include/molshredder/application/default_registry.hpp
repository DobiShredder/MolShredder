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

// Owner-thread publication shared by synchronous CLI/Python handlers and the
// Desktop plan/build/commit analysis scheduler.
[[nodiscard]] operation::Result<command::Response> persist_sasa_analysis(
    Workspace &workspace, const command::Arguments &arguments,
    SasaAnalysisResult result);
[[nodiscard]] operation::Result<command::Response> persist_rdf_analysis(
    Workspace &workspace, const command::Arguments &arguments,
    RdfAnalysisResult result);
[[nodiscard]] operation::Result<command::Response>
persist_rmsd_matrix_analysis(Workspace &workspace,
                             const command::Arguments &arguments,
                             RmsdMatrixAnalysisResult result);

}  // namespace molshredder::application
