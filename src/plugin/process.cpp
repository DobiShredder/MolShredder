#include "process.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace molshredder::plugin::detail {
namespace {

using operation::Error;
using operation::ErrorCode;
using operation::Result;

Error spawn_error(std::string message) {
  return {ErrorCode::script_failed, std::move(message), {}};
}

}  // namespace

#if defined(_WIN32)

struct ChildProcess::Native {
  HANDLE process{};
  HANDLE thread{};
  HANDLE stdin_write{};
  HANDLE stdout_read{};
  HANDLE stderr_read{};
};

namespace {
std::wstring widen(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}
std::wstring quote_argument(const std::string& argument) {
  std::wstring text = widen(argument);
  if (!text.empty() && text.find_first_of(L" \t\"") == std::wstring::npos) return text;
  std::wstring out = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t c : text) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'"') {
      out.append(backslashes * 2U + 1U, L'\\');
      out += L'"';
      backslashes = 0;
    } else {
      out.append(backslashes, L'\\');
      backslashes = 0;
      out += c;
    }
  }
  out.append(backslashes * 2U, L'\\');
  return out + L"\"";
}
}  // namespace

Result<std::unique_ptr<ChildProcess>> ChildProcess::start(
    const std::vector<std::string>& argv, const std::filesystem::path& working_directory,
    std::size_t max_line_bytes) {
  SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
  HANDLE in_read{}, in_write{}, out_read{}, out_write{}, err_read{}, err_write{};
  if (!CreatePipe(&in_read, &in_write, &attributes, 0) || !CreatePipe(&out_read, &out_write, &attributes, 0) ||
      !CreatePipe(&err_read, &err_write, &attributes, 0)) {
    return Result<std::unique_ptr<ChildProcess>>::failure(spawn_error("cannot create pipes"));
  }
  SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);
  std::wstring command_line;
  for (const auto& argument : argv) {
    if (!command_line.empty()) command_line += L' ';
    command_line += quote_argument(argument);
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = in_read;
  startup.hStdOutput = out_write;
  startup.hStdError = err_write;
  PROCESS_INFORMATION information{};
  const auto directory = working_directory.wstring();
  const BOOL created = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &information);
  CloseHandle(in_read);
  CloseHandle(out_write);
  CloseHandle(err_write);
  if (!created) {
    CloseHandle(in_write);
    CloseHandle(out_read);
    CloseHandle(err_read);
    return Result<std::unique_ptr<ChildProcess>>::failure(spawn_error("cannot start " + argv.front()));
  }
  std::unique_ptr<ChildProcess> child{new ChildProcess};
  child->native_ = std::make_unique<Native>();
  child->native_->process = information.hProcess;
  child->native_->thread = information.hThread;
  child->native_->stdin_write = in_write;
  child->native_->stdout_read = out_read;
  child->native_->stderr_read = err_read;
  child->max_line_bytes_ = max_line_bytes;
  child->reader_ = std::thread{[raw = child.get()] { raw->reader_loop(); }};
  child->stderr_reader_ = std::thread{[raw = child.get()] { raw->stderr_loop(); }};
  return Result<std::unique_ptr<ChildProcess>>::success(std::move(child));
}

bool ChildProcess::write_line(const std::string& line) {
  const std::string text = line + "\n";
  std::size_t offset = 0;
  while (offset < text.size()) {
    DWORD written = 0;
    if (!WriteFile(native_->stdin_write, text.data() + offset, static_cast<DWORD>(text.size() - offset), &written, nullptr) ||
        written == 0) {
      return false;
    }
    offset += written;
  }
  return true;
}

bool ChildProcess::running() {
  return native_->process != nullptr && WaitForSingleObject(native_->process, 0) == WAIT_TIMEOUT;
}

std::optional<int> ChildProcess::exit_code() {
  if (native_->process == nullptr || running()) return std::nullopt;
  DWORD code = 0;
  if (!GetExitCodeProcess(native_->process, &code)) return std::nullopt;
  return static_cast<int>(code);
}

void ChildProcess::kill() {
  if (native_ && native_->process != nullptr && running()) TerminateProcess(native_->process, 1);
}

void ChildProcess::stop(std::chrono::milliseconds grace) {
  if (native_->stdin_write != nullptr) {
    CloseHandle(native_->stdin_write);
    native_->stdin_write = nullptr;
  }
  if (native_->process != nullptr &&
      WaitForSingleObject(native_->process, static_cast<DWORD>(grace.count())) == WAIT_TIMEOUT) {
    TerminateProcess(native_->process, 1);
    WaitForSingleObject(native_->process, 5000);
  }
}

ChildProcess::~ChildProcess() {
  if (native_) {
    kill();
    if (native_->stdin_write != nullptr) CloseHandle(native_->stdin_write);
    if (native_->process != nullptr) WaitForSingleObject(native_->process, 5000);
  }
  if (reader_.joinable()) reader_.join();
  if (stderr_reader_.joinable()) stderr_reader_.join();
  if (native_) {
    if (native_->stdout_read) CloseHandle(native_->stdout_read);
    if (native_->stderr_read) CloseHandle(native_->stderr_read);
    if (native_->thread) CloseHandle(native_->thread);
    if (native_->process) CloseHandle(native_->process);
  }
}

namespace {
bool read_some(void* handle, char* buffer, std::size_t size, std::size_t& count) {
  DWORD read = 0;
  if (!ReadFile(static_cast<HANDLE>(handle), buffer, static_cast<DWORD>(size), &read, nullptr) || read == 0) return false;
  count = read;
  return true;
}
}  // namespace
#define MOLSHREDDER_READ_STDOUT(buffer, size, count) read_some(native_->stdout_read, buffer, size, count)
#define MOLSHREDDER_READ_STDERR(buffer, size, count) read_some(native_->stderr_read, buffer, size, count)

#else  // POSIX

struct ChildProcess::Native {
  pid_t pid{-1};
  int stdin_fd{-1};
  int stdout_fd{-1};
  int stderr_fd{-1};
  bool reaped{false};
  int exit_status{-1};
};

Result<std::unique_ptr<ChildProcess>> ChildProcess::start(
    const std::vector<std::string>& argv, const std::filesystem::path& working_directory,
    std::size_t max_line_bytes) {
  int in_pipe[2], out_pipe[2], err_pipe[2];
  if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    return Result<std::unique_ptr<ChildProcess>>::failure(spawn_error("cannot create pipes"));
  }
  // Close-on-exec for the ends the parent keeps; the child's ends are dup2'ed.
  for (const int fd : {in_pipe[1], out_pipe[0], err_pipe[0]}) fcntl(fd, F_SETFD, FD_CLOEXEC);
  // Everything the child needs is prepared before fork: it may only call
  // async-signal-safe functions until exec.
  std::vector<std::string> storage = argv;
  std::vector<char*> arguments;
  for (auto& argument : storage) arguments.push_back(argument.data());
  arguments.push_back(nullptr);
  // Minimal environment: nothing from the host process leaks to the plugin.
  std::vector<std::string> environment_storage{
      "PATH=/usr/bin:/bin:/usr/sbin:/sbin", "LANG=C.UTF-8",
      "MOLSHREDDER_PLUGIN_DIR=" + working_directory.string()};
  std::vector<char*> environment;
  for (auto& entry : environment_storage) environment.push_back(entry.data());
  environment.push_back(nullptr);
  const auto directory = working_directory.string();
  const auto executable = argv.front();
  int exec_pipe[2];
  if (pipe(exec_pipe) != 0) {
    return Result<std::unique_ptr<ChildProcess>>::failure(spawn_error("cannot create pipes"));
  }
  fcntl(exec_pipe[0], F_SETFD, FD_CLOEXEC);
  fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC);
  const pid_t pid = fork();
  if (pid == 0) {
    dup2(in_pipe[0], STDIN_FILENO);
    dup2(out_pipe[1], STDOUT_FILENO);
    dup2(err_pipe[1], STDERR_FILENO);
    for (int fd = 3; fd < 1024; ++fd) {
      if (fd != exec_pipe[1]) close(fd);
    }
    if (chdir(directory.c_str()) == 0) execve(executable.c_str(), arguments.data(), environment.data());
    const int failure = errno;
    const auto ignored = write(exec_pipe[1], &failure, sizeof(failure));
    static_cast<void>(ignored);
    _exit(127);
  }
  close(in_pipe[0]);
  close(out_pipe[1]);
  close(err_pipe[1]);
  close(exec_pipe[1]);
  int exec_errno = 0;
  ssize_t exec_read = 0;
  do {
    exec_read = ::read(exec_pipe[0], &exec_errno, sizeof(exec_errno));
  } while (exec_read < 0 && errno == EINTR);
  close(exec_pipe[0]);
  if (pid < 0 || exec_read == static_cast<ssize_t>(sizeof(exec_errno))) {
    if (pid > 0) {
      int status = 0;
      waitpid(pid, &status, 0);
    }
    close(in_pipe[1]);
    close(out_pipe[0]);
    close(err_pipe[0]);
    return Result<std::unique_ptr<ChildProcess>>::failure(spawn_error(
        "cannot start " + argv.front() + ": " + std::strerror(pid < 0 ? errno : exec_errno)));
  }
  std::unique_ptr<ChildProcess> child{new ChildProcess};
  child->native_ = std::make_unique<Native>();
  child->native_->pid = pid;
  child->native_->stdin_fd = in_pipe[1];
  child->native_->stdout_fd = out_pipe[0];
  child->native_->stderr_fd = err_pipe[0];
  child->max_line_bytes_ = max_line_bytes;
  child->reader_ = std::thread{[raw = child.get()] { raw->reader_loop(); }};
  child->stderr_reader_ = std::thread{[raw = child.get()] { raw->stderr_loop(); }};
  return Result<std::unique_ptr<ChildProcess>>::success(std::move(child));
}

bool ChildProcess::write_line(const std::string& line) {
  if (native_->stdin_fd < 0) return false;
  const std::string text = line + "\n";
  std::size_t offset = 0;
  while (offset < text.size()) {
    // MSG_NOSIGNAL is socket-only; ignore SIGPIPE around the write instead.
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    struct sigaction previous {};
    sigaction(SIGPIPE, &ignore, &previous);
    const auto written = write(native_->stdin_fd, text.data() + offset, text.size() - offset);
    sigaction(SIGPIPE, &previous, nullptr);
    if (written < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

bool ChildProcess::running() {
  if (native_->reaped) return false;
  int status = 0;
  const auto result = waitpid(native_->pid, &status, WNOHANG);
  if (result == native_->pid) {
    native_->reaped = true;
    native_->exit_status = WIFEXITED(status)     ? WEXITSTATUS(status)
                           : WIFSIGNALED(status) ? 128 + WTERMSIG(status)
                                                 : -1;
    return false;
  }
  return true;
}

std::optional<int> ChildProcess::exit_code() {
  if (running()) return std::nullopt;
  return native_->exit_status;
}

void ChildProcess::kill() {
  if (native_ && !native_->reaped && native_->pid > 0) ::kill(native_->pid, SIGKILL);
}

void ChildProcess::stop(std::chrono::milliseconds grace) {
  if (native_->stdin_fd >= 0) {
    close(native_->stdin_fd);
    native_->stdin_fd = -1;
  }
  const auto deadline = std::chrono::steady_clock::now() + grace;
  while (running() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  if (running()) {
    ::kill(native_->pid, SIGKILL);
    int status = 0;
    waitpid(native_->pid, &status, 0);
    native_->reaped = true;
    native_->exit_status = 128 + SIGKILL;
  }
}

ChildProcess::~ChildProcess() {
  if (native_) {
    if (native_->stdin_fd >= 0) close(native_->stdin_fd);
    if (!native_->reaped && native_->pid > 0) {
      ::kill(native_->pid, SIGKILL);
      int status = 0;
      waitpid(native_->pid, &status, 0);
    }
  }
  if (reader_.joinable()) reader_.join();
  if (stderr_reader_.joinable()) stderr_reader_.join();
  if (native_) {
    if (native_->stdout_fd >= 0) close(native_->stdout_fd);
    if (native_->stderr_fd >= 0) close(native_->stderr_fd);
  }
}

namespace {
bool read_fd(int fd, char* buffer, std::size_t size, std::size_t& count) {
  while (true) {
    const auto result = read(fd, buffer, size);
    if (result > 0) {
      count = static_cast<std::size_t>(result);
      return true;
    }
    if (result < 0 && errno == EINTR) continue;
    return false;
  }
}
}  // namespace
#define MOLSHREDDER_READ_STDOUT(buffer, size, count) read_fd(native_->stdout_fd, buffer, size, count)
#define MOLSHREDDER_READ_STDERR(buffer, size, count) read_fd(native_->stderr_fd, buffer, size, count)

#endif

void ChildProcess::reader_loop() {
  std::string pending;
  char buffer[4096];
  std::size_t count = 0;
  bool discarding = false;
  while (MOLSHREDDER_READ_STDOUT(buffer, sizeof(buffer), count)) {
    for (std::size_t i = 0; i < count; ++i) {
      const char c = buffer[i];
      if (c == '\n') {
        if (!discarding) {
          if (!pending.empty() && pending.back() == '\r') pending.pop_back();
          std::lock_guard<std::mutex> lock{mutex_};
          lines_.push_back(std::move(pending));
          condition_.notify_all();
        }
        pending.clear();
        discarding = false;
      } else if (!discarding) {
        pending += c;
        if (pending.size() > max_line_bytes_) {
          // An over-long line is dropped, and the host treats it as a failure.
          std::lock_guard<std::mutex> lock{mutex_};
          line_too_long_ = true;
          condition_.notify_all();
          pending.clear();
          discarding = true;
        }
      }
    }
  }
  std::lock_guard<std::mutex> lock{mutex_};
  closed_ = true;
  condition_.notify_all();
}

void ChildProcess::stderr_loop() {
  char buffer[1024];
  std::size_t count = 0;
  while (MOLSHREDDER_READ_STDERR(buffer, sizeof(buffer), count)) {
    std::lock_guard<std::mutex> lock{mutex_};
    stderr_tail_.append(buffer, count);
    if (stderr_tail_.size() > 4096U) stderr_tail_.erase(0U, stderr_tail_.size() - 4096U);
  }
}

ChildProcess::Message ChildProcess::read(std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock{mutex_};
  condition_.wait_for(lock, timeout, [this] { return !lines_.empty() || closed_ || line_too_long_; });
  if (!lines_.empty()) {
    Message message{Event::line, std::move(lines_.front())};
    lines_.pop_front();
    return message;
  }
  if (closed_ || line_too_long_) return {Event::closed, {}};
  return {Event::timeout, {}};
}

std::string ChildProcess::stderr_tail() {
  std::lock_guard<std::mutex> lock{mutex_};
  return stderr_tail_;
}

}  // namespace molshredder::plugin::detail
