#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <string>
#include <thread>

#include "molshredder/plugin/host.hpp"

namespace {

using molshredder::command::Value;
using molshredder::operation::ErrorCode;
using namespace molshredder::plugin;

bool expect(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAILED: " << message << '\n';
  return condition;
}

struct Fixture {
  std::filesystem::path directory;
  std::filesystem::path manifest;
  std::filesystem::path store;
};

Fixture make_plugin(const std::filesystem::path& root, const std::filesystem::path& library,
                    const std::string& name, const std::string& license = "GPL-3.0-or-later") {
  Fixture fixture;
  fixture.directory = root / name;
  std::filesystem::create_directories(fixture.directory);
  std::filesystem::copy_file(library, fixture.directory / library.filename(),
                             std::filesystem::copy_options::overwrite_existing);
  fixture.manifest = fixture.directory / kManifestFileName;
  std::ofstream out{fixture.manifest};
  out << R"({"schema_version":1,"id":"org.example.native","namespace":"native","version":"1.0.0","license":")"
      << license << R"(","sdk":{"abi_major":1,"abi_minor":0},
    "execution":{"mode":"in_process","library":")" << library.filename().string() << R"("},
    "required_capabilities":[],"optional_capabilities":["progress","cancellation","host.selection.atoms"],
    "operations":[{"name":"echo","parameters":[{"name":"text","type":"text","required":true},
                  {"name":"times","type":"unsigned_integer","default":"1"}]},
                  {"name":"sum","parameters":[{"name":"count","type":"unsigned_integer","default":"4"}]},
                  {"name":"spin"},{"name":"atoms","parameters":[{"name":"selection","type":"text","default":"all"}]},
                  {"name":"fail"}]})";
  fixture.store = root / (name + "-trust.json");
  return fixture;
}

bool approve(const Fixture& fixture, bool allow) {
  auto store = TrustStore::load(fixture.store);
  const auto manifest = load_manifest_file(fixture.manifest);
  if (!manifest.has_value()) return false;
  const auto entry = store.value().approve(manifest.value(), fixture.manifest, allow, "t");
  if (!entry.has_value()) return false;
  return store.value().save().has_value();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  std::map<std::string, std::filesystem::path> libraries;
  {
    std::ifstream list{argv[1]};
    for (std::string line; std::getline(list, line);) {
      const auto at = line.find('=');
      if (at != std::string::npos) libraries[line.substr(0U, at)] = line.substr(at + 1U);
    }
  }
  const auto root = std::filesystem::path{argv[2]} / "plugin-inprocess-fixtures";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  bool passed = true;
  molshredder::operation::TaskContext context;
  const auto host = std::make_shared<PluginHost>(nullptr);
  const auto run = [&](const Fixture& f, const std::string& operation, const Value::Object& arguments,
                       molshredder::operation::TaskContext& ctx) {
    return host->invoke(f.store, "org.example.native", operation, arguments, ctx, {});
  };

  // Approval rules: the explicit flag, the license gate and the entry file.
  const auto good = make_plugin(root, libraries.at("good"), "good");
  passed &= expect(!approve(good, false), "an in-process plugin cannot be approved without the flag");
  passed &= expect(approve(good, true), "approval with the flag works");
  const auto proprietary = make_plugin(root, libraries.at("good"), "proprietary", "Proprietary");
  passed &= expect(!load_manifest_file(proprietary.manifest).has_value(),
                   "a non GPL-compatible in-process license is rejected at the manifest");

  // Normal operation: results, progress, tamper detection.
  const auto echoed = run(good, "echo", {{"text", Value{"a\"b"}}, {"times", std::uint64_t{2}}}, context);
  passed &= expect(echoed.has_value() && std::get<std::string>(echoed.value().fields.at("text").data) == "a\"ba\"b",
                   "echo returns escaped text");
  std::vector<double> fractions;
  molshredder::operation::TaskContext progress;
  progress.report_progress = [&fractions](const molshredder::operation::ProgressUpdate& update) {
    fractions.push_back(update.fraction);
  };
  const auto summed = run(good, "sum", {{"count", std::uint64_t{5}}}, progress);
  passed &= expect(summed.has_value() && fractions.size() == 5U && fractions.back() == 1.0,
                   "progress callbacks reach the task context");
  const auto failed = run(good, "fail", {}, context);
  passed &= expect(!failed.has_value() && failed.error().code == ErrorCode::invalid_argument &&
                       failed.error().message == "native boom" && failed.error().suggestion == "retry",
                   "plugin errors are mapped");
  passed &= expect(run(good, "echo", {{"text", Value{"x"}}}, context).has_value() && host->loaded().size() == 1U &&
                       host->loaded()[0].kind == "in_process",
                   "the library stays loaded between calls");

  // Cooperative cancellation through is_cancelled.
  {
    molshredder::operation::TaskContext cancel_context;
    auto future = std::async(std::launch::async, [&] { return run(good, "spin", {}, cancel_context); });
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    const auto before = std::chrono::steady_clock::now();
    cancel_context.cancellation.request_cancel();
    const auto cancelled = future.get();
    passed &= expect(!cancelled.has_value() && cancelled.error().code == ErrorCode::cancelled &&
                         std::chrono::steady_clock::now() - before < std::chrono::seconds{5},
                     "a cooperative in-process plugin stops on cancel");
  }
  molshredder::operation::TaskContext pre_cancelled;
  pre_cancelled.cancellation.request_cancel();
  passed &= expect(!run(good, "echo", {{"text", Value{"x"}}}, pre_cancelled).has_value(),
                   "a pre-cancelled call never reaches the plugin");

  // Host data requests are capability-gated and report why they fail.
  const auto atoms = run(good, "atoms", {{"selection", Value{"all"}}}, context);
  passed &= expect(!atoms.has_value() && atoms.error().message.find("workspace") != std::string::npos,
                   "a host request without a workspace fails with a reason the plugin forwards");

  // Tamper after approval: the file is verified again before it is loaded.
  host->forget("org.example.native");
  {
    std::ofstream append{good.directory / libraries.at("good").filename(), std::ios::binary | std::ios::app};
    append << "tampered";
  }
  const auto tampered = run(good, "echo", {{"text", Value{"x"}}}, context);
  passed &= expect(!tampered.has_value() && tampered.error().message.find("changed since approval") != std::string::npos &&
                       host->loaded().empty(),
                   "a modified library is refused before loading");

  // Broken variants are rejected with a clear reason and never half-load.
  const std::pair<const char*, const char*> faulty[] = {
      {"abi_major", "not compatible"},   {"abi_minor", "not compatible"},
      {"small_struct", "too small"},     {"bad_describe", "operations differ"},
      {"no_entry", "does not export"},   {"fail_entry", "entry reported a failure"}};
  for (const auto& [variant, reason] : faulty) {
    host->forget("org.example.native");
    const auto fixture = make_plugin(root, libraries.at(variant), std::string{"variant-"} + variant);
    passed &= expect(approve(fixture, true), std::string{"approval of variant "} + variant);
    const auto result = run(fixture, "echo", {{"text", Value{"x"}}}, context);
    passed &= expect(!result.has_value() && result.error().message.find(reason) != std::string::npos &&
                         host->loaded().size() == 1U && !host->loaded()[0].active,
                     std::string{"variant must be rejected: "} + variant + " (" +
                         (result.has_value() ? "accepted" : result.error().message) + ")");
  }
  host->forget("org.example.native");
  const auto bad_result = make_plugin(root, libraries.at("bad_result"), "variant-bad-result");
  passed &= expect(approve(bad_result, true), "approval of the bad-result variant");
  const auto garbage = run(bad_result, "echo", {{"text", Value{"x"}}}, context);
  passed &= expect(!garbage.has_value() && garbage.error().message.find("not a JSON object") != std::string::npos,
                   "a non-JSON result is an error, not a crash");
  host->forget("org.example.native");
  return passed ? 0 : 1;
}
