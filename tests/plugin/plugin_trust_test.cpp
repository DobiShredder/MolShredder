#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "molshredder/plugin/trust_store.hpp"

namespace {

bool expect(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAILED: " << message << '\n';
  return condition;
}

void write(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  output << text;
}

std::string manifest_text(const std::string& id, const std::string& ns, const std::string& mode,
                          const std::string& entry, const std::string& files = "") {
  return R"({"schema_version":1,"id":")" + id + R"(","namespace":")" + ns + R"(","version":"1.0.0",
    "license":"MIT","sdk":{"abi_major":1,"abi_minor":0},"execution":{"mode":")" + mode + R"(",)" +
         (mode == "worker" ? R"("command":[")" + entry + R"("])" : R"("library":")" + entry + R"(")") +
         (files.empty() ? "" : R"(,"files":[")" + files + R"("])") + R"(},"operations":[{"name":"op"}]})";
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder::plugin;
  if (argc != 2) return 2;
  const std::filesystem::path root = std::filesystem::path{argv[1]} / "plugin-trust-fixtures";
  std::filesystem::remove_all(root);
  bool passed = true;

  write(root / "plugins/alpha/molshredder-plugin.json", manifest_text("org.example.alpha", "alpha", "worker", "bin/run"));
  write(root / "plugins/alpha/bin/run", "#!/bin/sh\necho alpha\n");
  write(root / "plugins/beta/molshredder-plugin.json", manifest_text("org.example.beta", "beta", "in_process", "lib/beta.so"));
  write(root / "plugins/beta/lib/beta.so", "binary-beta");
  write(root / "plugins/broken/molshredder-plugin.json", "{ not json");
  write(root / "plugins/empty/readme.txt", "no manifest here");
  const auto store_path = root / "state/trust.json";

  auto store = TrustStore::load(store_path);
  passed &= expect(store.has_value() && store.value().entries().empty(), "a missing store is empty");

  auto listed = discover(root / "plugins", store.value());
  passed &= expect(listed.has_value() && listed.value().size() == 3U, "three manifests are listed");
  if (listed.has_value()) {
    int unapproved = 0, invalid = 0;
    for (const auto& item : listed.value()) {
      unapproved += item.state == TrustState::unapproved;
      invalid += item.state == TrustState::invalid;
    }
    passed &= expect(unapproved == 2 && invalid == 1, "discovery marks unapproved and invalid plugins");
  }

  const auto alpha_path = root / "plugins/alpha/molshredder-plugin.json";
  const auto alpha = load_manifest_file(alpha_path);
  passed &= expect(alpha.has_value(), "alpha manifest loads");
  const auto approved = store.value().approve(alpha.value(), alpha_path, false, "2026-10-04T00:00:00Z");
  passed &= expect(approved.has_value() && approved.value().manifest_sha256.size() == 64U &&
                       approved.value().entry_sha256.size() == 64U,
                   "approval pins both hashes");
  passed &= expect(store.value().save().has_value() && std::filesystem::exists(store_path) &&
                       !std::filesystem::exists(store_path.string() + ".tmp"),
                   "the store is written atomically");

  // Reload: the entry persists and verifies.
  auto reloaded = TrustStore::load(store_path);
  passed &= expect(reloaded.has_value() && reloaded.value().entries().size() == 1U &&
                       reloaded.value().find("org.example.alpha") != nullptr,
                   "approval persists");
  passed &= expect(verify_approved(*reloaded.value().find("org.example.alpha")).has_value(),
                   "an unchanged plugin verifies");

  // In-process approval needs an explicit flag, and the flag only fits in-process plugins.
  const auto beta_path = root / "plugins/beta/molshredder-plugin.json";
  const auto beta = load_manifest_file(beta_path);
  passed &= expect(!reloaded.value().approve(beta.value(), beta_path, false, "t").has_value(),
                   "in-process plugins cannot be approved without --allow-in-process");
  passed &= expect(!reloaded.value().approve(alpha.value(), alpha_path, true, "t").has_value(),
                   "the in-process flag is rejected for worker plugins");
  passed &= expect(reloaded.value().approve(beta.value(), beta_path, true, "t").has_value(),
                   "in-process approval works with the flag");

  // Tampering: entry, manifest, missing entry.
  const auto entry_file = root / "plugins/alpha/bin/run";
  write(entry_file, "#!/bin/sh\necho tampered\n");
  auto changed = verify_approved(*reloaded.value().find("org.example.alpha"));
  passed &= expect(!changed.has_value() && changed.error().message.find("entry file") != std::string::npos,
                   "a modified entry is detected");
  auto after = discover(root / "plugins", reloaded.value());
  bool saw_changed = false;
  if (after.has_value()) {
    for (const auto& item : after.value()) saw_changed |= item.id == "org.example.alpha" && item.state == TrustState::changed;
  }
  passed &= expect(saw_changed, "discovery reports the changed plugin");
  write(entry_file, "#!/bin/sh\necho alpha\n");
  passed &= expect(verify_approved(*reloaded.value().find("org.example.alpha")).has_value(),
                   "restoring the bytes restores trust");
  write(alpha_path, manifest_text("org.example.alpha", "alpha", "worker", "bin/run") + "\n");
  passed &= expect(!verify_approved(*reloaded.value().find("org.example.alpha")).has_value(),
                   "a modified manifest is detected");
  write(alpha_path, manifest_text("org.example.alpha", "alpha", "worker", "bin/run"));
  std::filesystem::remove(entry_file);
  passed &= expect(!verify_approved(*reloaded.value().find("org.example.alpha")).has_value(),
                   "a missing entry is detected");

  // Extra pinned files (for script plugins) are part of the approval.
  write(root / "plugins/gamma/molshredder-plugin.json",
        manifest_text("org.example.gamma", "gamma", "worker", "run.sh", "plugin.py"));
  write(root / "plugins/gamma/run.sh", "#!/bin/sh\nexec python3 plugin.py\n");
  write(root / "plugins/gamma/plugin.py", "print('v1')\n");
  const auto gamma_path = root / "plugins/gamma/molshredder-plugin.json";
  const auto gamma = load_manifest_file(gamma_path);
  const auto gamma_entry = reloaded.value().approve(gamma.value(), gamma_path, false, "t");
  passed &= expect(gamma_entry.has_value() && gamma_entry.value().pinned_files.size() == 1U,
                   "the pinned file is hashed at approval");
  passed &= expect(reloaded.value().save().has_value(), "pinned files are saved");
  const auto gamma_reloaded = TrustStore::load(store_path);
  passed &= expect(gamma_reloaded.has_value() && gamma_reloaded.value().find("org.example.gamma") != nullptr &&
                       gamma_reloaded.value().find("org.example.gamma")->pinned_files.size() == 1U &&
                       verify_approved(*gamma_reloaded.value().find("org.example.gamma")).has_value(),
                   "pinned files persist and verify");
  write(root / "plugins/gamma/plugin.py", "print('v2 tampered')\n");
  const auto gamma_changed = verify_approved(*gamma_reloaded.value().find("org.example.gamma"));
  passed &= expect(!gamma_changed.has_value() &&
                       gamma_changed.error().message.find("pinned file") != std::string::npos,
                   "a changed script is detected even though the entry file is unchanged");
  write(root / "plugins/gamma/molshredder-plugin.json",
        manifest_text("org.example.gamma", "gamma", "worker", "run.sh", "../outside/run"));
  passed &= expect(!load_manifest_file(root / "plugins/gamma/molshredder-plugin.json").has_value(),
                   "pinned files cannot leave the plugin directory");

  // Revoke and malformed stores.
  passed &= expect(reloaded.value().revoke("org.example.alpha") && !reloaded.value().revoke("org.example.alpha"),
                   "revoke removes the entry once");
  write(store_path, "{broken");
  passed &= expect(!TrustStore::load(store_path).has_value(), "a malformed store is an error, not an empty store");
  write(store_path, R"({"schema_version":1,"entries":[{"id":"x"}]})");
  passed &= expect(!TrustStore::load(store_path).has_value(), "an incomplete entry is rejected");
  write(store_path, R"({"schema_version":2,"entries":[]})");
  passed &= expect(!TrustStore::load(store_path).has_value(), "an unknown schema version is rejected");

  // Entry paths cannot escape the plugin directory through a symlink.
  write(root / "plugins/escape/molshredder-plugin.json", manifest_text("org.example.escape", "escape", "worker", "bin/run"));
  write(root / "outside/run", "outside");
  std::filesystem::create_directories(root / "plugins/escape/bin");
  std::error_code link_error;
  std::filesystem::create_symlink(root / "outside/run", root / "plugins/escape/bin/run", link_error);
  if (!link_error) {
    const auto escape = load_manifest_file(root / "plugins/escape/molshredder-plugin.json");
    passed &= expect(escape.has_value() && !resolve_entry_path(escape.value()).has_value(),
                     "a symlink that leaves the plugin directory is refused");
  }
  return passed ? 0 : 1;
}
