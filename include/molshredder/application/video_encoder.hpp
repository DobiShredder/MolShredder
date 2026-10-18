#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::application {

// Optional FFmpeg adapter (Q-037): MolShredder renders numbered PNG frames itself and hands them to an
// external `ffmpeg` executable. The executable is never required, bundled or searched beyond an explicit
// path, MOLSHREDDER_FFMPEG and PATH; it is started with an argument vector (no shell) and a minimal environment.
struct VideoEncodeRequest {
  std::filesystem::path frames_directory;
  // printf-style name of the numbered PNG frames inside frames_directory, e.g. "frame_%04d.png".
  std::string frame_pattern{"frame_%04d.png"};
  std::size_t first_frame_number{1};
  std::filesystem::path output;
  // mp4, mov, webm, gif or mpeg1.
  std::string format;
  double frames_per_second{30.0};
  // 0 (smallest) to 100 (best); mapped to the codec's quality scale.
  int quality{70};
  std::optional<std::filesystem::path> executable;
  std::chrono::seconds timeout{std::chrono::hours{1}};
};

struct VideoEncodeResult {
  std::filesystem::path executable;
  std::vector<std::string> command;
  std::size_t bytes{};
  // Tail of the encoder's diagnostic output (empty on a clean run).
  std::string log;
};

// Video format for an output file name ("mp4", "mov", "webm", "gif", "mpeg1") or empty when unknown.
[[nodiscard]] std::string video_format_from_extension(const std::filesystem::path &path);

// Resolves the executable: explicit path, then MOLSHREDDER_FFMPEG, then PATH.
[[nodiscard]] operation::Result<std::filesystem::path> find_ffmpeg(
    const std::optional<std::filesystem::path> &explicit_path);

// The ffmpeg argument vector (without the executable) for a request; exposed for tests.
[[nodiscard]] operation::Result<std::vector<std::string>> ffmpeg_arguments(
    const VideoEncodeRequest &request, const std::filesystem::path &temporary_output);

// Encodes the frames into request.output (written to a temporary name first and renamed on success).
[[nodiscard]] operation::Result<VideoEncodeResult> encode_video(
    const VideoEncodeRequest &request, operation::TaskContext &context);

} // namespace molshredder::application
