#include "volume_reader_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/core/parse_number.hpp"

// Readers for the X-PLOR/CNS, BRIX/DSN6, Delphi PHI, GRD, AVS field and ACNT map
// formats. Implemented from the public format descriptions and checked against
// Open-Source PyMOL 3.1.0 (grid shape, origin, spacing and values).
namespace molshredder::io::detail {
namespace {

using operation::Result;

constexpr std::size_t kMaximumPoints = 256U * 1024U * 1024U / sizeof(float);

operation::Error bad(std::string format, std::string message, std::string suggestion = {}) {
  return operation::Error{operation::ErrorCode::invalid_argument, std::move(format) + ": " + std::move(message),
                          std::move(suggestion)};
}

constexpr double kPi = 3.14159265358979323846;

struct Cell {
  double a{1.0}, b{1.0}, c{1.0};
  double alpha{90.0}, beta{90.0}, gamma{90.0};
};

// Columns of the fractional-to-Cartesian matrix (PDB convention: a along x,
// b in the xy plane).
std::optional<std::array<model::Vec3d, 3>> cell_vectors(const Cell &cell) {
  if (!(cell.a > 0.0 && cell.b > 0.0 && cell.c > 0.0)) return std::nullopt;
  const auto radians = [](double degrees) { return degrees * kPi / 180.0; };
  const auto ca = std::cos(radians(cell.alpha));
  const auto cb = std::cos(radians(cell.beta));
  const auto cg = std::cos(radians(cell.gamma));
  const auto sg = std::sin(radians(cell.gamma));
  if (!(std::abs(sg) > 1.0e-9)) return std::nullopt;
  const auto cx = cb;
  const auto cy = (ca - cb * cg) / sg;
  const auto cz2 = 1.0 - cx * cx - cy * cy;
  if (!(cz2 > 0.0)) return std::nullopt;
  return std::array<model::Vec3d, 3>{model::Vec3d{cell.a, 0.0, 0.0}, model::Vec3d{cell.b * cg, cell.b * sg, 0.0},
                                     model::Vec3d{cell.c * cx, cell.c * cy, cell.c * std::sqrt(cz2)}};
}

model::Vec3d combine(const std::array<model::Vec3d, 3> &v, double fa, double fb, double fc) {
  return {v[0].x * fa + v[1].x * fb + v[2].x * fc, v[0].y * fa + v[1].y * fb + v[2].y * fc,
          v[0].z * fa + v[1].z * fb + v[2].z * fc};
}

Result<VolumeDocument> finish(VolumeFormat format, const VolumeReadOptions &options, std::string default_name,
                              model::VolumeShape shape, model::Vec3d origin, std::array<model::Vec3d, 3> deltas,
                              std::vector<float> values, std::map<std::string, std::string, std::less<>> fields) {
  for (const auto value : values)
    if (!std::isfinite(value)) return Result<VolumeDocument>::failure(bad(default_name, "the map contains a non-finite value"));
  model::VolumeMetadata metadata;
  metadata.coordinate_unit = options.coordinate_unit;
  metadata.fields = std::move(fields);
  const auto grid = model::VolumeGrid::create(shape, origin, deltas, model::VolumeScalarBuffer{std::move(values)},
                                              std::move(metadata));
  if (!grid.has_value()) return Result<VolumeDocument>::failure(grid.error());
  return Result<VolumeDocument>::success(
      VolumeDocument{format, {VolumeData{options.name.value_or(default_name), grid.value()}}});
}

// A grid defined on fractional divisions of a unit cell (X-PLOR, BRIX).
Result<VolumeDocument> crystal_grid(VolumeFormat format, std::string label, const VolumeReadOptions &options,
                                    std::array<long, 3> division, std::array<long, 3> minimum,
                                    std::array<std::size_t, 3> size, const Cell &cell, std::vector<float> values,
                                    std::map<std::string, std::string, std::less<>> fields) {
  const auto vectors = cell_vectors(cell);
  if (!vectors.has_value()) return Result<VolumeDocument>::failure(bad(label, "the unit cell is invalid"));
  for (const auto d : division)
    if (d <= 0) return Result<VolumeDocument>::failure(bad(label, "grid divisions must be positive"));
  const auto origin = combine(*vectors, static_cast<double>(minimum[0]) / static_cast<double>(division[0]),
                              static_cast<double>(minimum[1]) / static_cast<double>(division[1]),
                              static_cast<double>(minimum[2]) / static_cast<double>(division[2]));
  const std::array<model::Vec3d, 3> deltas{
      model::Vec3d{(*vectors)[0].x / static_cast<double>(division[0]), (*vectors)[0].y / static_cast<double>(division[0]),
                   (*vectors)[0].z / static_cast<double>(division[0])},
      model::Vec3d{(*vectors)[1].x / static_cast<double>(division[1]), (*vectors)[1].y / static_cast<double>(division[1]),
                   (*vectors)[1].z / static_cast<double>(division[1])},
      model::Vec3d{(*vectors)[2].x / static_cast<double>(division[2]), (*vectors)[2].y / static_cast<double>(division[2]),
                   (*vectors)[2].z / static_cast<double>(division[2])}};
  fields.emplace("cell_lengths_angstrom", std::to_string(cell.a) + " " + std::to_string(cell.b) + " " + std::to_string(cell.c));
  fields.emplace("cell_angles_degrees",
                 std::to_string(cell.alpha) + " " + std::to_string(cell.beta) + " " + std::to_string(cell.gamma));
  return finish(format, options, std::move(label), {size[0], size[1], size[2]}, origin, deltas, std::move(values),
                std::move(fields));
}

std::size_t checked_points(const std::array<std::size_t, 3> &size, std::string_view label) {
  if (size[0] == 0U || size[1] == 0U || size[2] == 0U) throw std::runtime_error(std::string{label} + ": an empty grid dimension");
  if (size[0] > kMaximumPoints || size[1] > kMaximumPoints || size[2] > kMaximumPoints)
    throw std::runtime_error(std::string{label} + ": grid dimension is beyond the supported size");
  const auto points = size[0] * size[1] * size[2];
  if (points / size[0] / size[1] != size[2] || points > kMaximumPoints)
    throw std::runtime_error(std::string{label} + ": the grid is larger than the supported 256 MiB");
  return points;
}

// VolumeGrid stores z fastest: ((x * ny) + y) * nz + z.
std::size_t at(const std::array<std::size_t, 3> &size, std::size_t x, std::size_t y, std::size_t z) {
  return (x * size[1] + y) * size[2] + z;
}

// -------- text scanning helpers

struct Scanner {
  std::string_view text;
  std::size_t position{};

  bool at_end() const { return position >= text.size(); }
  void skip_space() {
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0) ++position;
  }
  std::string_view line() {
    const auto end = text.find('\n', position);
    auto result = text.substr(position, end == std::string_view::npos ? text.size() - position : end - position);
    position = end == std::string_view::npos ? text.size() : end + 1U;
    if (!result.empty() && result.back() == '\r') result.remove_suffix(1U);
    return result;
  }
  std::optional<std::string_view> word() {
    skip_space();
    const auto start = position;
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) == 0) ++position;
    if (start == position) return std::nullopt;
    return text.substr(start, position - start);
  }
};

template <typename T>
std::optional<T> parse(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1U);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1U);
  T value{};
  const auto result = molshredder::core::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()) return std::nullopt;
  return value;
}

template <typename T>
T need(std::optional<T> value, std::string_view what) {
  if (!value.has_value()) throw std::runtime_error(std::string{"cannot read "} + std::string{what});
  return *value;
}

std::string lowered(std::string_view text) {
  std::string out{text};
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

// -------- binary helpers

template <typename T>
T load(const unsigned char *data, bool swap) {
  std::array<unsigned char, sizeof(T)> bytes{};
  std::memcpy(bytes.data(), data, sizeof(T));
  if (swap) std::reverse(bytes.begin(), bytes.end());
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  return value;
}

} // namespace

// ===================================================================== X-PLOR / CNS

Result<VolumeDocument> read_xplor(std::string_view content, VolumeReadOptions options) {
  try {
    Scanner in{content};
    // Title block: "<n> !NTITLE" followed by n title lines; REMARKS lines are free text.
    std::string title;
    long first_count = 0;
    bool have_grid = false;
    while (!in.at_end()) {
      const auto line = in.line();
      auto trimmed = std::string{line};
      if (trimmed.find_first_not_of(" \t") == std::string::npos) continue;
      if (trimmed.find("REMARKS") != std::string::npos) continue;
      if (trimmed.find("!NTITLE") != std::string::npos) {
        const auto n = need(parse<long>(trimmed.substr(0, trimmed.find("!NTITLE"))), "the title line count");
        for (long i = 0; i < n && !in.at_end(); ++i) title += std::string{in.line()} + (i + 1 < n ? "\n" : "");
        continue;
      }
      // grid line: nine integers in 8-column fields
      const auto fields = [&](std::string_view l) {
        std::vector<long> numbers;
        for (std::size_t offset = 0; offset + 8U <= l.size() + 7U && offset < l.size(); offset += 8U)
          if (const auto v = parse<long>(l.substr(offset, 8U)); v.has_value()) numbers.push_back(*v);
        return numbers;
      }(line);
      if (fields.size() != 9U) throw std::runtime_error("expected the grid line (nine integers) after the title");
      first_count = fields[0];
      have_grid = true;
      std::array<long, 3> division{fields[0], fields[3], fields[6]};
      std::array<long, 3> minimum{fields[1], fields[4], fields[7]};
      std::array<long, 3> maximum{fields[2], fields[5], fields[8]};
      (void)first_count;
      std::array<std::size_t, 3> size{};
      for (int i = 0; i < 3; ++i) {
        if (maximum[static_cast<std::size_t>(i)] < minimum[static_cast<std::size_t>(i)])
          throw std::runtime_error("a grid axis has max below min");
        size[static_cast<std::size_t>(i)] =
            static_cast<std::size_t>(maximum[static_cast<std::size_t>(i)] - minimum[static_cast<std::size_t>(i)] + 1);
      }
      const auto points = checked_points(size, "X-PLOR");
      // cell line: six numbers in 12-column fields
      const auto cell_line = in.line();
      std::array<double, 6> cell{};
      for (std::size_t i = 0; i < 6U; ++i) {
        if (cell_line.size() < 12U * i + 1U) throw std::runtime_error("the cell line is too short");
        cell[i] = need(parse<double>(cell_line.substr(12U * i, 12U)), "a cell parameter");
      }
      const auto order = std::string{in.line()};
      if (order.find("ZYX") == std::string::npos) throw std::runtime_error("only ZYX section order is supported");
      // sections: an index line, then the values 6 per line in 12-column fields
      std::vector<float> values(points);
      for (std::size_t c = 0; c < size[2]; ++c) {
        if (in.at_end()) throw std::runtime_error("the map ends before all sections are read");
        in.line();  // section number
        std::size_t read = 0;
        const auto per_section = size[0] * size[1];
        while (read < per_section) {
          if (in.at_end()) throw std::runtime_error("the map ends inside a section");
          const auto row = in.line();
          for (std::size_t offset = 0; offset + 1U <= row.size() && read < per_section; offset += 12U) {
            const auto v = need(parse<double>(row.substr(offset, 12U)), "a density value");
            // within a section the first axis varies fastest
            values[at(size, read % size[0], read / size[0], c)] = static_cast<float>(v);
            ++read;
          }
        }
      }
      std::map<std::string, std::string, std::less<>> fields_out{{"format", "xplor"}};
      if (!title.empty()) fields_out.emplace("title", title);
      return crystal_grid(VolumeFormat::xplor, "X-PLOR map", options, division, minimum, size,
                          Cell{cell[0], cell[1], cell[2], cell[3], cell[4], cell[5]}, std::move(values),
                          std::move(fields_out));
    }
    (void)have_grid;
    throw std::runtime_error("no grid definition found");
  } catch (const std::runtime_error &error) {
    return Result<VolumeDocument>::failure(bad("X-PLOR map", error.what()));
  }
}

// ===================================================================== BRIX / DSN6

Result<VolumeDocument> read_brix(std::string_view content, VolumeReadOptions options) {
  try {
    std::array<long, 3> minimum{}, extent{}, division{};
    Cell cell;
    double prod = 1.0, plus = 0.0;
    std::optional<double> sigma;
    const auto *data = reinterpret_cast<const unsigned char *>(content.data());
    if (content.size() < 512U) throw std::runtime_error("the header is shorter than 512 bytes");
    std::vector<unsigned char> body;
    if (content.substr(0, 3) == ":-)") {
      const auto header = content.substr(0, 512);
      Scanner in{header};
      bool got_origin = false, got_extent = false, got_grid = false, got_cell = false, got_prod = false,
           got_plus = false;
      while (!in.at_end()) {
        const auto word = in.word();
        if (!word.has_value()) break;
        const auto key = lowered(*word);
        const auto next_long = [&]() { return need(parse<long>(in.word().value_or("")), "a header integer"); };
        const auto next_double = [&]() { return need(parse<double>(in.word().value_or("")), "a header number"); };
        if (key == "origin") { for (auto &v : minimum) v = next_long(); got_origin = true; }
        else if (key == "extent") { for (auto &v : extent) v = next_long(); got_extent = true; }
        else if (key == "grid") { for (auto &v : division) v = next_long(); got_grid = true; }
        else if (key == "cell") {
          std::array<double, 6> v{};
          for (auto &x : v) x = next_double();
          cell = {v[0], v[1], v[2], v[3], v[4], v[5]};
          got_cell = true;
        } else if (key == "prod") { prod = next_double(); got_prod = true; }
        else if (key == "plus") { plus = next_double(); got_plus = true; }
        else if (key == "sigma") { sigma = next_double(); }
      }
      if (!(got_origin && got_extent && got_grid && got_cell && got_prod && got_plus))
        throw std::runtime_error("the ASCII header is missing origin, extent, grid, cell, prod or plus");
    } else {
      // DSN6: 256 16-bit header words; the check word (index 18) is 100 in the file's own byte order.
      const auto word_at = [&](bool swap, std::size_t index) { return load<std::int16_t>(data + 2U * index, swap); };
      bool swap = false;
      if (word_at(false, 18) != 100) {
        if (word_at(true, 18) != 100) throw std::runtime_error("this looks like a DSN6 map but the header is not recognized");
        swap = true;
      }
      std::size_t w = 0;
      for (auto &v : minimum) v = word_at(swap, w++);
      for (auto &v : extent) v = word_at(swap, w++);
      for (auto &v : division) v = word_at(swap, w++);
      std::array<double, 6> raw{};
      for (auto &v : raw) v = word_at(swap, w++);
      prod = word_at(swap, w++) / 100.0;
      plus = word_at(swap, w++);
      const double scale = word_at(swap, w++);
      if (scale == 0.0) throw std::runtime_error("the DSN6 cell scale factor is zero");
      cell = {raw[0] / scale, raw[1] / scale, raw[2] / scale, raw[3] / scale, raw[4] / scale, raw[5] / scale};
      // The bricks are stored as byte pairs swapped relative to the header's convention.
      body.assign(data + 512, data + content.size());
      for (std::size_t i = 0; i + 1U < body.size(); i += 2U) std::swap(body[i], body[i + 1U]);
    }
    if (content.substr(0, 3) == ":-)") body.assign(data + 512, data + content.size());
    std::array<std::size_t, 3> size{};
    for (std::size_t i = 0; i < 3U; ++i) {
      if (extent[i] <= 0) throw std::runtime_error("an extent is not positive");
      size[i] = static_cast<std::size_t>(extent[i]);
    }
    const auto points = checked_points(size, "BRIX");
    if (prod == 0.0) throw std::runtime_error("prod is zero");
    constexpr std::size_t block = 8U;
    const auto bx = (size[0] - 1U) / block + 1U, by = (size[1] - 1U) / block + 1U, bz = (size[2] - 1U) / block + 1U;
    if (body.size() < bx * by * bz * block * block * block)
      throw std::runtime_error("the map ends before all 8x8x8 bricks are read");
    std::vector<float> values(points, 0.0F);
    std::size_t cursor = 0;
    double sum = 0.0, sum_squares = 0.0;
    for (std::size_t cz = 0; cz < bz; ++cz)
      for (std::size_t cy = 0; cy < by; ++cy)
        for (std::size_t cx = 0; cx < bx; ++cx)
          for (std::size_t c = 0; c < block; ++c)
            for (std::size_t b = 0; b < block; ++b)
              for (std::size_t a = 0; a < block; ++a) {
                const auto x = cx * block + a, y = cy * block + b, z = cz * block + c;
                const auto density = (static_cast<double>(body[cursor++]) - plus) / prod;
                if (x < size[0] && y < size[1] && z < size[2]) {
                  values[at(size, x, y, z)] = static_cast<float>(density);
                  sum += density;
                  sum_squares += density * density;
                }
              }
    const auto n = static_cast<double>(points);
    const double mean = n > 0.0 ? sum / n : 0.0;
    const double calc_sigma = points > 1U ? std::sqrt(std::max(0.0, (sum_squares - sum * sum / n) / (n - 1.0))) : 0.0;
    std::map<std::string, std::string, std::less<>> fields{{"format", "brix"},
                                                           {"brix_prod", std::to_string(prod)},
                                                           {"brix_plus", std::to_string(plus)},
                                                           {"raw_mean", std::to_string(mean)},
                                                           {"raw_sigma", std::to_string(calc_sigma)}};
    if (sigma.has_value()) fields.emplace("reported_sigma", std::to_string(*sigma));
    // O-style maps are shown in sigma units (PyMOL normalize_o_maps, default on).
    if (calc_sigma > 1.0e-8) {
      for (auto &v : values) v = static_cast<float>((static_cast<double>(v) - mean) / calc_sigma);
      fields.emplace("normalized", "mean0_sigma1");
    }
    return crystal_grid(VolumeFormat::brix, "BRIX map", options, division, minimum, size, cell, std::move(values),
                        std::move(fields));
  } catch (const std::runtime_error &error) {
    return Result<VolumeDocument>::failure(bad("BRIX/DSN6 map", error.what()));
  }
}

// ===================================================================== Delphi PHI

Result<VolumeDocument> read_phi(std::string_view content, VolumeReadOptions options) {
  try {
    const auto *data = reinterpret_cast<const unsigned char *>(content.data());
    // The first Fortran record marker (20) decides the byte order.
    if (content.size() < 4U + 20U + 4U + 4U + 70U + 4U + 4U) throw std::runtime_error("the file is too short");
    bool swap = false;
    if (load<std::uint32_t>(data, false) == 20U) swap = false;
    else if (load<std::uint32_t>(data, true) == 20U) swap = true;
    else throw std::runtime_error("the first record marker is not 20; not an unformatted Delphi PHI map");
    std::size_t p = 4U + 20U + 4U;  // "uplbl" record
    p += 4U;                         // start of the title record
    const std::string label{reinterpret_cast<const char *>(data + p), 10U};
    const std::string title{reinterpret_cast<const char *>(data + p + 10U), 60U};
    p += 70U + 4U;                  // title + record end
    const auto map_bytes = load<std::uint32_t>(data + p, swap);
    p += 4U;
    auto dim = static_cast<std::size_t>(std::llround(std::cbrt(static_cast<double>(map_bytes) / 4.0)));
    if (4U * dim * dim * dim != map_bytes) throw std::runtime_error("the data record is not a cube of float32 values");
    if (dim < 3U || dim % 2U == 0U) throw std::runtime_error("the grid size must be odd and at least 3");
    const std::array<std::size_t, 3> size{dim, dim, dim};
    const auto points = checked_points(size, "PHI");
    if (content.size() < p + map_bytes + 4U + 4U + 16U + 4U + 4U + 16U + 4U)
      throw std::runtime_error("the file ends before the trailer");
    std::vector<float> values(points);
    for (std::size_t i = 0; i < points; ++i)
      values[at(size, i % dim, (i / dim) % dim, i / (dim * dim))] = load<float>(data + p + 4U * i, swap);
    p += map_bytes + 4U;            // data + record end
    p += 4U + 16U + 4U;             // "botlbl" record
    p += 4U;                        // start of the geometry record
    const auto scale = load<float>(data + p, swap);
    const model::Vec3d center{load<float>(data + p + 4U, swap), load<float>(data + p + 8U, swap),
                              load<float>(data + p + 12U, swap)};
    if (!(scale > 0.0F) || !std::isfinite(scale)) throw std::runtime_error("the grid scale is not positive");
    const double spacing = 1.0 / static_cast<double>(scale);
    const double half = static_cast<double>((dim - 1U) / 2U);
    const model::Vec3d origin{center.x - spacing * half, center.y - spacing * half, center.z - spacing * half};
    const std::array<model::Vec3d, 3> deltas{model::Vec3d{spacing, 0.0, 0.0}, model::Vec3d{0.0, spacing, 0.0},
                                             model::Vec3d{0.0, 0.0, spacing}};
    return finish(VolumeFormat::phi, options, "PHI map", {dim, dim, dim}, origin, deltas, std::move(values),
                  {{"format", "phi"}, {"phi_title", title}, {"phi_label", label},
                   {"phi_byte_order", swap ? "swapped" : "native"}});
  } catch (const std::runtime_error &error) {
    return Result<VolumeDocument>::failure(bad("PHI map", error.what()));
  }
}

// ===================================================================== GRD (ASCII)

Result<VolumeDocument> read_grd(std::string_view content, VolumeReadOptions options) {
  try {
    if (content.size() >= 4U && (content[0] == '\0' || content[1] == '\0' || content[2] == '\0' || content[3] == '\0'))
      throw std::runtime_error("binary GRD files are not supported; export an ASCII GRD map");
    Scanner in{content};
    const std::string title{in.line()};
    in.line();  // format line
    // cell
    std::array<double, 6> cell{};
    for (auto &v : cell) v = need(parse<double>(in.word().value_or("")), "a cell parameter");
    in.line();
    std::array<long, 3> brick{};
    for (auto &v : brick) v = need(parse<long>(in.word().value_or("")), "a grid dimension");
    in.line();
    const auto fast = need(parse<long>(in.word().value_or("")), "the fast axis");
    std::array<long, 3> minimum{}, division{};
    for (std::size_t i = 0; i < 3U; ++i) {
      minimum[i] = need(parse<long>(in.word().value_or("")), "a grid minimum");
      division[i] = need(parse<long>(in.word().value_or("")), "a grid maximum");
    }
    if (fast != 1) throw std::runtime_error("only the fast-X value order is supported");
    std::array<std::size_t, 3> size{};
    for (std::size_t i = 0; i < 3U; ++i) {
      if (brick[i] < 0) throw std::runtime_error("negative grid dimension");
      size[i] = static_cast<std::size_t>(brick[i] + 1);
      division[i] -= minimum[i];
    }
    const auto points = checked_points(size, "GRD");
    in.line();
    std::vector<float> values(points);
    for (std::size_t i = 0; i < points; ++i) {
      const auto line = in.line();
      values[at(size, i % size[0], (i / size[0]) % size[1], i / (size[0] * size[1]))] =
          static_cast<float>(need(parse<double>(line), "a density value"));
    }
    return crystal_grid(VolumeFormat::grd, "GRD map", options, division, minimum, size,
                        Cell{cell[0], cell[1], cell[2], cell[3], cell[4], cell[5]}, std::move(values),
                        {{"format", "grd"}, {"title", title}});
  } catch (const std::runtime_error &error) {
    return Result<VolumeDocument>::failure(bad("GRD map", error.what()));
  }
}

// ===================================================================== AVS field (FLD)

Result<VolumeDocument> read_fld(std::string_view content, VolumeReadOptions options) {
  try {
    const auto sentinel = content.find("\x0c\x0c");
    if (sentinel == std::string_view::npos) throw std::runtime_error("the ^L^L header terminator is missing");
    std::map<std::string, std::string> keys;
    Scanner header{content.substr(0, sentinel)};
    while (!header.at_end()) {
      const auto line = std::string{header.line()};
      if (line.empty() || line[0] == '#') continue;
      const auto equals = line.find('=');
      if (equals == std::string::npos) continue;
      auto key = lowered(line.substr(0, equals));
      key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c) != 0; }), key.end());
      keys[key] = line.substr(equals + 1U);
    }
    const auto number = [&](const char *key) {
      const auto found = keys.find(key);
      if (found == keys.end()) throw std::runtime_error(std::string{"missing header key "} + key);
      return need(parse<long>(found->second.substr(0, found->second.find('#'))), key);
    };
    if (number("ndim") != 3) throw std::runtime_error("only 3-dimensional fields are supported");
    if (number("veclen") != 1) throw std::runtime_error("only scalar fields (veclen=1) are supported");
    if (keys.count("data") == 0U || lowered(keys["data"]).find("float") == std::string::npos)
      throw std::runtime_error("only float data is supported");
    if (keys.count("field") == 0U || lowered(keys["field"]).find("uniform") == std::string::npos)
      throw std::runtime_error("only uniform fields are supported");
    const std::array<long, 3> dims{number("dim1"), number("dim2"), number("dim3")};
    std::array<std::size_t, 3> size{};
    for (std::size_t i = 0; i < 3U; ++i) {
      if (dims[i] < 2) throw std::runtime_error("each dimension needs at least two points");
      size[i] = static_cast<std::size_t>(dims[i]);
    }
    const auto points = checked_points(size, "FLD");
    const auto triple = [&](const char *key) {
      const auto found = keys.find(key);
      if (found == keys.end()) throw std::runtime_error(std::string{"missing header key "} + key);
      Scanner s{found->second};
      return model::Vec3d{need(parse<double>(s.word().value_or("")), key), need(parse<double>(s.word().value_or("")), key),
                          need(parse<double>(s.word().value_or("")), key)};
    };
    const auto low = triple("min_ext");
    const auto high = triple("max_ext");
    const auto body = content.substr(sentinel + 2U);
    if (body.size() < points * 4U) throw std::runtime_error("the file ends before all values are read");
    const auto *bytes = reinterpret_cast<const unsigned char *>(body.data());
    // Byte order is not recorded; try native order first and fall back to swapped (as PyMOL does).
    for (const bool swap : {false, true}) {
      std::vector<float> values(points);
      bool finite = true;
      double peak = 0.0;
      for (std::size_t i = 0; i < points && finite; ++i) {
        const auto v = load<float>(bytes + 4U * i, swap);
        values[at(size, i % size[0], (i / size[0]) % size[1], i / (size[0] * size[1]))] = v;
        finite = std::isfinite(v);
        peak = std::max(peak, std::abs(static_cast<double>(v)));
      }
      if (!finite || peak > 1.0e30) continue;
      const std::array<model::Vec3d, 3> deltas{
          model::Vec3d{(high.x - low.x) / static_cast<double>(size[0] - 1U), 0.0, 0.0},
          model::Vec3d{0.0, (high.y - low.y) / static_cast<double>(size[1] - 1U), 0.0},
          model::Vec3d{0.0, 0.0, (high.z - low.z) / static_cast<double>(size[2] - 1U)}};
      return finish(VolumeFormat::fld, options, "AVS field", {size[0], size[1], size[2]}, low, deltas, std::move(values),
                    {{"format", "fld"}, {"fld_byte_order", swap ? "swapped" : "native"}});
    }
    throw std::runtime_error("the values are not valid float32 numbers in either byte order");
  } catch (const std::runtime_error &error) {
    return Result<VolumeDocument>::failure(bad("AVS field map", error.what()));
  }
}

// ===================================================================== ACNT

Result<VolumeDocument> read_acnt(std::string_view content, VolumeReadOptions options) {
  try {
    Scanner in{content};
    const std::string title{in.line()};
    std::array<double, 3> origin{}, spacing{};
    std::array<std::size_t, 3> size{};
    // The three header lines describe y, x, z in that order.
    constexpr std::array<std::size_t, 3> axis{1U, 0U, 2U};
    for (std::size_t line = 0; line < 3U; ++line) {
      Scanner words{in.line()};
      const auto a = axis[line];
      origin[a] = need(parse<double>(words.word().value_or("")), "an origin");
      spacing[a] = need(parse<double>(words.word().value_or("")), "a grid spacing");
      const auto n = need(parse<long>(words.word().value_or("")), "a dimension");
      if (n < 1) throw std::runtime_error("a dimension is not positive");
      size[a] = static_cast<std::size_t>(n);
    }
    in.line();  // angles (ignored)
    const auto points = checked_points(size, "ACNT");
    std::vector<float> values(points);
    // z slowest, then x, y fastest
    for (std::size_t c = 0; c < size[2]; ++c)
      for (std::size_t a = 0; a < size[0]; ++a)
        for (std::size_t b = 0; b < size[1]; ++b) {
          if (in.at_end()) throw std::runtime_error("the file ends before all values are read");
          values[at(size, a, b, c)] =
              static_cast<float>(need(parse<double>(in.line()), "a density value"));
        }
    const std::array<model::Vec3d, 3> deltas{model::Vec3d{spacing[0], 0.0, 0.0}, model::Vec3d{0.0, spacing[1], 0.0},
                                             model::Vec3d{0.0, 0.0, spacing[2]}};
    return finish(VolumeFormat::acnt, options, "ACNT map", {size[0], size[1], size[2]},
                  model::Vec3d{origin[0], origin[1], origin[2]}, deltas, std::move(values),
                  {{"format", "acnt"}, {"title", title}});
  } catch (const std::runtime_error &error) {
    return Result<VolumeDocument>::failure(bad("ACNT map", error.what()));
  }
}

} // namespace molshredder::io::detail
