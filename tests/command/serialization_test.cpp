#include <iostream>
#include <string_view>

#include "molshredder/command/registry.hpp"
#include "molshredder/command/serialization.hpp"
#include "molshredder/operation/result.hpp"

namespace {

using molshredder::command::Arguments;
using molshredder::command::Descriptor;
using molshredder::command::Invocation;
using molshredder::command::InvocationSource;
using molshredder::command::ParameterSpec;
using molshredder::command::ParameterType;
using molshredder::command::Registry;
using molshredder::command::Response;
using molshredder::operation::Result;
using molshredder::operation::TaskContext;

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  bool passed = true;
  const Invocation original{
      "analyze center",
      {{"empty", ""},
       {"path", "folder\\model \"A\".pdb"},
       {"selection", "chain A and\nname CA"}}};

  const auto serialized = molshredder::command::serialize(original);
  const auto parsed = molshredder::command::parse_canonical(serialized);
  passed &= expect(parsed.has_value(), "serialized command must parse");
  passed &= expect(parsed.has_value() && parsed.value() == original,
                   "canonical command must round-trip exactly");
  passed &= expect(molshredder::command::serialize(parsed.value()) == serialized,
                   "canonical serialization must be deterministic");

  const auto malformed =
      molshredder::command::parse_canonical("invoke \"load\" --path");
  passed &= expect(!malformed.has_value(), "missing value must fail parsing");
  const auto duplicate = molshredder::command::parse_canonical(
      "invoke \"load\" --path \"a\" --path \"b\"");
  passed &= expect(!duplicate.has_value(),
                   "duplicate canonical parameter must fail");
  const auto unterminated =
      molshredder::command::parse_canonical("invoke \"load");
  passed &= expect(!unterminated.has_value(),
                   "unterminated canonical quote must fail");

  const auto record =
      molshredder::command::make_record(original, InvocationSource::gui, 42);
  passed &= expect(record.provenance.schema_version ==
                       molshredder::command::kInvocationSchemaVersion,
                   "record must carry schema version");
  passed &= expect(record.provenance.source == InvocationSource::gui,
                   "record must carry frontend source");
  passed &= expect(record.provenance.sequence == 42,
                   "record must carry sequence number");
  passed &= expect(record.provenance.canonical_command == serialized,
                   "record must preserve canonical command");

  Registry registry;
  const auto registration = registry.add(
      Descriptor{"analyze center",
                 "center",
                 {ParameterSpec{"empty", ParameterType::text, false},
                  ParameterSpec{"path", ParameterType::text, false},
                  ParameterSpec{"selection", ParameterType::text, false}},
                 {}},
      [](const Arguments& arguments, TaskContext&) {
        return Result<Response>::success(
            {"replayed", {{"argument_count", std::to_string(arguments.size())}}});
      });
  passed &= expect(!registration.has_value(), "test command must register");

  TaskContext context;
  const auto replayed = registry.invoke(parsed.value(), context);
  passed &= expect(replayed.has_value(), "parsed invocation must replay");
  passed &= expect(replayed.has_value() &&
                       replayed.value().fields.at("argument_count") == "3",
                   "replay must preserve all arguments");

  return passed ? 0 : 1;
}
