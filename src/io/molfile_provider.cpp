#include "molshredder/io/molfile_provider.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
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
          plugin->read_structure == nullptr ||
          plugin->read_next_timestep == nullptr ||
          plugin->close_file_read == nullptr) {
        return Result<std::unique_ptr<LibraryRecord>>::failure(invalid(
            "molfile provider lacks the structure/timestep read callbacks: " +
            provider_id));
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
    : impl_{std::make_unique<Impl>()} {}

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
      sizeof(abi18::Atom) + 3U * sizeof(float) + 2U * sizeof(double) +
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
  if ((options & abi18::kCharge) == 0 ||
      (options & abi18::kRadius) == 0) {
    return Result<StructureDocument>::failure(invalid(
        "PQR molfile provider did not supply charge and radius fields"));
  }
  if (count > (std::numeric_limits<std::size_t>::max)() / 3U) {
    return Result<StructureDocument>::failure(
        invalid("molfile coordinate buffer size overflow"));
  }
  std::vector<float> coordinate_values(count * 3U);
  abi18::Timestep timestep{};
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

  model::TopologyBuilder topology_builder;
  std::map<std::tuple<std::string, int, std::string, std::string, std::string>,
           model::ResidueIndex>
      residues;
  std::vector<double> charges;
  std::vector<double> radii;
  std::vector<std::string> atom_types;
  std::vector<model::Vec3f> positions;
  charges.reserve(count);
  radii.reserve(count);
  atom_types.reserve(count);
  positions.reserve(count);
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
    const auto residue_name =
        bounded_string(atoms[index].residue_name, "residue name");
    const auto segment =
        bounded_string(atoms[index].segment_id, "segment ID");
    const auto chain = bounded_string(atoms[index].chain, "chain ID");
    auto alternate = Result<std::string>::success({});
    if ((options & abi18::kAltLoc) != 0) {
      alternate = bounded_string(atoms[index].alternate_location,
                                 "alternate location");
    }
    auto insertion = Result<std::string>::success({});
    if ((options & abi18::kInsertion) != 0) {
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
    if (name.value().empty() || residue_name.value().empty()) {
      return Result<StructureDocument>::failure(
          invalid("molfile provider returned an empty atom or residue name"));
    }
    const auto charge = static_cast<double>(atoms[index].charge);
    const auto radius = static_cast<double>(atoms[index].radius);
    const auto& x = coordinate_values[index * 3U];
    const auto& y = coordinate_values[index * 3U + 1U];
    const auto& z = coordinate_values[index * 3U + 2U];
    if (!std::isfinite(charge) || !std::isfinite(radius) || radius <= 0.0 ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
      return Result<StructureDocument>::failure(invalid(
          "molfile provider returned non-finite coordinates/charge or a non-positive radius"));
    }
    std::optional<std::uint8_t> atomic_number;
    if ((options & abi18::kAtomicNumber) != 0) {
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
         (options & abi18::kAltLoc) != 0 ? alternate.value() : std::string{},
         0, std::nullopt});
    if (!added.has_value()) {
      return Result<StructureDocument>::failure(added.error());
    }
    charges.push_back(charge);
    radii.push_back(radius);
    atom_types.push_back(type.value());
    positions.push_back({x, y, z});
  }
  for (auto property :
       {std::tuple<std::string, model::AtomPropertyColumn,
                   model::PropertyMetadata>{
            "partial_charge", std::move(charges),
            {"elementary_charge", "molfile ABI 18 PQR provider", {}}},
        {"pqr.radius", std::move(radii),
         {"angstrom", "molfile ABI 18 PQR provider", {}}},
        {"molfile.atom_type", std::move(atom_types),
         {std::nullopt, "molfile ABI 18 provider", {}}}}) {
    if (const auto error = topology_builder.add_property(
            std::move(std::get<0>(property)),
            std::move(std::get<1>(property)),
            std::move(std::get<2>(property)));
        error.has_value()) {
      return Result<StructureDocument>::failure(*error);
    }
  }
  topology_builder.set_source_metadata("format", "pqr");
  topology_builder.set_source_metadata("provider", provider_id);
  topology_builder.set_source_metadata("source_name", path.string());
  progress(context, 0.92, "molfile-validate");
  if (context.cancellation.is_cancelled()) {
    return Result<StructureDocument>::failure(cancelled("validation"));
  }
  const auto topology = topology_builder.build();
  if (!topology.has_value()) {
    return Result<StructureDocument>::failure(topology.error());
  }
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
  const auto frame = model::CoordinateFrame::create(
      model::CoordinateBuffer{std::move(positions)}, std::nullopt, {},
      std::move(frame_metadata));
  if (!frame.has_value()) {
    return Result<StructureDocument>::failure(frame.error());
  }
  const auto coordinates = model::InMemoryCoordinateSource::create(
      count, {frame.value()});
  if (!coordinates.has_value()) {
    return Result<StructureDocument>::failure(coordinates.error());
  }
  StructureData structure;
  structure.name = structure_name(path);
  structure.topology = topology.value();
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", "pqr");
  structure.metadata.emplace("provider", provider_id);
  StructureDocument document;
  document.format = StructureFormat::pqr;
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
