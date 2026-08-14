#include "structure_reader_internal.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <msgpack.hpp>

namespace molshredder::io::detail {
namespace {

using operation::Result;

constexpr std::size_t maximum_bcif_rows = 10'000'000U;
constexpr std::size_t maximum_decode_depth = 16U;

class BcifError final : public std::runtime_error {
public:
  explicit BcifError(std::string message)
      : std::runtime_error(std::move(message)) {}
};

const msgpack::object *map_value(const msgpack::object &object,
                                 std::string_view key) {
  if (object.type != msgpack::type::MAP) {
    throw BcifError("expected a MessagePack map while reading '" +
                    std::string{key} + "'");
  }
  const msgpack::object *found{};
  for (std::uint32_t index = 0; index < object.via.map.size; ++index) {
    const auto &item = object.via.map.ptr[index];
    if (item.key.type != msgpack::type::STR) {
      throw BcifError("BinaryCIF map contains a non-string key");
    }
    const std::string_view candidate{item.key.via.str.ptr,
                                     item.key.via.str.size};
    if (candidate == key) {
      if (found != nullptr) {
        throw BcifError("BinaryCIF map contains duplicate field '" +
                        std::string{key} + "'");
      }
      found = &item.val;
    }
  }
  return found;
}

const msgpack::object &required(const msgpack::object &object,
                                std::string_view key) {
  const auto *value = map_value(object, key);
  if (value == nullptr) {
    throw BcifError("BinaryCIF object is missing required field '" +
                    std::string{key} + "'");
  }
  return *value;
}

std::string as_string(const msgpack::object &object, std::string_view field) {
  if (object.type != msgpack::type::STR) {
    throw BcifError("BinaryCIF field '" + std::string{field} +
                    "' must be a string");
  }
  return {object.via.str.ptr, object.via.str.size};
}

std::uint64_t as_unsigned(const msgpack::object &object,
                          std::string_view field) {
  if (object.type != msgpack::type::POSITIVE_INTEGER) {
    throw BcifError("BinaryCIF field '" + std::string{field} +
                    "' must be a non-negative integer");
  }
  return object.via.u64;
}

std::int64_t as_signed(const msgpack::object &object, std::string_view field) {
  if (object.type == msgpack::type::POSITIVE_INTEGER) {
    if (object.via.u64 >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      throw BcifError("BinaryCIF field '" + std::string{field} +
                      "' exceeds int64 range");
    }
    return static_cast<std::int64_t>(object.via.u64);
  }
  if (object.type == msgpack::type::NEGATIVE_INTEGER) {
    return object.via.i64;
  }
  throw BcifError("BinaryCIF field '" + std::string{field} +
                  "' must be an integer");
}

double as_number(const msgpack::object &object, std::string_view field) {
  double value{};
  if (object.type == msgpack::type::FLOAT32 ||
      object.type == msgpack::type::FLOAT64) {
    value = object.via.f64;
  } else if (object.type == msgpack::type::POSITIVE_INTEGER) {
    value = static_cast<double>(object.via.u64);
  } else if (object.type == msgpack::type::NEGATIVE_INTEGER) {
    value = static_cast<double>(object.via.i64);
  } else {
    throw BcifError("BinaryCIF field '" + std::string{field} +
                    "' must be numeric");
  }
  if (!std::isfinite(value)) {
    throw BcifError("BinaryCIF field '" + std::string{field} +
                    "' must be finite");
  }
  return value;
}

bool as_bool(const msgpack::object &object, std::string_view field) {
  if (object.type != msgpack::type::BOOLEAN) {
    throw BcifError("BinaryCIF field '" + std::string{field} +
                    "' must be boolean");
  }
  return object.via.boolean;
}

void require_integer_type(const msgpack::object &encoding,
                          std::string_view context) {
  const auto type = as_signed(required(encoding, "srcType"),
                              std::string{context} + ".srcType");
  if (type < 1 || type > 6) {
    throw BcifError(std::string{"BinaryCIF "} + std::string{context} +
                    " srcType must be an integer data type");
  }
}

void require_float_type(const msgpack::object &encoding,
                        std::string_view context) {
  const auto type = as_signed(required(encoding, "srcType"),
                              std::string{context} + ".srcType");
  if (type != 32 && type != 33) {
    throw BcifError(std::string{"BinaryCIF "} + std::string{context} +
                    " srcType must be Float32 or Float64");
  }
}

std::vector<std::uint8_t> as_bytes(const msgpack::object &object,
                                   std::string_view field) {
  const char *pointer{};
  std::uint32_t size{};
  if (object.type == msgpack::type::BIN) {
    pointer = object.via.bin.ptr;
    size = object.via.bin.size;
  } else if (object.type == msgpack::type::STR) {
    pointer = object.via.str.ptr;
    size = object.via.str.size;
  } else {
    throw BcifError("BinaryCIF field '" + std::string{field} +
                    "' must contain bytes");
  }
  if (size == 0U)
    return {};
  const auto *first = reinterpret_cast<const std::uint8_t *>(pointer);
  return {first, first + size};
}

const msgpack::object_array &as_array(const msgpack::object &object,
                                      std::string_view field) {
  if (object.type != msgpack::type::ARRAY) {
    throw BcifError("BinaryCIF field '" + std::string{field} +
                    "' must be an array");
  }
  return object.via.array;
}

std::size_t checked_size(std::uint64_t value, std::string_view field,
                         std::size_t maximum = maximum_bcif_rows) {
  if (value > maximum || value > std::numeric_limits<std::size_t>::max()) {
    throw BcifError("BinaryCIF field '" + std::string{field} +
                    "' exceeds the supported size limit");
  }
  return static_cast<std::size_t>(value);
}

using RawBytes = std::vector<std::uint8_t>;
using SignedArray = std::vector<std::int64_t>;
using UnsignedArray = std::vector<std::uint64_t>;
using FloatArray = std::vector<double>;
using StringArray = std::vector<std::string>;
using DecodedArray =
    std::variant<RawBytes, SignedArray, UnsignedArray, FloatArray, StringArray>;

std::uint64_t little_unsigned(const RawBytes &bytes, std::size_t offset,
                              std::size_t width) {
  std::uint64_t value{};
  for (std::size_t byte = 0; byte < width; ++byte) {
    value |= static_cast<std::uint64_t>(bytes[offset + byte]) << (byte * 8U);
  }
  return value;
}

template <typename Integer> Integer bit_integer(std::uint64_t bits) {
  using Unsigned = std::make_unsigned_t<Integer>;
  return std::bit_cast<Integer>(static_cast<Unsigned>(bits));
}

DecodedArray decode_byte_array(const RawBytes &bytes, std::int64_t type) {
  std::size_t width{};
  switch (type) {
  case 1:
  case 4:
    width = 1U;
    break;
  case 2:
  case 5:
    width = 2U;
    break;
  case 3:
  case 6:
  case 32:
    width = 4U;
    break;
  case 33:
    width = 8U;
    break;
  default:
    throw BcifError("BinaryCIF ByteArray has an unknown numeric type");
  }
  if (bytes.size() % width != 0U) {
    throw BcifError(
        "BinaryCIF ByteArray byte count is not aligned to its type");
  }
  const auto count = bytes.size() / width;
  if (count > maximum_bcif_rows) {
    throw BcifError("BinaryCIF ByteArray exceeds the supported element limit");
  }
  if (type == 1 || type == 2 || type == 3) {
    SignedArray values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      const auto bits = little_unsigned(bytes, index * width, width);
      if (type == 1)
        values.push_back(bit_integer<std::int8_t>(bits));
      if (type == 2)
        values.push_back(bit_integer<std::int16_t>(bits));
      if (type == 3)
        values.push_back(bit_integer<std::int32_t>(bits));
    }
    return values;
  }
  if (type == 4 || type == 5 || type == 6) {
    UnsignedArray values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      values.push_back(little_unsigned(bytes, index * width, width));
    }
    return values;
  }
  FloatArray values;
  values.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const auto bits = little_unsigned(bytes, index * width, width);
    const auto value = type == 32 ? static_cast<double>(std::bit_cast<float>(
                                        static_cast<std::uint32_t>(bits)))
                                  : std::bit_cast<double>(bits);
    if (!std::isfinite(value)) {
      throw BcifError("BinaryCIF ByteArray contains a non-finite float");
    }
    values.push_back(value);
  }
  return values;
}

SignedArray signed_values(const DecodedArray &data, std::string_view context) {
  if (const auto *values = std::get_if<SignedArray>(&data)) {
    return *values;
  }
  if (const auto *values = std::get_if<UnsignedArray>(&data)) {
    SignedArray result;
    result.reserve(values->size());
    for (const auto value : *values) {
      if (value > static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max())) {
        throw BcifError(std::string{context} + " integer exceeds int64 range");
      }
      result.push_back(static_cast<std::int64_t>(value));
    }
    return result;
  }
  throw BcifError(std::string{context} + " requires an integer array");
}

std::int64_t checked_add(std::int64_t left, std::int64_t right,
                         std::string_view context) {
  if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
      (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
    throw BcifError(std::string{context} + " overflows int64");
  }
  return left + right;
}

DecodedArray decode_steps(DecodedArray current,
                          const msgpack::object_array &encodings,
                          std::size_t depth);

DecodedArray decode_integer_packing(const DecodedArray &current,
                                    const msgpack::object &encoding) {
  const auto byte_count =
      as_signed(required(encoding, "byteCount"), "IntegerPacking.byteCount");
  const auto source_size = checked_size(
      as_unsigned(required(encoding, "srcSize"), "IntegerPacking.srcSize"),
      "IntegerPacking.srcSize");
  const auto is_unsigned =
      as_bool(required(encoding, "isUnsigned"), "IntegerPacking.isUnsigned");
  if (byte_count != 1 && byte_count != 2) {
    throw BcifError("BinaryCIF IntegerPacking byteCount must be 1 or 2");
  }
  const auto values = signed_values(current, "BinaryCIF IntegerPacking");
  if (values.size() == source_size) {
    return values;
  }
  SignedArray output;
  output.reserve(source_size);
  const std::int64_t upper = is_unsigned ? (byte_count == 1 ? 255 : 65535)
                                         : (byte_count == 1 ? 127 : 32767);
  const std::int64_t lower =
      is_unsigned ? 0 : (byte_count == 1 ? -128 : -32768);
  std::size_t index{};
  while (index < values.size()) {
    std::int64_t value{};
    while (
        index < values.size() &&
        (values[index] == upper || (!is_unsigned && values[index] == lower))) {
      value = checked_add(value, values[index], "BinaryCIF IntegerPacking");
      ++index;
    }
    if (index >= values.size()) {
      throw BcifError(
          "BinaryCIF IntegerPacking ends with a continuation sentinel");
    }
    value = checked_add(value, values[index++], "BinaryCIF IntegerPacking");
    output.push_back(value);
    if (output.size() > source_size) {
      throw BcifError("BinaryCIF IntegerPacking expands beyond srcSize");
    }
  }
  if (output.size() != source_size) {
    throw BcifError("BinaryCIF IntegerPacking output does not match srcSize");
  }
  return output;
}

DecodedArray decode_string_array(const RawBytes &bytes,
                                 const msgpack::object &encoding,
                                 std::size_t depth) {
  const auto &data_encoding =
      as_array(required(encoding, "dataEncoding"), "StringArray.dataEncoding");
  const auto &offset_encoding = as_array(required(encoding, "offsetEncoding"),
                                         "StringArray.offsetEncoding");
  const auto offset_bytes =
      as_bytes(required(encoding, "offsets"), "StringArray.offsets");
  const auto string_data =
      as_string(required(encoding, "stringData"), "StringArray.stringData");
  const auto decoded_indices =
      decode_steps(DecodedArray{bytes}, data_encoding, depth + 1U);
  const auto decoded_offsets =
      decode_steps(DecodedArray{offset_bytes}, offset_encoding, depth + 1U);
  const auto indices =
      signed_values(decoded_indices, "BinaryCIF StringArray index");
  const auto offsets =
      signed_values(decoded_offsets, "BinaryCIF StringArray offset");
  if (offsets.empty() || offsets.front() != 0) {
    throw BcifError("BinaryCIF StringArray offsets must begin at zero");
  }
  for (std::size_t index = 1; index < offsets.size(); ++index) {
    if (offsets[index] < offsets[index - 1] || offsets[index] < 0) {
      throw BcifError(
          "BinaryCIF StringArray offsets must be non-negative and monotonic");
    }
  }
  if (static_cast<std::uint64_t>(offsets.back()) > string_data.size()) {
    throw BcifError("BinaryCIF StringArray offset exceeds stringData");
  }
  std::vector<std::string> dictionary;
  dictionary.reserve(offsets.size() - 1U);
  for (std::size_t index = 1; index < offsets.size(); ++index) {
    const auto begin = static_cast<std::size_t>(offsets[index - 1U]);
    const auto end = static_cast<std::size_t>(offsets[index]);
    dictionary.emplace_back(string_data.substr(begin, end - begin));
  }
  StringArray output;
  output.reserve(indices.size());
  for (const auto index : indices) {
    if (index == -1) {
      output.emplace_back();
      continue;
    }
    if (index < -1 || static_cast<std::uint64_t>(index) >= dictionary.size()) {
      throw BcifError("BinaryCIF StringArray index is outside its dictionary");
    }
    output.push_back(dictionary[static_cast<std::size_t>(index)]);
  }
  return output;
}

DecodedArray decode_steps(DecodedArray current,
                          const msgpack::object_array &encodings,
                          std::size_t depth) {
  if (depth > maximum_decode_depth) {
    throw BcifError("BinaryCIF encoding nesting exceeds the safety limit");
  }
  for (std::size_t reverse = encodings.size; reverse > 0U; --reverse) {
    const auto &encoding = encodings.ptr[reverse - 1U];
    const auto kind = as_string(required(encoding, "kind"), "encoding.kind");
    if (kind == "ByteArray") {
      const auto *bytes = std::get_if<RawBytes>(&current);
      if (bytes == nullptr) {
        throw BcifError("BinaryCIF ByteArray must decode raw bytes");
      }
      current = decode_byte_array(
          *bytes, as_signed(required(encoding, "type"), "ByteArray.type"));
    } else if (kind == "IntegerPacking") {
      current = decode_integer_packing(current, encoding);
    } else if (kind == "RunLength") {
      require_integer_type(encoding, "RunLength");
      const auto values = signed_values(current, "BinaryCIF RunLength");
      const auto source_size = checked_size(
          as_unsigned(required(encoding, "srcSize"), "RunLength.srcSize"),
          "RunLength.srcSize");
      if (values.size() % 2U != 0U) {
        throw BcifError("BinaryCIF RunLength requires value/count pairs");
      }
      SignedArray output;
      output.reserve(source_size);
      for (std::size_t index = 0; index < values.size(); index += 2U) {
        if (values[index + 1U] <= 0) {
          throw BcifError("BinaryCIF RunLength count must be positive");
        }
        const auto count =
            checked_size(static_cast<std::uint64_t>(values[index + 1U]),
                         "RunLength.count", source_size - output.size());
        output.insert(output.end(), count, values[index]);
      }
      if (output.size() != source_size) {
        throw BcifError("BinaryCIF RunLength output does not match srcSize");
      }
      current = std::move(output);
    } else if (kind == "Delta") {
      require_integer_type(encoding, "Delta");
      auto values = signed_values(current, "BinaryCIF Delta");
      auto previous = as_signed(required(encoding, "origin"), "Delta.origin");
      for (auto &value : values) {
        previous = checked_add(previous, value, "BinaryCIF Delta");
        value = previous;
      }
      current = std::move(values);
    } else if (kind == "FixedPoint") {
      require_float_type(encoding, "FixedPoint");
      const auto values = signed_values(current, "BinaryCIF FixedPoint");
      const auto factor =
          as_number(required(encoding, "factor"), "FixedPoint.factor");
      if (factor == 0.0) {
        throw BcifError("BinaryCIF FixedPoint factor must be non-zero");
      }
      FloatArray output;
      output.reserve(values.size());
      for (const auto value : values)
        output.push_back(static_cast<double>(value) / factor);
      current = std::move(output);
    } else if (kind == "IntervalQuantization") {
      require_float_type(encoding, "IntervalQuantization");
      const auto values =
          signed_values(current, "BinaryCIF IntervalQuantization");
      const auto minimum =
          as_number(required(encoding, "min"), "IntervalQuantization.min");
      const auto maximum =
          as_number(required(encoding, "max"), "IntervalQuantization.max");
      const auto steps = as_unsigned(required(encoding, "numSteps"),
                                     "IntervalQuantization.numSteps");
      if (steps < 2U || maximum < minimum) {
        throw BcifError(
            "BinaryCIF IntervalQuantization has an invalid interval");
      }
      const auto delta = (maximum - minimum) / static_cast<double>(steps - 1U);
      FloatArray output;
      output.reserve(values.size());
      for (const auto value : values) {
        if (value < 0 || static_cast<std::uint64_t>(value) >= steps) {
          throw BcifError(
              "BinaryCIF IntervalQuantization index is out of range");
        }
        output.push_back(minimum + delta * static_cast<double>(value));
      }
      current = std::move(output);
    } else if (kind == "StringArray") {
      const auto *bytes = std::get_if<RawBytes>(&current);
      if (bytes == nullptr) {
        throw BcifError("BinaryCIF StringArray must decode raw bytes");
      }
      current = decode_string_array(*bytes, encoding, depth);
    } else {
      throw BcifError("BinaryCIF uses unsupported encoding kind '" + kind +
                      "'");
    }
  }
  if (std::holds_alternative<RawBytes>(current)) {
    throw BcifError("BinaryCIF encoded data has no terminal decoding step");
  }
  return current;
}

DecodedArray decode_data(const msgpack::object &data, std::size_t depth = 0U) {
  const auto bytes = as_bytes(required(data, "data"), "Data.data");
  const auto &encoding = as_array(required(data, "encoding"), "Data.encoding");
  return decode_steps(DecodedArray{bytes}, encoding, depth);
}

std::size_t decoded_size(const DecodedArray &values) {
  return std::visit([](const auto &array) { return array.size(); }, values);
}

std::string scalar_text(const DecodedArray &values, std::size_t index) {
  if (const auto *strings = std::get_if<StringArray>(&values))
    return (*strings)[index];
  char buffer[128]{};
  if (const auto *signed_array = std::get_if<SignedArray>(&values)) {
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer),
                                      (*signed_array)[index]);
    return {buffer, result.ptr};
  }
  if (const auto *unsigned_array = std::get_if<UnsignedArray>(&values)) {
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer),
                                      (*unsigned_array)[index]);
    return {buffer, result.ptr};
  }
  const auto value = std::get<FloatArray>(values)[index];
  const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value,
                                    std::chars_format::general,
                                    std::numeric_limits<double>::max_digits10);
  if (result.ec != std::errc{}) {
    throw BcifError("BinaryCIF float could not be converted to text");
  }
  return {buffer, result.ptr};
}

std::string normalized_category(std::string value) {
  if (value.empty())
    throw BcifError("BinaryCIF category name must not be empty");
  if (value.front() != '_')
    value.insert(value.begin(), '_');
  if (value.find('.') != std::string::npos ||
      std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
      })) {
    throw BcifError("BinaryCIF category name contains an invalid separator");
  }
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string normalized_column(std::string category, std::string column) {
  if (column.empty() || column.find('.') != std::string::npos) {
    throw BcifError(
        "BinaryCIF column name is empty or contains a category separator");
  }
  if (std::any_of(column.begin(), column.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
      })) {
    throw BcifError("BinaryCIF column name contains whitespace");
  }
  std::transform(
      column.begin(), column.end(), column.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return std::move(category) + "." + std::move(column);
}

struct DecodedColumn {
  std::string name;
  DecodedArray values;
  std::vector<std::uint8_t> mask;
};

DecodedColumn decode_column(const msgpack::object &column,
                            std::size_t row_count) {
  DecodedColumn result;
  result.name = as_string(required(column, "name"), "Column.name");
  result.values = decode_data(required(column, "data"));
  if (decoded_size(result.values) != row_count) {
    throw BcifError("BinaryCIF column '" + result.name +
                    "' value count does not match category rowCount");
  }
  if (const auto *mask = map_value(column, "mask");
      mask != nullptr && mask->type != msgpack::type::NIL) {
    const auto decoded = signed_values(decode_data(*mask), "BinaryCIF mask");
    if (decoded.size() != row_count) {
      throw BcifError("BinaryCIF column '" + result.name +
                      "' mask count does not match category rowCount");
    }
    result.mask.reserve(decoded.size());
    for (const auto value : decoded) {
      if (value < 0 || value > 2) {
        throw BcifError("BinaryCIF mask values must be 0, 1 or 2");
      }
      result.mask.push_back(static_cast<std::uint8_t>(value));
    }
  }
  return result;
}

std::vector<CifBlock> decode_blocks(const msgpack::object &root) {
  const auto &data_blocks =
      as_array(required(root, "dataBlocks"), "File.dataBlocks");
  if (data_blocks.size == 0U)
    throw BcifError("BinaryCIF file contains no data block");
  std::vector<CifBlock> blocks;
  blocks.reserve(data_blocks.size);
  for (std::uint32_t block_index = 0; block_index < data_blocks.size;
       ++block_index) {
    const auto &source_block = data_blocks.ptr[block_index];
    CifBlock block;
    block.name =
        as_string(required(source_block, "header"), "DataBlock.header");
    block.line = static_cast<std::size_t>(block_index) + 1U;
    if (block.name.empty())
      throw BcifError("BinaryCIF data-block header must not be empty");
    const auto &categories =
        as_array(required(source_block, "categories"), "DataBlock.categories");
    for (std::uint32_t category_index = 0; category_index < categories.size;
         ++category_index) {
      const auto &source_category = categories.ptr[category_index];
      const auto category_name = normalized_category(
          as_string(required(source_category, "name"), "Category.name"));
      const auto row_count =
          checked_size(as_unsigned(required(source_category, "rowCount"),
                                   "Category.rowCount"),
                       "Category.rowCount");
      if (row_count == 0U)
        continue;
      const auto &source_columns =
          as_array(required(source_category, "columns"), "Category.columns");
      if (source_columns.size == 0U)
        throw BcifError("BinaryCIF non-empty category has no columns");
      std::vector<DecodedColumn> columns;
      columns.reserve(source_columns.size);
      for (std::uint32_t column_index = 0; column_index < source_columns.size;
           ++column_index) {
        columns.push_back(
            decode_column(source_columns.ptr[column_index], row_count));
      }
      const auto force_loop = category_name == "_atom_site" ||
                              category_name == "_struct_conn" || row_count > 1U;
      if (!force_loop) {
        for (const auto &column : columns) {
          const auto name = normalized_column(category_name, column.name);
          auto value = scalar_text(column.values, 0U);
          if (!column.mask.empty() && column.mask[0] != 0U)
            value = column.mask[0] == 1U ? "." : "?";
          if (!block.scalars
                   .emplace(name, CifToken{std::move(value), block.line, true})
                   .second) {
            throw BcifError("BinaryCIF contains duplicate scalar column '" +
                            name + "'");
          }
        }
        continue;
      }
      CifLoop loop;
      loop.line = block.line;
      loop.columns.reserve(columns.size());
      if (row_count >
          std::numeric_limits<std::size_t>::max() / columns.size()) {
        throw BcifError(
            "BinaryCIF category row/column product overflows size_t");
      }
      loop.values.reserve(row_count * columns.size());
      for (const auto &column : columns)
        loop.columns.push_back(normalized_column(category_name, column.name));
      for (std::size_t row = 0; row < row_count; ++row) {
        for (const auto &column : columns) {
          auto value = scalar_text(column.values, row);
          if (!column.mask.empty() && column.mask[row] != 0U)
            value = column.mask[row] == 1U ? "." : "?";
          loop.values.push_back(CifToken{std::move(value), block.line, true});
        }
      }
      block.loops.push_back(std::move(loop));
    }
    blocks.push_back(std::move(block));
  }
  return blocks;
}

std::pair<unsigned, unsigned> parse_version(std::string_view version) {
  const auto first_dot = version.find('.');
  const auto second_dot = first_dot == std::string_view::npos
                              ? std::string_view::npos
                              : version.find('.', first_dot + 1U);
  if (first_dot == std::string_view::npos ||
      second_dot == std::string_view::npos)
    throw BcifError("BinaryCIF version must use major.minor.patch syntax");
  unsigned major{};
  unsigned minor{};
  const auto major_result =
      std::from_chars(version.data(), version.data() + first_dot, major);
  const auto minor_result = std::from_chars(version.data() + first_dot + 1U,
                                            version.data() + second_dot, minor);
  unsigned patch{};
  const auto patch_result = std::from_chars(
      version.data() + second_dot + 1U, version.data() + version.size(), patch);
  if (major_result.ec != std::errc{} || minor_result.ec != std::errc{} ||
      patch_result.ec != std::errc{} ||
      major_result.ptr != version.data() + first_dot ||
      minor_result.ptr != version.data() + second_dot ||
      patch_result.ptr != version.data() + version.size()) {
    throw BcifError(
        "BinaryCIF version must use numeric major.minor.patch syntax");
  }
  return {major, minor};
}

} // namespace

Result<StructureDocument> read_bcif(std::string_view content,
                                    std::string source_name) {
  if (content.empty()) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, 1, "BinaryCIF input is empty", "provide a .bcif file"));
  }
  try {
    std::size_t offset{};
    const msgpack::unpack_limit limits{
        1'000'000U, 1'000'000U, content.size(), content.size(), 0U, 64U};
    const auto unpacked = msgpack::unpack(content.data(), content.size(),
                                          offset, nullptr, nullptr, limits);
    if (offset != content.size()) {
      throw BcifError(
          "BinaryCIF contains trailing bytes after its MessagePack object");
    }
    const auto &root = unpacked.get();
    const auto version = as_string(required(root, "version"), "File.version");
    const auto [major, minor] = parse_version(version);
    if (major != 0U || minor != 3U) {
      throw BcifError("unsupported BinaryCIF version " + version +
                      "; this reader supports the 0.3.x schema");
    }
    const auto encoder = as_string(required(root, "encoder"), "File.encoder");
    auto blocks = decode_blocks(root);
    auto document = build_cif_document(
        blocks, source_name, StructureFormat::bcif, "BinaryCIF " + version);
    if (!document.has_value())
      return document;
    for (auto &structure : document.value().structures) {
      structure.metadata["bcif.version"] = version;
      structure.metadata["bcif.encoder"] = encoder;
    }
    return document;
  } catch (const BcifError &error) {
    return Result<StructureDocument>::failure(
        parse_error(source_name, 1, error.what(),
                    "verify the BinaryCIF 0.3 encoding and column shapes"));
  } catch (const std::exception &error) {
    return Result<StructureDocument>::failure(parse_error(
        source_name, 1,
        "invalid BinaryCIF MessagePack payload: " + std::string{error.what()},
        "verify that the file is an uncompressed .bcif document"));
  }
}

} // namespace molshredder::io::detail
