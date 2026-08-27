#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "molshredder/command/registry.hpp"
#include "molshredder/operation/error.hpp"

namespace molshredder::application {
struct DispatchOutcome;
class Workspace;
}

namespace molshredder::automation {

inline constexpr std::size_t kDefaultScriptSourceLimit = 8U * 1024U * 1024U;
inline constexpr std::size_t kDefaultScriptOutputLimit = 8U * 1024U * 1024U;
inline constexpr std::uint64_t kDefaultIsolatedScriptTimeoutMs = 30'000U;

struct PythonScriptRequest {
  std::string path;
  std::string arguments_json{"[]"};
  std::optional<std::string> working_directory;
  bool trusted{false};
  std::size_t max_source_bytes{kDefaultScriptSourceLimit};
  std::size_t max_output_bytes{kDefaultScriptOutputLimit};
  std::uint64_t timeout_ms{kDefaultIsolatedScriptTimeoutMs};
  bool inherit_environment{false};
};

class PythonScriptService {
 public:
  explicit PythonScriptService(
      const command::Registry& registry,
      std::shared_ptr<application::Workspace> workspace = {})
      : registry_{registry}, workspace_{std::move(workspace)} {}

  [[nodiscard]] operation::Result<command::Response> run(
      const PythonScriptRequest& request,
      operation::TaskContext& context) const;
  [[nodiscard]] operation::Result<command::Response> run_isolated(
      const PythonScriptRequest& request,
      operation::TaskContext& context) const;

 private:
  const command::Registry& registry_;
  std::shared_ptr<application::Workspace> workspace_;
};

[[nodiscard]] std::optional<operation::Error>
register_python_script_command(
    command::Registry& registry,
    std::shared_ptr<application::Workspace> workspace = {});

namespace detail {

[[nodiscard]] const command::Registry* active_registry() noexcept;
[[nodiscard]] std::shared_ptr<application::Workspace>
active_workspace() noexcept;
void record_nested_invocation(const command::Registry& registry,
                              const application::DispatchOutcome& outcome);

}  // namespace detail

}  // namespace molshredder::automation
