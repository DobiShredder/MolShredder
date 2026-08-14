#include "molshredder/command/serialization.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "molshredder/operation/error.hpp"

namespace molshredder::command {
namespace {

using operation::Error;
using operation::ErrorCode;

std::string quote(std::string_view text) {
  std::string result{"\""};
  for (const char character : text) {
    switch (character) {
      case '\\':
        result += "\\\\";
        break;
      case '"':
        result += "\\\"";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        result += character;
        break;
    }
  }
  result += '"';
  return result;
}

operation::Result<std::vector<std::string>> tokenize(std::string_view input) {
  std::vector<std::string> tokens;
  std::size_t position = 0;

  while (position < input.size()) {
    while (position < input.size() &&
           std::isspace(static_cast<unsigned char>(input[position])) != 0) {
      ++position;
    }
    if (position == input.size()) {
      break;
    }

    std::string token;
    if (input[position] == '"') {
      ++position;
      bool closed = false;
      while (position < input.size()) {
        const char character = input[position++];
        if (character == '"') {
          closed = true;
          break;
        }
        if (character != '\\') {
          token += character;
          continue;
        }
        if (position == input.size()) {
          return operation::Result<std::vector<std::string>>::failure(
              Error{ErrorCode::invalid_argument,
                    "canonical command ends with an escape", {}});
        }
        const char escaped = input[position++];
        switch (escaped) {
          case '\\':
            token += '\\';
            break;
          case '"':
            token += '"';
            break;
          case 'n':
            token += '\n';
            break;
          case 't':
            token += '\t';
            break;
          default:
            return operation::Result<std::vector<std::string>>::failure(
                Error{ErrorCode::invalid_argument,
                      "unsupported canonical escape sequence", {}});
        }
      }
      if (!closed) {
        return operation::Result<std::vector<std::string>>::failure(
            Error{ErrorCode::invalid_argument,
                  "canonical command has an unterminated quote", {}});
      }
      if (position < input.size() &&
          std::isspace(static_cast<unsigned char>(input[position])) == 0) {
        return operation::Result<std::vector<std::string>>::failure(
            Error{ErrorCode::invalid_argument,
                  "quoted canonical token must end at whitespace", {}});
      }
    } else {
      const auto begin = position;
      while (position < input.size() &&
             std::isspace(static_cast<unsigned char>(input[position])) == 0) {
        ++position;
      }
      token = std::string{input.substr(begin, position - begin)};
    }
    tokens.push_back(std::move(token));
  }

  return operation::Result<std::vector<std::string>>::success(
      std::move(tokens));
}

}  // namespace

std::string serialize(const Invocation& invocation) {
  std::string result = "invoke " + quote(invocation.canonical_name);
  for (const auto& [name, value] : invocation.arguments) {
    result += " --";
    result += name;
    result += ' ';
    result += quote(value);
  }
  return result;
}

operation::Result<Invocation> parse_canonical(std::string_view command) {
  auto token_result = tokenize(command);
  if (!token_result.has_value()) {
    return operation::Result<Invocation>::failure(token_result.error());
  }
  const auto& tokens = token_result.value();
  if (tokens.size() < 2 || tokens.front() != "invoke") {
    return operation::Result<Invocation>::failure(
        Error{ErrorCode::invalid_argument,
              "canonical command must start with invoke and a command name",
              {}});
  }
  if ((tokens.size() - 2) % 2 != 0) {
    return operation::Result<Invocation>::failure(
        Error{ErrorCode::invalid_argument,
              "canonical command parameters require name/value pairs", {}});
  }

  Invocation invocation{tokens[1], {}};
  for (std::size_t index = 2; index < tokens.size(); index += 2) {
    const auto& option = tokens[index];
    if (!option.starts_with("--") || option.size() == 2) {
      return operation::Result<Invocation>::failure(
          Error{ErrorCode::invalid_argument,
                "canonical parameter must start with --", {}});
    }
    const std::string name = option.substr(2);
    if (!invocation.arguments.emplace(name, tokens[index + 1]).second) {
      return operation::Result<Invocation>::failure(
          Error{ErrorCode::invalid_argument,
                "duplicate canonical parameter: " + name, {}});
    }
  }
  return operation::Result<Invocation>::success(std::move(invocation));
}

InvocationRecord make_record(const Invocation& invocation,
                             InvocationSource source,
                             unsigned long long sequence) {
  return InvocationRecord{
      invocation,
      Provenance{kInvocationSchemaVersion, source, sequence,
                 serialize(invocation)}};
}

}  // namespace molshredder::command
