// Evaluates a Tcl script from stdin with the adapter's interpreter. The only
// command besides set/unset/list is `probe`, which records its words.
#include <cstdio>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/automation/tcl.hpp"
#include "molshredder/automation/vmd_tcl.hpp"

namespace {

std::string quote(const std::string& text) {
  std::string out = "\"";
  for (const char raw : text) {
    const auto c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          out += buffer;
        } else {
          out += raw;
        }
    }
  }
  return out + "\"";
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder::automation::tcl;
  bool translate_mode = false;
  std::vector<std::string> known_objects;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--translate") translate_mode = true;
    if (argument.rfind("--object=", 0) == 0) known_objects.push_back(argument.substr(9));
  }
  const std::string source{std::istreambuf_iterator<char>{std::cin},
                           std::istreambuf_iterator<char>{}};
  if (translate_mode) {
    molshredder::automation::vmd::TranslationContext context;
    context.is_object = [&known_objects](std::string_view name) {
      for (const auto& object : known_objects) {
        if (object == name) return true;
      }
      return false;
    };
    const auto translation = molshredder::automation::vmd::translate_script(source, context);
    auto workspace = std::make_shared<molshredder::application::Workspace>();
    const auto registry = molshredder::application::make_default_registry(workspace);
    std::string json = "{\"stopped\":" + std::string{translation.stopped ? "true" : "false"} +
                       ",\"commands\":[";
    for (std::size_t i = 0; i < translation.commands.size(); ++i) {
      const auto& command = translation.commands[i];
      if (i != 0U) json += ',';
      json += "{\"line\":" + std::to_string(command.line) + ",\"stop\":" +
              std::string{(command.stop_script || command.tcl_error) ? "true" : "false"} + ",\"tcl_error\":" +
              std::string{command.tcl_error ? "true" : "false"} + ",\"invocations\":[";
      for (std::size_t n = 0; n < command.invocations.size(); ++n) {
        if (n != 0U) json += ',';
        json += "{\"command\":" + quote(command.invocations[n].command) + ",\"arguments\":{";
        bool first = true;
        for (const auto& [key, value] : command.invocations[n].arguments) {
          if (!first) json += ',';
          first = false;
          json += quote(key) + ":" + quote(value);
        }
        json += "}";
        const auto normalized = registry.normalize(molshredder::command::Invocation{
            command.invocations[n].command, command.invocations[n].arguments});
        json += ",\"valid\":" + std::string{normalized.has_value() ? "true" : "false"};
        if (!normalized.has_value()) json += ",\"error\":" + quote(normalized.error().message);
        json += "}";
      }
      json += "],\"diagnostics\":[";
      for (std::size_t d = 0; d < command.diagnostics.size(); ++d) {
        if (d != 0U) json += ',';
        json += "{\"code\":" + quote(command.diagnostics[d].code) + ",\"severity\":" +
                quote(command.diagnostics[d].severity) + "}";
      }
      json += "]}";
    }
    json += "]}\n";
    std::fputs(json.c_str(), stdout);
    return 0;
  }
  std::vector<std::vector<std::string>> calls;
  Interpreter interpreter;
  interpreter.register_command(
      "probe", [&calls](Interpreter&, const std::vector<std::string>& words) {
        calls.emplace_back(words.begin() + 1, words.end());
        return molshredder::operation::Result<std::string>::success({});
      });
  const auto parsed = parse(source);
  std::string error;
  for (const auto& command : parsed.commands) {
    const auto result = interpreter.evaluate(command);
    if (!result.has_value()) {
      error = result.error().message;
      break;
    }
  }
  if (error.empty() && parsed.error.has_value()) error = parsed.error->message;
  std::string out = "{\"calls\":[";
  for (std::size_t i = 0; i < calls.size(); ++i) {
    if (i != 0U) out += ',';
    out += '[';
    for (std::size_t j = 0; j < calls[i].size(); ++j) {
      if (j != 0U) out += ',';
      out += quote(calls[i][j]);
    }
    out += ']';
  }
  out += "],\"error\":" + (error.empty() ? std::string{"null"} : quote(error)) + "}\n";
  std::fputs(out.c_str(), stdout);
  return 0;
}
