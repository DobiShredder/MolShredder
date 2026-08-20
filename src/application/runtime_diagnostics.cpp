#include "molshredder/application/runtime_diagnostics.hpp"

#include <utility>

namespace molshredder::application {

std::string_view to_string(RuntimeStatus status) noexcept {
  switch (status) {
  case RuntimeStatus::unavailable:
    return "unavailable";
  case RuntimeStatus::not_initialized:
    return "not_initialized";
  case RuntimeStatus::ready:
    return "ready";
  case RuntimeStatus::failed:
    return "failed";
  }
  return "failed";
}

GraphicsRuntimeInfo RuntimeDiagnostics::graphics() const {
  const std::scoped_lock lock{mutex_};
  return graphics_;
}

void RuntimeDiagnostics::set_graphics(GraphicsRuntimeInfo info) {
  const std::scoped_lock lock{mutex_};
  graphics_ = std::move(info);
}

}  // namespace molshredder::application
