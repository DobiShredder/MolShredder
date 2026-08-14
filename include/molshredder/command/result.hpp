#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "molshredder/operation/common_types.hpp"
#include "molshredder/operation/error.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::command {

inline constexpr unsigned int kResultSchemaVersion = 1;

struct Number {
  double value{};
  unsigned int decimal_places{6U};

  friend bool operator==(const Number&, const Number&) = default;
};

struct Value {
  using Array = std::vector<Value>;
  using Object = std::map<std::string, Value, std::less<>>;
  using Storage = std::variant<std::nullptr_t, bool, std::int64_t,
                               std::uint64_t, double, Number, std::string,
                               Array, Object>;

  Value() : data{nullptr} {}
  Value(std::nullptr_t) : data{nullptr} {}
  Value(bool value) : data{value} {}
  template <std::signed_integral Integer>
    requires(!std::same_as<Integer, bool>)
  Value(Integer value) : data{static_cast<std::int64_t>(value)} {}
  template <std::unsigned_integral Integer>
    requires(!std::same_as<Integer, bool>)
  Value(Integer value) : data{static_cast<std::uint64_t>(value)} {}
  Value(double value) : data{value} {}
  Value(Number value) : data{value} {}
  Value(const char* value) : data{std::string{value}} {}
  Value(std::string value) : data{std::move(value)} {}
  Value(Array value) : data{std::move(value)} {}
  Value(Object value) : data{std::move(value)} {}

  Storage data;

  friend bool operator==(const Value&, const Value&) = default;
};

struct Table {
  std::vector<std::string> columns;
  std::vector<std::vector<Value>> rows;
};

struct Response {
  Response() = default;
  Response(std::string response_summary, Value::Object response_fields,
           std::optional<Table> response_table = std::nullopt)
      : summary{std::move(response_summary)},
        fields{std::move(response_fields)},
        table{std::move(response_table)} {}

  std::string summary;
  Value::Object fields;
  std::optional<Table> table;
};

struct ResultEnvelope {
  unsigned int schema_version{kResultSchemaVersion};
  std::string canonical_command;
  std::variant<Response, operation::Error> payload;

  [[nodiscard]] bool succeeded() const noexcept {
    return std::holds_alternative<Response>(payload);
  }
};

[[nodiscard]] ResultEnvelope make_envelope(
    std::string canonical_command,
    const operation::Result<Response>& operation_result);

[[nodiscard]] operation::Result<operation::OutputFormat> parse_output_format(
    std::string_view name);

[[nodiscard]] operation::Result<std::string> render(
    const ResultEnvelope& envelope, operation::OutputFormat format);

}  // namespace molshredder::command
