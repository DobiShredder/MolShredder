#include "pickle_reader.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>

// Restricted pickle reader (protocols 0-5, data opcodes only). Implemented from the documented pickle opcode
// set. Unsupported or dangerous constructs are rejected: any GLOBAL outside the allow-list, REDUCE/NEWOBJ/BUILD
// on anything but the allow-listed callables, persistent ids, extension registry codes and pickle-5 buffers.
namespace molshredder::io::detail {
namespace {

using operation::Error;
using operation::ErrorCode;
using operation::Result;

constexpr std::size_t kMaxValues = 200'000'000U;
constexpr std::size_t kMaxStack = 100'000'000U;

// "module.name" entries that may appear as callables.
bool allowed_global(std::string_view name) { return name == "_codecs.encode" || name == "pymol.Session_Storage"; }

struct Failure {
  std::string message;
};

class Reader {
public:
  explicit Reader(std::string_view data) : data_{data} {}

  PickleRef run() {
    for (;;) {
      const auto op = u8();
      switch (op) {
      case 0x80: // PROTO
        if (u8() > 5U) fail("unsupported pickle protocol");
        break;
      case 0x95: // FRAME
        skip(8U);
        break;
      case '.': { // STOP
        auto value = pop();
        if (!stack_.empty()) fail("pickle stack is not empty at STOP");
        return value;
      }
      case '(': marks_.push_back(stack_.size()); break;
      case 'N': push(make(PickleValue::Kind::none)); break;
      case 0x88: push(make_bool(true)); break;
      case 0x89: push(make_bool(false)); break;
      case 'I': {
        const auto text = line();
        if (text == "00") push(make_bool(false));
        else if (text == "01") push(make_bool(true));
        else push(make_int_text(text));
        break;
      }
      case 'L': {
        auto text = line();
        if (!text.empty() && text.back() == 'L') text.remove_suffix(1U);
        push(make_int_text(text));
        break;
      }
      case 'J': push(make_int(static_cast<std::int32_t>(le(4U)))); break;
      case 'K': push(make_int(static_cast<std::int64_t>(u8()))); break;
      case 'M': push(make_int(static_cast<std::int64_t>(le(2U)))); break;
      case 0x8a: push(make_long(take(u8()))); break;
      case 0x8b: push(make_long(take(checked_length(le(4U))))); break;
      case 'F': {
        const auto text = std::string{line()};
        char *end = nullptr;
        const auto value = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0') fail("invalid FLOAT literal");
        push(make_real(value));
        break;
      }
      case 'G': {
        const auto raw = take(8U);
        std::uint64_t bits = 0;
        for (const auto byte : raw) bits = (bits << 8U) | static_cast<unsigned char>(byte);
        double value{};
        std::memcpy(&value, &bits, sizeof value);
        push(make_real(value));
        break;
      }
      case 'S': push(make_text(unescape(line()))); break;
      case 'T': push(make_text(std::string{take(checked_length(le(4U)))})); break;
      case 'U': push(make_text(std::string{take(u8())})); break;
      case 'V': push(make_text(unescape(line()))); break;
      case 'X': push(make_text(std::string{take(checked_length(le(4U)))})); break;
      case 0x8c: push(make_text(std::string{take(u8())})); break;
      case 0x8d: push(make_text(std::string{take(checked_length(le(8U)))})); break;
      case 'B': push(make_bytes(std::string{take(checked_length(le(4U)))})); break;
      case 'C': push(make_bytes(std::string{take(u8())})); break;
      case 0x8e: push(make_bytes(std::string{take(checked_length(le(8U)))})); break;
      case 0x96: push(make_bytes(std::string{take(checked_length(le(8U)))})); break;
      case ']': push(make(PickleValue::Kind::list)); break;
      case ')': push(make(PickleValue::Kind::tuple)); break;
      case '}': push(make(PickleValue::Kind::dict)); break;
      case 0x8f: push(make(PickleValue::Kind::list)); break; // EMPTY_SET (treated as a list)
      case 'l': push(collect(PickleValue::Kind::list)); break;
      case 't': push(collect(PickleValue::Kind::tuple)); break;
      case 'd': push(collect(PickleValue::Kind::dict)); break;
      case 0x91: push(collect(PickleValue::Kind::list)); break; // FROZENSET
      case 0x85: push(tuple_of(1U)); break;
      case 0x86: push(tuple_of(2U)); break;
      case 0x87: push(tuple_of(3U)); break;
      case 'a': {
        auto item = pop();
        auto &list = *top();
        if (list.kind != PickleValue::Kind::list) fail("APPEND target is not a list");
        list.items.push_back(std::move(item));
        break;
      }
      case 'e': case 0x90: { // APPENDS, ADDITEMS
        const auto begin = pop_mark();
        auto &list = *stack_.at(begin - 1U);
        if (list.kind != PickleValue::Kind::list) fail("APPENDS target is not a list");
        for (std::size_t i = begin; i < stack_.size(); ++i) list.items.push_back(stack_[i]);
        stack_.resize(begin);
        break;
      }
      case 's': {
        auto value = pop();
        auto key = pop();
        auto &dict = *top();
        if (dict.kind != PickleValue::Kind::dict) fail("SETITEM target is not a dict");
        dict.items.push_back(std::move(key));
        dict.items.push_back(std::move(value));
        break;
      }
      case 'u': {
        const auto begin = pop_mark();
        if ((stack_.size() - begin) % 2U != 0U) fail("SETITEMS needs an even number of items");
        auto &dict = *stack_.at(begin - 1U);
        if (dict.kind != PickleValue::Kind::dict) fail("SETITEMS target is not a dict");
        for (std::size_t i = begin; i < stack_.size(); ++i) dict.items.push_back(stack_[i]);
        stack_.resize(begin);
        break;
      }
      case '0': (void)pop(); break;
      case '1': {
        const auto begin = pop_mark();
        stack_.resize(begin);
        break;
      }
      case '2': push(top_ref()); break;
      case 'p': memo_[to_index(line())] = top_ref(); break;
      case 'q': memo_[u8()] = top_ref(); break;
      case 'r': memo_[le(4U)] = top_ref(); break;
      case 0x94: memo_[memo_.size()] = top_ref(); break; // MEMOIZE
      case 'g': push(memo_get(to_index(line()))); break;
      case 'h': push(memo_get(u8())); break;
      case 'j': push(memo_get(le(4U))); break;
      case 'c': {
        const auto module = std::string{line()};
        const auto name = std::string{line()};
        push(global_ref(module + "." + name));
        break;
      }
      case 0x93: { // STACK_GLOBAL
        auto name = pop();
        auto module = pop();
        if (name->kind != PickleValue::Kind::text || module->kind != PickleValue::Kind::text) fail("STACK_GLOBAL needs two strings");
        push(global_ref(module->text + "." + name->text));
        break;
      }
      case 'R': {
        auto arguments = pop();
        auto callable = pop();
        push(call(*callable, *arguments));
        break;
      }
      case 0x81: { // NEWOBJ
        auto arguments = pop();
        auto callable = pop();
        push(call(*callable, *arguments));
        break;
      }
      case 'b': { // BUILD
        auto state = pop();
        auto &object = *top();
        if (object.kind != PickleValue::Kind::object && object.kind != PickleValue::Kind::dict) fail("BUILD target is not an allow-listed object");
        if (state->kind != PickleValue::Kind::dict) fail("BUILD state is not a dictionary");
        for (const auto &item : state->items) object.items.push_back(item);
        break;
      }
      default:
        fail("pickle opcode 0x" + hex(op) + " is not allowed (only data opcodes are interpreted)");
      }
      if (stack_.size() > kMaxStack) fail("pickle stack is too deep");
    }
  }

private:
  std::string_view data_;
  std::size_t pos_{};
  std::size_t values_{};
  std::vector<PickleRef> stack_;
  std::vector<std::size_t> marks_;
  std::unordered_map<std::uint64_t, PickleRef> memo_;

  [[noreturn]] static void fail(std::string message) { throw Failure{std::move(message)}; }

  static std::string hex(unsigned value) {
    static const char digits[] = "0123456789abcdef";
    return std::string{digits[(value >> 4U) & 0xfU], digits[value & 0xfU]};
  }

  std::uint8_t u8() {
    if (pos_ >= data_.size()) fail("pickle ends before STOP");
    return static_cast<std::uint8_t>(data_[pos_++]);
  }

  void skip(std::size_t count) {
    if (data_.size() - pos_ < count) fail("pickle ends before STOP");
    pos_ += count;
  }

  std::uint64_t le(std::size_t width) {
    if (data_.size() - pos_ < width) fail("pickle ends before STOP");
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data_[pos_ + i])) << (8U * i);
    pos_ += width;
    return value;
  }

  std::size_t checked_length(std::uint64_t length) {
    if (length > data_.size() - pos_) fail("pickle string length exceeds the remaining input");
    return static_cast<std::size_t>(length);
  }

  std::string_view take(std::size_t count) {
    if (data_.size() - pos_ < count) fail("pickle string length exceeds the remaining input");
    const auto view = data_.substr(pos_, count);
    pos_ += count;
    return view;
  }

  std::string_view line() {
    const auto end = data_.find('\n', pos_);
    if (end == std::string_view::npos) fail("pickle text line is not terminated");
    const auto view = data_.substr(pos_, end - pos_);
    pos_ = end + 1U;
    return view;
  }

  static std::uint64_t to_index(std::string_view text) {
    if (text.empty() || text.size() > 12U) fail("invalid memo index");
    std::uint64_t value = 0;
    for (const auto c : text) {
      if (c < '0' || c > '9') fail("invalid memo index");
      value = value * 10U + static_cast<std::uint64_t>(c - '0');
    }
    return value;
  }

  PickleRef make(PickleValue::Kind kind) {
    if (++values_ > kMaxValues) fail("pickle holds more values than the supported limit");
    auto value = std::make_shared<PickleValue>();
    value->kind = kind;
    return value;
  }
  PickleRef make_bool(bool flag) {
    auto value = make(PickleValue::Kind::boolean);
    value->boolean = flag;
    return value;
  }
  PickleRef make_int(std::int64_t number) {
    auto value = make(PickleValue::Kind::integer);
    value->integer = number;
    return value;
  }
  PickleRef make_real(double number) {
    auto value = make(PickleValue::Kind::real);
    value->real = number;
    return value;
  }
  PickleRef make_text(std::string text) {
    auto value = make(PickleValue::Kind::text);
    value->text = std::move(text);
    return value;
  }
  PickleRef make_bytes(std::string bytes) {
    auto value = make(PickleValue::Kind::bytes);
    value->text = std::move(bytes);
    return value;
  }
  PickleRef make_int_text(std::string_view text) {
    if (text.empty()) fail("empty integer literal");
    const std::string copy{text};
    char *end = nullptr;
    errno = 0;
    const auto number = std::strtoll(copy.c_str(), &end, 10);
    if (end != copy.c_str() + copy.size()) fail("invalid integer literal");
    if (errno == ERANGE) return make_real(std::strtod(copy.c_str(), nullptr));
    return make_int(number);
  }
  PickleRef make_long(std::string_view little_endian) {
    if (little_endian.empty()) return make_int(0);
    const bool negative = (static_cast<unsigned char>(little_endian.back()) & 0x80U) != 0U;
    // Values wider than 64 bits (after dropping sign-extension bytes) are kept as doubles.
    std::size_t width = little_endian.size();
    const unsigned char fill = negative ? 0xffU : 0x00U;
    while (width > 1U && static_cast<unsigned char>(little_endian[width - 1U]) == fill &&
           ((static_cast<unsigned char>(little_endian[width - 2U]) & 0x80U) != 0U) == negative)
      --width;
    if (width > 8U) {
      double result = 0.0;
      for (std::size_t i = width; i-- > 0;) result = result * 256.0 + static_cast<unsigned char>(little_endian[i]);
      if (negative) result -= std::ldexp(1.0, static_cast<int>(8U * width));
      return make_real(result);
    }
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < width; ++i) bits |= static_cast<std::uint64_t>(static_cast<unsigned char>(little_endian[i])) << (8U * i);
    if (negative && width < 8U) bits |= ~std::uint64_t{0} << (8U * width);
    return make_int(static_cast<std::int64_t>(bits));
  }

  static std::string unescape(std::string_view text) {
    if (text.size() >= 2U && (text.front() == '\'' || text.front() == '"') && text.back() == text.front())
      text = text.substr(1U, text.size() - 2U);
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
      if (text[i] != '\\' || i + 1U >= text.size()) {
        out.push_back(text[i]);
        continue;
      }
      const auto c = text[++i];
      if (c == 'n') out.push_back('\n');
      else if (c == 't') out.push_back('\t');
      else if (c == 'r') out.push_back('\r');
      else if (c == '\\' || c == '\'' || c == '"') out.push_back(c);
      else if (c == 'x' && i + 2U < text.size() + 0U) {
        const auto hexval = [](char h) { return h >= '0' && h <= '9' ? h - '0' : (h >= 'a' && h <= 'f' ? h - 'a' + 10 : (h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1)); };
        const auto hi = hexval(text[i + 1U]), lo = hexval(text[i + 2U]);
        if (hi < 0 || lo < 0) fail("invalid \\x escape in a pickle string");
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2U;
      } else if (c == 'u' && i + 4U < text.size() + 0U) {
        std::uint32_t code = 0;
        for (int k = 1; k <= 4; ++k) {
          const char h = text[i + static_cast<std::size_t>(k)];
          const int v = h >= '0' && h <= '9' ? h - '0' : (h >= 'a' && h <= 'f' ? h - 'a' + 10 : (h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1));
          if (v < 0) fail("invalid \\u escape in a pickle string");
          code = code * 16U + static_cast<std::uint32_t>(v);
        }
        i += 4U;
        if (code < 0x80U) out.push_back(static_cast<char>(code));
        else if (code < 0x800U) {
          out.push_back(static_cast<char>(0xC0U | (code >> 6U)));
          out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        } else {
          out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
          out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
          out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
        }
      } else {
        fail("unsupported escape in a pickle string");
      }
    }
    return out;
  }

  void push(PickleRef value) { stack_.push_back(std::move(value)); }

  PickleRef pop() {
    if (stack_.empty()) fail("pickle stack underflow");
    auto value = std::move(stack_.back());
    stack_.pop_back();
    return value;
  }

  PickleValue *top() {
    if (stack_.empty()) fail("pickle stack underflow");
    return stack_.back().get();
  }

  PickleRef top_ref() {
    if (stack_.empty()) fail("pickle stack underflow");
    return stack_.back();
  }

  std::size_t pop_mark() {
    if (marks_.empty()) fail("pickle MARK stack underflow");
    const auto mark = marks_.back();
    marks_.pop_back();
    if (mark > stack_.size()) fail("pickle MARK refers beyond the stack");
    if (mark == 0U) fail("pickle MARK without a container below it");
    return mark;
  }

  PickleRef collect(PickleValue::Kind kind) {
    if (marks_.empty()) fail("pickle MARK stack underflow");
    const auto begin = marks_.back();
    marks_.pop_back();
    if (begin > stack_.size()) fail("pickle MARK refers beyond the stack");
    auto value = make(kind);
    value->items.assign(stack_.begin() + static_cast<std::ptrdiff_t>(begin), stack_.end());
    stack_.resize(begin);
    if (kind == PickleValue::Kind::dict && value->items.size() % 2U != 0U) fail("DICT needs an even number of items");
    return value;
  }

  PickleRef tuple_of(std::size_t count) {
    if (stack_.size() < count) fail("pickle stack underflow");
    auto value = make(PickleValue::Kind::tuple);
    value->items.assign(stack_.end() - static_cast<std::ptrdiff_t>(count), stack_.end());
    stack_.resize(stack_.size() - count);
    return value;
  }

  PickleRef memo_get(std::uint64_t index) {
    const auto found = memo_.find(index);
    if (found == memo_.end()) fail("pickle refers to an unset memo entry");
    return found->second;
  }

  PickleRef global_ref(const std::string &name) {
    if (!allowed_global(name)) fail("pickle references the global '" + name + "', which is not allowed in a session file");
    auto value = make(PickleValue::Kind::global_ref);
    value->text = name;
    return value;
  }

  PickleRef call(const PickleValue &callable, const PickleValue &arguments) {
    if (callable.kind != PickleValue::Kind::global_ref) fail("pickle calls something that is not an allow-listed global");
    if (arguments.kind != PickleValue::Kind::tuple) fail("pickle call arguments are not a tuple");
    if (callable.text == "_codecs.encode") {
      if (arguments.items.size() != 2U || arguments.items[0]->kind != PickleValue::Kind::text || arguments.items[1]->kind != PickleValue::Kind::text)
        fail("_codecs.encode needs (text, encoding)");
      const auto &encoding = arguments.items[1]->text;
      if (encoding != "latin1" && encoding != "latin-1" && encoding != "iso-8859-1") fail("_codecs.encode only supports the latin1 encoding");
      // The text holds UTF-8 for code points 0-255; convert back to raw bytes.
      std::string bytes;
      const auto &text = arguments.items[0]->text;
      for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80U) {
          bytes.push_back(static_cast<char>(c));
          ++i;
        } else if ((c & 0xE0U) == 0xC0U && i + 1U < text.size()) {
          const auto code = ((c & 0x1FU) << 6U) | (static_cast<unsigned char>(text[i + 1U]) & 0x3FU);
          if (code > 0xffU) fail("_codecs.encode latin1 got a code point above 255");
          bytes.push_back(static_cast<char>(code));
          i += 2U;
        } else {
          fail("_codecs.encode latin1 got a code point above 255");
        }
      }
      return make_bytes(std::move(bytes));
    }
    if (callable.text == "pymol.Session_Storage") {
      if (!arguments.items.empty()) fail("pymol.Session_Storage takes no arguments");
      return make(PickleValue::Kind::object);
    }
    fail("pickle calls a global that is not implemented");
  }
};

} // namespace

Result<PickleRef> unpickle_restricted(std::string_view data) {
  if (data.empty()) return Result<PickleRef>::failure(Error{ErrorCode::invalid_argument, "pickle input is empty", {}});
  try {
    Reader reader{data};
    return Result<PickleRef>::success(reader.run());
  } catch (const Failure &failure) {
    return Result<PickleRef>::failure(Error{ErrorCode::invalid_argument, "unsafe or invalid pickle: " + failure.message,
                                            "only data from a PyMOL session (.pse) is accepted; code in pickles is never executed"});
  }
}

} // namespace molshredder::io::detail
