#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/operation/task_context.hpp"

namespace {

using namespace molshredder;

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

// PyMOL 3.1.0 black-box capture (SEQ-311 S8): spectrum EXPR, rainbow over
// 1ubq without waters (602 atoms). Expected color indices are run-length
// encoded (color, atom count) in file serial order.
struct Case {
  std::string_view expression;
  std::vector<std::pair<std::int32_t, std::size_t>> runs;
};

const std::vector<Case> &cases() {
  static const std::vector<Case> all{
      {"ss",
       {{4363, 59}, {4756, 15}, {4363, 59}, {4756, 37}, {5148, 98}, {4756, 34}, {4363, 56}, {4756, 9}, {4363, 26}, {4756, 39}, {5148, 34}, {4756, 34}, {4363, 74}, {4756, 28}}},
      {"type",
       {{4363, 602}}},
      {"text_type",
       {{4363, 602}}},
      {"resv",
       {{4363, 8}, {4373, 9}, {4384, 8}, {4394, 11}, {4405, 7}, {4415, 9}, {4426, 7}, {4436, 8}, {4447, 7}, {4457, 4}, {4468, 9}, {4478, 7}, {4489, 8}, {4499, 7}, {4510, 8}, {4520, 9}, {4531, 7}, {4541, 9}, {4552, 7}, {4562, 6}, {4573, 8}, {4583, 7}, {4594, 8}, {4604, 9}, {4615, 8}, {4625, 7}, {4635, 9}, {4646, 5}, {4656, 9}, {4667, 8}, {4677, 9}, {4688, 8}, {4698, 9}, {4709, 9}, {4719, 4}, {4730, 8}, {4740, 7}, {4751, 7}, {4761, 8}, {4772, 9}, {4782, 9}, {4793, 11}, {4803, 8}, {4814, 8}, {4824, 11}, {4835, 5}, {4845, 4}, {4856, 9}, {4866, 9}, {4877, 8}, {4887, 9}, {4897, 8}, {4908, 4}, {4918, 11}, {4929, 7}, {4939, 8}, {4950, 6}, {4960, 8}, {4971, 12}, {4981, 8}, {4992, 8}, {5002, 9}, {5013, 9}, {5023, 9}, {5034, 6}, {5044, 7}, {5055, 8}, {5065, 10}, {5076, 8}, {5086, 7}, {5097, 8}, {5107, 11}, {5118, 8}, {5128, 11}, {5139, 4}, {5148, 5}}},
      {"segi",
       {{4363, 602}}},
      {"alt",
       {{4363, 602}}},
      {"partial_charge",
       {{4363, 602}}},
      {"vdw",
       {{4447, 1}, {4868, 2}, {4363, 1}, {4868, 2}, {5148, 1}, {4868, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4363, 1}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 7}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 1}, {4363, 1}, {4868, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 1}, {4363, 1}, {4868, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 1}, {4363, 1}, {4868, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 1}, {4363, 1}, {4868, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4363, 2}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4363, 2}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 1}, {4363, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 2}, {4363, 2}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 1}, {4363, 1}, {4868, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4363, 2}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 2}, {4363, 1}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 3}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4363, 1}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 2}, {4363, 2}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 3}, {4363, 2}, {4447, 1}, {4868, 2}, {4363, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 2}, {4363, 2}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4363, 1}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 3}, {4363, 1}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 3}, {4447, 1}, {4868, 1}, {4447, 3}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 7}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 3}, {4363, 1}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4363, 2}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 2}, {4363, 2}, {4447, 1}, {4868, 2}, {4363, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4447, 1}, {4868, 1}, {4447, 3}, {4868, 2}, {4363, 1}, {4868, 1}, {4363, 1}, {4868, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 1}, {4363, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 2}, {4363, 2}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 7}, {4363, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 2}, {4363, 1}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4363, 1}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 3}, {4363, 2}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 1}, {4363, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 1}, {4363, 1}, {4868, 1}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 2}, {4447, 1}, {4868, 2}, {4447, 2}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4447, 1}, {4868, 1}, {4447, 3}, {4868, 2}, {4363, 1}, {4868, 4}, {4447, 1}, {4868, 2}, {4363, 1}, {4868, 3}, {4447, 1}, {4868, 1}, {4447, 3}, {4868, 2}, {4363, 1}, {4447, 1}, {4868, 2}, {4363, 2}}},
      {"elec_radius",
       {{4363, 602}}},
      {"numeric_type",
       {{4363, 602}}},
      {"cartoon",
       {{4363, 602}}},
  };
  return all;
}

// Expressions PyMOL rejects.
constexpr std::array<std::string_view, 3> kRejected{"fc", "stereo", "state"};

std::vector<std::int32_t> atom_colors(const application::Workspace &workspace,
                                      std::size_t count) {
  std::vector<std::int32_t> colors(count, -1000);
  const auto *object = workspace.active_object();
  if (object == nullptr || object->atom_colors == nullptr)
    return colors;
  const auto &topology = *object->system->topology();
  for (const auto &[id, color] : *object->atom_colors) {
    const auto index = topology.atom_index(model::AtomId{id});
    if (index.has_value() && index->value < colors.size())
      colors[index->value] = color;
  }
  return colors;
}

} // namespace

int main(int argc, char **argv) {
  bool passed = true;
  if (argc != 2) {
    std::cerr << "expected PDB fixture path\n";
    return 1;
  }
  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  operation::TaskContext context;
  const auto run = [&](std::string name, command::Arguments arguments) {
    return dispatcher.dispatch({std::move(name), std::move(arguments)}, context);
  };
  passed &= expect(run("load", {{"file-format", "pdb"},
                                {"path", std::filesystem::path{argv[1]}.string()}})
                       .succeeded(),
                   "1ubq fixture must load");
  constexpr std::size_t kAtoms = 602;
  for (const auto &item : cases()) {
    std::vector<std::int32_t> expected;
    for (const auto &[color, count] : item.runs)
      expected.insert(expected.end(), count, color);
    const auto outcome = run("spectrum", {{"expression", std::string{item.expression}},
                                          {"palette", "rainbow"},
                                          {"selection", "not resn HOH"}});
    const auto colors = atom_colors(*workspace, kAtoms);
    std::size_t mismatches{};
    for (std::size_t atom = 0; atom < kAtoms; ++atom)
      mismatches += colors[atom] != expected[atom] ? 1U : 0U;
    passed &= expect(outcome.succeeded() && expected.size() == kAtoms &&
                         mismatches == 0U,
                     std::string{"PyMOL spectrum property parity: "} +
                         std::string{item.expression} + " mismatches " +
                         std::to_string(mismatches));
  }
  for (const auto expression : kRejected) {
    passed &= expect(!run("spectrum", {{"expression", std::string{expression}},
                                       {"palette", "rainbow"},
                                       {"selection", "not resn HOH"}})
                          .succeeded(),
                     std::string{"PyMOL rejects spectrum "} +
                         std::string{expression});
  }
  return passed ? 0 : 1;
}
