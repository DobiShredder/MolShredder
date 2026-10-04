#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "molshredder/operation/result.hpp"

// Narrow platform adapter: a child process with line-oriented stdio.
// POSIX (macOS, Linux) is exercised by tests; the Windows branch is written
// against the documented Win32 API and is verified at the SEQ-410A checkpoint.
namespace molshredder::plugin::detail {

class ChildProcess {
 public:
  enum class Event { line, closed, timeout };

  struct Message {
    Event event{Event::timeout};
    std::string line;
  };

  ~ChildProcess();
  ChildProcess(const ChildProcess&) = delete;
  ChildProcess& operator=(const ChildProcess&) = delete;

  // Starts `argv` (argv[0] is the executable path) with a minimal environment
  // and the given working directory. stdout is read as lines of at most
  // `max_line_bytes`; stderr is discarded to a bounded tail.
  [[nodiscard]] static operation::Result<std::unique_ptr<ChildProcess>> start(
      const std::vector<std::string>& argv, const std::filesystem::path& working_directory,
      std::size_t max_line_bytes);

  [[nodiscard]] bool write_line(const std::string& line);
  // Waits for the next stdout line, the end of the stream or the timeout.
  [[nodiscard]] Message read(std::chrono::milliseconds timeout);
  [[nodiscard]] bool running();
  // Closes stdin, waits briefly, then terminates.
  void stop(std::chrono::milliseconds grace);
  void kill();
  [[nodiscard]] std::string stderr_tail();
  [[nodiscard]] bool line_too_long() const { return line_too_long_; }

 private:
  ChildProcess() = default;
  void reader_loop();
  void stderr_loop();

  struct Native;
  std::unique_ptr<Native> native_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<std::string> lines_;
  bool closed_{};
  std::size_t max_line_bytes_{};
  bool line_too_long_{};
  std::string stderr_tail_;
  std::thread reader_;
  std::thread stderr_reader_;
};

}  // namespace molshredder::plugin::detail
