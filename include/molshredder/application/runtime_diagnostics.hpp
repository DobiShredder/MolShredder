#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace molshredder::application {

enum class RuntimeStatus { unavailable, not_initialized, ready, failed };

[[nodiscard]] std::string_view to_string(RuntimeStatus status) noexcept;

struct GraphicsRuntimeInfo {
  RuntimeStatus status{RuntimeStatus::unavailable};
  std::string api{"unknown"};
  std::string backend{"none"};
  bool rhi_based{};
  std::optional<std::string> device_name;
  std::optional<std::uint64_t> device_id;
  std::optional<std::uint64_t> vendor_id;
  std::optional<std::string> device_type;
  std::optional<std::string> driver_version;
  std::optional<std::string> failure_reason{
      "desktop graphics runtime is not attached"};

  friend bool operator==(const GraphicsRuntimeInfo &,
                         const GraphicsRuntimeInfo &) = default;
};

class RuntimeDiagnostics {
public:
  [[nodiscard]] GraphicsRuntimeInfo graphics() const;
  void set_graphics(GraphicsRuntimeInfo info);

private:
  mutable std::mutex mutex_;
  GraphicsRuntimeInfo graphics_;
};

}  // namespace molshredder::application
