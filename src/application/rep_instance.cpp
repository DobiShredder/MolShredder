#include "molshredder/application/rep_instance.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <variant>

#include "molshredder/analysis/basic.hpp"

namespace molshredder::application {
namespace {

using render::RepresentationKind;

std::string lowered(std::string_view text) {
  std::string out{text};
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

struct StyleEntry {
  std::string_view name;
  bool renderable;
  std::array<RepresentationKind, 2> kinds;
  std::size_t kind_count;
};

constexpr std::array<StyleEntry, 27> kStyles{{
    {"Lines", true, {RepresentationKind::lines, RepresentationKind::lines}, 1},
    {"Bonds", true, {RepresentationKind::sticks, RepresentationKind::sticks}, 1},
    {"DynamicBonds", true, {RepresentationKind::lines, RepresentationKind::lines}, 1},
    {"HBonds", false, {}, 0},
    {"Points", false, {}, 0},
    {"VDW", true, {RepresentationKind::spheres, RepresentationKind::spheres}, 1},
    {"CPK", true, {RepresentationKind::spheres, RepresentationKind::sticks}, 2},
    {"Licorice", true, {RepresentationKind::sticks, RepresentationKind::sticks}, 1},
    {"Polyhedra", false, {}, 0},
    {"Trace", true, {RepresentationKind::ribbon, RepresentationKind::ribbon}, 1},
    {"Tube", true, {RepresentationKind::ribbon, RepresentationKind::ribbon}, 1},
    {"Ribbons", true, {RepresentationKind::ribbon, RepresentationKind::ribbon}, 1},
    {"NewRibbons", true, {RepresentationKind::ribbon, RepresentationKind::ribbon}, 1},
    {"Cartoon", true, {RepresentationKind::cartoon, RepresentationKind::cartoon}, 1},
    {"NewCartoon", true, {RepresentationKind::cartoon, RepresentationKind::cartoon}, 1},
    {"PaperChain", false, {}, 0},
    {"Twister", false, {}, 0},
    {"QuickSurf", false, {}, 0},
    {"MSMS", false, {}, 0},
    {"NanoShaper", false, {}, 0},
    {"Surf", false, {}, 0},
    {"VolumeSlice", false, {}, 0},
    {"Isosurface", false, {}, 0},
    {"FieldLines", false, {}, 0},
    {"Orbital", false, {}, 0},
    {"Beads", false, {}, 0},
    {"Dotted", false, {}, 0},
}};

constexpr std::array<std::string_view, 32> kColorings{
    "Name", "Type", "Element", "ResName", "ResType", "ResID", "Chain", "SegName", "Conformation",
    "Molecule", "Structure", "ColorID", "Beta", "Occupancy", "Mass", "Charge", "Pos", "PosX",
    "PosY", "PosZ", "User", "User2", "User3", "User4", "Fragment", "Index", "Backbone", "Throb",
    "PhysicalTime", "Timestep", "Velocity", "Volume"};

constexpr std::array<std::string_view, 22> kMaterials{
    "Opaque", "Transparent", "BrushedMetal", "Diffuse", "Ghost", "Glass1", "Glass2", "Glass3",
    "Glossy", "HardPlastic", "MetallicPastel", "Steel", "Translucent", "Edgy", "EdgyShiny",
    "EdgyGlass", "Goodsell", "AOShiny", "AOChalky", "AOEdgy", "BlownGlass", "GlassBubble"};

// Methods MolShredder can color atoms by; the others keep the molecule color.
constexpr std::array<std::string_view, 22> kRenderableColorings{
    "Name", "Type", "Element", "ResName", "ResType", "ResID", "Chain", "SegName", "Conformation",
    "Molecule", "Structure", "ColorID", "Beta", "Occupancy", "Mass", "Charge", "Pos", "PosX",
    "PosY", "PosZ", "Fragment", "Index"};

// Approximate RGB of the 33 VMD color ids (names follow the public User's
// Guide; the values are MolShredder's own approximations).
constexpr std::array<std::array<float, 3>, 33> kColorIds{{
    {0.00F, 0.00F, 1.00F}, {1.00F, 0.00F, 0.00F}, {0.35F, 0.35F, 0.35F}, {1.00F, 0.50F, 0.00F},
    {1.00F, 1.00F, 0.00F}, {0.80F, 0.60F, 0.40F}, {0.60F, 0.60F, 0.60F}, {0.00F, 1.00F, 0.00F},
    {1.00F, 1.00F, 1.00F}, {1.00F, 0.60F, 0.60F}, {0.25F, 0.75F, 0.75F}, {0.65F, 0.00F, 0.65F},
    {0.50F, 0.90F, 0.40F}, {1.00F, 0.40F, 0.65F}, {0.50F, 0.50F, 0.20F}, {0.00F, 0.50F, 1.00F},
    {0.00F, 0.00F, 0.00F}, {0.88F, 0.97F, 0.02F}, {0.90F, 0.80F, 0.15F}, {0.00F, 0.90F, 0.50F},
    {0.00F, 0.70F, 0.30F}, {0.00F, 0.90F, 1.00F}, {0.00F, 0.70F, 0.70F}, {0.00F, 0.40F, 1.00F},
    {0.00F, 0.20F, 0.80F}, {0.60F, 0.20F, 0.80F}, {0.45F, 0.10F, 0.70F}, {0.90F, 0.00F, 0.90F},
    {0.70F, 0.00F, 0.70F}, {0.90F, 0.10F, 0.10F}, {0.70F, 0.00F, 0.00F}, {1.00F, 0.65F, 0.10F},
    {0.85F, 0.50F, 0.00F}}};

render::ColorRgba id_color(std::int32_t id) {
  const auto count = static_cast<std::int32_t>(kColorIds.size());
  const auto wrapped = ((id % count) + count) % count;
  const auto &c = kColorIds[static_cast<std::size_t>(wrapped)];
  return render::ColorRgba{c[0], c[1], c[2], 1.0F};
}

// Red-white-blue color scale (VMD default RWB) for t in [0, 1].
render::ColorRgba rwb(double t) {
  t = std::clamp(t, 0.0, 1.0);
  if (t < 0.5) {
    const auto k = static_cast<float>(t * 2.0);
    return render::ColorRgba{1.0F, k, k, 1.0F};
  }
  const auto k = static_cast<float>((1.0 - t) * 2.0);
  return render::ColorRgba{k, k, 1.0F, 1.0F};
}

std::vector<double> numeric_property(const model::Topology &topology,
                                     const model::CoordinateFrame &frame, std::string_view name) {
  const model::AtomProperty *property = nullptr;
  // Readers store the temperature factor as b_iso_or_equiv.
  const std::string alias = name == "b_factor" ? "b_iso_or_equiv" : std::string{name};
  for (const auto &candidate : {std::string{name}, alias}) {
    if (const auto found = frame.metadata().atom_properties.find(candidate);
        found != frame.metadata().atom_properties.end()) {
      property = &found->second;
      break;
    }
  }
  if (property == nullptr) {
    for (const auto &candidate : {std::string{name}, alias}) {
      property = topology.properties().find(candidate);
      if (property != nullptr) break;
    }
  }
  std::vector<double> values(topology.atom_count(), 0.0);
  if (property == nullptr) return values;
  std::visit(
      [&](const auto &column) {
        using Column = std::decay_t<decltype(column)>;
        if constexpr (std::is_same_v<Column, model::BooleanColumn>) {
          for (std::size_t i = 0; i < values.size() && i < column.values.size(); ++i)
            values[i] = column.values[i] != 0U ? 1.0 : 0.0;
        } else if constexpr (!std::is_same_v<Column, std::vector<std::string>>) {
          for (std::size_t i = 0; i < values.size() && i < column.size(); ++i)
            values[i] = static_cast<double>(column[i]);
        }
      },
      property->values);
  return values;
}

render::ColorRgba element_color(std::uint8_t atomic_number) {
  switch (atomic_number) {
    case 1: return id_color(8);    // H white
    case 6: return id_color(10);   // C cyan
    case 7: return id_color(0);    // N blue
    case 8: return id_color(1);    // O red
    case 15: return id_color(5);   // P tan
    case 16: return id_color(4);   // S yellow
    default: return id_color(14);  // other: ochre
  }
}

} // namespace

std::optional<VmdStyleInfo> find_vmd_style(std::string_view name) {
  const auto wanted = lowered(name);
  for (const auto &style : kStyles)
    if (lowered(style.name) == wanted) {
      VmdStyleInfo info;
      info.name = std::string{style.name};
      info.renderable = style.renderable;
      for (std::size_t i = 0; i < style.kind_count; ++i) info.kinds.push_back(style.kinds[i]);
      return info;
    }
  return std::nullopt;
}

std::optional<std::string> canonical_vmd_coloring(std::string_view name) {
  const auto wanted = lowered(name);
  for (const auto candidate : kColorings)
    if (lowered(candidate) == wanted) return std::string{candidate};
  return std::nullopt;
}

bool coloring_renderable(std::string_view canonical) {
  return std::find(kRenderableColorings.begin(), kRenderableColorings.end(), canonical) !=
         kRenderableColorings.end();
}

std::optional<std::string> canonical_vmd_material(std::string_view name) {
  const auto wanted = lowered(name);
  for (const auto candidate : kMaterials)
    if (lowered(candidate) == wanted) return std::string{candidate};
  return std::nullopt;
}

std::vector<std::string> vmd_style_names() {
  std::vector<std::string> out;
  for (const auto &style : kStyles) out.emplace_back(style.name);
  return out;
}
std::vector<std::string> vmd_coloring_names() {
  return {kColorings.begin(), kColorings.end()};
}
std::vector<std::string> vmd_material_names() {
  return {kMaterials.begin(), kMaterials.end()};
}

std::optional<render::ColorRgba> vmd_color_id_rgba(std::int32_t id) noexcept {
  if (id < 0 || id >= static_cast<std::int32_t>(kColorIds.size())) return std::nullopt;
  return id_color(id);
}

std::vector<render::ColorRgba> vmd_atom_colors(const RepInstance &instance,
                                               const model::Topology &topology,
                                               const model::CoordinateFrame &frame,
                                               std::int32_t molecule_color_id) {
  const auto count = topology.atom_count();
  std::vector<render::ColorRgba> colors(count, id_color(molecule_color_id));
  const auto &method = instance.color;
  const auto &atoms = topology.atoms();
  const auto &residues = topology.residues();

  const auto categorical = [&](auto key_of) {
    std::map<std::string, std::int32_t> order;
    for (std::size_t i = 0; i < count; ++i) {
      const auto key = key_of(i);
      const auto [it, inserted] = order.emplace(key, static_cast<std::int32_t>(order.size()));
      static_cast<void>(inserted);
      colors[i] = id_color(it->second % 16);
    }
  };
  const auto by_value = [&](const std::vector<double> &values) {
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (const auto v : values) {
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    if (instance.scale_range.has_value()) {
      lo = instance.scale_range->first;
      hi = instance.scale_range->second;
    }
    for (std::size_t i = 0; i < count; ++i)
      colors[i] = hi > lo ? rwb((values[i] - lo) / (hi - lo)) : rwb(0.5);
  };

  if (method == "ColorID") {
    std::fill(colors.begin(), colors.end(), id_color(instance.color_id));
  } else if (method == "Name" || method == "Type" || method == "Element") {
    for (std::size_t i = 0; i < count; ++i) colors[i] = element_color(atoms[i].atomic_number);
  } else if (method == "ResName") {
    categorical([&](std::size_t i) { return residues[atoms[i].residue.value].name; });
  } else if (method == "ResType") {
    for (std::size_t i = 0; i < count; ++i) {
      const auto polymer = residues[atoms[i].residue.value].polymer_type;
      const auto kind = residues[atoms[i].residue.value].kind;
      colors[i] = polymer == model::PolymerType::protein ? id_color(7)
                  : (polymer == model::PolymerType::dna || polymer == model::PolymerType::rna)
                      ? id_color(3)
                  : kind == model::ResidueKind::solvent ? id_color(10)
                                                         : id_color(2);
    }
  } else if (method == "ResID") {
    for (std::size_t i = 0; i < count; ++i)
      colors[i] = id_color(static_cast<std::int32_t>(residues[atoms[i].residue.value].sequence_number % 16));
  } else if (method == "Chain") {
    categorical([&](std::size_t i) { return residues[atoms[i].residue.value].chain_id; });
  } else if (method == "SegName") {
    categorical([&](std::size_t i) { return residues[atoms[i].residue.value].segment_id; });
  } else if (method == "Conformation") {
    categorical([&](std::size_t i) { return atoms[i].alternate_location; });
  } else if (method == "Fragment") {
    categorical([&](std::size_t i) {
      return residues[atoms[i].residue.value].chain_id + "/" +
             std::to_string(atoms[i].residue.value);
    });
  } else if (method == "Structure") {
    const auto *ss = topology.properties().find("ss");
    const auto *letters = ss == nullptr ? nullptr : std::get_if<std::vector<std::string>>(&ss->values);
    for (std::size_t i = 0; i < count; ++i) {
      const std::string letter = letters != nullptr && i < letters->size() ? (*letters)[i] : "";
      colors[i] = letter == "H" ? id_color(11) : letter == "S" ? id_color(4) : id_color(8);
    }
  } else if (method == "Beta") {
    by_value(numeric_property(topology, frame, "b_factor"));
  } else if (method == "Occupancy") {
    by_value(numeric_property(topology, frame, "occupancy"));
  } else if (method == "Charge") {
    by_value(numeric_property(topology, frame, "partial_charge"));
  } else if (method == "Mass") {
    auto masses = analysis::masses_from_property(topology);
    if (!masses.has_value()) masses = analysis::estimated_element_masses(topology);
    if (masses.has_value()) by_value(masses.value().values);
  } else if (method == "Index") {
    std::vector<double> values(count);
    for (std::size_t i = 0; i < count; ++i) values[i] = static_cast<double>(i);
    by_value(values);
  } else if (method == "Pos" || method == "PosX" || method == "PosY" || method == "PosZ") {
    std::vector<double> values(count, 0.0);
    model::Vec3d center{};
    std::size_t present = 0;
    std::vector<model::Vec3d> points(count);
    for (std::size_t i = 0; i < count; ++i) {
      points[i] = std::visit(
          [i](const auto &list) {
            return model::Vec3d{static_cast<double>(list[i].x), static_cast<double>(list[i].y),
                                static_cast<double>(list[i].z)};
          },
          frame.positions().values());
      if (frame.atom_present(i)) {
        center = {center.x + points[i].x, center.y + points[i].y, center.z + points[i].z};
        ++present;
      }
    }
    if (present > 0) center = {center.x / static_cast<double>(present), center.y / static_cast<double>(present),
                               center.z / static_cast<double>(present)};
    for (std::size_t i = 0; i < count; ++i) {
      if (method == "PosX") values[i] = points[i].x;
      else if (method == "PosY") values[i] = points[i].y;
      else if (method == "PosZ") values[i] = points[i].z;
      else
        values[i] = std::sqrt((points[i].x - center.x) * (points[i].x - center.x) +
                              (points[i].y - center.y) * (points[i].y - center.y) +
                              (points[i].z - center.z) * (points[i].z - center.z));
    }
    by_value(values);
  }
  return colors;
}

void apply_vmd_style_parameters(const RepInstance &instance, render::RepresentationKind kind,
                                render::RepresentationStyle &style, double &sphere_scale_out) {
  const auto param = [&](std::size_t index, double fallback) {
    return index < instance.params.size() ? instance.params[index] : fallback;
  };
  sphere_scale_out = 1.0;
  if (instance.style == "Lines" || instance.style == "DynamicBonds") {
    style.line_width_pixels = static_cast<float>(std::max(0.5, param(0, 1.0)));
  } else if (instance.style == "Bonds" || instance.style == "Licorice") {
    style.stick_radius = std::max(0.01, param(0, 0.3));
  } else if (instance.style == "VDW") {
    sphere_scale_out = std::max(0.01, param(0, 1.0));
  } else if (instance.style == "CPK") {
    if (kind == render::RepresentationKind::spheres) sphere_scale_out = 0.25 * std::max(0.01, param(0, 1.0));
    else style.stick_radius = std::max(0.01, param(1, 0.3));
  }
}

} // namespace molshredder::application
