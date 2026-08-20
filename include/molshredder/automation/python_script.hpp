#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "molshredder/command/registry.hpp"
#include "molshredder/operation/error.hpp"

namespace molshredder::application {
struct DispatchOutcome;
}

namespace molshredder::automation {

inline constexpr std::size_t kDefaultScriptSourceLimit = 8U * 1024U * 1024U;

struct PythonScriptRequest {
  std::string path;
  std::string arguments_json{"[]"};
  std::optional<std::string> working_directory;
  bool trusted{false};
  std::size_t max_source_bytes{kDefaultScriptSourceLimit};
};

class PythonScriptService {
 public:
  explicit PythonScriptService(const command::Registry& registry)
      : registry_{registry} {}

  [[nodiscard]] operation::Result<command::Response> run(
      const PythonScriptRequest& request,
      operation::TaskContext& context) const;

 private:
  const command::Registry& registry_;
};

[[nodiscard]] std::optional<operation::Error>
register_python_script_command(command::Registry& registry);

namespace detail {

[[nodiscard]] const command::Registry* active_registry() noexcept;
void record_nested_invocation(const command::Registry& registry,
                              const application::DispatchOutcome& outcome);

}  // namespace detail

}  // namespace molshredder::automation
