#include "molshredder/automation/pml_commands.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/automation/pml.hpp"
#include "molshredder/automation/pml_translator.hpp"
#include "molshredder/automation/python_script.hpp"
#include "molshredder/automation/tcl.hpp"
#include "molshredder/automation/vmd_tcl.hpp"
#include "molshredder/core/sha256.hpp"

namespace molshredder::automation {
namespace {

using command::Value;
using operation::Error;
using operation::ErrorCode;
using operation::Result;

Error invalid(std::string message, std::string suggestion = {}) {
  return {ErrorCode::invalid_argument, std::move(message), std::move(suggestion)};
}

struct Source {
  std::string text;
  std::string origin;
  std::string path;
  std::string sha256;
};

Result<Source> load_source(const command::Arguments& arguments) {
  const auto path = arguments.find("path");
  const auto text = arguments.find("text");
  const bool has_path = path != arguments.end() && !path->second.empty();
  const bool has_text = text != arguments.end() && !text->second.empty();
  if (has_path == has_text) {
    return Result<Source>::failure(invalid(
        "give exactly one of --path and --text", "use --path FILE.pml or --text \"...\""));
  }
  Source source;
  if (has_text) {
    if (text->second.size() > kDefaultScriptSourceLimit) {
      return Result<Source>::failure(invalid("script text exceeds the source limit"));
    }
    source.text = text->second;
    source.origin = "text";
  } else {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path->second, error);
    if (error || !std::filesystem::is_regular_file(canonical)) {
      return Result<Source>::failure(
          invalid("script is not a regular file: " + path->second));
    }
    const auto size = std::filesystem::file_size(canonical, error);
    if (error || size > kDefaultScriptSourceLimit) {
      return Result<Source>::failure(invalid(
          "script exceeds the source limit of " +
          std::to_string(kDefaultScriptSourceLimit) + " bytes"));
    }
    std::ifstream input{canonical, std::ios::binary};
    if (!input) {
      return Result<Source>::failure(invalid("script cannot be read: " + path->second));
    }
    source.text.assign(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
    source.origin = "path";
    source.path = canonical.string();
  }
  source.sha256 = core::sha256_hex(source.text);
  return Result<Source>::success(std::move(source));
}

pml::TranslationContext live_context(
    const std::shared_ptr<application::Workspace>& workspace) {
  pml::TranslationContext context;
  if (workspace == nullptr) return context;
  context.is_object = [workspace](std::string_view name) {
    for (const auto& object : workspace->list_objects()) {
      if (object.name == name) return true;
    }
    return false;
  };
  context.is_selection = [workspace](std::string_view name) {
    for (const auto& object : workspace->objects()) {
      for (const auto& selection : object.selections.list()) {
        if (selection.name == name) return true;
      }
    }
    return false;
  };
  context.list_objects = [workspace] {
    std::vector<std::string> names;
    for (const auto& object : workspace->list_objects()) names.push_back(object.name);
    return names;
  };
  return context;
}

Value diagnostic_value(const pml::Diagnostic& diagnostic) {
  return Value::Object{{"code", diagnostic.code},
                       {"line", static_cast<std::uint64_t>(diagnostic.line)},
                       {"message", diagnostic.message},
                       {"severity", diagnostic.severity}};
}

Value diagnostics_value(const std::vector<pml::Diagnostic>& diagnostics) {
  Value::Array array;
  for (const auto& diagnostic : diagnostics) array.push_back(diagnostic_value(diagnostic));
  return array;
}

Value arguments_value(const std::map<std::string, std::string, std::less<>>& arguments) {
  Value::Object object;
  for (const auto& [key, value] : arguments) object.emplace(key, value);
  return object;
}

Value invocation_value(const pml::Invocation& invocation) {
  return Value::Object{{"arguments", arguments_value(invocation.arguments)},
                       {"command", invocation.command}};
}

Value statement_value(const pml::TranslatedStatement& statement) {
  Value::Array invocations;
  for (const auto& invocation : statement.invocations) {
    invocations.push_back(invocation_value(invocation));
  }
  return Value::Object{{"command", statement.command},
                       {"diagnostics", diagnostics_value(statement.diagnostics)},
                       {"invocations", std::move(invocations)},
                       {"kind", std::string{pml::to_string(statement.kind)}},
                       {"line", static_cast<std::uint64_t>(statement.line)},
                       {"stop", statement.stop_script},
                       {"text", statement.text}};
}

Value::Object source_fields(const Source& source, const char* language = "pml") {
  Value::Object fields{{"language", language},
                       {"source", source.origin},
                       {"source_sha256", source.sha256}};
  if (!source.path.empty()) fields.emplace("source_path", source.path);
  return fields;
}

Result<command::Response> translate(const command::Arguments& arguments,
                                    const std::shared_ptr<application::Workspace>& workspace) {
  const auto source = load_source(arguments);
  if (!source.has_value()) return Result<command::Response>::failure(source.error());
  const auto script = pml::parse(source.value().text);
  const auto translation = pml::translate_script(script, live_context(workspace));
  Value::Array statements;
  for (const auto& statement : translation.statements) {
    statements.push_back(statement_value(statement));
  }
  auto fields = source_fields(source.value());
  fields.emplace("blank_lines", static_cast<std::uint64_t>(script.blank_lines));
  fields.emplace("comment_lines", static_cast<std::uint64_t>(script.comment_lines));
  fields.emplace("failed_statements", static_cast<std::uint64_t>(translation.failed_statements));
  fields.emplace("invocation_count", static_cast<std::uint64_t>(translation.invocation_count));
  fields.emplace("parse_diagnostics", diagnostics_value(translation.parse_diagnostics));
  fields.emplace("statement_count", static_cast<std::uint64_t>(translation.statements.size()));
  fields.emplace("statements", std::move(statements));
  fields.emplace("stopped", translation.stopped);
  fields.emplace("translated_statements",
                 static_cast<std::uint64_t>(translation.translated_statements));
  return Result<command::Response>::success(
      {"translated " + std::to_string(translation.translated_statements) + " of " +
           std::to_string(translation.statements.size()) + " PML statements into " +
           std::to_string(translation.invocation_count) + " operations",
       std::move(fields)});
}

Result<command::Response> run(const command::Registry& registry,
                              const command::Arguments& arguments,
                              const std::shared_ptr<application::Workspace>& workspace,
                              operation::TaskContext& context) {
  const auto source = load_source(arguments);
  if (!source.has_value()) return Result<command::Response>::failure(source.error());
  const bool stop_on_error = arguments.at("on-error") == "stop";
  const auto script = pml::parse(source.value().text);
  pml::Translator translator{live_context(workspace)};
  const application::Dispatcher dispatcher{registry};

  Value::Array results;
  std::uint64_t executed_statements = 0, executed_invocations = 0, failed_statements = 0,
                mutations = 0;
  bool stopped = false;
  std::optional<Error> first_failure;
  std::size_t failure_line = 0;
  const auto descriptors = registry.descriptors();
  const auto is_undoable = [&descriptors](const std::string& name) {
    for (const auto& descriptor : descriptors) {
      if (descriptor.canonical_name == name) {
        return descriptor.undo_policy == command::UndoPolicy::undoable;
      }
    }
    return false;
  };

  for (const auto& statement : script.statements) {
    if (context.cancellation.is_cancelled()) {
      return Result<command::Response>::failure(
          {ErrorCode::cancelled, "PML run was cancelled between statements",
           "mutations already committed stay in the Workspace",
           {{"executed_invocations", std::to_string(executed_invocations)},
            {"mutations_committed", std::to_string(mutations)}}});
    }
    const auto translated = translator.translate(statement);
    Value::Array invocation_results;
    bool failed = translated.has_error();
    std::string failure_message;
    if (failed) {
      for (const auto& diagnostic : translated.diagnostics) {
        if (diagnostic.severity == "error") {
          failure_message = diagnostic.code + ": " + diagnostic.message;
          break;
        }
      }
    } else {
      for (const auto& invocation : translated.invocations) {
        const auto outcome = dispatcher.dispatch(
            command::Invocation{invocation.command, invocation.arguments}, context);
        ++executed_invocations;
        Value::Object entry{{"command", invocation.command}};
        if (outcome.succeeded()) {
          entry.emplace("status", "ok");
          if (is_undoable(invocation.command)) ++mutations;
        } else {
          const auto& error = std::get<Error>(outcome.envelope.payload);
          entry.emplace("status", "failed");
          entry.emplace("error", error.message);
          failed = true;
          failure_message = invocation.command + ": " + error.message;
        }
        invocation_results.push_back(std::move(entry));
        if (failed) break;
      }
    }
    ++executed_statements;
    results.push_back(Value::Object{
        {"diagnostics", diagnostics_value(translated.diagnostics)},
        {"invocations", std::move(invocation_results)},
        {"line", static_cast<std::uint64_t>(statement.line)},
        {"status", failed ? "failed" : (translated.invocations.empty() ? "no_effect" : "ok")},
        {"text", statement.text}});
    if (failed) {
      ++failed_statements;
      if (!first_failure.has_value()) {
        first_failure = Error{ErrorCode::script_failed,
                              "PML statement at line " + std::to_string(statement.line) +
                                  " failed: " + failure_message,
                              "fix the statement or run with --on-error continue", {}};
        failure_line = statement.line;
      }
      if (stop_on_error) break;
    }
    if (translated.stop_script) {
      stopped = true;
      break;
    }
  }
  if (first_failure.has_value() && stop_on_error) {
    first_failure->details = {{"executed_invocations", std::to_string(executed_invocations)},
                              {"failed_line", std::to_string(failure_line)},
                              {"mutations_committed", std::to_string(mutations)}};
    return Result<command::Response>::failure(std::move(*first_failure));
  }
  auto fields = source_fields(source.value());
  fields.emplace("executed_invocations", executed_invocations);
  fields.emplace("executed_statements", executed_statements);
  fields.emplace("failed_statements", failed_statements);
  fields.emplace("mutations_committed", mutations);
  fields.emplace("on_error", arguments.at("on-error"));
  fields.emplace("parse_diagnostics", diagnostics_value(script.diagnostics));
  fields.emplace("results", std::move(results));
  fields.emplace("stopped", stopped || script.aborted);
  return Result<command::Response>::success(
      {"ran " + std::to_string(executed_statements) + " PML statements (" +
           std::to_string(failed_statements) + " failed)",
       std::move(fields)});
}


// --- VMD Tcl -------------------------------------------------------------

vmd::TranslationContext live_tcl_context(
    const std::shared_ptr<application::Workspace>& workspace) {
  vmd::TranslationContext context;
  if (workspace == nullptr) return context;
  context.is_object = [workspace](std::string_view name) {
    for (const auto& object : workspace->list_objects()) {
      if (object.name == name) return true;
    }
    return false;
  };
  return context;
}

Value tcl_command_value(const vmd::TranslatedCommand& command) {
  Value::Array invocations;
  for (const auto& invocation : command.invocations) {
    invocations.push_back(invocation_value(invocation));
  }
  return Value::Object{{"command", command.command},
                       {"diagnostics", diagnostics_value(command.diagnostics)},
                       {"invocations", std::move(invocations)},
                       {"line", static_cast<std::uint64_t>(command.line)},
                       {"stop", command.stop_script || command.tcl_error},
                       {"tcl_error", command.tcl_error},
                       {"text", command.text}};
}

Result<command::Response> translate_tcl(
    const command::Arguments& arguments,
    const std::shared_ptr<application::Workspace>& workspace) {
  const auto source = load_source(arguments);
  if (!source.has_value()) return Result<command::Response>::failure(source.error());
  const auto translation =
      vmd::translate_script(source.value().text, live_tcl_context(workspace));
  Value::Array commands;
  for (const auto& translated : translation.commands) {
    commands.push_back(tcl_command_value(translated));
  }
  auto fields = source_fields(source.value(), "tcl");
  fields.emplace("command_count", static_cast<std::uint64_t>(translation.commands.size()));
  fields.emplace("commands", std::move(commands));
  fields.emplace("comment_lines", static_cast<std::uint64_t>(translation.comment_lines));
  fields.emplace("failed_commands", static_cast<std::uint64_t>(translation.failed_commands));
  fields.emplace("invocation_count", static_cast<std::uint64_t>(translation.invocation_count));
  fields.emplace("stopped", translation.stopped);
  fields.emplace("translated_commands",
                 static_cast<std::uint64_t>(translation.translated_commands));
  return Result<command::Response>::success(
      {"translated " + std::to_string(translation.translated_commands) + " of " +
           std::to_string(translation.commands.size()) + " Tcl commands into " +
           std::to_string(translation.invocation_count) + " operations",
       std::move(fields)});
}

Result<command::Response> run_tcl(const command::Registry& registry,
                                  const command::Arguments& arguments,
                                  const std::shared_ptr<application::Workspace>& workspace,
                                  operation::TaskContext& context) {
  const auto source = load_source(arguments);
  if (!source.has_value()) return Result<command::Response>::failure(source.error());
  const bool stop_on_error = arguments.at("on-error") == "stop";
  const auto script = tcl::parse(source.value().text);
  vmd::Translator translator{live_tcl_context(workspace)};
  const application::Dispatcher dispatcher{registry};
  const auto descriptors = registry.descriptors();
  const auto is_undoable = [&descriptors](const std::string& name) {
    for (const auto& descriptor : descriptors) {
      if (descriptor.canonical_name == name) {
        return descriptor.undo_policy == command::UndoPolicy::undoable;
      }
    }
    return false;
  };

  Value::Array results;
  std::uint64_t executed_commands = 0, executed_invocations = 0, failed_commands = 0, mutations = 0;
  bool stopped = false;
  std::optional<Error> first_failure;
  std::size_t failure_line = 0;
  const auto run_one = [&](const vmd::TranslatedCommand& translated) -> bool {
    Value::Array invocation_results;
    bool failed = translated.has_error();
    std::string failure_message;
    if (failed) {
      for (const auto& diagnostic : translated.diagnostics) {
        if (diagnostic.severity == "error") {
          failure_message = diagnostic.code + ": " + diagnostic.message;
          break;
        }
      }
    } else {
      for (const auto& invocation : translated.invocations) {
        const auto outcome = dispatcher.dispatch(
            command::Invocation{invocation.command, invocation.arguments}, context);
        ++executed_invocations;
        Value::Object entry{{"command", invocation.command}};
        if (outcome.succeeded()) {
          entry.emplace("status", "ok");
          if (is_undoable(invocation.command)) ++mutations;
        } else {
          const auto& error = std::get<Error>(outcome.envelope.payload);
          entry.emplace("status", "failed");
          entry.emplace("error", error.message);
          failed = true;
          failure_message = invocation.command + ": " + error.message;
        }
        invocation_results.push_back(std::move(entry));
        if (failed) break;
      }
    }
    ++executed_commands;
    results.push_back(Value::Object{
        {"diagnostics", diagnostics_value(translated.diagnostics)},
        {"invocations", std::move(invocation_results)},
        {"line", static_cast<std::uint64_t>(translated.line)},
        {"status", failed ? "failed" : (translated.invocations.empty() ? "no_effect" : "ok")},
        {"text", translated.text}});
    if (failed) {
      ++failed_commands;
      if (!first_failure.has_value()) {
        first_failure = Error{ErrorCode::script_failed,
                              "Tcl command at line " + std::to_string(translated.line) +
                                  " failed: " + failure_message,
                              "fix the command or run with --on-error continue", {}};
        failure_line = translated.line;
      }
    }
    return failed;
  };
  const auto cancelled_error = [&] {
    return Result<command::Response>::failure(
        {ErrorCode::cancelled, "Tcl run was cancelled between commands",
         "mutations already committed stay in the Workspace",
         {{"executed_invocations", std::to_string(executed_invocations)},
          {"mutations_committed", std::to_string(mutations)}}});
  };
  for (const auto& command : script.commands) {
    if (context.cancellation.is_cancelled()) return cancelled_error();
    const auto translated = translator.translate(command);
    const bool failed = run_one(translated);
    if (translated.stop_script) {
      stopped = true;
      break;
    }
    // A Tcl error aborts a Tcl script whatever the policy says.
    if (failed && (stop_on_error || translated.tcl_error)) {
      stopped = translated.tcl_error;
      break;
    }
  }
  if (!stopped && script.error.has_value()) {
    if (context.cancellation.is_cancelled()) return cancelled_error();
    run_one(vmd::Translator::syntax_failure(*script.error));
    stopped = true;
  }
  if (first_failure.has_value() && (stop_on_error || stopped)) {
    first_failure->details = {{"executed_invocations", std::to_string(executed_invocations)},
                              {"failed_line", std::to_string(failure_line)},
                              {"mutations_committed", std::to_string(mutations)}};
    return Result<command::Response>::failure(std::move(*first_failure));
  }
  auto fields = source_fields(source.value(), "tcl");
  fields.emplace("executed_commands", executed_commands);
  fields.emplace("executed_invocations", executed_invocations);
  fields.emplace("failed_commands", failed_commands);
  fields.emplace("mutations_committed", mutations);
  fields.emplace("on_error", arguments.at("on-error"));
  fields.emplace("results", std::move(results));
  fields.emplace("stopped", stopped);
  return Result<command::Response>::success(
      {"ran " + std::to_string(executed_commands) + " Tcl commands (" +
           std::to_string(failed_commands) + " failed)",
       std::move(fields)});
}

}  // namespace

std::optional<operation::Error> register_pml_commands(
    command::Registry& registry, std::shared_ptr<application::Workspace> workspace) {
  const command::Descriptor translate_descriptor{
      "pml translate",
      "Translate a PyMOL command script into canonical MolShredder operations",
      {command::ParameterSpec{"path", command::ParameterType::text, false},
       command::ParameterSpec{"text", command::ParameterType::text, false}},
      command::UndoPolicy::not_applicable};
  if (const auto error = registry.add(
          translate_descriptor,
          [workspace](const command::Arguments& arguments, operation::TaskContext&) {
            return translate(arguments, workspace);
          });
      error.has_value()) {
    return error;
  }
  const command::Descriptor run_descriptor{
      "pml run",
      "Run a PyMOL command script as canonical MolShredder operations",
      {command::ParameterSpec{"path", command::ParameterType::text, false},
       command::ParameterSpec{"text", command::ParameterType::text, false},
       command::ParameterSpec{"on-error", command::ParameterType::text, false, "stop",
                              {"stop", "continue"}}},
      command::UndoPolicy::not_undoable};
  if (const auto error = registry.add(
          run_descriptor,
          [&registry, workspace](const command::Arguments& arguments,
                                 operation::TaskContext& context) {
            return run(registry, arguments, workspace, context);
          });
      error.has_value()) {
    return error;
  }
  const command::Descriptor tcl_translate_descriptor{
      "tcl translate",
      "Translate a VMD Tcl script subset into canonical MolShredder operations",
      {command::ParameterSpec{"path", command::ParameterType::text, false},
       command::ParameterSpec{"text", command::ParameterType::text, false}},
      command::UndoPolicy::not_applicable};
  if (const auto error = registry.add(
          tcl_translate_descriptor,
          [workspace](const command::Arguments& arguments, operation::TaskContext&) {
            return translate_tcl(arguments, workspace);
          });
      error.has_value()) {
    return error;
  }
  const command::Descriptor tcl_run_descriptor{
      "tcl run",
      "Run a VMD Tcl script subset as canonical MolShredder operations",
      {command::ParameterSpec{"path", command::ParameterType::text, false},
       command::ParameterSpec{"text", command::ParameterType::text, false},
       command::ParameterSpec{"on-error", command::ParameterType::text, false, "stop",
                              {"stop", "continue"}}},
      command::UndoPolicy::not_undoable};
  return registry.add(
      tcl_run_descriptor,
      [&registry, workspace](const command::Arguments& arguments,
                             operation::TaskContext& context) {
        return run_tcl(registry, arguments, workspace, context);
      });
}

}  // namespace molshredder::automation
