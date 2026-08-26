#pragma once

#include <optional>
#include <vector>

#include <QString>

namespace molshredder::desktop {

struct RedirectedRenderSmokeOptions {
  QString backend;
  std::vector<QString> open_paths;
  std::optional<QString> representation;
  std::optional<QString> trajectory;
  QString trajectory_coordinate_unit;
  QString trajectory_mapping;
};

[[nodiscard]] int
run_redirected_render_smoke(const RedirectedRenderSmokeOptions &options);

} // namespace molshredder::desktop
