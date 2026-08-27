#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/command/foundation_grammar.hpp"
#include "molshredder/command/serialization.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/result.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main(int argc, char *argv[]) {
  using molshredder::command::Arguments;
  using molshredder::command::Invocation;
  using molshredder::command::Registry;
  using molshredder::command::Response;
  using molshredder::operation::ErrorCode;
  using molshredder::operation::Result;
  using molshredder::operation::TaskContext;

  bool passed = true;
  passed &= expect(molshredder::command::kFoundationGrammarVersion == 1U,
                   "foundation command grammar must be pinned at version 1");
  passed &=
      expect(argc == 2, "foundation grammar test requires one golden fixture");
  if (argc != 2) {
    return 1;
  }

  const auto descriptors =
      molshredder::command::foundation_command_descriptors();
  const auto aliases = molshredder::command::foundation_command_aliases();
  const auto view_aliases = molshredder::command::view_command_aliases();
  const auto file_descriptors =
      molshredder::command::file_command_descriptors();
  const auto trajectory_descriptors =
      molshredder::command::trajectory_command_descriptors();
  const auto find_descriptor = [](const auto &items, std::string_view name) {
    return std::find_if(items.begin(), items.end(), [name](const auto &item) {
      return item.canonical_name == name;
    });
  };
  const auto has_all_commands = [&find_descriptor](
                                    const auto &items,
                                    const std::vector<std::string_view> &names) {
    return std::ranges::all_of(names, [&items, &find_descriptor](auto name) {
      return find_descriptor(items, name) != items.end();
    });
  };
  auto foundation_names = std::vector<std::string>{};
  foundation_names.reserve(descriptors.size());
  std::ranges::transform(descriptors, std::back_inserter(foundation_names),
                         [](const auto &descriptor) {
                           return descriptor.canonical_name;
                         });
  std::ranges::sort(foundation_names);
  passed &= expect(std::ranges::adjacent_find(foundation_names) ==
                       foundation_names.end(),
                   "foundation grammar canonical command names must be unique");
  for (const auto name :
       std::vector<std::string_view>{"load", "select", "show", "hide", "as",
                                     "toggle", "surface show", "surface hide",
                                     "setting set", "analyze center",
                                     "measure distance", "analyze contacts"}) {
    passed &= expect(find_descriptor(descriptors, name) != descriptors.end(),
                     std::string{"foundation grammar is missing command: "} +
                         std::string{name});
  }
  auto alias_names = std::vector<std::string>{};
  alias_names.reserve(aliases.size());
  std::ranges::transform(aliases, std::back_inserter(alias_names),
                         [](const auto &alias) { return alias.name; });
  std::ranges::sort(alias_names);
  passed &= expect(std::ranges::adjacent_find(alias_names) == alias_names.end(),
                   "foundation shorthand alias names must be unique");
  for (const auto name : std::vector<std::string_view>{
           "com", "distance", "angle", "dihedral", "sasa"}) {
    passed &= expect(std::ranges::binary_search(alias_names, name),
                     std::string{"foundation grammar is missing alias: "} +
                         std::string{name});
  }
  passed &= expect(view_aliases.size() == 3,
                   "view grammar must expose three projection aliases");
  passed &= expect(
      has_all_commands(file_descriptors,
                       {"format list", "volume load", "volume list",
                        "volume save", "volume isosurface", "volume slice",
                        "volume ramp set", "volume ramp define",
                        "volume ramp get", "volume render", "volume hide",
                        "save", "load batch"}),
      "additive file grammar must expose format, volume, save and batch commands");
  const auto has_provider = [](const auto &descriptor) {
    const auto found = std::find_if(
        descriptor.parameters.begin(), descriptor.parameters.end(),
        [](const auto &parameter) { return parameter.name == "provider"; });
    return found != descriptor.parameters.end() &&
           found->default_value == "auto" && found->allowed_values.empty();
  };
  const auto has_plugin_path = [](const auto &descriptor) {
    return std::ranges::any_of(descriptor.parameters, [](const auto &parameter) {
      return parameter.name == "plugin-path" && !parameter.required &&
             !parameter.default_value.has_value();
    });
  };
  const auto foundation_load = find_descriptor(descriptors, "load");
  const auto format_list = find_descriptor(file_descriptors, "format list");
  const auto volume_load = find_descriptor(file_descriptors, "volume load");
  const auto volume_save = find_descriptor(file_descriptors, "volume save");
  const auto structure_save = find_descriptor(file_descriptors, "save");
  const auto batch_load = find_descriptor(file_descriptors, "load batch");
  passed &= expect(
      foundation_load != descriptors.end() && has_provider(*foundation_load) &&
          has_plugin_path(*foundation_load) &&
          format_list != file_descriptors.end() && has_provider(*format_list) &&
          volume_load != file_descriptors.end() && has_provider(*volume_load) &&
          volume_save != file_descriptors.end() && has_provider(*volume_save) &&
          structure_save != file_descriptors.end() &&
          has_provider(*structure_save) && batch_load != file_descriptors.end() &&
          has_provider(*batch_load),
      "all structure and volume I/O commands must expose provider override");
  const auto trajectory_save = std::find_if(
      trajectory_descriptors.begin(), trajectory_descriptors.end(),
      [](const auto &descriptor) {
        return descriptor.canonical_name == "traj save";
      });
  const auto trajectory_load = std::find_if(
      trajectory_descriptors.begin(), trajectory_descriptors.end(),
      [](const auto &descriptor) {
        return descriptor.canonical_name == "traj load";
      });
  passed &= expect(trajectory_load != trajectory_descriptors.end() &&
                       trajectory_save != trajectory_descriptors.end() &&
                       has_provider(*trajectory_load) &&
                       has_provider(*trajectory_save),
                   "trajectory load/save must expose provider override");
  const auto trajectory_mapping =
      trajectory_load == trajectory_descriptors.end()
          ? std::vector<molshredder::command::ParameterSpec>::const_iterator{}
          : std::find_if(trajectory_load->parameters.begin(),
                         trajectory_load->parameters.end(),
                         [](const auto &parameter) {
                           return parameter.name == "mapping";
                         });
  passed &= expect(
      trajectory_load != trajectory_descriptors.end() &&
          trajectory_mapping != trajectory_load->parameters.end() &&
          trajectory_mapping->required &&
          !trajectory_mapping->default_value.has_value() &&
          trajectory_mapping->allowed_values ==
              std::vector<std::string>{"exact", "index", "explicit"},
      "trajectory load must require an explicit topology mapping policy");
  const auto trajectory_load_format =
      trajectory_load == trajectory_descriptors.end()
          ? std::vector<molshredder::command::ParameterSpec>::const_iterator{}
          : std::find_if(trajectory_load->parameters.begin(),
                         trajectory_load->parameters.end(),
                         [](const auto &parameter) {
                           return parameter.name == "file-format";
                         });
  passed &= expect(
      trajectory_load != trajectory_descriptors.end() &&
          trajectory_load_format != trajectory_load->parameters.end() &&
          std::find(trajectory_load_format->allowed_values.begin(),
                    trajectory_load_format->allowed_values.end(),
                    "ncrst") != trajectory_load_format->allowed_values.end(),
      "trajectory grammar must expose the AMBERRESTART alias");
  const auto trajectory_save_format =
      trajectory_save == trajectory_descriptors.end()
          ? std::vector<molshredder::command::ParameterSpec>::const_iterator{}
          : std::find_if(trajectory_save->parameters.begin(),
                         trajectory_save->parameters.end(),
                         [](const auto &parameter) {
                           return parameter.name == "file-format";
                         });
  passed &= expect(trajectory_save != trajectory_descriptors.end() &&
                       trajectory_save->undo_policy ==
                           molshredder::command::UndoPolicy::not_applicable &&
                       trajectory_save_format !=
                           trajectory_save->parameters.end() &&
                       std::find(trajectory_save_format->allowed_values.begin(),
                                 trajectory_save_format->allowed_values.end(),
                                 "crdbox") !=
                           trajectory_save_format->allowed_values.end() &&
                       std::find(trajectory_save_format->allowed_values.begin(),
                                 trajectory_save_format->allowed_values.end(),
                                 "binpos") !=
                           trajectory_save_format->allowed_values.end(),
                   "trajectory grammar must expose CRDBOX and BINPOS export");

  Registry registry;
  for (auto descriptor : descriptors) {
    const auto command_name = descriptor.canonical_name;
    const auto failure =
        registry.add(std::move(descriptor),
                     [command_name](const Arguments &arguments, TaskContext &) {
                       molshredder::command::Value::Object fields;
                       for (const auto &[name, value] : arguments) {
                         fields.emplace(name, value);
                       }
                       return Result<Response>::success(
                           {command_name + " accepted", std::move(fields)});
                     });
    passed &= expect(!failure.has_value(), "foundation command must register");
  }
  for (auto alias : aliases) {
    passed &= expect(!registry.add_alias(std::move(alias)).has_value(),
                     "foundation alias must register");
  }

  const std::vector<Invocation> inputs{
      {"open", {{"path", "model.pdb"}, {"file-format", "pdb"}}},
      {"select", {{"name", "protein"}, {"expression", "polymer.protein"}}},
      {"show", {{"representation", "sticks"}}},
      {"center", {}},
      {"com", {{"selection", "chain A"}, {"precision", "4"}}},
      {"dist", {{"from", "id 1"}, {"to", "id 2"}}},
  };
  std::string actual;
  for (const auto &input : inputs) {
    const auto normalized = registry.normalize(input);
    passed &= expect(normalized.has_value(),
                     "valid foundation invocation must normalize");
    if (normalized.has_value()) {
      actual += molshredder::command::serialize(normalized.value()) + '\n';
    }
  }

  std::ifstream golden_stream{argv[1]};
  const std::string expected{std::istreambuf_iterator<char>{golden_stream},
                             std::istreambuf_iterator<char>{}};
  passed &= expect(actual == expected,
                   "normalized grammar must match the canonical golden file");

  const auto invalid_representation =
      registry.normalize(Invocation{"show", {{"representation", "surface"}}});
  passed &= expect(!invalid_representation.has_value() &&
                       invalid_representation.error().code ==
                           ErrorCode::invalid_argument,
                   "representation choices must be registry-validated");
  const auto invalid_precision =
      registry.normalize(Invocation{"analyze center", {{"precision", "16"}}});
  passed &= expect(!invalid_precision.has_value() &&
                       invalid_precision.error().message.find("not allowed") !=
                           std::string::npos,
                   "precision must remain in the common 0-15 range");
  const auto missing_path = registry.normalize(Invocation{"load", {}});
  passed &= expect(!missing_path.has_value() &&
                       missing_path.error().message ==
                           "missing required parameter: path",
                   "load must require an explicit path");
  const auto windows_path = registry.normalize(
      Invocation{"load", {{"path", "D:\\data\\model.pdb"}}});
  passed &= expect(
      windows_path.has_value() &&
          windows_path.value().arguments.at("path") == "D:/data/model.pdb",
      "canonical filesystem arguments must use portable separators");
  const auto setting_set = registry.normalize(
      Invocation{"setting set", {{"name", "line_width"}, {"value", "2.5"}}});
  passed &= expect(
      setting_set.has_value() &&
          setting_set.value().arguments.at("scope") == "global" &&
          setting_set.value().arguments.at("object") == "current" &&
          setting_set.value().arguments.at("state") == "current",
      "render setting grammar must normalize one canonical frontend schema");

  TaskContext context;
  const auto invoked =
      registry.invoke(Invocation{"com", {{"selection", "protein"}}}, context);
  passed &= expect(invoked.has_value(),
                   "registry invoke must normalize aliases and defaults");
  passed &= expect(
      invoked.has_value() &&
          std::get<std::string>(invoked.value().fields.at("mode").data) ==
              "com" &&
          std::get<std::string>(invoked.value().fields.at("unit").data) ==
              "angstrom",
      "handler must receive normalized default arguments");

  return passed ? 0 : 1;
}
