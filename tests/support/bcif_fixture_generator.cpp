#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <msgpack.hpp>

namespace {

enum class EncodingKind {
  byte_array,
  fixed_point,
  interval,
  run_length,
  delta,
  integer_packing,
  string_array
};

struct Encoding {
  EncodingKind kind{EncodingKind::byte_array};
  std::int64_t type{};
  double factor{};
  double minimum{};
  double maximum{};
  std::uint64_t steps{};
  std::uint64_t source_size{};
  std::int64_t origin{};
  std::int64_t byte_count{};
  bool is_unsigned{};
  std::string string_data;
  std::vector<std::uint8_t> offsets;
  std::vector<Encoding> data_encoding;
  std::vector<Encoding> offset_encoding;
};

struct EncodedData {
  std::vector<std::uint8_t> data;
  std::vector<Encoding> encoding;
};

struct Column {
  std::string name;
  EncodedData data;
  std::optional<EncodedData> mask;

  Column(std::string column_name, EncodedData column_data,
         std::optional<EncodedData> column_mask = std::nullopt)
      : name(std::move(column_name)), data(std::move(column_data)),
        mask(std::move(column_mask)) {}
};

struct Category {
  std::string name;
  std::size_t row_count{};
  std::vector<Column> columns;
};

std::uint32_t packed_size(std::size_t value) {
  return static_cast<std::uint32_t>(value);
}

template <typename Integer>
void append_little(std::vector<std::uint8_t> &bytes, Integer value) {
  using Unsigned = std::make_unsigned_t<Integer>;
  const auto bits = std::bit_cast<Unsigned>(value);
  for (std::size_t index = 0; index < sizeof(Integer); ++index) {
    bytes.push_back(static_cast<std::uint8_t>(bits >> (index * 8U)));
  }
}

template <typename Float>
void append_float(std::vector<std::uint8_t> &bytes, Float value) {
  using Integer =
      std::conditional_t<sizeof(Float) == 4U, std::uint32_t, std::uint64_t>;
  const auto bits = std::bit_cast<Integer>(value);
  for (std::size_t index = 0; index < sizeof(Float); ++index) {
    bytes.push_back(static_cast<std::uint8_t>(bits >> (index * 8U)));
  }
}

Encoding byte_array(std::int64_t type) {
  Encoding result;
  result.kind = EncodingKind::byte_array;
  result.type = type;
  return result;
}

EncodedData int32_data(const std::vector<std::int32_t> &values) {
  EncodedData result;
  for (const auto value : values)
    append_little(result.data, value);
  result.encoding.push_back(byte_array(3));
  return result;
}

EncodedData float32_data(const std::vector<float> &values) {
  EncodedData result;
  for (const auto value : values)
    append_float(result.data, value);
  result.encoding.push_back(byte_array(32));
  return result;
}

EncodedData delta_fixed_data(const std::vector<double> &values, double factor) {
  std::vector<std::int32_t> scaled;
  scaled.reserve(values.size());
  for (const auto value : values)
    scaled.push_back(static_cast<std::int32_t>(value * factor));
  const auto origin = scaled.front();
  std::vector<std::int8_t> deltas;
  deltas.reserve(values.size());
  auto previous = origin;
  for (const auto value : scaled) {
    deltas.push_back(static_cast<std::int8_t>(value - previous));
    previous = value;
  }
  EncodedData result;
  for (const auto value : deltas)
    append_little(result.data, value);
  Encoding fixed;
  fixed.kind = EncodingKind::fixed_point;
  fixed.factor = factor;
  fixed.type = 33;
  Encoding delta;
  delta.kind = EncodingKind::delta;
  delta.origin = origin;
  delta.type = 3;
  result.encoding = {fixed, delta, byte_array(1)};
  return result;
}

EncodedData delta_integer_data(const std::vector<std::int32_t> &values) {
  const auto origin = values.front();
  EncodedData result;
  auto previous = origin;
  for (const auto value : values) {
    append_little(result.data, static_cast<std::int8_t>(value - previous));
    previous = value;
  }
  Encoding delta;
  delta.kind = EncodingKind::delta;
  delta.origin = origin;
  delta.type = 3;
  result.encoding = {delta, byte_array(1)};
  return result;
}

EncodedData interval_data(const std::vector<std::uint8_t> &values) {
  EncodedData result;
  result.data = values;
  Encoding interval;
  interval.kind = EncodingKind::interval;
  interval.minimum = 0.0;
  interval.maximum = 1.0;
  interval.steps = 101U;
  interval.type = 33;
  result.encoding = {interval, byte_array(4)};
  return result;
}

EncodedData run_length_data(const std::vector<std::int8_t> &pairs,
                            std::size_t source_size) {
  EncodedData result;
  for (const auto value : pairs)
    append_little(result.data, value);
  Encoding run_length;
  run_length.kind = EncodingKind::run_length;
  run_length.type = 3;
  run_length.source_size = source_size;
  result.encoding = {run_length, byte_array(1)};
  return result;
}

EncodedData packed_unsigned(std::vector<std::uint8_t> packed,
                            std::size_t source_size) {
  EncodedData result;
  result.data = std::move(packed);
  Encoding packing;
  packing.kind = EncodingKind::integer_packing;
  packing.byte_count = 1;
  packing.is_unsigned = true;
  packing.source_size = source_size;
  result.encoding = {packing, byte_array(4)};
  return result;
}

EncodedData string_data(const std::vector<std::string> &values,
                        bool packed_indices = false) {
  std::map<std::string, std::uint8_t, std::less<>> dictionary;
  std::vector<std::string> ordered;
  std::vector<std::uint8_t> indices;
  for (const auto &value : values) {
    auto found = dictionary.find(value);
    if (found == dictionary.end()) {
      const auto index = static_cast<std::uint8_t>(ordered.size());
      dictionary.emplace(value, index);
      ordered.push_back(value);
      indices.push_back(index);
    } else {
      indices.push_back(found->second);
    }
  }
  Encoding strings;
  strings.kind = EncodingKind::string_array;
  std::vector<std::int32_t> offsets{0};
  for (const auto &value : ordered) {
    strings.string_data += value;
    offsets.push_back(static_cast<std::int32_t>(strings.string_data.size()));
  }
  strings.offsets = int32_data(offsets).data;
  strings.offset_encoding = {byte_array(3)};
  if (packed_indices) {
    Encoding packing;
    packing.kind = EncodingKind::integer_packing;
    packing.byte_count = 1;
    packing.is_unsigned = true;
    packing.source_size = indices.size();
    strings.data_encoding = {packing, byte_array(4)};
  } else {
    strings.data_encoding = {byte_array(4)};
  }
  return EncodedData{std::move(indices), {std::move(strings)}};
}

EncodedData missing_string_data(std::size_t size) {
  Encoding strings;
  strings.kind = EncodingKind::string_array;
  strings.string_data = "";
  strings.offsets = int32_data({0}).data;
  strings.offset_encoding = {byte_array(3)};
  strings.data_encoding = {byte_array(1)};
  EncodedData result;
  result.data.assign(size, 0xffU);
  result.encoding = {std::move(strings)};
  return result;
}

void pack_encoding(msgpack::packer<msgpack::sbuffer> &packer,
                   const Encoding &encoding);

void pack_encoding_array(msgpack::packer<msgpack::sbuffer> &packer,
                         const std::vector<Encoding> &encodings) {
  packer.pack_array(packed_size(encodings.size()));
  for (const auto &encoding : encodings)
    pack_encoding(packer, encoding);
}

void pack_encoding(msgpack::packer<msgpack::sbuffer> &packer,
                   const Encoding &encoding) {
  const auto key = [&](std::string_view value) {
    packer.pack(std::string{value});
  };
  switch (encoding.kind) {
  case EncodingKind::byte_array:
    packer.pack_map(2U);
    key("kind");
    packer.pack(std::string{"ByteArray"});
    key("type");
    packer.pack_int64(encoding.type);
    break;
  case EncodingKind::fixed_point:
    packer.pack_map(3U);
    key("kind");
    packer.pack(std::string{"FixedPoint"});
    key("factor");
    packer.pack_double(encoding.factor);
    key("srcType");
    packer.pack_int64(encoding.type);
    break;
  case EncodingKind::interval:
    packer.pack_map(5U);
    key("kind");
    packer.pack(std::string{"IntervalQuantization"});
    key("min");
    packer.pack_double(encoding.minimum);
    key("max");
    packer.pack_double(encoding.maximum);
    key("numSteps");
    packer.pack_uint64(encoding.steps);
    key("srcType");
    packer.pack_int64(encoding.type);
    break;
  case EncodingKind::run_length:
    packer.pack_map(3U);
    key("kind");
    packer.pack(std::string{"RunLength"});
    key("srcType");
    packer.pack_int64(encoding.type);
    key("srcSize");
    packer.pack_uint64(encoding.source_size);
    break;
  case EncodingKind::delta:
    packer.pack_map(3U);
    key("kind");
    packer.pack(std::string{"Delta"});
    key("origin");
    packer.pack_int64(encoding.origin);
    key("srcType");
    packer.pack_int64(encoding.type);
    break;
  case EncodingKind::integer_packing:
    packer.pack_map(4U);
    key("kind");
    packer.pack(std::string{"IntegerPacking"});
    key("byteCount");
    packer.pack_int64(encoding.byte_count);
    key("isUnsigned");
    packer.pack(encoding.is_unsigned);
    key("srcSize");
    packer.pack_uint64(encoding.source_size);
    break;
  case EncodingKind::string_array:
    packer.pack_map(5U);
    key("kind");
    packer.pack(std::string{"StringArray"});
    key("dataEncoding");
    pack_encoding_array(packer, encoding.data_encoding);
    key("stringData");
    packer.pack(encoding.string_data);
    key("offsetEncoding");
    pack_encoding_array(packer, encoding.offset_encoding);
    key("offsets");
    packer.pack_bin(packed_size(encoding.offsets.size()));
    packer.pack_bin_body(
        reinterpret_cast<const char *>(encoding.offsets.data()),
        packed_size(encoding.offsets.size()));
    break;
  }
}

void pack_data(msgpack::packer<msgpack::sbuffer> &packer,
               const EncodedData &data) {
  packer.pack_map(2U);
  packer.pack(std::string{"encoding"});
  pack_encoding_array(packer, data.encoding);
  packer.pack(std::string{"data"});
  packer.pack_bin(packed_size(data.data.size()));
  packer.pack_bin_body(reinterpret_cast<const char *>(data.data.data()),
                       packed_size(data.data.size()));
}

void pack_column(msgpack::packer<msgpack::sbuffer> &packer,
                 const Column &column) {
  packer.pack_map(column.mask.has_value() ? 3U : 2U);
  packer.pack(std::string{"name"});
  packer.pack(column.name);
  packer.pack(std::string{"data"});
  pack_data(packer, column.data);
  if (column.mask.has_value()) {
    packer.pack(std::string{"mask"});
    pack_data(packer, *column.mask);
  }
}

std::vector<Category> categories() {
  const std::vector<std::string> atom_names{"C1", "N1", "C1", "N1"};
  std::vector<Category> result;
  result.push_back(
      Category{"_entry", 1U, {{"id", string_data({"BCIF_TEST"})}}});
  result.push_back(Category{"_cell",
                            1U,
                            {{"length_a", float32_data({10.0F})},
                             {"length_b", float32_data({11.0F})},
                             {"length_c", float32_data({12.0F})},
                             {"angle_alpha", float32_data({90.0F})},
                             {"angle_beta", float32_data({100.0F})},
                             {"angle_gamma", float32_data({90.0F})}}});
  result.push_back(
      Category{"_molshredder_test",
               1U,
               {{"packed_300", packed_unsigned({255U, 45U}, 1U)}}});
  result.push_back(
      Category{"_atom_site",
               4U,
               {{"group_PDB", string_data({"ATOM", "ATOM", "ATOM", "ATOM"})},
                {"id", delta_integer_data({1, 2, 1, 2})},
                {"type_symbol", string_data({"C", "N", "C", "N"}, true)},
                {"label_atom_id", string_data(atom_names)},
                {"label_alt_id", missing_string_data(4U),
                 run_length_data({0, 1, 2, 1, 0, 2}, 4U)},
                {"label_comp_id", string_data({"GLY", "GLY", "GLY", "GLY"})},
                {"label_asym_id", string_data({"A", "A", "A", "A"})},
                {"label_entity_id", string_data({"1", "1", "1", "1"})},
                {"label_seq_id", int32_data({1, 1, 1, 1})},
                {"auth_atom_id", string_data(atom_names)},
                {"auth_comp_id", string_data({"GLY", "GLY", "GLY", "GLY"})},
                {"auth_asym_id", string_data({"A", "A", "A", "A"})},
                {"auth_seq_id", int32_data({1, 1, 1, 1})},
                {"Cartn_x", delta_fixed_data({0.0, 1.2, 0.1, 1.3}, 100.0)},
                {"Cartn_y", delta_fixed_data({0.0, 0.0, 0.0, 0.0}, 100.0)},
                {"Cartn_z", delta_fixed_data({0.0, 0.0, 0.0, 0.0}, 100.0)},
                {"occupancy", interval_data({100U, 100U, 100U, 100U})},
                {"B_iso_or_equiv", float32_data({10.0F, 20.0F, 11.0F, 21.0F})},
                {"pdbx_formal_charge", int32_data({0, 0, 0, 0}),
                 run_length_data({0, 1, 1, 1, 0, 2}, 4U)},
                {"pdbx_PDB_model_num", run_length_data({1, 2, 2, 2}, 4U)}}});
  result.push_back(Category{"_struct_conn",
                            1U,
                            {{"ptnr1_label_asym_id", string_data({"A"})},
                             {"ptnr1_label_seq_id", int32_data({1})},
                             {"ptnr1_label_comp_id", string_data({"GLY"})},
                             {"ptnr1_label_atom_id", string_data({"C1"})},
                             {"ptnr2_label_asym_id", string_data({"A"})},
                             {"ptnr2_label_seq_id", int32_data({1})},
                             {"ptnr2_label_comp_id", string_data({"GLY"})},
                             {"ptnr2_label_atom_id", string_data({"N1"})},
                             {"pdbx_value_order", string_data({"SING"})}}});
  return result;
}

bool write_fixture(const std::filesystem::path &path, std::string version) {
  msgpack::sbuffer buffer;
  msgpack::packer<msgpack::sbuffer> packer{buffer};
  packer.pack_map(3U);
  packer.pack(std::string{"version"});
  packer.pack(version);
  packer.pack(std::string{"encoder"});
  packer.pack(std::string{"MolShredder synthetic fixture"});
  packer.pack(std::string{"dataBlocks"});
  packer.pack_array(1U);
  packer.pack_map(2U);
  packer.pack(std::string{"header"});
  packer.pack(std::string{"BCIF_BLOCK"});
  packer.pack(std::string{"categories"});
  const auto source_categories = categories();
  packer.pack_array(packed_size(source_categories.size()));
  for (const auto &category : source_categories) {
    packer.pack_map(3U);
    packer.pack(std::string{"name"});
    packer.pack(category.name);
    packer.pack(std::string{"rowCount"});
    packer.pack_uint64(category.row_count);
    packer.pack(std::string{"columns"});
    packer.pack_array(packed_size(category.columns.size()));
    for (const auto &column : category.columns)
      pack_column(packer, column);
  }
  std::ofstream stream{path, std::ios::binary | std::ios::trunc};
  stream.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  return stream.good();
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  return write_fixture(argv[1], "0.3.0") && write_fixture(argv[2], "0.2.0") ? 0
                                                                            : 1;
}
