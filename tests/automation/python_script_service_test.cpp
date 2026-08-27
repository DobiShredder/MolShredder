#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/automation/python_script.hpp"

namespace {

bool require(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

}  // namespace

int main(int argc, char** argv) {
  if (!require(argc == 3, "expected success and timeout fixtures")) return 1;
  auto workspace = std::make_shared<molshredder::application::Workspace>();
  auto registry =
      molshredder::application::make_default_registry(workspace);
  molshredder::automation::PythonScriptService service{registry, workspace};

  molshredder::automation::PythonScriptRequest success;
  success.path = argv[1];
  success.arguments_json = R"(["service"])";
  success.trusted = true;
  success.timeout_ms = 2'000U;
  molshredder::operation::TaskContext success_context;
  std::vector<double> progress;
  success_context.report_progress = [&](const auto& update) {
    progress.push_back(update.fraction);
  };
  const auto completed = service.run_isolated(success, success_context);
  if (!require(completed.has_value(), "isolated service success failed") ||
      !require(!progress.empty() && progress.back() == 1.0,
               "isolated service progress did not complete") ||
      !require(workspace->object_count() == 0U,
               "isolated service mutated the parent Workspace")) {
    return 1;
  }

  molshredder::automation::PythonScriptRequest cancelled;
  cancelled.path = argv[2];
  cancelled.trusted = true;
  cancelled.timeout_ms = 5'000U;
  molshredder::operation::TaskContext cancelled_context;
  const auto cancellation = cancelled_context.cancellation;
  std::thread cancel_thread{[cancellation] {
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    cancellation.request_cancel();
  }};
  const auto cancelled_result =
      service.run_isolated(cancelled, cancelled_context);
  cancel_thread.join();
  if (!require(!cancelled_result.has_value(),
               "cancelled isolated service unexpectedly succeeded") ||
      !require(cancelled_result.error().code ==
                   molshredder::operation::ErrorCode::cancelled,
               "isolated service returned the wrong cancellation code") ||
      !require(cancelled_result.error().details.at("timed_out") == "false",
               "cancellation was misreported as timeout") ||
      !require(workspace->object_count() == 0U,
               "cancelled child mutated the parent Workspace")) {
    return 1;
  }

  return 0;
}
