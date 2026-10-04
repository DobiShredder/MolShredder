#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

#include "molshredder/command/json.hpp"
#include "molshredder/plugin/manifest.hpp"

namespace {

bool expect(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAILED: " << message << '\n';
  return condition;
}

using molshredder::command::Value;
using molshredder::command::parse_json;
using molshredder::command::to_json;

bool rejects(const std::string& text) { return !parse_json(text).has_value(); }

const char* kValid = R"({
  "schema_version": 1, "id": "org.example.sample", "namespace": "sample", "name": "Sample",
  "version": "1.2.3", "license": "MIT",
  "sdk": {"abi_major": 1, "abi_minor": 0},
  "execution": {"mode": "worker", "command": ["bin/worker", "--flag"]},
  "required_capabilities": ["progress"], "optional_capabilities": ["cancellation", "future.thing"],
  "operations": [
    {"name": "echo", "summary": "Echo text", "parameters": [
      {"name": "text", "type": "text", "required": true},
      {"name": "times", "type": "unsigned_integer", "default": "1"},
      {"name": "mode", "type": "text", "default": "plain", "choices": ["plain", "loud"]}]}],
  "examples": [{"operation": "echo", "arguments": {"text": "hi"}}]
})";

std::string with(const std::string& from, const std::string& to) {
  std::string text = kValid;
  const auto at = text.find(from);
  if (at == std::string::npos) return text;
  text.replace(at, from.size(), to);
  return text;
}

}  // namespace

int main() {
  using namespace molshredder::plugin;
  bool passed = true;

  // JSON: values, numbers, escapes and the strictness rules.
  const auto parsed = parse_json(R"({"a":[1,-2,3.5,1e2,true,null,"x\u00e9\ud83d\ude00\n"],"b":{"c":18446744073709551615}})");
  passed &= expect(parsed.has_value(), "valid JSON must parse");
  if (parsed.has_value()) {
    const auto* object = std::get_if<Value::Object>(&parsed.value().data);
    const auto& list = std::get<Value::Array>(object->at("a").data);
    passed &= expect(std::get<std::int64_t>(list[0].data) == 1 && std::get<std::int64_t>(list[1].data) == -2 &&
                         std::get<double>(list[2].data) == 3.5 && std::get<double>(list[3].data) == 100.0 &&
                         std::get<bool>(list[4].data) && std::holds_alternative<std::nullptr_t>(list[5].data),
                     "numbers and literals keep their types");
    passed &= expect(std::get<std::string>(list[6].data) == "x\xc3\xa9\xf0\x9f\x98\x80\n",
                     "\\u escapes and surrogate pairs become UTF-8");
    passed &= expect(std::get<std::uint64_t>(std::get<Value::Object>(object->at("b").data).at("c").data) ==
                         18446744073709551615ULL,
                     "large integers stay exact");
    const auto text = to_json(parsed.value());
    // 1e2 serializes as 100 and reads back as an integer, so compare the text.
    const auto again = text.has_value() ? parse_json(text.value()) : parse_json("");
    passed &= expect(again.has_value() && again.value() == parsed.value() &&
                         to_json(again.value()).value() == text.value(),
                     "serialization round-trips exactly, including integral doubles");
    passed &= expect(to_json(Value{3.0}).value() == "3.0" && to_json(Value{std::int64_t{3}}).value() == "3" &&
                         to_json(Value{1.5e300}).value().find('e') != std::string::npos,
                     "integral doubles keep a decimal point");
  }
  for (const char* bad : {"", "{", "[1,]", "{\"a\":1,}", "{\"a\":1,\"a\":2}", "[01]", "[1.]", "[.5]", "[1e]",
                          "[\"\\x\"]", "[\"\\ud800\"]", "[\"\x01\"]", "[\"\xff\"]", "[1] x", "nul", "[1e999]",
                          "{'a':1}", "[\"unterminated"}) {
    passed &= expect(rejects(bad), std::string{"must reject: "} + bad);
  }
  std::string deep(200, '[');
  deep += std::string(200, ']');
  passed &= expect(rejects(deep), "deep nesting is rejected");
  passed &= expect(!parse_json(std::string(100, ' ') + "1", {50U, 64U}).has_value(), "size limit");
  passed &= expect(!to_json(Value{std::nan("")}).has_value(), "non-finite numbers cannot be serialized");
  passed &= expect(!to_json(Value{std::string{"\xff"}}).has_value(), "invalid UTF-8 cannot be serialized");

  // Manifest.
  const auto manifest = parse_manifest(kValid, std::filesystem::path{"/tmp"});
  passed &= expect(manifest.has_value(), manifest.has_value() ? "" : manifest.error().message);
  if (manifest.has_value()) {
    const auto& m = manifest.value();
    passed &= expect(m.id == "org.example.sample" && m.name_space == "sample" && m.mode == ExecutionMode::worker &&
                         m.worker_command.size() == 2U && m.operations.size() == 1U &&
                         m.operations[0].parameters.size() == 3U && m.operations[0].parameters[0].required &&
                         m.operations[0].parameters[1].default_value == "1" &&
                         m.operations[0].parameters[2].choices.size() == 2U && m.examples.size() == 1U,
                     "manifest fields");
    passed &= expect(canonical_operation_name(m, m.operations[0]) == "plugin sample echo", "canonical name");
    // Negotiation.
    const auto host = default_host_info();
    const auto negotiated = negotiate(m, host);
    passed &= expect(negotiated.has_value() && negotiated.value().capabilities.count("progress") == 1U &&
                         negotiated.value().capabilities.count("cancellation") == 1U &&
                         negotiated.value().capabilities.count("future.thing") == 0U,
                     "required and known optional capabilities are negotiated; unknown optional ones are dropped");
    auto newer = m;
    newer.sdk_minor = 1;
    passed &= expect(!negotiate(newer, host).has_value(), "a newer minor than the host is rejected");
    auto other_major = m;
    other_major.sdk_major = 2;
    passed &= expect(!negotiate(other_major, host).has_value(), "a different major is rejected");
    auto host_next = host;
    host_next.abi_minor = 3;
    auto older = m;
    older.sdk_minor = 0;
    const auto older_result = negotiate(older, host_next);
    passed &= expect(older_result.has_value() && older_result.value().abi_minor == 0U,
                     "an older plugin minor works on a newer host");
    auto needs = m;
    needs.required_capabilities.push_back("quantum.teleport");
    const auto missing = negotiate(needs, host);
    passed &= expect(!missing.has_value() && missing.error().message.find("quantum.teleport") != std::string::npos,
                     "a missing required capability is named");
  }
  const std::pair<const char*, std::string> invalid_cases[] = {
      {"schema version", with("\"schema_version\": 1", "\"schema_version\": 2")},
      {"id", with("org.example.sample", "X")},
      {"reserved namespace", with("\"namespace\": \"sample\"", "\"namespace\": \"list\"")},
      {"uppercase namespace", with("\"namespace\": \"sample\"", "\"namespace\": \"Sample\"")},
      {"version", with("1.2.3", "1.2")},
      {"license", with("\"license\": \"MIT\"", "\"license\": \"\"")},
      {"mode", with("\"worker\"", "\"thread\"")},
      {"absolute command", with("bin/worker", "/bin/sh")},
      {"escaping command", with("bin/worker", "../worker")},
      {"parameter type", with("unsigned_integer", "matrix")},
      {"duplicate parameter", with("\"name\": \"times\"", "\"name\": \"text\"")},
      {"required with default", with("\"name\": \"text\", \"type\": \"text\", \"required\": true",
                                     "\"name\": \"text\", \"type\": \"text\", \"required\": true, \"default\": \"x\"")},
      {"default outside choices", with("\"default\": \"plain\"", "\"default\": \"quiet\"")},
      {"undo", with("\"summary\": \"Echo text\"", "\"summary\": \"x\", \"undo\": \"undoable\"")},
      {"empty operations", with("\"operations\": [", "\"operations\": [] , \"ignored\": [")},
      {"example operation", with("\"operation\": \"echo\", \"arguments\"", "\"operation\": \"nope\", \"arguments\"")},
      {"example value", with("\"text\": \"hi\"", "\"text\": 5")},
  };
  for (const auto& [name, text] : invalid_cases) {
    passed &= expect(!parse_manifest(text, std::filesystem::path{"/tmp"}).has_value(),
                     std::string{"manifest must be rejected: "} + name);
  }
  const auto in_process = [](const char* license) {
    return parse_manifest(std::string{R"({"schema_version":1,"id":"org.example.native","namespace":"native",
      "version":"1.0.0","license":")"} + license + R"(","sdk":{"abi_major":1,"abi_minor":0},
      "execution":{"mode":"in_process","library":"lib/native.so"},
      "operations":[{"name":"op"}]})",
                          std::filesystem::path{"/tmp"});
  };
  passed &= expect(in_process("GPL-3.0-or-later").has_value() && in_process("MIT").has_value(),
                   "GPL-compatible in-process plugins are accepted");
  passed &= expect(!in_process("Proprietary").has_value() && !in_process("GPL-2.0-only").has_value(),
                   "in-process plugins need a GPL-3.0-or-later compatible license");
  return passed ? 0 : 1;
}
