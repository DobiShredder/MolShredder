#include "molshredder/plugin/trust_store.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <utility>

#include "molshredder/command/json.hpp"
#include "molshredder/core/sha256.hpp"

namespace molshredder::plugin {
namespace {

using command::Value;
using operation::Error;
using operation::ErrorCode;
using operation::Result;

Error invalid(std::string message, std::string suggestion = {}) {
  return {ErrorCode::invalid_argument, std::move(message), std::move(suggestion)};
}

constexpr std::uintmax_t kMaxHashedBytes = 512U * 1024U * 1024U;

const std::string* field(const Value::Object& object, const char* key) {
  const auto found = object.find(key);
  return found == object.end() ? nullptr : std::get_if<std::string>(&found->second.data);
}

}  // namespace

std::filesystem::path default_trust_store_path() {
  if (const char* override_path = std::getenv("MOLSHREDDER_PLUGIN_TRUST_STORE");
      override_path != nullptr && *override_path != '\0') {
    return override_path;
  }
#if defined(_WIN32)
  const char* home = std::getenv("USERPROFILE");
#else
  const char* home = std::getenv("HOME");
#endif
  if (home == nullptr || *home == '\0') return {};
  return std::filesystem::path{home} / ".molshredder" / "plugin-trust.json";
}

Result<std::string> sha256_of_file(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    return Result<std::string>::failure(invalid("not a regular file: " + path.string()));
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > kMaxHashedBytes) {
    return Result<std::string>::failure(invalid("file is unreadable or too large to pin: " + path.string()));
  }
  std::ifstream input{path, std::ios::binary};
  if (!input) return Result<std::string>::failure(invalid("cannot read " + path.string()));
  const std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  return Result<std::string>::success(core::sha256_hex(bytes));
}

const TrustEntry* TrustStore::find(const std::string& id) const {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&](const TrustEntry& entry) { return entry.id == id; });
  return found == entries_.end() ? nullptr : &*found;
}

Result<TrustStore> TrustStore::load(const std::filesystem::path& path) {
  TrustStore store;
  store.path_ = path;
  std::error_code error;
  if (!std::filesystem::exists(path, error)) return Result<TrustStore>::success(std::move(store));
  std::ifstream input{path, std::ios::binary};
  if (!input) return Result<TrustStore>::failure(invalid("cannot read the plugin trust store: " + path.string()));
  const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  auto parsed = command::parse_json(text, command::JsonLimits{4U * 1024U * 1024U, 8U});
  const auto broken = [&](const std::string& why) {
    return Result<TrustStore>::failure(invalid(
        "plugin trust store is malformed (" + why + "): " + path.string(),
        "fix or remove the file; it is not treated as empty"));
  };
  if (!parsed.has_value()) return broken(parsed.error().message);
  const auto* root = std::get_if<Value::Object>(&parsed.value().data);
  if (root == nullptr) return broken("top level is not an object");
  const auto version = root->find("schema_version");
  const auto* number = version == root->end() ? nullptr : std::get_if<std::int64_t>(&version->second.data);
  if (number == nullptr || *number != 1) return broken("unsupported schema_version");
  const auto entries = root->find("entries");
  const auto* list = entries == root->end() ? nullptr : std::get_if<Value::Array>(&entries->second.data);
  if (list == nullptr) return broken("entries is missing");
  for (const auto& element : *list) {
    const auto* object = std::get_if<Value::Object>(&element.data);
    if (object == nullptr) return broken("an entry is not an object");
    TrustEntry entry;
    const auto* id = field(*object, "id");
    const auto* entry_version = field(*object, "version");
    const auto* mode = field(*object, "mode");
    const auto* manifest_path = field(*object, "manifest_path");
    const auto* manifest_sha = field(*object, "manifest_sha256");
    const auto* entry_path = field(*object, "entry_path");
    const auto* entry_sha = field(*object, "entry_sha256");
    const auto* approved_at = field(*object, "approved_at");
    const auto pinned_it = object->find("pinned_files");
    const auto* pinned_list = pinned_it == object->end() ? nullptr : std::get_if<Value::Array>(&pinned_it->second.data);
    if (pinned_list == nullptr) return broken("an entry has no pinned_files list");
    for (const auto& item : *pinned_list) {
      const auto* pinned_object = std::get_if<Value::Object>(&item.data);
      const auto* pinned_path = pinned_object != nullptr ? field(*pinned_object, "path") : nullptr;
      const auto* pinned_sha = pinned_object != nullptr ? field(*pinned_object, "sha256") : nullptr;
      if (pinned_path == nullptr || pinned_sha == nullptr || pinned_sha->size() != 64U) {
        return broken("a pinned file entry is incomplete");
      }
      entry.pinned_files.emplace_back(*pinned_path, *pinned_sha);
    }
    const auto allow = object->find("allow_in_process");
    const auto* allow_flag = allow == object->end() ? nullptr : std::get_if<bool>(&allow->second.data);
    if (id == nullptr || entry_version == nullptr || mode == nullptr || manifest_path == nullptr ||
        manifest_sha == nullptr || entry_path == nullptr || entry_sha == nullptr ||
        approved_at == nullptr || allow_flag == nullptr ||
        (*mode != "worker" && *mode != "in_process") || manifest_sha->size() != 64U ||
        entry_sha->size() != 64U) {
      return broken("an entry is incomplete");
    }
    entry.id = *id;
    entry.version = *entry_version;
    entry.mode = *mode == "worker" ? ExecutionMode::worker : ExecutionMode::in_process;
    entry.manifest_path = *manifest_path;
    entry.manifest_sha256 = *manifest_sha;
    entry.entry_path = *entry_path;
    entry.entry_sha256 = *entry_sha;
    entry.allow_in_process = *allow_flag;
    entry.approved_at = *approved_at;
    if (store.find(entry.id) != nullptr) return broken("duplicate plugin id '" + entry.id + "'");
    store.entries_.push_back(std::move(entry));
  }
  return Result<TrustStore>::success(std::move(store));
}

Result<TrustEntry> TrustStore::approve(const PluginManifest& manifest,
                                       const std::filesystem::path& manifest_path,
                                       bool allow_in_process, std::string approved_at) {
  if (allow_in_process && manifest.mode != ExecutionMode::in_process) {
    return Result<TrustEntry>::failure(invalid("--allow-in-process only applies to in-process plugins"));
  }
  if (manifest.mode == ExecutionMode::in_process && !allow_in_process) {
    return Result<TrustEntry>::failure(invalid(
        "'" + manifest.id + "' runs inside the MolShredder process and cannot be isolated",
        "approve it with --allow-in-process true if you trust it with full process access"));
  }
  std::error_code error;
  const auto canonical_manifest = std::filesystem::canonical(manifest_path, error);
  if (error) return Result<TrustEntry>::failure(invalid("manifest not found: " + manifest_path.string()));
  const auto entry_path = resolve_entry_path(manifest);
  if (!entry_path.has_value()) return Result<TrustEntry>::failure(entry_path.error());
  const auto manifest_hash = sha256_of_file(canonical_manifest);
  const auto entry_hash = sha256_of_file(entry_path.value());
  if (!manifest_hash.has_value()) return Result<TrustEntry>::failure(manifest_hash.error());
  if (!entry_hash.has_value()) return Result<TrustEntry>::failure(entry_hash.error());
  TrustEntry entry;
  const auto base = std::filesystem::canonical(manifest.directory, error);
  for (const auto& relative : manifest.pinned_files) {
    const auto resolved = std::filesystem::canonical(base / relative, error);
    const auto relation = error ? std::filesystem::path{} : std::filesystem::relative(resolved, base, error);
    if (error || relation.empty() || *relation.begin() == "..") {
      return Result<TrustEntry>::failure(invalid("pinned file is missing or outside the plugin directory: " + relative));
    }
    const auto hash = sha256_of_file(resolved);
    if (!hash.has_value()) return Result<TrustEntry>::failure(hash.error());
    entry.pinned_files.emplace_back(relative, hash.value());
  }
  entry.id = manifest.id;
  entry.version = manifest.version;
  entry.mode = manifest.mode;
  entry.manifest_path = canonical_manifest.string();
  entry.manifest_sha256 = manifest_hash.value();
  entry.entry_path = entry_path.value().string();
  entry.entry_sha256 = entry_hash.value();
  entry.allow_in_process = allow_in_process;
  entry.approved_at = std::move(approved_at);
  static_cast<void>(revoke(entry.id));
  entries_.push_back(entry);
  return Result<TrustEntry>::success(std::move(entry));
}

bool TrustStore::revoke(const std::string& id) {
  const auto before = entries_.size();
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&](const TrustEntry& entry) { return entry.id == id; }),
                 entries_.end());
  return entries_.size() != before;
}

Result<bool> TrustStore::save() const {
  if (path_.empty()) {
    return Result<bool>::failure(invalid("no trust store location",
                                         "pass --trust-store or set MOLSHREDDER_PLUGIN_TRUST_STORE"));
  }
  Value::Array list;
  for (const auto& entry : entries_) {
    Value::Array pinned;
    for (const auto& [relative, hash] : entry.pinned_files) {
      pinned.push_back(Value::Object{{"path", relative}, {"sha256", hash}});
    }
    list.push_back(Value::Object{
        {"pinned_files", std::move(pinned)},
        {"allow_in_process", entry.allow_in_process},
        {"approved_at", entry.approved_at},
        {"entry_path", entry.entry_path},
        {"entry_sha256", entry.entry_sha256},
        {"id", entry.id},
        {"manifest_path", entry.manifest_path},
        {"manifest_sha256", entry.manifest_sha256},
        {"mode", entry.mode == ExecutionMode::worker ? "worker" : "in_process"},
        {"version", entry.version}});
  }
  const auto text = command::to_json(Value::Object{{"entries", std::move(list)}, {"schema_version", 1}});
  if (!text.has_value()) return Result<bool>::failure(text.error());
  std::error_code error;
  std::filesystem::create_directories(path_.parent_path(), error);
  const auto temporary = path_.string() + ".tmp";
  {
    std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
    output << text.value() << '\n';
    if (!output) return Result<bool>::failure(invalid("cannot write " + temporary));
  }
  std::filesystem::rename(temporary, path_, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return Result<bool>::failure(invalid("cannot replace the trust store: " + path_.string()));
  }
  return Result<bool>::success(true);
}

Result<VerifiedPlugin> verify_approved(const TrustEntry& entry) {
  const auto manifest = load_manifest_file(entry.manifest_path);
  if (!manifest.has_value()) {
    return Result<VerifiedPlugin>::failure(Error{
        ErrorCode::invalid_argument,
        "approved plugin '" + entry.id + "' cannot be loaded: " + manifest.error().message,
        "approve it again after checking the files", {}});
  }
  const auto changed = [&](const char* what) {
    return Result<VerifiedPlugin>::failure(Error{
        ErrorCode::invalid_argument,
        "approved plugin '" + entry.id + "' changed since approval (" + what + ")",
        "review the plugin and approve it again", {}});
  };
  const auto manifest_hash = sha256_of_file(entry.manifest_path);
  if (!manifest_hash.has_value() || manifest_hash.value() != entry.manifest_sha256) return changed("manifest");
  const auto entry_path = resolve_entry_path(manifest.value());
  if (!entry_path.has_value()) return changed("entry file is missing");
  if (entry_path.value().string() != entry.entry_path) return changed("entry path");
  const auto entry_hash = sha256_of_file(entry_path.value());
  if (!entry_hash.has_value() || entry_hash.value() != entry.entry_sha256) return changed("entry file");
  const auto base = std::filesystem::path{manifest.value().directory};
  if (manifest.value().pinned_files.size() != entry.pinned_files.size()) return changed("pinned file list");
  for (std::size_t i = 0; i < entry.pinned_files.size(); ++i) {
    if (manifest.value().pinned_files[i] != entry.pinned_files[i].first) return changed("pinned file list");
    std::error_code ignored;
    const auto resolved = std::filesystem::canonical(base / entry.pinned_files[i].first, ignored);
    const auto relation = ignored ? std::filesystem::path{} : std::filesystem::relative(resolved, base, ignored);
    if (ignored || relation.empty() || *relation.begin() == "..") return changed("a pinned file is missing");
    const auto hash = sha256_of_file(resolved);
    if (!hash.has_value() || hash.value() != entry.pinned_files[i].second) {
      return changed(("pinned file " + entry.pinned_files[i].first).c_str());
    }
  }
  if (manifest.value().id != entry.id || manifest.value().version != entry.version ||
      manifest.value().mode != entry.mode) {
    return changed("identity");
  }
  return Result<VerifiedPlugin>::success({manifest.value(), entry_path.value(), entry});
}

std::string to_string(TrustState state) {
  switch (state) {
    case TrustState::approved: return "approved";
    case TrustState::unapproved: return "unapproved";
    case TrustState::changed: return "changed";
    case TrustState::invalid: return "invalid";
  }
  return "invalid";
}

Result<std::vector<DiscoveredPlugin>> discover(const std::filesystem::path& directory,
                                               const TrustStore& store) {
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) {
    return Result<std::vector<DiscoveredPlugin>>::failure(
        invalid("not a directory: " + directory.string()));
  }
  std::vector<std::filesystem::path> manifests;
  if (std::filesystem::is_regular_file(directory / kManifestFileName, error)) {
    manifests.push_back(directory / kManifestFileName);
  }
  for (const auto& child : std::filesystem::directory_iterator{directory, error}) {
    if (child.is_directory(error) &&
        std::filesystem::is_regular_file(child.path() / kManifestFileName, error)) {
      manifests.push_back(child.path() / kManifestFileName);
    }
  }
  std::sort(manifests.begin(), manifests.end());
  std::vector<DiscoveredPlugin> found;
  for (const auto& path : manifests) {
    DiscoveredPlugin item;
    item.manifest_path = path;
    const auto manifest = load_manifest_file(path);
    if (!manifest.has_value()) {
      item.state = TrustState::invalid;
      item.detail = manifest.error().message;
    } else {
      item.id = manifest.value().id;
      item.version = manifest.value().version;
      item.mode = manifest.value().mode == ExecutionMode::worker ? "worker" : "in_process";
      const auto* entry = store.find(item.id);
      if (entry == nullptr) {
        item.state = TrustState::unapproved;
        item.detail = "not approved; nothing has been executed";
      } else {
        const auto verified = verify_approved(*entry);
        item.state = verified.has_value() ? TrustState::approved : TrustState::changed;
        item.detail = verified.has_value() ? "approved" : verified.error().message;
      }
    }
    found.push_back(std::move(item));
  }
  return Result<std::vector<DiscoveredPlugin>>::success(std::move(found));
}

}  // namespace molshredder::plugin
