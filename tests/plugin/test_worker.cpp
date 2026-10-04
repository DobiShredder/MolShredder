// A scripted worker for the plugin host tests. Operations select the
// behavior; argv flags select handshake faults and a start marker file.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "molshredder/command/json.hpp"

namespace {

using molshredder::command::Value;
using molshredder::command::parse_json;
using molshredder::command::to_json;

void send(const Value& value) {
  const auto text = to_json(value);
  std::cout << text.value() << '\n' << std::flush;
}

const Value* find(const Value::Object& object, const char* key) {
  const auto found = object.find(key);
  return found == object.end() ? nullptr : &found->second;
}

std::uint64_t as_unsigned(const Value* value, std::uint64_t fallback) {
  if (value == nullptr) return fallback;
  if (const auto* u = std::get_if<std::uint64_t>(&value->data)) return *u;
  if (const auto* i = std::get_if<std::int64_t>(&value->data)) return *i < 0 ? fallback : static_cast<std::uint64_t>(*i);
  return fallback;
}

}  // namespace

int main(int argc, char** argv) {
  std::string mode = "normal";
  std::string marker;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument.rfind("--mode=", 0) == 0) mode = argument.substr(7);
    if (argument.rfind("--marker=", 0) == 0) marker = argument.substr(9);
  }
  if (!marker.empty()) {
    std::ofstream out{marker, std::ios::app};
    out << "started\n";
  }
  std::string line;
  if (!std::getline(std::cin, line)) return 1;  // host hello
  Value::Array operations;
  for (const char* name : {"echo", "sum", "centroid", "fail", "crash", "hang", "flood", "badjson", "wait-cancel",
                           "badid", "table", "environment", "directory", "nested"}) {
    if (mode == "badops" && std::string{name} == "nested") continue;
    operations.emplace_back(name);
  }
  Value::Object plugin{{"id", mode == "badid-hello" ? "org.example.other" : "org.example.worker"},
                       {"version", "1.0.0"}};
  Value::Object protocol{{"major", mode == "badprotocol" ? 2 : 1}, {"minor", 0}};
  if (mode == "nohello") {
    send(Value::Object{{"type", "ready"}});
  } else {
    send(Value::Object{{"operations", operations}, {"plugin", plugin}, {"protocol", protocol}, {"type", "hello"}});
  }
  while (std::getline(std::cin, line)) {
    const auto parsed = parse_json(line);
    if (!parsed.has_value()) continue;
    const auto* message = std::get_if<Value::Object>(&parsed.value().data);
    if (message == nullptr) continue;
    const auto* type = std::get_if<std::string>(&find(*message, "type")->data);
    if (type == nullptr) continue;
    if (*type == "shutdown") return 0;
    if (*type != "invoke") continue;
    const auto id = as_unsigned(find(*message, "id"), 0);
    const auto operation = std::get<std::string>(find(*message, "operation")->data);
    const Value::Object empty;
    const auto* arguments_value = find(*message, "arguments");
    const auto* arguments = arguments_value != nullptr ? std::get_if<Value::Object>(&arguments_value->data) : nullptr;
    const auto& args = arguments != nullptr ? *arguments : empty;
    const Value idv{id};
    if (operation == "echo") {
      std::string text;
      if (const auto* t = find(args, "text")) text = std::get<std::string>(t->data);
      const auto times = as_unsigned(find(args, "times"), 1);
      std::string out;
      for (std::uint64_t i = 0; i < times; ++i) out += text;
      send(Value::Object{{"fields", Value::Object{{"text", out}, {"times", times}}},
                         {"id", idv}, {"summary", "echoed"}, {"type", "result"}});
    } else if (operation == "sum") {
      const auto count = as_unsigned(find(args, "count"), 4);
      std::uint64_t total = 0;
      for (std::uint64_t i = 1; i <= count; ++i) {
        total += i;
        send(Value::Object{{"fraction", static_cast<double>(i) / static_cast<double>(count)}, {"id", idv},
                           {"message", "adding"}, {"type", "progress"}});
      }
      send(Value::Object{{"fields", Value::Object{{"total", total}}}, {"id", idv}, {"summary", "summed"}, {"type", "result"}});
    } else if (operation == "centroid") {
      Value::Object params{{"selection", find(args, "selection") != nullptr ? *find(args, "selection") : Value{"all"}}};
      send(Value::Object{{"id", idv}, {"method", "selection.atoms"}, {"params", params},
                         {"request_id", 7}, {"type", "host_request"}});
      std::string reply;
      if (!std::getline(std::cin, reply)) return 1;
      const auto response = parse_json(reply);
      const auto* object = response.has_value() ? std::get_if<Value::Object>(&response.value().data) : nullptr;
      const auto* ok = object != nullptr ? find(*object, "ok") : nullptr;
      if (ok == nullptr || !std::get<bool>(ok->data)) {
        const auto* error = object != nullptr ? find(*object, "error") : nullptr;
        send(Value::Object{{"code", "failed"}, {"id", idv},
                           {"message", error != nullptr ? std::get<std::string>(error->data) : std::string{"no response"}},
                           {"type", "error"}});
        continue;
      }
      const auto& result = std::get<Value::Object>(find(*object, "result")->data);
      const auto& positions = std::get<Value::Array>(find(result, "positions")->data);
      double sum[3]{};
      for (const auto& point : positions) {
        const auto& xyz = std::get<Value::Array>(point.data);
        for (std::size_t k = 0; k < 3U; ++k) sum[k] += std::get<double>(xyz[k].data);
      }
      const auto n = static_cast<double>(positions.size());
      send(Value::Object{{"fields", Value::Object{{"centroid", Value::Array{Value{sum[0] / n}, Value{sum[1] / n}, Value{sum[2] / n}}},
                                                 {"count", static_cast<std::uint64_t>(positions.size())}}},
                         {"id", idv}, {"summary", "centroid computed by the plugin"}, {"type", "result"}});
    } else if (operation == "fail") {
      send(Value::Object{{"code", "invalid_argument"}, {"id", idv}, {"message", "boom"},
                         {"suggestion", "do not do that"}, {"type", "error"}});
    } else if (operation == "crash") {
      std::cerr << "crashing on purpose\n" << std::flush;
      return 3;
    } else if (operation == "hang") {
      std::this_thread::sleep_for(std::chrono::hours{1});
    } else if (operation == "wait-cancel") {
      std::string next;
      while (std::getline(std::cin, next)) {
        if (next.find("\"cancel\"") != std::string::npos) {
          send(Value::Object{{"code", "cancelled"}, {"id", idv}, {"message", "stopped on request"}, {"type", "error"}});
          break;
        }
      }
    } else if (operation == "flood") {
      std::cout << std::string(3U * 1024U * 1024U, 'x') << '\n' << std::flush;
    } else if (operation == "badjson") {
      std::cout << "this is not json\n" << std::flush;
    } else if (operation == "badid") {
      send(Value::Object{{"fields", Value::Object{}}, {"id", id + 100}, {"summary", "wrong id"}, {"type", "result"}});
    } else if (operation == "table") {
      send(Value::Object{{"id", idv}, {"summary", "table"},
                         {"table", Value::Object{{"columns", Value::Array{Value{"index"}, Value{"square"}}},
                                                 {"rows", Value::Array{Value::Array{Value{1}, Value{1}}, Value::Array{Value{2}, Value{4}}}}}},
                         {"type", "result"}});
    } else if (operation == "environment") {
      send(Value::Object{{"fields", Value::Object{{"home", std::getenv("HOME") == nullptr ? "absent" : "present"},
                                                 {"plugin_dir", std::getenv("MOLSHREDDER_PLUGIN_DIR") == nullptr ? "" : std::getenv("MOLSHREDDER_PLUGIN_DIR")}}},
                         {"id", idv}, {"summary", "environment"}, {"type", "result"}});
    } else if (operation == "directory") {
      send(Value::Object{{"fields", Value::Object{{"cwd", std::filesystem::current_path().string()}}},
                         {"id", idv}, {"summary", "directory"}, {"type", "result"}});
    } else if (operation == "nested") {
      send(Value::Object{{"fields", Value::Object{{"deep", Value::Object{{"list", Value::Array{Value{1}, Value{2.5}, Value{"x"}, Value{nullptr}, Value{true}}}}}}},
                         {"id", idv}, {"summary", "nested"}, {"type", "result"}});
    }
  }
  return 0;
}
