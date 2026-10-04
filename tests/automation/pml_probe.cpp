// Parses PML read from stdin and prints the statements as JSON. Extra probe
// commands (name:mode) extend the PyMOL keyword table for conformance tests.
#include <cstdio>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/automation/pml.hpp"
#include "molshredder/automation/pml_translator.hpp"

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
          out += static_cast<char>(c);
        }
    }
  }
  return out + "\"";
}

}  // namespace

int main(int argc, char** argv) {
  using namespace molshredder::automation::pml;
  KeywordTable keywords = KeywordTable::pymol_310();
  bool translate_mode = false;
  std::vector<std::string> known_objects;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--translate") {
      translate_mode = true;
      continue;
    }
    if (argument.rfind("--object=", 0) == 0) {
      known_objects.push_back(argument.substr(9));
      continue;
    }
    const auto colon = argument.find(':');
    if (colon == std::string::npos) return 2;
    keywords.add(argument.substr(0, colon), std::stoi(argument.substr(colon + 1)));
  }
  const std::string source{std::istreambuf_iterator<char>{std::cin},
                           std::istreambuf_iterator<char>{}};
  const auto script = parse(source, keywords);
  if (translate_mode) {
    TranslationContext context;
    context.is_object = [&known_objects](std::string_view name) {
      for (const auto& object : known_objects) {
        if (object == name) return true;
      }
      return false;
    };
    context.list_objects = [&known_objects] { return known_objects; };
    const auto translation = translate_script(script, context);
    auto workspace = std::make_shared<molshredder::application::Workspace>();
    const auto registry = molshredder::application::make_default_registry(workspace);
    std::string json = "{\"statements\":[";
    for (std::size_t i = 0; i < translation.statements.size(); ++i) {
      const auto& statement = translation.statements[i];
      if (i != 0U) json += ',';
      json += "{\"line\":" + std::to_string(statement.line);
      json += ",\"stop\":" + std::string{statement.stop_script ? "true" : "false"};
      json += ",\"invocations\":[";
      for (std::size_t n = 0; n < statement.invocations.size(); ++n) {
        if (n != 0U) json += ',';
        json += "{\"command\":" + quote(statement.invocations[n].command) + ",\"arguments\":{";
        bool first = true;
        for (const auto& [key, value] : statement.invocations[n].arguments) {
          if (!first) json += ',';
          first = false;
          json += quote(key) + ":" + quote(value);
        }
        json += "}";
        const auto normalized = registry.normalize(molshredder::command::Invocation{
            statement.invocations[n].command, statement.invocations[n].arguments});
        json += ",\"valid\":" + std::string{normalized.has_value() ? "true" : "false"};
        if (!normalized.has_value()) json += ",\"error\":" + quote(normalized.error().message);
        json += "}";
      }
      json += "],\"diagnostics\":[";
      for (std::size_t d = 0; d < statement.diagnostics.size(); ++d) {
        if (d != 0U) json += ',';
        json += "{\"code\":" + quote(statement.diagnostics[d].code) + ",\"severity\":" +
                quote(statement.diagnostics[d].severity) + "}";
      }
      json += "]}";
    }
    json += "],\"parse_diagnostics\":[";
    for (std::size_t d = 0; d < translation.parse_diagnostics.size(); ++d) {
      if (d != 0U) json += ',';
      json += quote(translation.parse_diagnostics[d].code);
    }
    json += "]}\n";
    std::fputs(json.c_str(), stdout);
    return 0;
  }
  std::string out = "{\"aborted\":";
  out += script.aborted ? "true" : "false";
  out += ",\"comments\":" + std::to_string(script.comment_lines);
  out += ",\"diagnostics\":[";
  for (std::size_t i = 0; i < script.diagnostics.size(); ++i) {
    if (i != 0U) out += ',';
    out += "{\"code\":" + quote(script.diagnostics[i].code) + ",\"line\":" +
           std::to_string(script.diagnostics[i].line) + "}";
  }
  out += "],\"statements\":[";
  for (std::size_t i = 0; i < script.statements.size(); ++i) {
    const auto& statement = script.statements[i];
    if (i != 0U) out += ',';
    out += "{\"kind\":" + std::string{quote(std::string{to_string(statement.kind)})};
    out += ",\"command\":" + quote(statement.command);
    out += ",\"line\":" + std::to_string(statement.line);
    out += ",\"args\":[";
    for (std::size_t a = 0; a < statement.args.size(); ++a) {
      if (a != 0U) out += ',';
      out += statement.args[a].has_value() ? quote(*statement.args[a]) : "null";
    }
    out += "],\"kwargs\":{";
    for (std::size_t k = 0; k < statement.kwargs.size(); ++k) {
      if (k != 0U) out += ',';
      out += quote(statement.kwargs[k].first) + ":" + quote(statement.kwargs[k].second);
    }
    out += "},\"block_lines\":" + std::to_string(statement.block_lines.size());
    out += ",\"diagnostics\":[";
    for (std::size_t d = 0; d < statement.diagnostics.size(); ++d) {
      if (d != 0U) out += ',';
      out += quote(statement.diagnostics[d].code);
    }
    out += "]}";
  }
  out += "]}\n";
  std::fputs(out.c_str(), stdout);
  return 0;
}
