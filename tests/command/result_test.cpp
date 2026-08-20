#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

#include "molshredder/command/result.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::string read_file(const char* path) {
  std::ifstream stream{path};
  return std::string{std::istreambuf_iterator<char>{stream},
                     std::istreambuf_iterator<char>{}};
}

}  // namespace

int main(int argc, char* argv[]) {
  using molshredder::command::Response;
  using molshredder::command::Value;
  using molshredder::operation::Error;
  using molshredder::operation::ErrorCode;
  using molshredder::operation::OutputFormat;
  using molshredder::operation::Result;

  bool passed = true;
  passed &= expect(argc == 3,
                   "result test requires success and error golden paths");
  if (argc != 3) {
    return 1;
  }

  const Response response{
      "center \"calculated\"\nready",
      {{"active", true},
       {"count", std::uint64_t{2}},
       {"label", "chain\tA"},
       {"metadata", Value::Object{{"unit", "angstrom"}}},
       {"position", Value::Array{1.25, -2.0, nullptr}}}};
  const auto success = Result<Response>::success(response);
  const auto success_envelope = molshredder::command::make_envelope(
      "invoke \"analyze center\" --selection \"chain A\"", success);

  passed &= expect(success_envelope.succeeded(),
                   "success result must produce a success envelope");
  passed &= expect(success_envelope.schema_version == 2,
                   "current result schema version must be two");

  const auto text =
      molshredder::command::render(success_envelope, OutputFormat::text);
  passed &= expect(text.has_value(), "typed response must render as text");
  passed &= expect(
      text.has_value() &&
          text.value() ==
              "center \"calculated\"\nready\nactive=true\ncount=2\n"
              "label=chain\tA\nmetadata={unit: angstrom}\n"
              "position=[1.25, -2, null]\n",
      "text rendering must preserve deterministic typed field order");

  const auto json =
      molshredder::command::render(success_envelope, OutputFormat::json);
  passed &= expect(json.has_value(), "typed response must render as JSON");
  passed &= expect(json.has_value() && json.value() == read_file(argv[1]),
                   "success JSON must match the schema golden fixture");

  const Error failure{ErrorCode::invalid_selection,
                      "selection \"missing\"\natom",
                      "use protein\tselection",
                      {{"selection", "missing"},
                       {"stdout", "before failure\n"}}};
  const auto failed = Result<Response>::failure(failure);
  const auto error_envelope = molshredder::command::make_envelope(
      "invoke \"analyze center\" --selection \"missing\"", failed);
  passed &= expect(!error_envelope.succeeded(),
                   "failure result must produce an error envelope");
  const auto error_json =
      molshredder::command::render(error_envelope, OutputFormat::json);
  passed &= expect(error_json.has_value() &&
                       error_json.value() == read_file(argv[2]),
                   "error JSON must match the schema golden fixture");
  const auto error_csv =
      molshredder::command::render(error_envelope, OutputFormat::csv);
  passed &= expect(
      error_csv.has_value() &&
          error_csv.value() ==
              "status,error_code,message,suggestion,details\r\n"
              "error,invalid_selection,\"selection \"\"missing\"\"\natom\","
              "use protein\tselection,\"{\"\"selection\"\":\"\"missing\"\","
              "\"\"stdout\"\":\"\"before failure\\n\"\"}\"\r\n",
      "CSV mode must preserve stable command errors instead of masking them");

  const auto text_format = molshredder::command::parse_output_format("text");
  const auto json_format = molshredder::command::parse_output_format("json");
  const auto csv_format = molshredder::command::parse_output_format("csv");
  const auto bad_format = molshredder::command::parse_output_format("yaml");
  passed &= expect(text_format.has_value() &&
                       text_format.value() == OutputFormat::text &&
                       json_format.has_value() &&
                       json_format.value() == OutputFormat::json &&
                       csv_format.has_value() &&
                       csv_format.value() == OutputFormat::csv,
                   "supported output formats must parse deterministically");
  passed &= expect(!bad_format.has_value() &&
                       bad_format.error().code == ErrorCode::unsupported,
                   "unsupported output format must use a stable error");
  const auto csv =
      molshredder::command::render(success_envelope, OutputFormat::csv);
  passed &= expect(!csv.has_value() &&
                       csv.error().code == ErrorCode::unsupported,
                   "CSV envelope must remain explicitly unsupported");

  const Response table_response{
      "series calculated", {{"selection", "chain A"}},
      molshredder::command::Table{
          {"frame", "label", "value", "missing"},
          {{std::uint64_t{0}, "alpha,beta",
            molshredder::command::Number{1.25, 2U}, nullptr},
           {std::uint64_t{2}, "quote\"line\nnext", -2.0, true},
           {std::uint64_t{3}, "rounded",
            molshredder::command::Number{173.205081, 6U}, false}}}};
  const auto table_envelope = molshredder::command::make_envelope(
      "invoke \"analyze trajectory center\"",
      Result<Response>::success(table_response));
  const auto table_csv =
      molshredder::command::render(table_envelope, OutputFormat::csv);
  passed &= expect(
      table_csv.has_value() &&
          table_csv.value() ==
              "frame,label,value,missing\r\n"
              "0,\"alpha,beta\",1.25,\r\n"
              "2,\"quote\"\"line\nnext\",-2,true\r\n"
              "3,rounded,173.205081,false\r\n",
      "typed tables must render deterministic RFC 4180-compatible CSV");
  const auto table_json =
      molshredder::command::render(table_envelope, OutputFormat::json);
  passed &= expect(
      table_json.has_value() &&
          table_json.value().find(
              "\"table\":{\"columns\":[\"frame\",\"label\",\"value\","
              "\"missing\"],\"rows\":[[0,\"alpha,beta\",1.25,null]") !=
              std::string::npos,
      "JSON data must expose the same typed table columns and rows");

  const Response malformed_table{
      "bad", {}, molshredder::command::Table{{"a", "b"}, {{1U}}}};
  const auto malformed_envelope = molshredder::command::make_envelope(
      "invoke \"test malformed table\"",
      Result<Response>::success(malformed_table));
  const auto malformed_csv =
      molshredder::command::render(malformed_envelope, OutputFormat::csv);
  passed &= expect(!malformed_csv.has_value() &&
                       malformed_csv.error().code ==
                           ErrorCode::invalid_argument,
                   "malformed table widths must never serialize");
  const Response invalid_precision_table{
      "bad precision", {},
      molshredder::command::Table{
          {"value"}, {{molshredder::command::Number{1.0, 16U}}}}};
  const auto invalid_precision_envelope =
      molshredder::command::make_envelope(
          "invoke \"test invalid precision\"",
          Result<Response>::success(invalid_precision_table));
  const auto invalid_precision_csv = molshredder::command::render(
      invalid_precision_envelope, OutputFormat::csv);
  passed &= expect(!invalid_precision_csv.has_value() &&
                       invalid_precision_csv.error().code ==
                           ErrorCode::invalid_argument,
                   "formatted numbers must enforce the public 0..15 precision range");

  const Response non_finite{
      "invalid numeric result",
      {{"value", std::numeric_limits<double>::infinity()}}};
  const auto non_finite_envelope = molshredder::command::make_envelope(
      "invoke \"test non-finite\"", Result<Response>::success(non_finite));
  const auto invalid_json =
      molshredder::command::render(non_finite_envelope, OutputFormat::json);
  passed &= expect(!invalid_json.has_value() &&
                       invalid_json.error().code == ErrorCode::invalid_argument,
                   "non-finite numbers must never produce invalid JSON");

  const std::string invalid_utf8{1, static_cast<char>(0xff)};
  const Response invalid_text{"", {{"value", invalid_utf8}}};
  const auto invalid_text_envelope = molshredder::command::make_envelope(
      "invoke \"test invalid-utf8\"",
      Result<Response>::success(invalid_text));
  const auto invalid_text_json =
      molshredder::command::render(invalid_text_envelope, OutputFormat::json);
  passed &= expect(!invalid_text_json.has_value() &&
                       invalid_text_json.error().code ==
                           ErrorCode::invalid_argument,
                   "invalid UTF-8 must never produce invalid JSON");

  const std::string control_text{"prefix\x01suffix", 13};
  const Response control_response{"", {{"control", control_text}}};
  const auto control_envelope = molshredder::command::make_envelope(
      "invoke \"test control\"",
      Result<Response>::success(control_response));
  const auto control_json =
      molshredder::command::render(control_envelope, OutputFormat::json);
  passed &= expect(control_json.has_value() &&
                       control_json.value().find("prefix\\u0001suffix") !=
                           std::string::npos,
                   "JSON must escape all ASCII control characters");

  return passed ? 0 : 1;
}
#include <cstdint>
