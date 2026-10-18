#include "molshredder/application/video_encoder.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <thread>

#include "../plugin/process.hpp"

namespace molshredder::application {
namespace {

using operation::Error;
using operation::ErrorCode;

Error invalid(std::string message, std::string suggestion = {}) {
  return Error{ErrorCode::invalid_argument, std::move(message), std::move(suggestion)};
}

bool is_regular_executable(const std::filesystem::path &path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) return false;
#if defined(_WIN32)
  return true;
#else
  const auto permissions = std::filesystem::status(path, error).permissions();
  return (permissions & (std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                         std::filesystem::perms::others_exec)) != std::filesystem::perms::none;
#endif
}

std::string format_number(double value) {
  std::ostringstream stream;
  stream.precision(6);
  stream << value;
  return stream.str();
}

} // namespace

std::string video_format_from_extension(const std::filesystem::path &path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (extension == ".mp4" || extension == ".m4v") return "mp4";
  if (extension == ".mov") return "mov";
  if (extension == ".webm") return "webm";
  if (extension == ".gif") return "gif";
  if (extension == ".mpg" || extension == ".mpeg") return "mpeg1";
  return {};
}

operation::Result<std::filesystem::path> find_ffmpeg(const std::optional<std::filesystem::path> &explicit_path) {
  using R = operation::Result<std::filesystem::path>;
  if (explicit_path.has_value()) {
    if (!is_regular_executable(*explicit_path))
      return R::failure(Error{ErrorCode::not_found, "encoder is not an executable file: " + explicit_path->string(),
                              "pass the full path of the ffmpeg executable"});
    return R::success(std::filesystem::absolute(*explicit_path));
  }
  if (const char *environment = std::getenv("MOLSHREDDER_FFMPEG"); environment != nullptr && environment[0] != '\0') {
    const std::filesystem::path candidate{environment};
    if (!is_regular_executable(candidate))
      return R::failure(Error{ErrorCode::not_found, std::string{"MOLSHREDDER_FFMPEG does not name an executable: "} + environment,
                              "unset it or point it at the ffmpeg executable"});
    return R::success(std::filesystem::absolute(candidate));
  }
  if (const char *path_variable = std::getenv("PATH"); path_variable != nullptr) {
#if defined(_WIN32)
    constexpr char separator = ';';
    const char *name = "ffmpeg.exe";
#else
    constexpr char separator = ':';
    const char *name = "ffmpeg";
#endif
    std::string_view remaining{path_variable};
    while (!remaining.empty()) {
      const auto cut = remaining.find(separator);
      const auto directory = remaining.substr(0U, cut);
      if (!directory.empty()) {
        const auto candidate = std::filesystem::path{std::string{directory}} / name;
        if (is_regular_executable(candidate)) return R::success(candidate);
      }
      if (cut == std::string_view::npos) break;
      remaining.remove_prefix(cut + 1U);
    }
  }
  return R::failure(Error{ErrorCode::unsupported, "ffmpeg was not found",
                          "install ffmpeg, put it on PATH or set MOLSHREDDER_FFMPEG; image frames can be exported without it "
                          "(movie export)"});
}

operation::Result<std::vector<std::string>> ffmpeg_arguments(const VideoEncodeRequest &request,
                                                              const std::filesystem::path &temporary_output) {
  using R = operation::Result<std::vector<std::string>>;
  if (!std::isfinite(request.frames_per_second) || request.frames_per_second <= 0.0 || request.frames_per_second > 240.0)
    return R::failure(invalid("video frame rate must be greater than 0 and at most 240"));
  if (request.quality < 0 || request.quality > 100) return R::failure(invalid("video quality must be between 0 and 100"));
  const auto &format = request.format;
  if (format != "mp4" && format != "mov" && format != "webm" && format != "gif" && format != "mpeg1")
    return R::failure(invalid("unsupported video format '" + format + "'", "use mp4, mov, webm, gif or mpeg1"));
  std::vector<std::string> arguments{"-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-framerate",
                                     format_number(request.frames_per_second), "-start_number",
                                     std::to_string(request.first_frame_number), "-i",
                                     (request.frames_directory / request.frame_pattern).string()};
  const auto add = [&](std::initializer_list<const char *> items) {
    for (const auto *item : items) arguments.emplace_back(item);
  };
  // Even dimensions are required by the yuv420p codecs.
  const char *even = "scale=trunc(iw/2)*2:trunc(ih/2)*2";
  if (format == "mp4" || format == "mov") {
    const auto crf = static_cast<int>(std::lround(35.0 - request.quality * 0.2));
    add({"-vf", even, "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf"});
    arguments.push_back(std::to_string(crf));
    if (format == "mp4") add({"-movflags", "+faststart"});
  } else if (format == "webm") {
    const auto crf = static_cast<int>(std::lround(50.0 - request.quality * 0.27));
    add({"-vf", even, "-c:v", "libvpx-vp9", "-b:v", "0", "-pix_fmt", "yuv420p", "-crf"});
    arguments.push_back(std::to_string(crf));
  } else if (format == "mpeg1") {
    const auto scale = static_cast<int>(std::lround(31.0 - request.quality * 0.29));
    add({"-vf", even, "-c:v", "mpeg1video", "-qscale:v"});
    arguments.push_back(std::to_string(scale));
  } else {
    add({"-vf", "split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse", "-loop", "0"});
  }
  arguments.push_back(temporary_output.string());
  return R::success(std::move(arguments));
}

operation::Result<VideoEncodeResult> encode_video(const VideoEncodeRequest &given, operation::TaskContext &context) {
  using R = operation::Result<VideoEncodeResult>;
  // The encoder runs inside the frames directory, so every path it receives must be absolute.
  VideoEncodeRequest request = given;
  if (!request.frames_directory.empty()) request.frames_directory = std::filesystem::absolute(request.frames_directory);
  if (!request.output.empty()) request.output = std::filesystem::absolute(request.output);
  const auto executable = find_ffmpeg(request.executable);
  if (!executable.has_value()) return R::failure(executable.error());
  if (request.output.empty()) return R::failure(invalid("video output path must not be empty"));
  auto temporary = request.output;
  temporary.replace_filename(request.output.stem().string() + ".molshredder-tmp" + request.output.extension().string());
  const auto arguments = ffmpeg_arguments(request, temporary);
  if (!arguments.has_value()) return R::failure(arguments.error());
  std::vector<std::string> command{executable.value().string()};
  command.insert(command.end(), arguments.value().begin(), arguments.value().end());

  std::error_code remove_error;
  std::filesystem::remove(temporary, remove_error);
  const auto started = plugin::detail::ChildProcess::start(command, request.frames_directory, 4096U);
  if (!started.has_value()) return R::failure(started.error());
  auto &process = *started.value();
  const auto deadline = std::chrono::steady_clock::now() + request.timeout;
  for (;;) {
    if (context.cancellation.is_cancelled()) {
      process.kill();
      process.stop(std::chrono::milliseconds{100});
      std::filesystem::remove(temporary, remove_error);
      return R::failure(Error{ErrorCode::cancelled, "video encoding cancelled", {}});
    }
    if (std::chrono::steady_clock::now() > deadline) {
      process.kill();
      process.stop(std::chrono::milliseconds{100});
      std::filesystem::remove(temporary, remove_error);
      return R::failure(Error{ErrorCode::resource_exhausted, "video encoder timed out", "raise the timeout or lower the frame count"});
    }
    const auto message = process.read(std::chrono::milliseconds{100});
    if (message.event == plugin::detail::ChildProcess::Event::closed) break;
  }
  process.stop(std::chrono::seconds{30});
  const auto code = process.exit_code();
  VideoEncodeResult result;
  result.executable = executable.value();
  result.command = command;
  result.log = process.stderr_tail();
  if (!code.has_value() || *code != 0) {
    std::filesystem::remove(temporary, remove_error);
    return R::failure(Error{ErrorCode::script_failed,
                            "ffmpeg failed (exit " + (code.has_value() ? std::to_string(*code) : std::string{"unknown"}) + "): " +
                                (result.log.empty() ? std::string{"no diagnostic output"} : result.log),
                            "check the frame size, format and encoder installation"});
  }
  std::error_code size_error;
  const auto bytes = std::filesystem::file_size(temporary, size_error);
  if (size_error || bytes == 0U) {
    std::filesystem::remove(temporary, remove_error);
    return R::failure(Error{ErrorCode::script_failed, "ffmpeg finished without writing the video file", {}});
  }
  std::error_code rename_error;
  std::filesystem::rename(temporary, request.output, rename_error);
  if (rename_error) {
    std::filesystem::remove(temporary, remove_error);
    return R::failure(Error{ErrorCode::internal, "could not move the encoded video into place: " + rename_error.message(), {}});
  }
  result.bytes = static_cast<std::size_t>(bytes);
  return R::success(std::move(result));
}

} // namespace molshredder::application
