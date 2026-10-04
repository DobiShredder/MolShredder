#include "molshredder/io/molfile_provider.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <shared_mutex>
#include <string_view>
#include <tuple>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "molfile_abi18.hpp"
#include "structure_reader_internal.hpp"

namespace molshredder::io {
namespace {

using operation::Error;
using operation::ErrorCode;
using operation::Result;

Error invalid(std::string message, std::string suggestion = {}) {
  return {ErrorCode::invalid_argument, std::move(message),
          std::move(suggestion)};
}

Error internal(std::string message, std::string suggestion = {}) {
  return {ErrorCode::internal, std::move(message), std::move(suggestion)};
}

Error cancelled(std::string_view stage) {
  return {ErrorCode::cancelled,
          "molfile provider read cancelled during " + std::string{stage}, {}};
}

void progress(operation::TaskContext& context, double fraction,
              std::string_view stage) {
  if (context.report_progress) {
    context.report_progress({fraction, stage});
  }
}

class DynamicLibrary {
 public:
  DynamicLibrary() = default;
  ~DynamicLibrary() { close(); }
  DynamicLibrary(const DynamicLibrary&) = delete;
  DynamicLibrary& operator=(const DynamicLibrary&) = delete;

  DynamicLibrary(DynamicLibrary&& other) noexcept
      : handle_{std::exchange(other.handle_, nullptr)},
        path_{std::move(other.path_)} {}

  DynamicLibrary& operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = std::exchange(other.handle_, nullptr);
      path_ = std::move(other.path_);
    }
    return *this;
  }

  static Result<DynamicLibrary> open(const std::filesystem::path& path) {
    DynamicLibrary library;
#if defined(_WIN32)
    library.handle_ = LoadLibraryW(path.c_str());
    if (library.handle_ == nullptr) {
      return Result<DynamicLibrary>::failure(invalid(
          "could not load molfile plugin library: " + path.string() +
          " (Windows error " + std::to_string(GetLastError()) + ")"));
    }
#else
    library.handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library.handle_ == nullptr) {
      const auto* detail = dlerror();
      return Result<DynamicLibrary>::failure(invalid(
          "could not load molfile plugin library: " + path.string() +
          (detail == nullptr ? std::string{} : " (" + std::string{detail} + ")")));
    }
#endif
    library.path_ = path;
    return Result<DynamicLibrary>::success(std::move(library));
  }

  template <typename Function>
  Result<Function> required_symbol(const char* name) const {
#if defined(_WIN32)
    const auto raw = GetProcAddress(handle_, name);
#else
    dlerror();
    const auto raw = dlsym(handle_, name);
#endif
    if (raw == nullptr) {
      return Result<Function>::failure(invalid(
          "molfile plugin is missing required entry point: " +
              std::string{name},
          "use an ABI 18 plugin library"));
    }
    static_assert(sizeof(Function) == sizeof(raw));
    Function function{};
    std::memcpy(&function, &raw, sizeof(function));
    return Result<Function>::success(function);
  }

  void close() noexcept {
    if (handle_ == nullptr) return;
#if defined(_WIN32)
    FreeLibrary(handle_);
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
  }

 private:
#if defined(_WIN32)
  HMODULE handle_{};
#else
  void* handle_{};
#endif
  std::filesystem::path path_;
};

template <std::size_t Size>
Result<std::string> bounded_string(const char (&value)[Size],
                                   std::string_view field) {
  const auto* end = static_cast<const char*>(
      std::memchr(value, '\0', Size));
  if (end == nullptr) {
    return Result<std::string>::failure(
        invalid("molfile plugin returned an unterminated " +
                std::string{field}));
  }
  return Result<std::string>::success(
      std::string{value, static_cast<std::size_t>(end - value)});
}

Result<std::string> plugin_string(const char* value, std::string_view field,
                                  bool allow_empty = false) {
  if (value == nullptr) {
    return Result<std::string>::failure(
        invalid("molfile plugin has a null " + std::string{field}));
  }
  constexpr std::size_t kMaxPluginString = 4096U;
  const auto* end = static_cast<const char*>(
      std::memchr(value, '\0', kMaxPluginString));
  if (end == nullptr || (!allow_empty && end == value)) {
    return Result<std::string>::failure(invalid(
        "molfile plugin has an invalid " + std::string{field}));
  }
  return Result<std::string>::success(
      std::string{value, static_cast<std::size_t>(end - value)});
}

std::vector<std::string> split_extensions(std::string_view value) {
  std::vector<std::string> extensions;
  std::size_t position{};
  while (position < value.size()) {
    while (position < value.size() &&
           (std::isspace(static_cast<unsigned char>(value[position])) != 0 ||
            value[position] == ',' || value[position] == ';')) {
      ++position;
    }
    const auto start = position;
    while (position < value.size() &&
           std::isspace(static_cast<unsigned char>(value[position])) == 0 &&
           value[position] != ',' && value[position] != ';') {
      ++position;
    }
    if (position != start) {
      auto extension = std::string{value.substr(start, position - start)};
      if (!extension.empty() && extension.front() == '.') {
        extension.erase(extension.begin());
      }
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                     });
      if (!extension.empty()) extensions.push_back(std::move(extension));
    }
  }
  std::sort(extensions.begin(), extensions.end());
  extensions.erase(std::unique(extensions.begin(), extensions.end()),
                   extensions.end());
  return extensions;
}

bool is_library_path(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
#if defined(_WIN32)
  return extension == ".dll";
#elif defined(__APPLE__)
  return extension == ".dylib" || extension == ".so";
#else
  return extension == ".so";
#endif
}

struct ChannelProfile {
  std::string_view plugin;
  StructureFormat format;
  std::string_view format_name;
  // Atom option flags the plugin must report for this channel.
  int required_options;
  // The channel supplies coordinates through read_next_timestep.
  bool has_coordinates;
  // The channel has no residue identity; a synthetic MOL residue is reported.
  bool synthetic_residue;
  bool radius_positive;
};

// MOL-03 structure/mixed P0 channels. `vcf` is coordinate-only and belongs to
// the trajectory adapter; names outside this table are not dispatched.
const ChannelProfile* find_channel_profile(std::string_view plugin) {
  static constexpr ChannelProfile kProfiles[] = {
      {"pdb", StructureFormat::pdb, "pdb", 0, true, false, false},
      {"mmcif", StructureFormat::mmcif, "mmcif", 0, true, false, false},
      {"pdbx", StructureFormat::mmcif, "mmcif", 0, true, false, false},
      {"pqr", StructureFormat::pqr, "pqr",
       abi18::kCharge | abi18::kRadius, true, false, true},
      {"mol2", StructureFormat::mol2, "mol2", 0, true, false, false},
      {"psf", StructureFormat::psf, "psf", 0, false, false, false},
      {"parm7", StructureFormat::prmtop, "prmtop", 0, false, false, false},
      {"gro", StructureFormat::gro, "gro", 0, true, false, false},
      {"g96", StructureFormat::g96, "g96", 0, true, false, false},
      {"vtf", StructureFormat::vtf, "vtf", 0, true, false, false},
      {"vsf", StructureFormat::vtf, "vtf", 0, false, false, false},
      {"xyz", StructureFormat::xyz, "xyz", 0, true, true, false},
  };
  for (const auto& profile : kProfiles) {
    if (profile.plugin == plugin) return &profile;
  }
  return nullptr;
}

struct TrajectoryProfile {
  std::string_view plugin;
  std::string_view format_name;
};

// MOL-03 trajectory P0 channels. `vcf` is coordinate-only (structure comes
// from a topology that is already loaded).
const TrajectoryProfile* find_trajectory_profile(std::string_view plugin) {
  static constexpr TrajectoryProfile kProfiles[] = {
      {"dcd", "dcd"},       {"trr", "trr"},           {"xtc", "xtc"},
      {"crd", "mdcrd"},     {"crdbox", "crdbox"},     {"lammpstrj", "lammps"},
      {"netcdf", "netcdf"}, {"binpos", "binpos"},     {"rst7", "rst7"},
      {"vcf", "vtf"},
  };
  for (const auto& profile : kProfiles) {
    if (profile.plugin == plugin) return &profile;
  }
  return nullptr;
}

// One plugin handle that is read forward. Every callback runs under the
// provider's callback mutex when the plugin is not thread-safe; the handle
// itself is serialized by `state_mutex_`.
class SequentialMolfileSource final : public MolfileTrajectorySource {
 public:
  struct Config {
    std::shared_ptr<void> keepalive;
    const abi18::MolfilePlugin* plugin{};
    std::timed_mutex* callback_mutex{};
    std::string plugin_name;
    std::string provider_id;
    std::filesystem::path path;
    std::size_t atom_count{};
    MolfileTrajectoryLimits limits;
  };

  explicit SequentialMolfileSource(Config config)
      : config_{std::move(config)},
        loss_{"velocity", "force", "step_number",
              "physical_time_not_imported_unit_unverified"} {}

  ~SequentialMolfileSource() override {
    const std::lock_guard state{state_mutex_};
    close_handle();
  }

  [[nodiscard]] std::size_t atom_count() const noexcept override {
    return config_.atom_count;
  }
  [[nodiscard]] std::optional<std::size_t> frame_count() const
      noexcept override {
    return frame_count_;
  }
  [[nodiscard]] model::FrameAccess access() const noexcept override {
    return model::FrameAccess::sequential;
  }
  [[nodiscard]] const std::vector<std::string>& loss() const
      noexcept override {
    return loss_;
  }

  // Counts frames with a throwaway handle; memory stays at one frame.
  Result<std::size_t> count_frames(operation::TaskContext& context) {
    const std::lock_guard state{state_mutex_};
    close_handle();
    auto opened = open_handle();
    if (!opened.has_value()) return Result<std::size_t>::failure(opened.error());
    handle_ = opened.value();
    std::size_t count{};
    while (true) {
      if (context.cancellation.is_cancelled()) {
        close_handle();
        return Result<std::size_t>::failure(cancelled("trajectory scan"));
      }
      if (count >= config_.limits.max_frames) {
        close_handle();
        return Result<std::size_t>::failure(invalid(
            "molfile trajectory exceeds the configured frame limit",
            "raise MolfileTrajectoryLimits::max_frames only for a trusted "
            "input"));
      }
      if (!read_one()) break;
      ++count;
      if (count % 256U == 0U) {
        progress(context, 0.5, "molfile-trajectory-scan");
      }
    }
    close_handle();
    frame_count_ = count;
    return Result<std::size_t>::success(count);
  }

  [[nodiscard]] Result<std::shared_ptr<const model::CoordinateFrame>>
  read_frame(std::size_t frame_index) const override {
    using FrameResult = Result<std::shared_ptr<const model::CoordinateFrame>>;
    if (!frame_count_.has_value() || frame_index >= *frame_count_) {
      return FrameResult::failure(invalid(
          "molfile trajectory frame index is out of range: " +
          std::to_string(frame_index)));
    }
    const std::lock_guard state{state_mutex_};
    if (handle_ == nullptr || frame_index < next_index_) {
      close_handle();
      auto opened = open_handle();
      if (!opened.has_value()) return FrameResult::failure(opened.error());
      handle_ = opened.value();
    }
    while (next_index_ <= frame_index) {
      if (!read_one()) {
        close_handle();
        return FrameResult::failure(invalid(
            "molfile trajectory callback failed at frame " +
            std::to_string(next_index_)));
      }
    }
    return build_frame(frame_index);
  }

 private:
  std::unique_lock<std::timed_mutex> lock_provider() const {
    if (config_.callback_mutex == nullptr) {
      return std::unique_lock<std::timed_mutex>{};
    }
    return std::unique_lock<std::timed_mutex>{*config_.callback_mutex};
  }

  Result<void*> open_handle() const {
    const auto lock = lock_provider();
    int atom_count = static_cast<int>(config_.atom_count);
    void* handle = config_.plugin->open_file_read(
        config_.path.string().c_str(), config_.plugin_name.c_str(),
        &atom_count);
    if (handle == nullptr) {
      return Result<void*>::failure(invalid(
          "molfile provider could not open trajectory: " +
          config_.path.string()));
    }
    if (atom_count < 0 ||
        static_cast<std::size_t>(atom_count) != config_.atom_count) {
      config_.plugin->close_file_read(handle);
      return Result<void*>::failure(invalid(
          "molfile trajectory atom count " + std::to_string(atom_count) +
          " does not match topology atom count " +
          std::to_string(config_.atom_count)));
    }
    coordinates_.assign(config_.atom_count * 3U, 0.0F);
    next_index_ = 0U;
    return Result<void*>::success(handle);
  }

  void close_handle() const noexcept {
    if (handle_ != nullptr) {
      const auto lock = lock_provider();
      config_.plugin->close_file_read(handle_);
      handle_ = nullptr;
    }
    next_index_ = 0U;
  }

  // Reads the next timestep into `coordinates_` and `step_`.
  bool read_one() const {
    const auto lock = lock_provider();
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    step_ = {};
    step_.coordinates = coordinates_.data();
    step_.a = step_.b = step_.c = kNaN;
    step_.alpha = step_.beta = step_.gamma = kNaN;
    step_.physical_time = std::numeric_limits<double>::quiet_NaN();
    if (config_.plugin->read_next_timestep(
            handle_, static_cast<int>(config_.atom_count), &step_) !=
        abi18::kSuccess) {
      return false;
    }
    ++next_index_;
    return true;
  }

  Result<std::shared_ptr<const model::CoordinateFrame>> build_frame(
      std::size_t frame_index) const {
    using FrameResult = Result<std::shared_ptr<const model::CoordinateFrame>>;
    std::vector<model::Vec3f> positions;
    positions.reserve(config_.atom_count);
    for (std::size_t atom = 0; atom < config_.atom_count; ++atom) {
      const auto x = coordinates_[atom * 3U];
      const auto y = coordinates_[atom * 3U + 1U];
      const auto z = coordinates_[atom * 3U + 2U];
      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return FrameResult::failure(invalid(
            "molfile provider returned non-finite coordinates at frame " +
            std::to_string(frame_index)));
      }
      positions.push_back({x, y, z});
    }
    model::FrameMetadata metadata;
    metadata.coordinate_unit = operation::LengthUnit::angstrom;
    // A cell is imported only when the plugin wrote all six values and they
    // are not all zero (molfile's "no cell" convention).
    const float cell[]{step_.a, step_.b, step_.c,
                       step_.alpha, step_.beta, step_.gamma};
    const bool finite_cell = std::all_of(
        std::begin(cell), std::end(cell),
        [](float value) { return std::isfinite(value); });
    const bool zero_cell = std::all_of(
        std::begin(cell), std::end(cell),
        [](float value) { return value == 0.0F; });
    if (finite_cell && !zero_cell) {
      const auto unit_cell = detail::make_unit_cell(
          step_.a, step_.b, step_.c, step_.alpha, step_.beta, step_.gamma,
          config_.path.string(), frame_index + 1U);
      if (!unit_cell.has_value()) return FrameResult::failure(unit_cell.error());
      metadata.unit_cell = unit_cell.value();
    }
    metadata.fields.emplace("molfile.provider", config_.provider_id);
    metadata.fields.emplace("molfile.frame_index",
                            std::to_string(frame_index));
    if (std::isfinite(step_.physical_time)) {
      char buffer[40];
      std::snprintf(buffer, sizeof(buffer), "%.17g", step_.physical_time);
      metadata.fields.emplace("molfile.physical_time_raw", buffer);
    }
    return model::CoordinateFrame::create(
        model::CoordinateBuffer{std::move(positions)}, std::nullopt, {},
        std::move(metadata));
  }

  Config config_;
  std::vector<std::string> loss_;
  std::optional<std::size_t> frame_count_;
  mutable std::mutex state_mutex_;
  mutable void* handle_{};
  mutable std::size_t next_index_{};
  mutable std::vector<float> coordinates_;
  mutable abi18::Timestep step_{};
};

struct VolumeProfile {
  std::string_view plugin;
  VolumeFormat format;
};

const VolumeProfile* find_volume_profile(std::string_view plugin) {
  static constexpr VolumeProfile kProfiles[] = {
      {"dx", VolumeFormat::opendx},
      {"ccp4", VolumeFormat::mrc},
  };
  for (const auto& profile : kProfiles) {
    if (profile.plugin == plugin) return &profile;
  }
  return nullptr;
}

std::optional<std::uint8_t> infer_element(std::string_view atom_name,
                                          std::string_view residue_name) {
  while (!atom_name.empty() &&
         std::isdigit(static_cast<unsigned char>(atom_name.front())) != 0) {
    atom_name.remove_prefix(1U);
  }
  if (atom_name.empty() ||
      std::isalpha(static_cast<unsigned char>(atom_name.front())) == 0) {
    return std::nullopt;
  }
  if (atom_name.size() >= 2U &&
      std::isalpha(static_cast<unsigned char>(atom_name[1])) != 0) {
    const auto candidate = atom_name.substr(0U, 2U);
    const auto two_letter = detail::atomic_number(candidate);
    const auto conventional_case =
        std::islower(static_cast<unsigned char>(atom_name[1])) != 0;
    std::string upper_atom{atom_name};
    std::string upper_residue{residue_name};
    std::transform(upper_atom.begin(), upper_atom.end(), upper_atom.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    std::transform(upper_residue.begin(), upper_residue.end(),
                   upper_residue.begin(), [](unsigned char c) {
                     return static_cast<char>(std::toupper(c));
                   });
    const auto elemental_residue = upper_atom.size() == 2U &&
                                   upper_atom == upper_residue;
    const auto common_unambiguous =
        upper_atom.starts_with("CL") || upper_atom.starts_with("BR") ||
        (upper_atom.size() == 2U && upper_atom == "FE");
    if (two_letter.has_value() &&
        (conventional_case || elemental_residue || common_unambiguous)) {
      return two_letter;
    }
  }
  return detail::atomic_number(atom_name.substr(0U, 1U));
}

std::string structure_name(const std::filesystem::path& path) {
  auto name = path.stem().string();
  return name.empty() ? std::string{"molfile_structure"} : name;
}

struct CallbackCollector {
  std::vector<abi18::PluginHeader*> plugins;
  bool failed{};
};

extern "C" int collect_plugin(void* context, abi18::PluginHeader* plugin) {
  if (context == nullptr) return abi18::kError;
  auto* collector = static_cast<CallbackCollector*>(context);
  if (plugin == nullptr) {
    collector->failed = true;
    return abi18::kError;
  }
  try {
    collector->plugins.push_back(plugin);
  } catch (...) {
    collector->failed = true;
    return abi18::kError;
  }
  return abi18::kSuccess;
}

}  // namespace

class MolfileProviderRegistry::Impl {
 public:
  struct LibraryRecord {
    DynamicLibrary library;
    abi18::FiniFunction fini{};
    bool initialized{};
    std::filesystem::path path;
    std::vector<const abi18::MolfilePlugin*> plugins;
    std::vector<MolfileProviderDescriptor> descriptors;
    std::vector<std::unique_ptr<std::timed_mutex>> callback_mutexes;

    ~LibraryRecord() { shutdown(); }
    LibraryRecord() = default;
    LibraryRecord(const LibraryRecord&) = delete;
    LibraryRecord& operator=(const LibraryRecord&) = delete;

    void shutdown() noexcept {
      if (initialized && fini != nullptr) {
        static_cast<void>(fini());
        initialized = false;
      }
      library.close();
    }
  };

  ~Impl() {
    for (auto iterator = libraries.rbegin(); iterator != libraries.rend();
         ++iterator) {
      (*iterator)->shutdown();
    }
  }

  Result<std::unique_ptr<LibraryRecord>> load(
      const std::filesystem::path& path, MolfileProviderTrust trust,
      const std::set<std::string, std::less<>>& existing_ids) {
    if (!std::filesystem::is_regular_file(path)) {
      return Result<std::unique_ptr<LibraryRecord>>::failure(
          invalid("molfile plugin path is not a regular file: " +
                  path.string()));
    }
    auto opened = DynamicLibrary::open(path);
    if (!opened.has_value()) {
      return Result<std::unique_ptr<LibraryRecord>>::failure(opened.error());
    }
    auto record = std::make_unique<LibraryRecord>();
    record->library = std::move(opened.value());
    record->path = path;
    const auto init = record->library.required_symbol<abi18::InitFunction>(
        "vmdplugin_init");
    const auto registration =
        record->library.required_symbol<abi18::RegisterFunction>(
            "vmdplugin_register");
    const auto fini = record->library.required_symbol<abi18::FiniFunction>(
        "vmdplugin_fini");
    if (!init.has_value()) {
      return Result<std::unique_ptr<LibraryRecord>>::failure(init.error());
    }
    if (!registration.has_value()) {
      return Result<std::unique_ptr<LibraryRecord>>::failure(
          registration.error());
    }
    if (!fini.has_value()) {
      return Result<std::unique_ptr<LibraryRecord>>::failure(fini.error());
    }
    record->fini = fini.value();
    if (init.value()() != abi18::kSuccess) {
      return Result<std::unique_ptr<LibraryRecord>>::failure(
          invalid("molfile plugin initialization failed: " + path.string()));
    }
    record->initialized = true;
    CallbackCollector collector;
    if (registration.value()(&collector, collect_plugin) != abi18::kSuccess ||
        collector.failed) {
      return Result<std::unique_ptr<LibraryRecord>>::failure(
          invalid("molfile plugin registration failed: " + path.string()));
    }
    if (collector.plugins.empty()) {
      return Result<std::unique_ptr<LibraryRecord>>::failure(
          invalid("molfile plugin library registered no providers: " +
                  path.string()));
    }
    auto ids = existing_ids;
    for (auto* header : collector.plugins) {
      if (header->abi_version != abi18::kAbiVersion) {
        return Result<std::unique_ptr<LibraryRecord>>::failure(invalid(
            "molfile plugin ABI mismatch: expected 18, received " +
            std::to_string(header->abi_version)));
      }
      const auto type = plugin_string(header->type, "type");
      const auto name = plugin_string(header->name, "name");
      const auto pretty = plugin_string(header->pretty_name, "pretty name");
      const auto author = plugin_string(header->author, "author", true);
      if (!type.has_value()) {
        return Result<std::unique_ptr<LibraryRecord>>::failure(type.error());
      }
      if (!name.has_value()) {
        return Result<std::unique_ptr<LibraryRecord>>::failure(name.error());
      }
      if (!pretty.has_value()) {
        return Result<std::unique_ptr<LibraryRecord>>::failure(pretty.error());
      }
      if (!author.has_value()) {
        return Result<std::unique_ptr<LibraryRecord>>::failure(author.error());
      }
      if (type.value() != abi18::kMolfilePluginType) {
        return Result<std::unique_ptr<LibraryRecord>>::failure(invalid(
            "unsupported VMD plugin type: " + type.value(),
            "provide a mol file reader plugin"));
      }
      if (header->is_reentrant != abi18::kThreadSafe &&
          header->is_reentrant != abi18::kThreadUnsafe) {
        return Result<std::unique_ptr<LibraryRecord>>::failure(
            invalid("molfile plugin has an invalid thread-safety flag"));
      }
      const auto provider_id = "molfile:" + name.value();
      if (!ids.insert(provider_id).second) {
        return Result<std::unique_ptr<LibraryRecord>>::failure(
            invalid("duplicate molfile provider registration: " + provider_id));
      }
      const auto* plugin = reinterpret_cast<const abi18::MolfilePlugin*>(header);
      const auto extensions =
          plugin_string(plugin->filename_extension, "filename extension", true);
      if (!extensions.has_value()) {
        return Result<std::unique_ptr<LibraryRecord>>::failure(
            extensions.error());
      }
      if (plugin->open_file_read == nullptr ||
          plugin->close_file_read == nullptr ||
          (plugin->read_structure == nullptr &&
           plugin->read_next_timestep == nullptr &&
           plugin->read_volumetric_metadata == nullptr)) {
        return Result<std::unique_ptr<LibraryRecord>>::failure(invalid(
            "molfile provider lacks the open/close and structure, timestep or "
            "volumetric read callbacks: " + provider_id));
      }
      record->plugins.push_back(plugin);
      record->descriptors.push_back(MolfileProviderDescriptor{
          provider_id, name.value(), pretty.value(), author.value(),
          header->major_version, header->minor_version,
          header->is_reentrant == abi18::kThreadSafe,
          split_extensions(extensions.value()), path, trust});
      record->callback_mutexes.push_back(std::make_unique<std::timed_mutex>());
    }
    return Result<std::unique_ptr<LibraryRecord>>::success(std::move(record));
  }

  std::vector<std::unique_ptr<LibraryRecord>> libraries;
  mutable std::shared_mutex libraries_mutex;
};

MolfileProviderRegistry::MolfileProviderRegistry()
    : impl_{std::make_shared<Impl>()} {}

MolfileProviderRegistry::~MolfileProviderRegistry() = default;
MolfileProviderRegistry::MolfileProviderRegistry(
    MolfileProviderRegistry&&) noexcept = default;
MolfileProviderRegistry& MolfileProviderRegistry::operator=(
    MolfileProviderRegistry&&) noexcept = default;

Result<std::vector<MolfileProviderDescriptor>>
MolfileProviderRegistry::discover(const MolfileDiscoveryRequest& request) {
  try {
  std::vector<std::pair<std::filesystem::path, MolfileProviderTrust>> paths;
  for (const auto& directory : request.approved_directories) {
    if (directory.trust == MolfileProviderTrust::explicit_path) {
      return Result<std::vector<MolfileProviderDescriptor>>::failure(invalid(
          "approved plugin directory requires bundled or user-approved trust"));
    }
    if (!std::filesystem::is_directory(directory.path)) {
      return Result<std::vector<MolfileProviderDescriptor>>::failure(invalid(
          "approved molfile plugin directory does not exist: " +
          directory.path.string()));
    }
    std::vector<std::filesystem::path> directory_paths;
    for (const auto& entry :
         std::filesystem::directory_iterator{directory.path}) {
      if (entry.is_regular_file() && is_library_path(entry.path())) {
        directory_paths.push_back(entry.path());
      }
    }
    std::sort(directory_paths.begin(), directory_paths.end());
    for (auto& path : directory_paths) {
      paths.emplace_back(std::move(path), directory.trust);
    }
  }
  for (const auto& path : request.explicit_files) {
    paths.emplace_back(path, MolfileProviderTrust::explicit_path);
  }
  std::set<std::filesystem::path> unique_paths;
  for (const auto& [path, trust] : paths) {
    static_cast<void>(trust);
    const auto canonical = std::filesystem::weakly_canonical(path);
    if (!unique_paths.insert(canonical).second) {
      return Result<std::vector<MolfileProviderDescriptor>>::failure(
          invalid("duplicate molfile plugin path in discovery request: " +
                  canonical.string()));
    }
  }

  std::unique_lock registry_lock{impl_->libraries_mutex};
  std::set<std::string, std::less<>> ids;
  for (const auto& library : impl_->libraries) {
    for (const auto& descriptor : library->descriptors) {
      ids.insert(descriptor.provider_id);
    }
  }
  std::vector<std::unique_ptr<Impl::LibraryRecord>> pending;
  for (const auto& [path, trust] : paths) {
    auto loaded = impl_->load(path, trust, ids);
    if (!loaded.has_value()) {
      return Result<std::vector<MolfileProviderDescriptor>>::failure(
          loaded.error());
    }
    for (const auto& descriptor : loaded.value()->descriptors) {
      ids.insert(descriptor.provider_id);
    }
    pending.push_back(std::move(loaded.value()));
  }
  for (auto& record : pending) {
    impl_->libraries.push_back(std::move(record));
  }
  std::vector<MolfileProviderDescriptor> result;
  for (const auto& library : impl_->libraries) {
    result.insert(result.end(), library->descriptors.begin(),
                  library->descriptors.end());
  }
  std::sort(result.begin(), result.end(), [](const auto& left,
                                             const auto& right) {
    return left.provider_id < right.provider_id;
  });
  return Result<std::vector<MolfileProviderDescriptor>>::success(
      std::move(result));
  } catch (const std::bad_alloc&) {
    return Result<std::vector<MolfileProviderDescriptor>>::failure(
        internal("molfile discovery allocation failed"));
  } catch (const std::filesystem::filesystem_error& exception) {
    return Result<std::vector<MolfileProviderDescriptor>>::failure(invalid(
        "molfile discovery filesystem error: " +
        std::string{exception.what()}));
  } catch (const std::exception& exception) {
    return Result<std::vector<MolfileProviderDescriptor>>::failure(internal(
        "molfile plugin raised an exception during discovery: " +
        std::string{exception.what()}));
  } catch (...) {
    return Result<std::vector<MolfileProviderDescriptor>>::failure(
        internal("molfile plugin raised an unknown exception during discovery"));
  }
}

std::vector<MolfileProviderDescriptor>
MolfileProviderRegistry::descriptors() const {
  std::shared_lock registry_lock{impl_->libraries_mutex};
  std::vector<MolfileProviderDescriptor> result;
  for (const auto& library : impl_->libraries) {
    result.insert(result.end(), library->descriptors.begin(),
                  library->descriptors.end());
  }
  std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
    return left.provider_id < right.provider_id;
  });
  return result;
}

Result<StructureDocument> MolfileProviderRegistry::read_structure(
    const std::filesystem::path& path, std::string provider_id,
    MolfileReadLimits limits) const {
  operation::TaskContext context;
  return read_structure(path, std::move(provider_id), context, limits);
}

Result<StructureDocument> MolfileProviderRegistry::read_structure(
    const std::filesystem::path& path, std::string provider_id,
    operation::TaskContext& context, MolfileReadLimits limits) const {
  if (limits.max_atoms == 0U || limits.max_staging_bytes == 0U) {
    return Result<StructureDocument>::failure(invalid(
        "molfile read limits must have positive atom and byte budgets"));
  }
  if (context.cancellation.is_cancelled()) {
    return Result<StructureDocument>::failure(cancelled("startup"));
  }
  const Impl::LibraryRecord* selected_library{};
  std::size_t selected_index{};
  {
    std::shared_lock registry_lock{impl_->libraries_mutex};
    for (const auto& library : impl_->libraries) {
      for (std::size_t index = 0; index < library->descriptors.size(); ++index) {
        if (library->descriptors[index].provider_id == provider_id) {
          selected_library = library.get();
          selected_index = index;
          break;
        }
      }
      if (selected_library != nullptr) break;
    }
  }
  if (selected_library == nullptr) {
    return Result<StructureDocument>::failure(
        invalid("molfile provider is not registered: " + provider_id));
  }
  try {
  progress(context, 0.0, "molfile-open");
  if (context.cancellation.is_cancelled()) {
    return Result<StructureDocument>::failure(cancelled("open"));
  }
  const auto* plugin = selected_library->plugins[selected_index];
  const auto* profile = find_channel_profile(
      selected_library->descriptors[selected_index].plugin_name);
  if (profile == nullptr) {
    return Result<StructureDocument>::failure(Error{
        ErrorCode::unsupported,
        "molfile plugin has no structure channel profile: " + provider_id,
        "only the P0 structure channels (pdb, mmcif, pdbx, pqr, mol2, psf, "
        "parm7, gro, g96, vtf, vsf, xyz) are dispatched"});
  }
  if (plugin->read_structure == nullptr) {
    return Result<StructureDocument>::failure(Error{
        ErrorCode::unsupported,
        "molfile plugin has no structure callback: " + provider_id,
        "use it as a trajectory provider with traj load"});
  }
  std::unique_lock callback_lock{
      *selected_library->callback_mutexes[selected_index], std::defer_lock};
  if (!selected_library->descriptors[selected_index].thread_safe) {
    progress(context, 0.0, "molfile-wait-provider");
    while (!callback_lock.try_lock_for(std::chrono::milliseconds{10})) {
      if (context.cancellation.is_cancelled()) {
        return Result<StructureDocument>::failure(
            cancelled("provider scheduling"));
      }
    }
  }
  int atom_count{};
  void* read_handle = plugin->open_file_read(
      path.string().c_str(),
      selected_library->descriptors[selected_index].plugin_name.c_str(),
      &atom_count);
  if (read_handle == nullptr) {
    return Result<StructureDocument>::failure(
        invalid("molfile provider could not open input: " + path.string()));
  }
  struct CloseGuard {
    const abi18::MolfilePlugin* plugin;
    void* handle;
    ~CloseGuard() { plugin->close_file_read(handle); }
  } close_guard{plugin, read_handle};

  if (context.cancellation.is_cancelled()) {
    return Result<StructureDocument>::failure(cancelled("open"));
  }

  if (atom_count <= 0) {
    return Result<StructureDocument>::failure(invalid(
        "molfile provider returned an invalid atom count: " +
        std::to_string(atom_count)));
  }
  const auto count = static_cast<std::size_t>(atom_count);
  if (count > limits.max_atoms) {
    return Result<StructureDocument>::failure(invalid(
        "molfile provider atom count exceeds the configured limit: " +
            std::to_string(count),
        "raise MolfileReadLimits::max_atoms only for a trusted input"));
  }
  constexpr std::size_t kPerAtomStagingEstimate =
      sizeof(abi18::Atom) + 3U * sizeof(float) + 6U * sizeof(double) +
      sizeof(std::string) + sizeof(model::Vec3f) + sizeof(model::AtomRecord) +
      sizeof(model::ResidueRecord) + 128U;
  if (count > (std::numeric_limits<std::size_t>::max)() /
                  kPerAtomStagingEstimate ||
      count * kPerAtomStagingEstimate > limits.max_staging_bytes) {
    return Result<StructureDocument>::failure(invalid(
        "molfile provider staging estimate exceeds the configured byte budget",
        "raise MolfileReadLimits::max_staging_bytes only for a trusted input"));
  }
  std::vector<abi18::Atom> atoms(count);
  int options{};
  progress(context, 0.2, "molfile-structure");
  if (context.cancellation.is_cancelled()) {
    return Result<StructureDocument>::failure(cancelled("structure"));
  }
  if (plugin->read_structure(read_handle, &options, atoms.data()) !=
      abi18::kSuccess) {
    return Result<StructureDocument>::failure(
        invalid("molfile provider structure callback failed"));
  }
  if (context.cancellation.is_cancelled()) {
    return Result<StructureDocument>::failure(cancelled("structure"));
  }
  if ((options & profile->required_options) != profile->required_options) {
    return Result<StructureDocument>::failure(invalid(
        "molfile provider did not supply the atom fields required by the " +
        std::string{profile->format_name} + " channel"));
  }
  std::vector<std::string> loss;
  loss.emplace_back("source_atom_serial");
  loss.emplace_back("velocity");
  loss.emplace_back("force");
  if ((options & abi18::kAtomicNumber) == 0) {
    loss.emplace_back("atomic_number_inferred_from_name");
  }
  if (profile->has_coordinates) {
    loss.emplace_back("physical_time");
  }

  std::vector<model::Bond> bonds;
  if (plugin->read_bonds != nullptr) {
    progress(context, 0.35, "molfile-bonds");
    int bond_count{};
    int* from{};
    int* to{};
    float* orders{};
    int* types{};
    int type_count{};
    char** type_names{};
    if (plugin->read_bonds(read_handle, &bond_count, &from, &to, &orders,
                           &types, &type_count, &type_names) !=
        abi18::kSuccess) {
      return Result<StructureDocument>::failure(
          invalid("molfile provider bond callback failed"));
    }
    if (context.cancellation.is_cancelled()) {
      return Result<StructureDocument>::failure(cancelled("bonds"));
    }
    if (bond_count < 0) {
      return Result<StructureDocument>::failure(invalid(
          "molfile provider returned a negative bond count"));
    }
    const auto total_bonds = static_cast<std::size_t>(bond_count);
    if (total_bonds != 0U && (from == nullptr || to == nullptr)) {
      return Result<StructureDocument>::failure(invalid(
          "molfile provider returned bonds without index arrays"));
    }
    constexpr std::size_t kPerBondStagingEstimate =
        sizeof(model::Bond) + 2U * sizeof(int) + 64U;
    if (total_bonds > limits.max_bonds ||
        total_bonds > (std::numeric_limits<std::size_t>::max)() /
                          kPerBondStagingEstimate ||
        count * kPerAtomStagingEstimate +
                total_bonds * kPerBondStagingEstimate >
            limits.max_staging_bytes) {
      return Result<StructureDocument>::failure(invalid(
          "molfile provider bond count exceeds the configured budget: " +
              std::to_string(total_bonds),
          "raise MolfileReadLimits::max_bonds/max_staging_bytes only for a "
          "trusted input"));
    }
    std::set<std::pair<int, int>> seen_bonds;
    std::size_t duplicate_bonds{};
    std::size_t unmapped_orders{};
    bonds.reserve(total_bonds);
    for (std::size_t index = 0; index < total_bonds; ++index) {
      if (index != 0U && index % 65536U == 0U &&
          context.cancellation.is_cancelled()) {
        return Result<StructureDocument>::failure(cancelled("bonds"));
      }
      // ABI 18 bond endpoints are 1-based atom indices.
      const int first = from[index];
      const int second = to[index];
      if (first < 1 || second < 1 || first > atom_count ||
          second > atom_count || first == second) {
        return Result<StructureDocument>::failure(invalid(
            "molfile provider returned an invalid bond endpoint: " +
            std::to_string(first) + "-" + std::to_string(second)));
      }
      if (!seen_bonds
               .insert({std::min(first, second), std::max(first, second)})
               .second) {
        ++duplicate_bonds;
        continue;
      }
      model::BondOrder order = model::BondOrder::unknown;
      if (orders != nullptr) {
        const auto value = static_cast<double>(orders[index]);
        if (!std::isfinite(value)) {
          return Result<StructureDocument>::failure(invalid(
              "molfile provider returned a non-finite bond order"));
        }
        const auto near = [value](double expected) {
          return std::abs(value - expected) < 1.0e-3;
        };
        if (near(1.0)) order = model::BondOrder::single;
        else if (near(2.0)) order = model::BondOrder::double_bond;
        else if (near(3.0)) order = model::BondOrder::triple;
        else if (near(1.5)) order = model::BondOrder::aromatic;
        else ++unmapped_orders;
      }
      bonds.emplace_back(
          model::AtomIndex{static_cast<std::size_t>(first - 1)},
          model::AtomIndex{static_cast<std::size_t>(second - 1)}, order,
          model::BondQuery::none, model::BondStereo::none,
          orders != nullptr && order != model::BondOrder::unknown
              ? model::ChemicalAnnotationOrigin::explicit_input
              : model::ChemicalAnnotationOrigin::unspecified);
    }
    if (orders == nullptr && total_bonds != 0U) {
      loss.emplace_back("bond_order_not_provided");
    }
    if (unmapped_orders != 0U) {
      loss.emplace_back("bond_order_unmapped:" +
                        std::to_string(unmapped_orders));
    }
    if (duplicate_bonds != 0U) {
      loss.emplace_back("duplicate_bonds_dropped:" +
                        std::to_string(duplicate_bonds));
    }
    if (type_count > 0) loss.emplace_back("bond_type_names");
  } else {
    loss.emplace_back("connectivity_not_provided");
  }

  if (count > (std::numeric_limits<std::size_t>::max)() / 3U) {
    return Result<StructureDocument>::failure(
        invalid("molfile coordinate buffer size overflow"));
  }
  std::vector<float> coordinate_values;
  abi18::Timestep timestep{};
  if (profile->has_coordinates) {
    if (plugin->read_next_timestep == nullptr) {
      return Result<StructureDocument>::failure(invalid(
          "molfile provider has no timestep callback for the " +
          std::string{profile->format_name} + " coordinate channel"));
    }
    coordinate_values.resize(count * 3U);
    timestep.coordinates = coordinate_values.data();
    progress(context, 0.5, "molfile-timestep");
    if (context.cancellation.is_cancelled()) {
      return Result<StructureDocument>::failure(cancelled("timestep"));
    }
    if (plugin->read_next_timestep(read_handle, atom_count, &timestep) !=
        abi18::kSuccess) {
      return Result<StructureDocument>::failure(
          invalid("molfile provider timestep callback failed"));
    }
    if (context.cancellation.is_cancelled()) {
      return Result<StructureDocument>::failure(cancelled("timestep"));
    }
  }

  model::TopologyBuilder topology_builder;
  std::map<std::tuple<std::string, int, std::string, std::string, std::string>,
           model::ResidueIndex>
      residues;
  const auto has = [options](int flag) { return (options & flag) != 0; };
  std::vector<double> charges;
  std::vector<double> radii;
  std::vector<double> masses;
  std::vector<double> occupancy;
  std::vector<double> b_factor;
  std::vector<std::string> atom_types;
  std::vector<model::Vec3f> positions;
  charges.reserve(has(abi18::kCharge) ? count : 0U);
  radii.reserve(has(abi18::kRadius) ? count : 0U);
  masses.reserve(has(abi18::kMass) ? count : 0U);
  occupancy.reserve(has(abi18::kOccupancy) ? count : 0U);
  b_factor.reserve(has(abi18::kBFactor) ? count : 0U);
  atom_types.reserve(count);
  positions.reserve(profile->has_coordinates ? count : 0U);
  bool synthetic_residue{};
  progress(context, 0.7, "molfile-convert");
  if (context.cancellation.is_cancelled()) {
    return Result<StructureDocument>::failure(cancelled("conversion"));
  }
  for (std::size_t index = 0; index < count; ++index) {
    if (index != 0U && index % 4096U == 0U) {
      if (context.cancellation.is_cancelled()) {
        return Result<StructureDocument>::failure(cancelled("conversion"));
      }
      progress(context,
               0.7 + 0.2 * static_cast<double>(index) /
                           static_cast<double>(count),
               "molfile-convert");
    }
    const auto name = bounded_string(atoms[index].name, "atom name");
    const auto type = bounded_string(atoms[index].type, "atom type");
    auto residue_name =
        bounded_string(atoms[index].residue_name, "residue name");
    const auto segment =
        bounded_string(atoms[index].segment_id, "segment ID");
    const auto chain = bounded_string(atoms[index].chain, "chain ID");
    auto alternate = Result<std::string>::success({});
    if (has(abi18::kAltLoc)) {
      alternate = bounded_string(atoms[index].alternate_location,
                                 "alternate location");
    }
    auto insertion = Result<std::string>::success({});
    if (has(abi18::kInsertion)) {
      insertion =
          bounded_string(atoms[index].insertion_code, "insertion code");
    }
    if (!name.has_value())
      return Result<StructureDocument>::failure(name.error());
    if (!type.has_value())
      return Result<StructureDocument>::failure(type.error());
    if (!residue_name.has_value())
      return Result<StructureDocument>::failure(residue_name.error());
    if (!segment.has_value())
      return Result<StructureDocument>::failure(segment.error());
    if (!chain.has_value())
      return Result<StructureDocument>::failure(chain.error());
    if (!alternate.has_value())
      return Result<StructureDocument>::failure(alternate.error());
    if (!insertion.has_value())
      return Result<StructureDocument>::failure(insertion.error());
    if (residue_name.value().empty() && profile->synthetic_residue) {
      residue_name = Result<std::string>::success("MOL");
      synthetic_residue = true;
    }
    if (name.value().empty() || residue_name.value().empty()) {
      return Result<StructureDocument>::failure(
          invalid("molfile provider returned an empty atom or residue name"));
    }
    if (has(abi18::kCharge)) {
      const auto charge = static_cast<double>(atoms[index].charge);
      if (!std::isfinite(charge)) {
        return Result<StructureDocument>::failure(
            invalid("molfile provider returned a non-finite charge"));
      }
      charges.push_back(charge);
    }
    if (has(abi18::kRadius)) {
      const auto radius = static_cast<double>(atoms[index].radius);
      if (!std::isfinite(radius) || radius < 0.0 ||
          (profile->radius_positive && radius <= 0.0)) {
        return Result<StructureDocument>::failure(invalid(
            "molfile provider returned an invalid radius"));
      }
      radii.push_back(radius);
    }
    if (has(abi18::kMass)) {
      const auto mass = static_cast<double>(atoms[index].mass);
      if (!std::isfinite(mass) || mass < 0.0) {
        return Result<StructureDocument>::failure(
            invalid("molfile provider returned an invalid mass"));
      }
      masses.push_back(mass);
    }
    if (has(abi18::kOccupancy)) {
      const auto value = static_cast<double>(atoms[index].occupancy);
      if (!std::isfinite(value)) {
        return Result<StructureDocument>::failure(
            invalid("molfile provider returned a non-finite occupancy"));
      }
      occupancy.push_back(value);
    }
    if (has(abi18::kBFactor)) {
      const auto value = static_cast<double>(atoms[index].b_factor);
      if (!std::isfinite(value)) {
        return Result<StructureDocument>::failure(
            invalid("molfile provider returned a non-finite B-factor"));
      }
      b_factor.push_back(value);
    }
    if (profile->has_coordinates) {
      const auto& x = coordinate_values[index * 3U];
      const auto& y = coordinate_values[index * 3U + 1U];
      const auto& z = coordinate_values[index * 3U + 2U];
      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return Result<StructureDocument>::failure(
            invalid("molfile provider returned non-finite coordinates"));
      }
      positions.push_back({x, y, z});
    }
    std::optional<std::uint8_t> atomic_number;
    if (has(abi18::kAtomicNumber)) {
      if (atoms[index].atomic_number <= 0 || atoms[index].atomic_number > 118) {
        return Result<StructureDocument>::failure(
            invalid("molfile provider returned an invalid atomic number"));
      }
      atomic_number = static_cast<std::uint8_t>(atoms[index].atomic_number);
    } else {
      atomic_number = infer_element(name.value(), residue_name.value());
    }
    if (!atomic_number.has_value() || atomic_number.value() == 0U) {
      return Result<StructureDocument>::failure(invalid(
          "could not infer an element from molfile atom name: " +
          name.value()));
    }
    const auto key = std::tuple{chain.value(), atoms[index].residue_id,
                                residue_name.value(), insertion.value(),
                                segment.value()};
    auto residue = residues.find(key);
    if (residue == residues.end()) {
      const auto added = topology_builder.add_residue(
          {residue_name.value(), atoms[index].residue_id, insertion.value(),
           chain.value(), segment.value()});
      if (!added.has_value()) {
        return Result<StructureDocument>::failure(added.error());
      }
      residue = residues.emplace(key, added.value()).first;
    }
    const auto added = topology_builder.add_atom(
        {name.value(), atomic_number.value(), residue->second,
         has(abi18::kAltLoc) ? alternate.value() : std::string{}, 0,
         std::nullopt});
    if (!added.has_value()) {
      return Result<StructureDocument>::failure(added.error());
    }
    atom_types.push_back(type.value());
  }
  if (synthetic_residue) loss.emplace_back("residue_identity_synthetic");
  for (const auto& bond : bonds) {
    if (const auto error = topology_builder.add_bond(bond);
        error.has_value()) {
      return Result<StructureDocument>::failure(*error);
    }
  }
  const std::string origin =
      "molfile ABI 18 " + std::string{profile->format_name} + " provider";
  std::vector<std::tuple<std::string, model::AtomPropertyColumn,
                         model::PropertyMetadata>>
      properties;
  if (has(abi18::kCharge)) {
    properties.emplace_back("partial_charge", std::move(charges),
                            model::PropertyMetadata{"elementary_charge",
                                                    origin, {}});
  }
  if (has(abi18::kRadius)) {
    properties.emplace_back(
        profile->format == StructureFormat::pqr ? "pqr.radius" : "vdw_radius",
        std::move(radii), model::PropertyMetadata{"angstrom", origin, {}});
  }
  if (has(abi18::kMass)) {
    properties.emplace_back("mass", std::move(masses),
                            model::PropertyMetadata{"dalton", origin, {}});
  }
  properties.emplace_back(
      "molfile.atom_type", std::move(atom_types),
      model::PropertyMetadata{std::nullopt, "molfile ABI 18 provider", {}});
  for (auto& property : properties) {
    if (const auto error = topology_builder.add_property(
            std::move(std::get<0>(property)),
            std::move(std::get<1>(property)),
            std::move(std::get<2>(property)));
        error.has_value()) {
      return Result<StructureDocument>::failure(*error);
    }
  }
  if (!profile->has_coordinates && (has(abi18::kOccupancy) || has(abi18::kBFactor))) {
    loss.emplace_back("occupancy_bfactor_without_coordinates");
  }
  std::string loss_text;
  for (const auto& entry : loss) {
    if (!loss_text.empty()) loss_text += ',';
    loss_text += entry;
  }
  topology_builder.set_source_metadata("format",
                                       std::string{profile->format_name});
  topology_builder.set_source_metadata("provider", provider_id);
  topology_builder.set_source_metadata("source_name", path.string());
  topology_builder.set_source_metadata("molfile.loss", loss_text);
  progress(context, 0.92, "molfile-validate");
  if (context.cancellation.is_cancelled()) {
    return Result<StructureDocument>::failure(cancelled("validation"));
  }
  const auto topology = topology_builder.build();
  if (!topology.has_value()) {
    return Result<StructureDocument>::failure(topology.error());
  }
  std::vector<std::shared_ptr<const model::CoordinateFrame>> frames;
  if (profile->has_coordinates) {
    model::FrameMetadata frame_metadata;
    frame_metadata.coordinate_unit = operation::LengthUnit::angstrom;
    const auto all_zero_cell = timestep.a == 0.0F && timestep.b == 0.0F &&
                               timestep.c == 0.0F && timestep.alpha == 0.0F &&
                               timestep.beta == 0.0F && timestep.gamma == 0.0F;
    if (!all_zero_cell) {
      const auto cell = detail::make_unit_cell(
          timestep.a, timestep.b, timestep.c, timestep.alpha, timestep.beta,
          timestep.gamma, path.string(), 1U);
      if (!cell.has_value()) {
        return Result<StructureDocument>::failure(cell.error());
      }
      frame_metadata.unit_cell = cell.value();
    }
    // Presence columns keep "not supplied by the plugin" distinct from zero.
    if (has(abi18::kOccupancy)) {
      frame_metadata.atom_properties.emplace(
          "occupancy",
          model::AtomProperty{std::move(occupancy),
                              model::PropertyMetadata{std::nullopt, origin, {}}});
      frame_metadata.atom_properties.emplace(
          "occupancy_present",
          model::AtomProperty{
              model::BooleanColumn{std::vector<std::uint8_t>(count, 1U)},
              model::PropertyMetadata{std::nullopt, origin, {}}});
    }
    if (has(abi18::kBFactor)) {
      frame_metadata.atom_properties.emplace(
          "b_iso_or_equiv",
          model::AtomProperty{
              std::move(b_factor),
              model::PropertyMetadata{"angstrom^2", origin, {}}});
      frame_metadata.atom_properties.emplace(
          "b_iso_or_equiv_present",
          model::AtomProperty{
              model::BooleanColumn{std::vector<std::uint8_t>(count, 1U)},
              model::PropertyMetadata{std::nullopt, origin, {}}});
    }
    const auto frame = model::CoordinateFrame::create(
        model::CoordinateBuffer{std::move(positions)}, std::nullopt, {},
        std::move(frame_metadata));
    if (!frame.has_value()) {
      return Result<StructureDocument>::failure(frame.error());
    }
    frames.push_back(frame.value());
  }
  const auto coordinates =
      model::InMemoryCoordinateSource::create(count, std::move(frames));
  if (!coordinates.has_value()) {
    return Result<StructureDocument>::failure(coordinates.error());
  }
  StructureData structure;
  structure.name = structure_name(path);
  structure.topology = topology.value();
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", std::string{profile->format_name});
  structure.metadata.emplace("provider", provider_id);
  if (!profile->has_coordinates) {
    structure.metadata.emplace("coordinate_state", "absent");
  }
  StructureDocument document;
  document.format = profile->format;
  document.source_name = path.string();
  document.structures.push_back(std::move(structure));
  if (context.cancellation.is_cancelled()) {
    return Result<StructureDocument>::failure(cancelled("commit staging"));
  }
  progress(context, 1.0, "molfile-complete");
  if (context.cancellation.is_cancelled()) {
    return Result<StructureDocument>::failure(cancelled("completion"));
  }
  return Result<StructureDocument>::success(std::move(document));
  } catch (const std::bad_alloc&) {
    return Result<StructureDocument>::failure(internal(
        "molfile provider staging allocation failed",
        "lower the atom/byte limits or free memory"));
  } catch (const std::exception& exception) {
    return Result<StructureDocument>::failure(internal(
        "molfile provider raised an exception: " +
        std::string{exception.what()}));
  } catch (...) {
    return Result<StructureDocument>::failure(
        internal("molfile provider raised an unknown exception"));
  }
}

Result<VolumeDocument> MolfileProviderRegistry::read_volume(
    const std::filesystem::path& path, std::string provider_id,
    operation::TaskContext& context, operation::LengthUnit coordinate_unit,
    std::optional<std::string> name, MolfileVolumeLimits limits) const {
  if (limits.max_values == 0U || limits.max_staging_bytes == 0U) {
    return Result<VolumeDocument>::failure(invalid(
        "molfile volume limits must have positive value and byte budgets"));
  }
  if (name.has_value() && name->empty()) {
    return Result<VolumeDocument>::failure(
        invalid("explicit volume object name must not be empty"));
  }
  if (context.cancellation.is_cancelled()) {
    return Result<VolumeDocument>::failure(cancelled("volume startup"));
  }
  const Impl::LibraryRecord* selected_library{};
  std::size_t selected_index{};
  {
    std::shared_lock registry_lock{impl_->libraries_mutex};
    for (const auto& library : impl_->libraries) {
      for (std::size_t index = 0; index < library->descriptors.size(); ++index) {
        if (library->descriptors[index].provider_id == provider_id) {
          selected_library = library.get();
          selected_index = index;
          break;
        }
      }
      if (selected_library != nullptr) break;
    }
  }
  if (selected_library == nullptr) {
    return Result<VolumeDocument>::failure(
        invalid("molfile provider is not registered: " + provider_id));
  }
  const auto& descriptor = selected_library->descriptors[selected_index];
  const auto* plugin = selected_library->plugins[selected_index];
  const auto* profile = find_volume_profile(descriptor.plugin_name);
  if (profile == nullptr) {
    return Result<VolumeDocument>::failure(Error{
        ErrorCode::unsupported,
        "molfile plugin has no volume channel profile: " + provider_id,
        "only dx and ccp4 are dispatched as volumes"});
  }
  if (plugin->read_volumetric_metadata == nullptr ||
      plugin->read_volumetric_data == nullptr) {
    return Result<VolumeDocument>::failure(Error{
        ErrorCode::unsupported,
        "molfile plugin has no volumetric callbacks: " + provider_id, {}});
  }
  try {
    progress(context, 0.0, "molfile-volume-open");
    std::unique_lock callback_lock{
        *selected_library->callback_mutexes[selected_index], std::defer_lock};
    if (!descriptor.thread_safe) {
      progress(context, 0.0, "molfile-wait-provider");
      while (!callback_lock.try_lock_for(std::chrono::milliseconds{10})) {
        if (context.cancellation.is_cancelled()) {
          return Result<VolumeDocument>::failure(
              cancelled("provider scheduling"));
        }
      }
    }
    int atom_count{};
    void* handle = plugin->open_file_read(
        path.string().c_str(), descriptor.plugin_name.c_str(), &atom_count);
    if (handle == nullptr) {
      return Result<VolumeDocument>::failure(
          invalid("molfile provider could not open volume: " + path.string()));
    }
    struct CloseGuard {
      const abi18::MolfilePlugin* plugin;
      void* handle;
      ~CloseGuard() { plugin->close_file_read(handle); }
    } close_guard{plugin, handle};
    if (context.cancellation.is_cancelled()) {
      return Result<VolumeDocument>::failure(cancelled("volume open"));
    }
    int set_count{};
    abi18::VolumetricMetadata* metadata{};
    if (plugin->read_volumetric_metadata(handle, &set_count, &metadata) !=
        abi18::kSuccess) {
      return Result<VolumeDocument>::failure(
          invalid("molfile provider volumetric metadata callback failed"));
    }
    if (set_count != 1 || metadata == nullptr) {
      return Result<VolumeDocument>::failure(Error{
          set_count > 1 ? ErrorCode::unsupported : ErrorCode::invalid_argument,
          "molfile volume import requires exactly one scalar dataset, found " +
              std::to_string(set_count),
          "multi-dataset files are not imported because the ABI dataset "
          "array stride is not verified"});
    }
    if (context.cancellation.is_cancelled()) {
      return Result<VolumeDocument>::failure(cancelled("volume metadata"));
    }
    const auto& info = *metadata;
    if (info.xsize <= 0 || info.ysize <= 0 || info.zsize <= 0) {
      return Result<VolumeDocument>::failure(
          invalid("molfile provider returned a non-positive grid dimension"));
    }
    const auto nx = static_cast<std::size_t>(info.xsize);
    const auto ny = static_cast<std::size_t>(info.ysize);
    const auto nz = static_cast<std::size_t>(info.zsize);
    constexpr auto kMax = (std::numeric_limits<std::size_t>::max)();
    if (nx > kMax / ny || nx * ny > kMax / nz) {
      return Result<VolumeDocument>::failure(
          invalid("molfile volume grid size overflows"));
    }
    const auto values = nx * ny * nz;
    constexpr std::size_t kPerValueStaging = 2U * sizeof(float);
    if (values > limits.max_values ||
        values > kMax / kPerValueStaging ||
        values * kPerValueStaging > limits.max_staging_bytes) {
      return Result<VolumeDocument>::failure(invalid(
          "molfile volume exceeds the configured value or byte budget: " +
              std::to_string(values) + " values",
          "raise MolfileVolumeLimits only for a trusted input"));
    }
    if (nx < 2U || ny < 2U || nz < 2U) {
      return Result<VolumeDocument>::failure(Error{
          ErrorCode::unsupported,
          "molfile volume axes with a single sample have no defined spacing "
          "in the ABI (extent / (count - 1))",
          "use the native reader for singleton-axis grids"});
    }
    const float* axes[3]{info.xaxis, info.yaxis, info.zaxis};
    const std::size_t counts[3]{nx, ny, nz};
    std::array<model::Vec3d, 3U> deltas{};
    for (std::size_t axis = 0; axis < 3U; ++axis) {
      for (std::size_t component = 0; component < 3U; ++component) {
        if (!std::isfinite(axes[axis][component]) ||
            !std::isfinite(info.origin[component])) {
          return Result<VolumeDocument>::failure(invalid(
              "molfile provider returned a non-finite grid origin or axis"));
        }
      }
      const auto divisor = static_cast<double>(counts[axis] - 1U);
      deltas[axis] = {static_cast<double>(axes[axis][0]) / divisor,
                      static_cast<double>(axes[axis][1]) / divisor,
                      static_cast<double>(axes[axis][2]) / divisor};
    }
    const auto dataset = bounded_string(metadata->dataname, "dataset name");
    if (!dataset.has_value()) {
      return Result<VolumeDocument>::failure(dataset.error());
    }
    const std::string& dataname = dataset.value();
    progress(context, 0.4, "molfile-volume-data");
    std::vector<float> block(values);
    if (plugin->read_volumetric_data(handle, 0, block.data(), nullptr) !=
        abi18::kSuccess) {
      return Result<VolumeDocument>::failure(
          invalid("molfile provider volumetric data callback failed"));
    }
    if (context.cancellation.is_cancelled()) {
      return Result<VolumeDocument>::failure(cancelled("volume data"));
    }
    progress(context, 0.7, "molfile-volume-transpose");
    std::vector<float> scalars(values);
    for (std::size_t z = 0; z < nz; ++z) {
      if (z % 16U == 0U && context.cancellation.is_cancelled()) {
        return Result<VolumeDocument>::failure(cancelled("volume conversion"));
      }
      for (std::size_t y = 0; y < ny; ++y) {
        for (std::size_t x = 0; x < nx; ++x) {
          const auto value = block[(z * ny + y) * nx + x];
          if (!std::isfinite(value)) {
            return Result<VolumeDocument>::failure(invalid(
                "molfile provider returned a non-finite scalar value"));
          }
          scalars[(x * ny + y) * nz + z] = value;
        }
      }
    }
    model::VolumeMetadata volume_metadata;
    volume_metadata.coordinate_unit = coordinate_unit;
    volume_metadata.fields.emplace("molfile.provider", provider_id);
    if (!dataname.empty()) volume_metadata.fields.emplace("molfile.dataname", dataname);
    const auto grid = model::VolumeGrid::create(
        {nx, ny, nz},
        {static_cast<double>(info.origin[0]),
         static_cast<double>(info.origin[1]),
         static_cast<double>(info.origin[2])},
        deltas, model::VolumeScalarBuffer{std::move(scalars)},
        std::move(volume_metadata));
    if (!grid.has_value()) return Result<VolumeDocument>::failure(grid.error());
    VolumeDocument document;
    document.format = profile->format;
    document.volumes.push_back(
        {name.has_value() ? *name : structure_name(path), grid.value()});
    progress(context, 1.0, "molfile-volume-complete");
    return Result<VolumeDocument>::success(std::move(document));
  } catch (const std::bad_alloc&) {
    return Result<VolumeDocument>::failure(internal(
        "molfile volume staging allocation failed",
        "lower the value/byte limits or free memory"));
  } catch (const std::exception& exception) {
    return Result<VolumeDocument>::failure(internal(
        "molfile provider raised an exception: " +
        std::string{exception.what()}));
  } catch (...) {
    return Result<VolumeDocument>::failure(
        internal("molfile provider raised an unknown exception"));
  }
}

Result<std::shared_ptr<const MolfileTrajectorySource>>
MolfileProviderRegistry::open_trajectory(
    const std::filesystem::path& path, std::string provider_id,
    std::size_t expected_atom_count, operation::TaskContext& context,
    MolfileTrajectoryLimits limits) const {
  using SourceResult =
      Result<std::shared_ptr<const MolfileTrajectorySource>>;
  if (limits.max_atoms == 0U || limits.max_frames == 0U ||
      limits.max_frame_bytes == 0U) {
    return SourceResult::failure(invalid(
        "molfile trajectory limits must have positive atom, frame and byte "
        "budgets"));
  }
  if (context.cancellation.is_cancelled()) {
    return SourceResult::failure(cancelled("trajectory startup"));
  }
  const Impl::LibraryRecord* selected_library{};
  std::size_t selected_index{};
  {
    std::shared_lock registry_lock{impl_->libraries_mutex};
    for (const auto& library : impl_->libraries) {
      for (std::size_t index = 0; index < library->descriptors.size(); ++index) {
        if (library->descriptors[index].provider_id == provider_id) {
          selected_library = library.get();
          selected_index = index;
          break;
        }
      }
      if (selected_library != nullptr) break;
    }
  }
  if (selected_library == nullptr) {
    return SourceResult::failure(
        invalid("molfile provider is not registered: " + provider_id));
  }
  const auto& descriptor = selected_library->descriptors[selected_index];
  const auto* plugin = selected_library->plugins[selected_index];
  if (find_trajectory_profile(descriptor.plugin_name) == nullptr) {
    return SourceResult::failure(Error{
        ErrorCode::unsupported,
        "molfile plugin has no trajectory channel profile: " + provider_id,
        "only dcd, trr, xtc, crd, crdbox, lammpstrj, netcdf, binpos, rst7 "
        "and vcf are dispatched as trajectories"});
  }
  if (plugin->read_next_timestep == nullptr) {
    return SourceResult::failure(Error{
        ErrorCode::unsupported,
        "molfile plugin has no timestep callback: " + provider_id, {}});
  }
  if (expected_atom_count == 0U || expected_atom_count > limits.max_atoms ||
      expected_atom_count >
          (std::numeric_limits<std::size_t>::max)() / 3U / sizeof(double) ||
      expected_atom_count * 3U * (sizeof(float) + sizeof(model::Vec3f)) >
          limits.max_frame_bytes ||
      expected_atom_count >
          static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    return SourceResult::failure(invalid(
        "molfile trajectory atom count is outside the configured budget: " +
            std::to_string(expected_atom_count),
        "raise MolfileTrajectoryLimits only for a trusted input"));
  }
  try {
    SequentialMolfileSource::Config config;
    config.keepalive = impl_;
    config.plugin = plugin;
    config.callback_mutex = descriptor.thread_safe
                                ? nullptr
                                : selected_library->callback_mutexes
                                      [selected_index]
                                          .get();
    config.plugin_name = descriptor.plugin_name;
    config.provider_id = provider_id;
    config.path = path;
    config.atom_count = expected_atom_count;
    config.limits = limits;
    auto source = std::make_shared<SequentialMolfileSource>(std::move(config));
    progress(context, 0.1, "molfile-trajectory-open");
    const auto counted = source->count_frames(context);
    if (!counted.has_value()) return SourceResult::failure(counted.error());
    if (counted.value() == 0U) {
      return SourceResult::failure(invalid(
          "molfile trajectory contains no readable frames: " + path.string()));
    }
    progress(context, 1.0, "molfile-trajectory-ready");
    return SourceResult::success(std::move(source));
  } catch (const std::bad_alloc&) {
    return SourceResult::failure(internal(
        "molfile trajectory staging allocation failed",
        "lower the atom/byte limits or free memory"));
  } catch (const std::exception& exception) {
    return SourceResult::failure(internal(
        "molfile provider raised an exception: " +
        std::string{exception.what()}));
  } catch (...) {
    return SourceResult::failure(
        internal("molfile provider raised an unknown exception"));
  }
}

std::string_view to_string(MolfileProviderTrust trust) noexcept {
  switch (trust) {
    case MolfileProviderTrust::application_bundled:
      return "application_bundled";
    case MolfileProviderTrust::user_approved:
      return "user_approved";
    case MolfileProviderTrust::explicit_path:
      return "explicit_path";
  }
  return "explicit_path";
}

}  // namespace molshredder::io
