// molshredder-plugin-check: the SDK compatibility kit. It runs a plugin
// through the same host code MolShredder uses (manifest, negotiation, pinned
// files, start and handshake, every manifest example) against a small
// synthetic molecule, so authors can test without MolShredder data.
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "molshredder/command/json.hpp"
#include "molshredder/plugin/host.hpp"

namespace {

using molshredder::command::Value;
using namespace molshredder::plugin;

struct Check {
  std::string name;
  std::string status;  // pass | fail | skip
  std::string detail;
};

// Twelve atoms of a small peptide-like fragment; positions are fixed so a
// plugin's results are comparable between runs and machines.
Value synthetic_atoms(const Value::Object& params) {
  static const char* names[] = {"N", "CA", "C", "O", "CB", "CG", "N", "CA", "C", "O", "CB", "SG"};
  static const int numbers[] = {7, 6, 6, 8, 6, 6, 7, 6, 6, 8, 6, 16};
  Value::Array ids, atom_names, atomic_numbers, positions;
  std::size_t limit = 12;
  if (const auto it = params.find("limit"); it != params.end()) {
    if (const auto* value = std::get_if<std::int64_t>(&it->second.data); value != nullptr && *value > 0) {
      limit = std::min<std::size_t>(12, static_cast<std::size_t>(*value));
    }
  }
  for (std::size_t i = 0; i < limit; ++i) {
    ids.emplace_back(static_cast<std::uint64_t>(i + 1));
    atom_names.emplace_back(names[i]);
    atomic_numbers.emplace_back(static_cast<std::uint64_t>(numbers[i]));
    const double x = static_cast<double>(i) * 1.5;
    positions.emplace_back(Value::Array{Value{x}, Value{(i % 2U == 0U) ? 0.5 : -0.5}, Value{static_cast<double>(i) * 0.25}});
  }
  return Value::Object{{"atomic_numbers", std::move(atomic_numbers)},
                       {"count", static_cast<std::uint64_t>(limit)},
                       {"frame_index", std::uint64_t{0}},
                       {"ids", std::move(ids)},
                       {"names", std::move(atom_names)},
                       {"object", "synthetic"},
                       {"positions", std::move(positions)},
                       {"unit", "angstrom"}};
}

Value::Object typed(const OperationDeclaration& operation, const molshredder::command::Arguments& arguments) {
  Value::Object out;
  for (const auto& parameter : operation.parameters) {
    const auto found = arguments.find(parameter.name);
    const std::string* text = found != arguments.end() ? &found->second
                              : parameter.default_value.has_value() ? &*parameter.default_value : nullptr;
    if (text == nullptr) continue;
    switch (parameter.type) {
      case molshredder::command::ParameterType::boolean: out.emplace(parameter.name, *text == "true"); break;
      case molshredder::command::ParameterType::integer: out.emplace(parameter.name, std::int64_t{std::atoll(text->c_str())}); break;
      case molshredder::command::ParameterType::unsigned_integer:
        out.emplace(parameter.name, static_cast<std::uint64_t>(std::strtoull(text->c_str(), nullptr, 10)));
        break;
      case molshredder::command::ParameterType::number: out.emplace(parameter.name, std::atof(text->c_str())); break;
      default: out.emplace(parameter.name, *text); break;
    }
  }
  return out;
}

// The message plus the host's suggestion, so the report says what to do.
std::string describe(const molshredder::operation::Error& error) {
  return error.suggestion.empty() ? error.message : error.message + " (" + error.suggestion + ")";
}

int usage() {
  std::cerr << "usage: molshredder-plugin-check --manifest FILE [--allow-in-process] [--timeout-ms N] [--json]\n";
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path manifest_path;
  bool allow_in_process = false;
  bool json_output = false;
  std::chrono::milliseconds timeout{30'000};
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--manifest" && i + 1 < argc) manifest_path = argv[++i];
    else if (argument == "--allow-in-process") allow_in_process = true;
    else if (argument == "--json") json_output = true;
    else if (argument == "--timeout-ms" && i + 1 < argc) timeout = std::chrono::milliseconds{std::atoll(argv[++i])};
    else return usage();
  }
  if (manifest_path.empty() || timeout.count() <= 0) return usage();

  std::vector<Check> checks;
  const auto add = [&checks](std::string name, bool ok, std::string detail = {}) {
    checks.push_back({std::move(name), ok ? "pass" : "fail", std::move(detail)});
    return ok;
  };
  const auto skip = [&checks](std::string name, std::string detail) {
    checks.push_back({std::move(name), "skip", std::move(detail)});
  };
  const auto finish = [&]() {
    bool failed = false;
    for (const auto& check : checks) failed = failed || check.status == "fail";
    if (json_output) {
      Value::Array items;
      for (const auto& check : checks) {
        items.emplace_back(Value::Object{{"detail", check.detail}, {"name", check.name}, {"status", check.status}});
      }
      const auto text = molshredder::command::to_json(Value::Object{
          {"checks", std::move(items)}, {"passed", !failed},
          {"sdk_version", std::to_string(kSdkAbiMajor) + "." + std::to_string(kSdkAbiMinor)}});
      std::cout << text.value() << '\n';
    } else {
      for (const auto& check : checks) {
        std::cout << (check.status == "pass" ? "[ ok ] " : check.status == "skip" ? "[skip] " : "[FAIL] ")
                  << check.name << (check.detail.empty() ? "" : ": " + check.detail) << '\n';
      }
      std::cout << (failed ? "\nThe plugin is NOT compatible with SDK " : "\nThe plugin is compatible with SDK ")
                << kSdkAbiMajor << '.' << kSdkAbiMinor << '\n';
    }
    return failed ? 1 : 0;
  };

  const auto manifest = load_manifest_file(manifest_path);
  if (!add("manifest is valid", manifest.has_value(), manifest.has_value() ? manifest.value().id + " " + manifest.value().version : describe(manifest.error()))) {
    return finish();
  }
  const auto negotiated = negotiate(manifest.value(), default_host_info());
  add("SDK version and capabilities negotiate", negotiated.has_value(),
      negotiated.has_value() ? "capabilities: " + std::to_string(negotiated.value().capabilities.size()) : describe(negotiated.error()));
  const auto entry = resolve_entry_path(manifest.value());
  add("entry file resolves inside the plugin directory", entry.has_value(), entry.has_value() ? entry.value().string() : describe(entry.error()));
  if (!negotiated.has_value() || !entry.has_value()) return finish();

  // The author asked for this check, which stands in for the approval step.
  const auto scratch = std::filesystem::temp_directory_path() /
                       ("molshredder-plugin-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(scratch);
  const auto store_path = scratch / "trust.json";
  auto store = TrustStore::load(store_path);
  const auto approved = store.value().approve(manifest.value(), manifest_path, allow_in_process, "compat-check");
  if (!add("files can be pinned for approval", approved.has_value(),
           approved.has_value() ? std::to_string(approved.value().pinned_files.size() + 2U) + " files hashed" : describe(approved.error()))) {
    std::filesystem::remove_all(scratch);
    return finish();
  }
  static_cast<void>(store.value().save());

  auto host = std::make_shared<PluginHost>(nullptr);
  InvokeOptions options;
  options.timeout = timeout;
  const auto started = host->check_start(store_path, manifest.value().id, options);
  const bool handshake = add(manifest.value().mode == ExecutionMode::worker ? "worker starts and completes the handshake"
                                                                              : "library loads and its description matches",
                             started.has_value(), started.has_value() ? "" : describe(started.error()));
  if (handshake) {
    if (manifest.value().examples.empty()) {
      skip("manifest examples", "none declared; add \"examples\" so the kit can exercise your operations");
    }
    // Replace the host's workspace-backed data service by the synthetic one.
    for (const auto& example : manifest.value().examples) {
      const auto operation = std::find_if(manifest.value().operations.begin(), manifest.value().operations.end(),
                                          [&](const OperationDeclaration& o) { return o.name == example.operation; });
      molshredder::operation::TaskContext context;
      std::size_t progress_updates = 0;
      context.report_progress = [&progress_updates](const molshredder::operation::ProgressUpdate&) { ++progress_updates; };
      const auto output = host->invoke_with_services(store_path, manifest.value().id, example.operation,
                                                     typed(*operation, example.arguments), context, options,
                                                     [](std::string_view method, const Value::Object& params) -> molshredder::operation::Result<Value> {
                                                       if (method != "selection.atoms") {
                                                         return molshredder::operation::Result<Value>::failure(
                                                             {molshredder::operation::ErrorCode::unsupported, "unknown host method", {}, {}});
                                                       }
                                                       return molshredder::operation::Result<Value>::success(synthetic_atoms(params));
                                                     });
      add("example '" + example.operation + "' runs", output.has_value(),
          output.has_value() ? "summary: " + (output.value().summary.empty() ? std::string{"(none)"} : output.value().summary) +
                                   ", " + std::to_string(output.value().fields.size()) + " fields" +
                                   (output.value().table.has_value() ? ", table" : "") +
                                   (progress_updates != 0U ? ", progress " + std::to_string(progress_updates) : "")
                             : describe(output.error()));
      if (output.has_value() && !output.value().fields.empty()) {
        const auto second = host->invoke_with_services(store_path, manifest.value().id, example.operation,
                                                       typed(*operation, example.arguments), context, options,
                                                       [](std::string_view, const Value::Object& params) -> molshredder::operation::Result<Value> {
                                                         return molshredder::operation::Result<Value>::success(synthetic_atoms(params));
                                                       });
        const auto first_text = molshredder::command::to_json(Value{output.value().fields});
        const auto second_text = second.has_value() ? molshredder::command::to_json(Value{second.value().fields})
                                                    : molshredder::command::to_json(Value{nullptr});
        add("example '" + example.operation + "' is repeatable on the same input", second.has_value() && first_text.has_value() &&
                                                                                       second_text.has_value() && first_text.value() == second_text.value(),
            second.has_value() ? "" : describe(second.error()));
      }
    }
    // A call to an undeclared operation must never reach the plugin.
    molshredder::operation::TaskContext context;
    const auto unknown = host->invoke(store_path, manifest.value().id, "no-such-operation", {}, context, options);
    add("undeclared operations are refused by the host", !unknown.has_value(), "");
  }
  host->forget(manifest.value().id);
  std::filesystem::remove_all(scratch);
  return finish();
}
