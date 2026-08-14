#include "molshredder/command/result.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>

namespace molshredder::command {
namespace {

using operation::Error;
using operation::ErrorCode;
using operation::OutputFormat;

bool valid_utf8(std::string_view text) {
  std::size_t index = 0;
  const auto byte = [&text](std::size_t position) {
    return static_cast<unsigned char>(text[position]);
  };
  const auto continuation = [&byte](std::size_t position) {
    return (byte(position) & 0xc0U) == 0x80U;
  };
  while (index < text.size()) {
    const auto first = byte(index);
    if (first <= 0x7fU) {
      ++index;
      continue;
    }
    if (first >= 0xc2U && first <= 0xdfU) {
      if (index + 1 >= text.size() || !continuation(index + 1)) {
        return false;
      }
      index += 2;
      continue;
    }
    if (first >= 0xe0U && first <= 0xefU) {
      if (index + 2 >= text.size() || !continuation(index + 1) ||
          !continuation(index + 2)) {
        return false;
      }
      const auto second = byte(index + 1);
      if ((first == 0xe0U && second < 0xa0U) ||
          (first == 0xedU && second > 0x9fU)) {
        return false;
      }
      index += 3;
      continue;
    }
    if (first >= 0xf0U && first <= 0xf4U) {
      if (index + 3 >= text.size() || !continuation(index + 1) ||
          !continuation(index + 2) || !continuation(index + 3)) {
        return false;
      }
      const auto second = byte(index + 1);
      if ((first == 0xf0U && second < 0x90U) ||
          (first == 0xf4U && second > 0x8fU)) {
        return false;
      }
      index += 4;
      continue;
    }
    return false;
  }
  return true;
}

std::optional<Error> validate_json_value(const Value& value);

bool scalar_value(const Value& value) {
  return !std::holds_alternative<Value::Array>(value.data) &&
         !std::holds_alternative<Value::Object>(value.data);
}

operation::Result<std::string> formatted_number(Number number) {
  if (!std::isfinite(number.value) || number.decimal_places > 15U) {
    return operation::Result<std::string>::failure(
        Error{ErrorCode::invalid_argument,
              "formatted result number is non-finite or has invalid precision",
              "use a finite value and 0..15 decimal places"});
  }
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::fixed
         << std::setprecision(static_cast<int>(number.decimal_places))
         << number.value;
  auto result = stream.str();
  if (result.find('.') != std::string::npos) {
    while (!result.empty() && result.back() == '0') result.pop_back();
    if (!result.empty() && result.back() == '.') result.pop_back();
  }
  if (result == "-0") result = "0";
  return operation::Result<std::string>::success(std::move(result));
}

Error invalid_utf8_error() {
  return Error{ErrorCode::invalid_argument,
               "JSON result contains invalid UTF-8",
               "return valid UTF-8 text values and field names"};
}

std::optional<Error> validate_json_object(const Value::Object& object) {
  for (const auto& [name, value] : object) {
    if (!valid_utf8(name)) {
      return invalid_utf8_error();
    }
    if (const auto failure = validate_json_value(value); failure.has_value()) {
      return failure;
    }
  }
  return std::nullopt;
}

std::optional<Error> validate_json_value(const Value& value) {
  return std::visit(
      [](const auto& item) -> std::optional<Error> {
        using Item = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Item, std::string>) {
          if (!valid_utf8(item)) {
            return invalid_utf8_error();
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<Item, Value::Array>) {
          for (const auto& nested : item) {
            if (const auto failure = validate_json_value(nested);
                failure.has_value()) {
              return failure;
            }
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<Item, Value::Object>) {
          return validate_json_object(item);
        } else {
          return std::nullopt;
        }
      },
      value.data);
}

std::string json_string(std::string_view value) {
  constexpr std::array<char, 16> hex{
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string result{"\""};
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    switch (character) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\b':
        result += "\\b";
        break;
      case '\f':
        result += "\\f";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (character < 0x20U) {
          result += "\\u00";
          result += hex[character >> 4U];
          result += hex[character & 0x0fU];
        } else {
          result += static_cast<char>(character);
        }
        break;
    }
  }
  result += '"';
  return result;
}

operation::Result<std::string> json_value(const Value& value);

operation::Result<std::string> json_array(const Value::Array& values) {
  std::string result{"["};
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto rendered = json_value(values[index]);
    if (!rendered.has_value()) {
      return operation::Result<std::string>::failure(rendered.error());
    }
    if (index != 0) {
      result += ',';
    }
    result += rendered.value();
  }
  result += ']';
  return operation::Result<std::string>::success(std::move(result));
}

operation::Result<std::string> json_object(const Value::Object& values) {
  std::string result{"{"};
  std::size_t index = 0;
  for (const auto& [name, value] : values) {
    const auto rendered = json_value(value);
    if (!rendered.has_value()) {
      return operation::Result<std::string>::failure(rendered.error());
    }
    if (index++ != 0) {
      result += ',';
    }
    result += json_string(name) + ':' + rendered.value();
  }
  result += '}';
  return operation::Result<std::string>::success(std::move(result));
}

operation::Result<std::string> json_value(const Value& value) {
  return std::visit(
      [](const auto& item) -> operation::Result<std::string> {
        using Item = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Item, std::nullptr_t>) {
          return operation::Result<std::string>::success("null");
        } else if constexpr (std::is_same_v<Item, bool>) {
          return operation::Result<std::string>::success(item ? "true"
                                                               : "false");
        } else if constexpr (std::is_same_v<Item, std::int64_t> ||
                             std::is_same_v<Item, std::uint64_t>) {
          return operation::Result<std::string>::success(
              std::to_string(item));
        } else if constexpr (std::is_same_v<Item, double>) {
          if (!std::isfinite(item)) {
            return operation::Result<std::string>::failure(
                Error{ErrorCode::invalid_argument,
                      "JSON result contains a non-finite number",
                      "return finite values or encode missing data as null"});
          }
          std::array<char, 64> buffer{};
          const auto converted = std::to_chars(
              buffer.data(), buffer.data() + buffer.size(), item,
              std::chars_format::general,
              std::numeric_limits<double>::max_digits10);
          if (converted.ec != std::errc{}) {
            return operation::Result<std::string>::failure(
                Error{ErrorCode::internal, "failed to format JSON number",
                      {}});
          }
          return operation::Result<std::string>::success(
              std::string{buffer.data(), converted.ptr});
        } else if constexpr (std::is_same_v<Item, Number>) {
          return formatted_number(item);
        } else if constexpr (std::is_same_v<Item, std::string>) {
          return operation::Result<std::string>::success(json_string(item));
        } else if constexpr (std::is_same_v<Item, Value::Array>) {
          return json_array(item);
        } else {
          return json_object(item);
        }
      },
      value.data);
}

std::string text_value(const Value& value) {
  return std::visit(
      [](const auto& item) -> std::string {
        using Item = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Item, std::nullptr_t>) {
          return "null";
        } else if constexpr (std::is_same_v<Item, bool>) {
          return item ? "true" : "false";
        } else if constexpr (std::is_same_v<Item, std::int64_t> ||
                             std::is_same_v<Item, std::uint64_t>) {
          return std::to_string(item);
        } else if constexpr (std::is_same_v<Item, double>) {
          std::ostringstream stream;
          stream.imbue(std::locale::classic());
          stream << std::setprecision(std::numeric_limits<double>::max_digits10)
                 << item;
          return stream.str();
        } else if constexpr (std::is_same_v<Item, Number>) {
          const auto rendered = formatted_number(item);
          return rendered.has_value() ? rendered.value() : "invalid-number";
        } else if constexpr (std::is_same_v<Item, std::string>) {
          return item;
        } else if constexpr (std::is_same_v<Item, Value::Array>) {
          std::string result{"["};
          for (std::size_t index = 0; index < item.size(); ++index) {
            if (index != 0) {
              result += ", ";
            }
            result += text_value(item[index]);
          }
          return result + ']';
        } else {
          std::string result{"{"};
          std::size_t index = 0;
          for (const auto& [name, nested] : item) {
            if (index++ != 0) {
              result += ", ";
            }
            result += name + ": " + text_value(nested);
          }
          return result + '}';
        }
      },
      value.data);
}

std::optional<Error> validate_table(const Table& table) {
  if (table.columns.empty()) {
    return Error{ErrorCode::invalid_argument,
                 "result table must contain at least one column", {}};
  }
  std::map<std::string, bool, std::less<>> names;
  for (const auto& column : table.columns) {
    if (column.empty() || !valid_utf8(column)) {
      return Error{ErrorCode::invalid_argument,
                   "result table contains an empty or invalid UTF-8 column",
                   {}};
    }
    if (!names.emplace(column, true).second) {
      return Error{ErrorCode::invalid_argument,
                   "result table contains duplicate column: " + column, {}};
    }
  }
  for (const auto& row : table.rows) {
    if (row.size() != table.columns.size()) {
      return Error{ErrorCode::invalid_argument,
                   "result table row width does not match its columns", {}};
    }
    for (const auto& cell : row) {
      if (!scalar_value(cell)) {
        return Error{ErrorCode::invalid_argument,
                     "result table cells must be scalar values", {}};
      }
      if (const auto failure = validate_json_value(cell); failure.has_value()) {
        return failure;
      }
      if (const auto* number = std::get_if<double>(&cell.data);
          number != nullptr && !std::isfinite(*number)) {
        return Error{ErrorCode::invalid_argument,
                     "result table contains a non-finite number", {}};
      }
      if (const auto* number = std::get_if<Number>(&cell.data);
          number != nullptr &&
          (!std::isfinite(number->value) || number->decimal_places > 15U)) {
        return Error{ErrorCode::invalid_argument,
                     "result table contains an invalid formatted number", {}};
      }
    }
  }
  return std::nullopt;
}

Value table_value(const Table& table) {
  Value::Array columns;
  columns.reserve(table.columns.size());
  for (const auto& column : table.columns) columns.emplace_back(column);
  Value::Array rows;
  rows.reserve(table.rows.size());
  for (const auto& row : table.rows) {
    Value::Array values = row;
    rows.emplace_back(std::move(values));
  }
  return Value::Object{{"columns", std::move(columns)},
                       {"rows", std::move(rows)}};
}

operation::Result<Value::Object> response_data(const Response& response) {
  if (!response.table.has_value()) {
    return operation::Result<Value::Object>::success(response.fields);
  }
  if (response.fields.contains("table")) {
    return operation::Result<Value::Object>::failure(
        Error{ErrorCode::invalid_argument,
              "response field 'table' conflicts with the typed result table",
              {}});
  }
  if (const auto failure = validate_table(*response.table);
      failure.has_value()) {
    return operation::Result<Value::Object>::failure(*failure);
  }
  auto data = response.fields;
  data.emplace("table", table_value(*response.table));
  return operation::Result<Value::Object>::success(std::move(data));
}

std::string csv_escape(std::string value) {
  if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
  std::string escaped{"\""};
  for (const auto character : value) {
    if (character == '"') escaped += '"';
    escaped += character;
  }
  escaped += '"';
  return escaped;
}

operation::Result<std::string> render_csv(const ResultEnvelope& envelope) {
  if (!envelope.succeeded()) {
    const auto& failure = std::get<Error>(envelope.payload);
    if (!valid_utf8(failure.message) || !valid_utf8(failure.suggestion)) {
      return operation::Result<std::string>::failure(invalid_utf8_error());
    }
    return operation::Result<std::string>::success(
        "status,error_code,message,suggestion\r\nerror," +
        csv_escape(std::string{operation::to_string(failure.code)}) + ',' +
        csv_escape(failure.message) + ',' +
        csv_escape(failure.suggestion) + "\r\n");
  }
  const auto& response = std::get<Response>(envelope.payload);
  if (!response.table.has_value()) {
    return operation::Result<std::string>::failure(
        Error{ErrorCode::unsupported,
              "command result does not contain a table",
              "use text or json for scalar command results"});
  }
  if (const auto failure = validate_table(*response.table);
      failure.has_value()) {
    return operation::Result<std::string>::failure(*failure);
  }
  std::string result;
  for (std::size_t column = 0; column < response.table->columns.size();
       ++column) {
    if (column != 0U) result += ',';
    result += csv_escape(response.table->columns[column]);
  }
  result += "\r\n";
  for (const auto& row : response.table->rows) {
    for (std::size_t column = 0; column < row.size(); ++column) {
      if (column != 0U) result += ',';
      if (!std::holds_alternative<std::nullptr_t>(row[column].data)) {
        result += csv_escape(text_value(row[column]));
      }
    }
    result += "\r\n";
  }
  return operation::Result<std::string>::success(std::move(result));
}

operation::Result<std::string> render_text(const ResultEnvelope& envelope) {
  std::string result;
  if (envelope.succeeded()) {
    const auto& response = std::get<Response>(envelope.payload);
    if (!response.summary.empty()) {
      result += response.summary + '\n';
    }
    for (const auto& [name, value] : response.fields) {
      result += name + '=' + text_value(value) + '\n';
    }
    if (response.table.has_value()) {
      if (const auto failure = validate_table(*response.table);
          failure.has_value()) {
        return operation::Result<std::string>::failure(*failure);
      }
      for (std::size_t column = 0;
           column < response.table->columns.size(); ++column) {
        if (column != 0U) result += '\t';
        result += response.table->columns[column];
      }
      result += '\n';
      for (const auto& row : response.table->rows) {
        for (std::size_t column = 0; column < row.size(); ++column) {
          if (column != 0U) result += '\t';
          result += text_value(row[column]);
        }
        result += '\n';
      }
    }
  } else {
    const auto& failure = std::get<Error>(envelope.payload);
    result += "error[" + std::string{operation::to_string(failure.code)} +
              "]: " + failure.message + '\n';
    if (!failure.suggestion.empty()) {
      result += "hint: " + failure.suggestion + '\n';
    }
  }
  return operation::Result<std::string>::success(std::move(result));
}

operation::Result<std::string> render_json(const ResultEnvelope& envelope) {
  if (!valid_utf8(envelope.canonical_command)) {
    return operation::Result<std::string>::failure(invalid_utf8_error());
  }
  if (envelope.succeeded()) {
    const auto& response = std::get<Response>(envelope.payload);
    if (!valid_utf8(response.summary)) {
      return operation::Result<std::string>::failure(invalid_utf8_error());
    }
    const auto data = response_data(response);
    if (!data.has_value()) {
      return operation::Result<std::string>::failure(data.error());
    }
    if (const auto failure = validate_json_object(data.value());
        failure.has_value()) {
      return operation::Result<std::string>::failure(*failure);
    }
  } else {
    const auto& failure = std::get<Error>(envelope.payload);
    if (!valid_utf8(failure.message) || !valid_utf8(failure.suggestion)) {
      return operation::Result<std::string>::failure(invalid_utf8_error());
    }
  }
  std::string result =
      "{\"schema_version\":" + std::to_string(envelope.schema_version) +
      ",\"status\":" + json_string(envelope.succeeded() ? "ok" : "error") +
      ",\"command\":" + json_string(envelope.canonical_command);
  if (envelope.succeeded()) {
    const auto& response = std::get<Response>(envelope.payload);
    const auto typed_data = response_data(response);
    if (!typed_data.has_value()) {
      return operation::Result<std::string>::failure(typed_data.error());
    }
    const auto data = json_object(typed_data.value());
    if (!data.has_value()) {
      return operation::Result<std::string>::failure(data.error());
    }
    result += ",\"summary\":" + json_string(response.summary) +
              ",\"data\":" + data.value();
  } else {
    const auto& failure = std::get<Error>(envelope.payload);
    result += ",\"error\":{\"code\":" +
              json_string(operation::to_string(failure.code)) +
              ",\"message\":" + json_string(failure.message) +
              ",\"suggestion\":" + json_string(failure.suggestion) + '}';
  }
  result += "}\n";
  return operation::Result<std::string>::success(std::move(result));
}

}  // namespace

ResultEnvelope make_envelope(
    std::string canonical_command,
    const operation::Result<Response>& operation_result) {
  if (operation_result.has_value()) {
    return ResultEnvelope{kResultSchemaVersion, std::move(canonical_command),
                          operation_result.value()};
  }
  return ResultEnvelope{kResultSchemaVersion, std::move(canonical_command),
                        operation_result.error()};
}

operation::Result<OutputFormat> parse_output_format(std::string_view name) {
  if (name == "text") {
    return operation::Result<OutputFormat>::success(OutputFormat::text);
  }
  if (name == "json") {
    return operation::Result<OutputFormat>::success(OutputFormat::json);
  }
  if (name == "csv") {
    return operation::Result<OutputFormat>::success(OutputFormat::csv);
  }
  return operation::Result<OutputFormat>::failure(
      Error{ErrorCode::unsupported,
            "unsupported result format: " + std::string{name},
            "choose text, json, or csv"});
}

operation::Result<std::string> render(const ResultEnvelope& envelope,
                                      OutputFormat format) {
  switch (format) {
    case OutputFormat::text:
      return render_text(envelope);
    case OutputFormat::json:
      return render_json(envelope);
    case OutputFormat::csv:
      return render_csv(envelope);
  }
  return operation::Result<std::string>::failure(
      Error{ErrorCode::internal, "unknown result format", {}});
}

}  // namespace molshredder::command
