#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string_view>

namespace molshredder::operation {

class CancellationToken {
 public:
  CancellationToken() : cancelled_{std::make_shared<std::atomic_bool>(false)} {}

  void request_cancel() const noexcept { cancelled_->store(true); }

  [[nodiscard]] bool is_cancelled() const noexcept {
    return cancelled_->load();
  }

 private:
  std::shared_ptr<std::atomic_bool> cancelled_;
};

struct ProgressUpdate {
  double fraction{};
  std::string_view stage;
};

using ProgressCallback = std::function<void(const ProgressUpdate&)>;

struct TaskContext {
  CancellationToken cancellation;
  ProgressCallback report_progress;
};

}  // namespace molshredder::operation
