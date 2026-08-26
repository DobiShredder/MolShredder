#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/command/result.hpp"
#include "molshredder/io/molfile_provider.hpp"

namespace {

bool expect(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAILED: " << message << '\n';
  return condition;
}

void touch(const std::filesystem::path& path) {
  std::ofstream output{path};
  output << "synthetic PQR input\n";
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder;
  if (argc != 8) {
    std::cerr << "usage: molfile_provider_test valid alternate incompatible "
                 "duplicate missing-entry wrong-type working-directory\n";
    return 2;
  }
  const std::filesystem::path valid_plugin{argv[1]};
  const std::filesystem::path alternate_plugin{argv[2]};
  const std::filesystem::path incompatible_plugin{argv[3]};
  const std::filesystem::path duplicate_plugin{argv[4]};
  const std::filesystem::path missing_entry_plugin{argv[5]};
  const std::filesystem::path wrong_type_plugin{argv[6]};
  const std::filesystem::path working_directory{argv[7]};
  const auto test_root = working_directory / "molfile-provider-fixtures";
  std::filesystem::create_directories(test_root);
  const auto approved_directory = test_root / "approved";
  const auto untrusted_directory = test_root / "untrusted";
  std::filesystem::create_directories(approved_directory);
  std::filesystem::create_directories(untrusted_directory);
  const auto approved_plugin = approved_directory / valid_plugin.filename();
  const auto untrusted_plugin = untrusted_directory / valid_plugin.filename();
  std::filesystem::copy_file(valid_plugin, approved_plugin,
                             std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(valid_plugin, untrusted_plugin,
                             std::filesystem::copy_options::overwrite_existing);

  bool passed = true;
  {
    io::MolfileProviderRegistry registry;
    const auto empty = registry.discover({});
    passed &= expect(empty.has_value() && empty.value().empty(),
                     "discovery must not scan untrusted or working directories");
    const auto missing = registry.discover(
        {{}, {test_root / "does-not-exist.plugin"}});
    passed &= expect(!missing.has_value() && registry.descriptors().empty(),
                     "missing explicit plugin must fail without registration");
    const auto incompatible = registry.discover({{}, {incompatible_plugin}});
    passed &= expect(!incompatible.has_value() && registry.descriptors().empty(),
                     "incompatible ABI must be rejected transactionally");
    const auto duplicate = registry.discover({{}, {duplicate_plugin}});
    passed &= expect(!duplicate.has_value() && registry.descriptors().empty(),
                     "duplicate registration in one library must be rejected");
    const auto missing_entry =
        registry.discover({{}, {missing_entry_plugin}});
    passed &= expect(!missing_entry.has_value() && registry.descriptors().empty(),
                     "missing lifecycle entry point must be rejected");
    const auto wrong_type = registry.discover({{}, {wrong_type_plugin}});
    passed &= expect(!wrong_type.has_value() && registry.descriptors().empty(),
                     "non-molfile plugin type must be rejected");
  }

  const auto good_input = test_root / "good.pqr";
  const auto failing_input = test_root / "timestep_fail.pqr";
  const auto concurrent_a = test_root / "concurrent-a.pqr";
  const auto concurrent_b = test_root / "concurrent-b.pqr";
  const auto shutdown_input = test_root / "shutdown.pqr";
  const auto oversized_input = test_root / "oversized.pqr";
  const auto byte_limited_input = test_root / "byte-limited.pqr";
  const auto progress_input = test_root / "progress.pqr";
  const auto pre_cancelled_input = test_root / "pre-cancelled.pqr";
  const auto cancelled_input = test_root / "cancelled.pqr";
  const auto slow_input = test_root / "slow.pqr";
  const auto waiting_input = test_root / "waiting.pqr";
  for (const auto& path : {good_input, failing_input, concurrent_a,
                           concurrent_b, shutdown_input, oversized_input,
                           byte_limited_input, progress_input,
                           pre_cancelled_input, cancelled_input, slow_input,
                           waiting_input}) {
    std::error_code ignored;
    std::filesystem::remove(path.string() + ".closed", ignored);
    std::filesystem::remove(path.string() + ".fini", ignored);
    std::filesystem::remove(path.string() + ".active", ignored);
    touch(path);
  }

  {
    auto workspace = std::make_shared<application::Workspace>();
    auto registry = application::make_default_registry(workspace);
    operation::TaskContext context;
    const command::Arguments base_arguments{
        {"file-format", "pqr"},
        {"path", good_input.string()},
        {"provider", "molfile:pqr"}};
    const auto missing_path = registry.invoke("load", base_arguments, context);
    passed &= expect(!missing_path.has_value() && workspace->object_count() == 0U,
                     "explicit molfile action must require a plugin path before registration");

    auto first_arguments = base_arguments;
    first_arguments.emplace("name", "action-pqr");
    first_arguments.emplace("plugin-path", valid_plugin.string());
    const auto first = registry.invoke("load", first_arguments, context);
    bool provider_provenance = false;
    if (first.has_value()) {
      const auto provider = first.value().fields.find("provider");
      if (provider != first.value().fields.end()) {
        const auto* fields = std::get_if<command::Value::Object>(
            &provider->second.data);
        if (fields != nullptr) {
          const auto id = fields->find("id");
          const auto origin = fields->find("origin");
          const auto trust = fields->find("trust");
          provider_provenance =
              id != fields->end() && origin != fields->end() &&
              trust != fields->end() &&
              std::get<std::string>(id->second.data) == "molfile:pqr" &&
              std::get<std::string>(origin->second.data) == "dynamic_plugin" &&
              std::get<std::string>(trust->second.data) == "untrusted";
        }
      }
    }
    passed &= expect(first.has_value() && workspace->object_count() == 1U &&
                         provider_provenance,
                     "shared load action must stage explicit molfile:pqr data and report provenance");

    auto repeated_arguments = base_arguments;
    repeated_arguments.emplace("name", "action-pqr-repeat");
    const auto repeated = registry.invoke("load", repeated_arguments, context);
    passed &= expect(repeated.has_value() && workspace->object_count() == 2U,
                     "registered explicit provider must remain available for the registry session");
  }

  {
    auto workspace = std::make_shared<application::Workspace>();
    auto registry = application::make_default_registry(workspace);
    std::promise<void> start;
    auto gate = start.get_future().share();
    const auto invoke_load = [&](const std::filesystem::path& path,
                                 std::string name) {
      gate.wait();
      operation::TaskContext context;
      return registry.invoke(
          "load", {{"file-format", "pqr"},
                   {"name", std::move(name)},
                   {"path", path.string()},
                   {"plugin-path", valid_plugin.string()},
                   {"provider", "molfile:pqr"}},
          context);
    };
    auto first = std::async(std::launch::async, invoke_load,
                            std::cref(concurrent_a), "action-concurrent-a");
    auto second = std::async(std::launch::async, invoke_load,
                             std::cref(concurrent_b), "action-concurrent-b");
    start.set_value();
    passed &= expect(first.get().has_value() && second.get().has_value() &&
                         workspace->object_count() == 2U,
                     "concurrent first actions must register once and serialize Workspace commits");
  }

  {
    auto workspace = std::make_shared<application::Workspace>();
    auto registry = application::make_default_registry(workspace);
    operation::TaskContext owner_context;
    auto owner = std::async(std::launch::async, [&] {
      return registry.invoke(
          "load", {{"file-format", "pqr"},
                   {"name", "action-owner"},
                   {"path", slow_input.string()},
                   {"plugin-path", valid_plugin.string()},
                   {"provider", "molfile:pqr"}},
          owner_context);
    });
    for (std::size_t attempt = 0U;
         attempt < 1000U &&
         !std::filesystem::exists(slow_input.string() + ".active");
         ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    operation::TaskContext waiting_context;
    const auto started = std::chrono::steady_clock::now();
    auto waiting = std::async(std::launch::async, [&] {
      return registry.invoke(
          "load", {{"file-format", "pqr"},
                   {"name", "action-waiting"},
                   {"path", waiting_input.string()},
                   {"plugin-path", valid_plugin.string()},
                   {"provider", "molfile:pqr"}},
          waiting_context);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    waiting_context.cancellation.request_cancel();
    const auto waiting_result = waiting.get();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    passed &= expect(
        owner.get().has_value() && !waiting_result.has_value() &&
            waiting_result.error().code == operation::ErrorCode::cancelled &&
            elapsed < std::chrono::milliseconds{750} &&
            workspace->object_count() == 1U &&
            !std::filesystem::exists(waiting_input.string() + ".closed"),
        "cancelled action scheduling must not open input or mutate Workspace");
  }

  {
    io::MolfileProviderRegistry registry;
    io::MolfileDiscoveryRequest request;
    request.approved_directories.push_back(
        {approved_directory, io::MolfileProviderTrust::user_approved});
    const auto discovered = registry.discover(request);
    passed &= expect(discovered.has_value() && discovered.value().size() == 1U,
                     "approved directory must discover exactly one plugin");
    if (discovered.has_value() && !discovered.value().empty()) {
      const auto& descriptor = discovered.value().front();
      passed &= expect(
          descriptor.provider_id == "molfile:pqr" &&
              descriptor.plugin_name == "pqr" &&
              descriptor.major_version == 0 && descriptor.minor_version == 6 &&
              !descriptor.thread_safe && descriptor.extensions ==
                                              std::vector<std::string>{"pqr"} &&
              descriptor.trust == io::MolfileProviderTrust::user_approved,
          "provider descriptor must preserve identity/version/capability/trust");
    }
    const auto repeated = registry.discover({{}, {approved_plugin}});
    passed &= expect(!repeated.has_value() && registry.descriptors().size() == 1U,
                     "duplicate provider across loaded libraries must not mutate registry");

    std::promise<void> discover_start;
    auto discover_gate = discover_start.get_future().share();
    auto alternate_discovery = std::async(std::launch::async, [&] {
      discover_gate.wait();
      return registry.discover({{}, {alternate_plugin}});
    });
    std::vector<std::future<std::vector<io::MolfileProviderDescriptor>>>
        snapshots;
    for (std::size_t index = 0U; index < 8U; ++index) {
      snapshots.push_back(std::async(std::launch::async, [&] {
        discover_gate.wait();
        return registry.descriptors();
      }));
    }
    discover_start.set_value();
    const auto alternate = alternate_discovery.get();
    bool snapshots_valid = true;
    for (auto& snapshot : snapshots) {
      const auto descriptors = snapshot.get();
      snapshots_valid &= descriptors.size() == 1U || descriptors.size() == 2U;
    }
    passed &= expect(
        alternate.has_value() && alternate.value().size() == 2U &&
            registry.descriptors().size() == 2U && snapshots_valid,
        "descriptor snapshots must remain coherent during concurrent discovery");

    const auto document = registry.read_structure(good_input, "molfile:pqr");
    passed &= expect(document.has_value() &&
                         document.value().format == io::StructureFormat::pqr &&
                         document.value().structures.size() == 1U,
                     "PQR provider must stage one typed structure document");
    if (document.has_value()) {
      const auto& structure = document.value().structures.front();
      passed &= expect(
          structure.topology->atom_count() == 2U &&
              structure.topology->residue_count() == 1U &&
              structure.topology->atoms()[0].atomic_number == 7U &&
              structure.topology->atoms()[1].atomic_number == 6U &&
              structure.topology->properties().find("partial_charge") != nullptr &&
              structure.topology->properties().find("pqr.radius") != nullptr &&
              structure.metadata.at("provider") == "molfile:pqr",
          "structure callback fields must convert to topology and properties");
      const auto frame = structure.coordinates->read_frame(0U);
      passed &= expect(
          frame.has_value() && frame.value()->metadata().unit_cell.has_value() &&
              frame.value()->atom_count() == 2U,
          "timestep callback must convert coordinates and unit cell");
    }
    passed &= expect(std::filesystem::exists(good_input.string() + ".closed"),
                     "successful read must close the provider handle");

    application::Workspace workspace;
    const auto scene_before = workspace.scene();
    const auto failed =
        registry.read_structure(failing_input, "molfile:pqr");
    passed &= expect(
        !failed.has_value() && workspace.object_count() == 0U &&
            workspace.scene() == scene_before &&
            std::filesystem::exists(failing_input.string() + ".closed"),
        "callback failure must close and leave Workspace completely unchanged");
    const auto oversized =
        registry.read_structure(oversized_input, "molfile:pqr");
    passed &= expect(
        !oversized.has_value() && workspace.object_count() == 0U &&
            workspace.scene() == scene_before &&
            std::filesystem::exists(oversized_input.string() + ".closed"),
        "oversized provider result must close before allocation and preserve Workspace");
    const auto invalid_limits = registry.read_structure(
        good_input, "molfile:pqr", io::MolfileReadLimits{0U, 1U});
    passed &= expect(!invalid_limits.has_value(),
                     "zero atom or staging budgets must be rejected");
    const auto byte_limited = registry.read_structure(
        byte_limited_input, "molfile:pqr", io::MolfileReadLimits{2U, 1U});
    passed &= expect(
        !byte_limited.has_value() &&
            std::filesystem::exists(byte_limited_input.string() + ".closed"),
        "staging byte budget must reject and close before allocating buffers");

    operation::TaskContext progress_context;
    std::vector<double> progress_values;
    std::vector<std::string> progress_stages;
    progress_context.report_progress = [&](const auto& update) {
      progress_values.push_back(update.fraction);
      progress_stages.emplace_back(update.stage);
    };
    const auto progressed = registry.read_structure(
        progress_input, "molfile:pqr", progress_context);
    passed &= expect(
        progressed.has_value() && !progress_values.empty() &&
            std::is_sorted(progress_values.begin(), progress_values.end()) &&
            progress_values.back() == 1.0 &&
            progress_stages.back() == "molfile-complete",
        "provider read must report monotonic callback-stage progress");

    operation::TaskContext pre_cancelled_context;
    pre_cancelled_context.cancellation.request_cancel();
    const auto pre_cancelled = registry.read_structure(
        pre_cancelled_input, "molfile:pqr", pre_cancelled_context);
    passed &= expect(
        !pre_cancelled.has_value() &&
            pre_cancelled.error().code == operation::ErrorCode::cancelled &&
            !std::filesystem::exists(pre_cancelled_input.string() + ".closed"),
        "pre-cancelled read must not open the plugin input");

    operation::TaskContext cancelled_context;
    cancelled_context.report_progress = [&](const auto& update) {
      if (update.stage == "molfile-structure") {
        cancelled_context.cancellation.request_cancel();
      }
    };
    const auto cancelled = registry.read_structure(
        cancelled_input, "molfile:pqr", cancelled_context);
    passed &= expect(
        !cancelled.has_value() &&
            cancelled.error().code == operation::ErrorCode::cancelled &&
            std::filesystem::exists(cancelled_input.string() + ".closed") &&
            workspace.object_count() == 0U && workspace.scene() == scene_before,
        "checkpoint cancellation must close and preserve Workspace state");

    auto slow_read = std::async(std::launch::async,
                                [&registry, &slow_input] {
      return registry.read_structure(slow_input, "molfile:pqr");
    });
    for (std::size_t attempt = 0U;
         attempt < 1000U &&
         !std::filesystem::exists(slow_input.string() + ".active");
         ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    passed &= expect(
        std::filesystem::exists(slow_input.string() + ".active"),
        "slow provider fixture must enter its serialized callback");
    operation::TaskContext waiting_context;
    const auto wait_started = std::chrono::steady_clock::now();
    auto waiting_read = std::async(
        std::launch::async, [&registry, &waiting_input, &waiting_context] {
          return registry.read_structure(waiting_input, "molfile:pqr",
                                         waiting_context);
        });
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    waiting_context.cancellation.request_cancel();
    const auto waiting_result = waiting_read.get();
    const auto wait_elapsed = std::chrono::steady_clock::now() - wait_started;
    passed &= expect(
        !waiting_result.has_value() &&
            waiting_result.error().code == operation::ErrorCode::cancelled &&
            wait_elapsed < std::chrono::milliseconds{750} &&
            !std::filesystem::exists(waiting_input.string() + ".closed"),
        "cancelled scheduler wait must return promptly without opening input");
    passed &= expect(slow_read.get().has_value(),
                     "lock owner must complete after waiting read cancellation");
    if (document.has_value()) {
      auto staged = document.value();
      const auto committed = workspace.load_structure_document(
          std::move(staged), good_input, std::string{"plugin_object"});
      passed &= expect(committed.has_value() && workspace.object_count() == 1U,
                       "validated staged document must commit once");
    }

    std::promise<void> start;
    auto gate = start.get_future().share();
    auto first = std::async(std::launch::async,
                            [&registry, &concurrent_a, gate] {
      gate.wait();
      return registry.read_structure(concurrent_a, "molfile:pqr");
    });
    auto second = std::async(std::launch::async,
                             [&registry, &concurrent_b, gate] {
      gate.wait();
      return registry.read_structure(concurrent_b, "molfile:pqr");
    });
    start.set_value();
    passed &= expect(first.get().has_value() && second.get().has_value(),
                     "thread-unsafe provider callback sequences must serialize");
    const auto shutdown =
        registry.read_structure(shutdown_input, "molfile:pqr");
    passed &= expect(shutdown.has_value(),
                     "provider must remain usable until registry shutdown");
  }
  passed &= expect(std::filesystem::exists(shutdown_input.string() + ".fini"),
                   "registry shutdown must call plugin fini before unload");

  {
    io::MolfileProviderRegistry registry;
    const auto partial = registry.discover(
        {{}, {valid_plugin, incompatible_plugin}});
    passed &= expect(!partial.has_value() && registry.descriptors().empty(),
                     "multi-library discovery failure must roll back pending providers");
  }

  return passed ? 0 : 1;
}
