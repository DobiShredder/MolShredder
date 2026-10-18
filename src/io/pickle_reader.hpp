#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/operation/result.hpp"

// Restricted Python pickle reader used for PyMOL session (.pse) files.
// It never executes code: only data opcodes are interpreted and only an explicit allow-list of
// globals can be referenced (see pickle_reader.cpp). Everything else is rejected with an error.
namespace molshredder::io::detail {

struct PickleValue;
using PickleRef = std::shared_ptr<PickleValue>;

struct PickleValue {
  enum class Kind { none, boolean, integer, real, text, bytes, list, tuple, dict, object, global_ref };
  Kind kind{Kind::none};
  bool boolean{};
  std::int64_t integer{};
  double real{};
  std::string text;           // text and bytes; "module.name" for allow-listed global references
  std::vector<PickleRef> items;  // list/tuple items; dict/object: key, value, key, value, ...

  [[nodiscard]] bool is_sequence() const noexcept { return kind == Kind::list || kind == Kind::tuple; }
  [[nodiscard]] bool is_number() const noexcept {
    return kind == Kind::integer || kind == Kind::real || kind == Kind::boolean;
  }
  [[nodiscard]] double as_double() const noexcept {
    return kind == Kind::real ? real : (kind == Kind::boolean ? (boolean ? 1.0 : 0.0) : static_cast<double>(integer));
  }
  [[nodiscard]] std::int64_t as_integer() const noexcept {
    return kind == Kind::real ? static_cast<std::int64_t>(real) : (kind == Kind::boolean ? (boolean ? 1 : 0) : integer);
  }
  // Dictionary/object lookup by text key; nullptr when absent.
  [[nodiscard]] const PickleValue *find(std::string_view key) const noexcept {
    if (kind != Kind::dict && kind != Kind::object) return nullptr;
    for (std::size_t i = 0; i + 1U < items.size(); i += 2U)
      if (items[i] != nullptr && items[i]->kind == Kind::text && items[i]->text == key) return items[i + 1U].get();
    return nullptr;
  }
};

// Throws nothing; returns an error naming the opcode/global that was rejected.
[[nodiscard]] operation::Result<PickleRef> unpickle_restricted(std::string_view data);

} // namespace molshredder::io::detail
