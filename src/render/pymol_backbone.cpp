#include "pymol_backbone.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <utility>

namespace molshredder::render::detail {
namespace {

using V = model::Vec3d;

V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V operator*(V a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V cross(V a, V b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(V a) { return std::sqrt(dot(a, a)); }
// Unit vector, or zero for (near) zero input.
V unit(V a) {
  const auto l = length(a);
  return l > 1.0e-8 ? a * (1.0 / l) : V{};
}
V without(V v, V direction) { return v - direction * dot(v, direction); }

// Power-biased easing used for sampling along a span.
double smooth(double x, double power) {
  if (x <= 0.5) {
    if (x <= 0.0)
      return 0.0;
    return 0.5 * std::pow(2.0 * x, power);
  }
  if (x >= 1.0)
    return 1.0;
  return 1.0 - 0.5 * std::pow(2.0 * (1.0 - x), power);
}

enum Car : int {
  kSkip = -1,
  kAuto = 0,
  kLoop = 1,
  kRect = 2,
  kOval = 3,
  kTube = 4,
  kArrow = 5,
  kDumbbell = 6,
  kPutty = 7,
  kDash = 8,
  kCylinder = 9
};

// skip beats dash beats loop when neighbouring residues disagree.
int prioritize(int a, int b) {
  if (a == b)
    return a;
  if (a == kSkip || b == kSkip)
    return kSkip;
  if (a == kDash || b == kDash)
    return kDash;
  return kLoop;
}

struct Guide {
  V position;
  V orientation;
  char ss{'L'};
  int car_in{kAuto};
  int car_out{0};
  std::size_t atom{};
  model::ResidueIndex residue;
  int segment{};
  [[nodiscard]] int out() const { return car_out != 0 ? car_out : car_in; }
};

struct ResidueAtoms {
  std::optional<std::size_t> ca, n, c, o;
};

std::vector<ResidueAtoms> residue_atoms(const PymolBackboneInput &input) {
  const auto &topology = *input.topology;
  std::vector<ResidueAtoms> atoms(topology.residue_count());
  for (std::size_t index = 0; index < topology.atom_count(); ++index) {
    if (input.present[index] == 0U)
      continue;
    const auto &atom = topology.atoms()[index];
    auto &entry = atoms[atom.residue.value];
    if (atom.name == "CA" && atom.atomic_number == 6U && !entry.ca)
      entry.ca = index;
    else if (atom.name == "N" && !entry.n)
      entry.n = index;
    else if (atom.name == "C" && !entry.c)
      entry.c = index;
    else if (atom.name == "O" && !entry.o)
      entry.o = index;
  }
  return atoms;
}

// Consecutive residues are joined through a C-N peptide bond (or, without
// backbone atoms, consecutive numbering in one chain).
bool connected(const PymolBackboneInput &input, const std::vector<ResidueAtoms> &atoms,
               std::size_t previous, std::size_t current) {
  const auto &residues = input.topology->residues();
  if (current != previous + 1U ||
      residues[previous].chain_id != residues[current].chain_id)
    return false;
  const auto &c = atoms[previous].c;
  const auto &n = atoms[current].n;
  if (c && n)
    return length(input.positions[*n] - input.positions[*c]) <= 1.9 * input.unit_scale;
  return residues[current].sequence_number == residues[previous].sequence_number + 1;
}

// Guide CA atoms with segment ids, cartoon types and orientation vectors.
std::vector<Guide> cartoon_guides(const PymolBackboneInput &input,
                                  const std::vector<ResidueAtoms> &atoms,
                                  int gap_cutoff, bool fancy_helices, bool fancy_sheets) {
  const auto &residues = input.topology->residues();
  std::vector<Guide> guides;
  int segment = 0;
  int parity = 1;
  bool invisible_found = false;
  std::optional<std::size_t> last;
  for (std::size_t residue = 0; residue < atoms.size(); ++residue) {
    const auto &entry = atoms[residue];
    if (!entry.ca)
      continue;
    if (input.visible[*entry.ca] == 0U) {
      invisible_found = true;
      continue;
    }
    bool new_segment = !last.has_value();
    if (last && !connected(input, atoms, *last, residue)) {
      const auto delta =
          residues[residue].sequence_number - residues[*last].sequence_number;
      if (delta < 1 || delta > gap_cutoff ||
          residues[residue].chain_id != residues[*last].chain_id) {
        new_segment = true;
      } else {
        guides.back().car_out = invisible_found ? kSkip : kDash;
        invisible_found = false;
      }
    }
    if (new_segment)
      ++segment;
    int car = input.atom_cartoon_type.empty() ? kAuto
                                              : input.atom_cartoon_type[*entry.ca];
    const auto ss = input.residue_ss[residue];
    if (ss == 'H') {
      if (car == kAuto)
        car = fancy_helices ? kDumbbell : kOval;
      parity = 0;
    } else if (ss == 'S') {
      if (car == kAuto)
        car = fancy_sheets ? kArrow : kRect;
      parity = parity == 0 ? 1 : 0;
    } else {
      if (car == kAuto)
        car = kLoop;
      parity = 0;
    }
    V orientation{};
    if (entry.n && entry.c && entry.o) {
      const auto n = input.positions[*entry.n];
      const auto from_c = unit(n - input.positions[*entry.c]);
      const auto from_o = unit(n - input.positions[*entry.o]);
      orientation = unit(cross(from_c, from_o));
      if (parity != 0)
        orientation = orientation * -1.0;
    }
    guides.push_back(Guide{input.positions[*entry.ca], orientation,
                           ss == 'H' || ss == 'S' ? ss : 'L', car, 0, *entry.ca,
                           model::ResidueIndex{residue}, segment});
    last = residue;
  }
  return guides;
}

struct Spans {
  std::vector<V> direction; // unit difference to the next guide (same segment)
  std::vector<double> distance;
  std::vector<V> tangent;
};

Spans spans(const std::vector<Guide> &guides) {
  const auto count = guides.size();
  Spans result{std::vector<V>(count), std::vector<double>(count, 0.0),
               std::vector<V>(count)};
  for (std::size_t a = 0; a + 1U < count; ++a) {
    if (guides[a].segment != guides[a + 1U].segment)
      continue;
    const auto difference = guides[a + 1U].position - guides[a].position;
    result.distance[a] = length(difference);
    if (result.distance[a] > 1.0e-4)
      result.direction[a] = difference * (1.0 / result.distance[a]);
    else if (a > 0U)
      result.direction[a] = result.direction[a - 1U];
  }
  if (count == 0U)
    return result;
  result.tangent[0] = result.direction[0];
  for (std::size_t a = 1; a + 1U < count; ++a) {
    const bool with_previous = guides[a].segment == guides[a - 1U].segment;
    const bool with_next = guides[a].segment == guides[a + 1U].segment;
    if (with_previous && with_next)
      result.tangent[a] = unit(result.direction[a] + result.direction[a - 1U]);
    else if (with_previous)
      result.tangent[a] = result.direction[a - 1U];
    else if (with_next)
      result.tangent[a] = result.direction[a];
  }
  if (count > 1U)
    result.tangent[count - 1U] = result.direction[count - 2U];
  return result;
}

bool interior(const std::vector<Guide> &guides, std::size_t a) {
  return guides[a].segment == guides[a - 1U].segment &&
         guides[a].segment == guides[a + 1U].segment;
}

// Helix orientations point away from the local helix axis.
void round_helices(std::vector<Guide> &guides, const std::vector<V> &tangent) {
  std::array<std::optional<std::size_t>, 5> window{};
  int last = 0;
  V previous_center{};
  for (std::size_t a = 0; a < guides.size(); ++a) {
    if (a > 0U && guides[a].segment != guides[a - 1U].segment) {
      window = {};
      last = 0;
    }
    window = {std::nullopt, window[0], window[1], window[2], window[3]};
    if (guides[a].ss == 'H') {
      window[0] = a;
    } else {
      if (last < 2 && window[1] && window[2]) {
        const auto position = [&](std::size_t index) { return guides[index].position; };
        auto axis = unit(position(*window[1]) - guides[a].position) +
                    unit(position(*window[2]) - position(*window[1]));
        if (window[3])
          axis = axis + unit(position(*window[3]) - position(*window[2]));
        if (window[4])
          axis = axis + unit(position(*window[4]) - position(*window[3]));
        axis = unit(axis);
        guides[a - 1U].orientation = unit(cross(axis, tangent[a - 1U]));
        guides[a - 2U].orientation = unit(cross(axis, tangent[a - 2U]));
        if (window[3])
          guides[a - 3U].orientation = unit(cross(axis, tangent[a - 3U]));
        if (window[4])
          guides[a - 4U].orientation = unit(cross(axis, tangent[a - 4U]));
        if (window[3] && window[4] &&
            dot(guides[a - 3U].orientation, guides[a - 4U].orientation) < -0.8)
          guides[a - 4U].orientation = guides[a - 4U].orientation * -1.0;
      }
      window = {};
      last = 0;
    }
    if (window[0] && window[1] && window[2] && window[3]) {
      const auto center = (guides[*window[0]].position + guides[*window[3]].position) * 0.2130 +
                          (guides[*window[1]].position + guides[*window[2]].position) * 0.2870;
      if (last != 0) {
        const auto axis = unit(previous_center - center);
        const auto count = last == 1 ? 5U : 3U;
        for (std::size_t back = 0; back < count; ++back)
          guides[a - back].orientation = unit(cross(axis, tangent[a - back]));
      }
      ++last;
      previous_center = center;
    }
  }
}

// Keeps orientations perpendicular to the path, flips them for continuity
// (except in helices) and softens sharp kinks.
void refine_normals(std::vector<Guide> &guides, const Spans &path) {
  const auto count = guides.size();
  if (count < 3U)
    return;
  for (std::size_t a = 1; a + 1U < count; ++a)
    if (interior(guides, a))
      guides[a].orientation = unit(without(guides[a].orientation, path.tangent[a]));
  std::vector<std::array<V, 2>> alternatives(count);
  for (std::size_t a = 0; a < count; ++a)
    alternatives[a] = {guides[a].orientation,
                       guides[a].ss == 'H' ? guides[a].orientation
                                           : guides[a].orientation * -1.0};
  for (std::size_t a = 1; a + 1U < count; ++a) {
    if (!interior(guides, a))
      continue;
    const auto chain = path.direction[a - 1U];
    const auto reference = unit(without(guides[a - 1U].orientation, chain));
    const auto first = unit(without(alternatives[a][0], chain));
    const auto second = unit(without(alternatives[a][1], chain));
    guides[a].orientation =
        dot(reference, second) > dot(reference, first) ? alternatives[a][1] : alternatives[a][0];
  }
  for (std::size_t a = 1; a + 1U < count; ++a) {
    if (!interior(guides, a))
      continue;
    const auto current = guides[a].orientation;
    const auto turn = dot(current, guides[a + 1U].orientation) *
                      dot(current, guides[a - 1U].orientation);
    if (turn < -0.10) {
      auto average = unit(without(guides[a + 1U].orientation + guides[a - 1U].orientation +
                                      current * 0.001,
                                  path.tangent[a]));
      const auto target =
          unit(dot(current, average) < 0.0 ? current - average : current + average);
      const auto fraction = std::min(1.0, 2.0 * (-0.10 - turn));
      alternatives[a][0] = current * (1.0 - fraction) + target * fraction;
    } else {
      alternatives[a][0] = current;
    }
  }
  for (std::size_t a = 1; a + 1U < count; ++a)
    if (interior(guides, a))
      guides[a].orientation = alternatives[a][0];
}

// Averages positions and orientations along sheet runs.
void flatten_sheets(std::vector<Guide> &guides, int cycles) {
  const auto count = static_cast<long>(guides.size());
  long first = -1;
  long last = 0;
  int car = guides.empty() ? kLoop : guides.front().car_in;
  for (long a = 0; a < count; ++a) {
    bool end = false;
    if (a > 0) {
      if (guides[static_cast<std::size_t>(a)].segment !=
              guides[static_cast<std::size_t>(a - 1)].segment ||
          guides[static_cast<std::size_t>(a)].ss != 'S')
        end = true;
      if (a == count - 1)
        end = true;
    }
    if (end) {
      if (car != kLoop && car != kTube) {
        std::vector<V> buffer(guides.size());
        for (int cycle = 0; cycle < cycles; ++cycle) {
          for (long b = first + 1; b <= last - 1; ++b)
            buffer[static_cast<std::size_t>(b)] =
                (guides[static_cast<std::size_t>(b - 1)].position +
                 guides[static_cast<std::size_t>(b)].position +
                 guides[static_cast<std::size_t>(b + 1)].position) *
                (1.0 / 3.0);
          for (long b = first + 1; b <= last - 1; ++b)
            guides[static_cast<std::size_t>(b)].position = buffer[static_cast<std::size_t>(b)];
          for (long b = first + 1; b <= last - 1; ++b)
            buffer[static_cast<std::size_t>(b)] =
                (guides[static_cast<std::size_t>(b - 1)].orientation +
                 guides[static_cast<std::size_t>(b)].orientation +
                 guides[static_cast<std::size_t>(b + 1)].orientation) *
                (1.0 / 3.0);
          for (long b = first + 1; b <= last - 1; ++b)
            guides[static_cast<std::size_t>(b)].orientation = buffer[static_cast<std::size_t>(b)];
          for (long b = first + 1; b <= last - 1; ++b) {
            const auto along = unit(guides[static_cast<std::size_t>(b + 1)].position -
                                    guides[static_cast<std::size_t>(b - 1)].position);
            auto &orientation = guides[static_cast<std::size_t>(b)].orientation;
            orientation = unit(without(orientation, along));
          }
        }
      }
      first = -1;
      last = -1;
    }
    if (guides[static_cast<std::size_t>(a)].ss == 'S') {
      if (first < 0)
        first = a;
      car = guides[static_cast<std::size_t>(a)].car_in;
      last = a;
    }
  }
}

// cartoon_smooth_loops: moving-average smoothing of positions and orientations along each loop
// run (loops are guides without helix or sheet), including one neighbor on either side inside the segment.
void smooth_loops(std::vector<Guide> &guides, int first_window, int last_window, int cycles) {
  const auto count = static_cast<long>(guides.size());
  if (count < 2)
    return;
  const auto at = [&](long index) -> Guide & { return guides[static_cast<std::size_t>(index)]; };
  long first = -1;
  long last = 0;
  bool end = false;
  std::vector<V> buffer(guides.size());
  for (long a = 0; a < count; ++a) {
    if (a > 0) {
      if (at(a).segment != at(a - 1).segment)
        end = true;
      else if (at(a).ss != 'L')
        end = true;
      if (a == count - 1)
        end = true;
    }
    if (end) {
      if (a > 0 && first > 0 && at(first).segment == at(first - 1).segment)
        --first;
      if (last > 0 && at(a).segment == at(a - 1).segment && last < count - 1)
        ++last;
      for (int window = first_window; window <= last_window; ++window) {
        for (int cycle = 0; cycle < cycles; ++cycle) {
          for (long b = first + window; b <= last - window; ++b) {
            V sum{};
            for (long e = -window; e <= window; ++e)
              sum = sum + at(b + e).position;
            buffer[static_cast<std::size_t>(b)] = sum * (1.0 / (2 * window + 1));
          }
          for (long b = first + window; b <= last - window; ++b)
            at(b).position = buffer[static_cast<std::size_t>(b)];
          for (long b = first + window; b <= last - window; ++b) {
            V sum{};
            for (long e = -window; e <= window; ++e)
              sum = sum + at(b + e).orientation;
            buffer[static_cast<std::size_t>(b)] = sum * (1.0 / (2 * window + 1));
          }
          for (long b = first + window; b <= last - window; ++b)
            at(b).orientation = unit(buffer[static_cast<std::size_t>(b)]);
        }
      }
      first = -1;
      last = -1;
      end = false;
    }
    if (at(a).ss == 'L') {
      if (first < 0)
        first = a;
      last = a;
    }
  }
}

// Bends sheet tangents toward the run at the sheet tips.
void refine_tips(const std::vector<Guide> &guides, std::vector<V> &tangent, double factor) {
  for (std::size_t a = 1; a + 1U < guides.size(); ++a) {
    if (guides[a].ss != 'S' || !interior(guides, a))
      continue;
    const bool next_same = guides[a + 1U].ss == guides[a].ss;
    const bool previous_same = guides[a - 1U].ss == guides[a].ss;
    if (next_same && !previous_same)
      tangent[a] = unit(tangent[a] + tangent[a + 1U] * factor);
    else if (!next_same && previous_same)
      tangent[a] = unit(tangent[a] + tangent[a - 1U] * factor);
  }
}

struct Path {
  std::vector<V> point;
  std::vector<V> orientation;
  std::vector<ColorRgba> color;
  std::vector<std::size_t> atom;
  void clear() {
    point.clear();
    orientation.clear();
    color.clear();
    atom.clear();
  }
};

struct Frame {
  V tangent, normal, binormal;
};

std::vector<V> path_tangents(const Path &path) {
  const auto count = path.point.size();
  std::vector<V> direction(count), tangent(count);
  for (std::size_t a = 1; a < count; ++a)
    direction[a - 1U] = unit(path.point[a] - path.point[a - 1U]);
  if (count == 0U)
    return tangent;
  tangent[0] = direction[0];
  for (std::size_t a = 1; a + 1U < count; ++a)
    tangent[a] = unit(direction[a] + direction[a - 1U]);
  if (count > 1U)
    tangent[count - 1U] = direction[count - 2U];
  return tangent;
}

// Frames from the sampled orientation vectors (flat profiles).
std::vector<Frame> oriented_frames(const Path &path) {
  const auto tangent = path_tangents(path);
  std::vector<Frame> frames(path.point.size());
  for (std::size_t a = 0; a < frames.size(); ++a) {
    const auto binormal = unit(cross(tangent[a], path.orientation[a]));
    frames[a] = {tangent[a], unit(cross(binormal, tangent[a])), binormal};
  }
  return frames;
}

// Frames transported from an arbitrary start (round profiles).
std::vector<Frame> transported_frames(const Path &path) {
  const auto tangent = path_tangents(path);
  std::vector<Frame> frames(path.point.size());
  if (frames.empty())
    return frames;
  // First perpendicular from a vector diverging from the start tangent.
  const auto start = tangent[0];
  V divergent = start;
  if (start.x != 0.0) {
    divergent = {-start.x, start.y + 0.1, start.z};
  } else if (start.y != 0.0) {
    divergent = {start.x + 0.1, -start.y, start.z};
  } else {
    divergent = {start.x + 0.1, start.y, -start.z};
  }
  V normal = unit(without(divergent, start));
  for (std::size_t a = 0; a < frames.size(); ++a) {
    const auto binormal = unit(cross(tangent[a], normal));
    normal = unit(cross(binormal, tangent[a]));
    frames[a] = {tangent[a], normal, binormal};
  }
  return frames;
}

struct Shape {
  std::vector<std::pair<double, double>> vertex; // (normal, binormal) offsets
  std::vector<std::pair<double, double>> normal;
};

Shape circle(int quality, double radius) {
  Shape shape;
  for (int b = 0; b < quality; ++b) {
    const auto angle = 2.0 * std::numbers::pi * b / quality;
    shape.vertex.emplace_back(std::cos(angle) * radius, std::sin(angle) * radius);
    shape.normal.emplace_back(std::cos(angle), std::sin(angle));
  }
  return shape;
}

Shape oval(int quality, double width, double length) {
  Shape shape;
  for (int b = 0; b < quality; ++b) {
    const auto angle = 2.0 * std::numbers::pi * b / quality;
    shape.vertex.emplace_back(std::cos(angle) * width, std::sin(angle) * length);
    shape.normal.emplace_back(std::cos(angle) * length, std::sin(angle) * width);
  }
  return shape;
}

// Eight vertices: four faces of two (normal-paired) vertices each.
Shape rectangle(double width, double length) {
  const auto w = std::cos(std::numbers::pi / 4.0) * width;
  const auto l = std::sin(std::numbers::pi / 4.0) * length;
  return {{{w, -l}, {w, l}, {w, l}, {-w, l}, {-w, l}, {-w, -l}, {-w, -l}, {w, -l}},
          {{1, 0}, {1, 0}, {0, 1}, {0, 1}, {-1, 0}, {-1, 0}, {0, -1}, {0, -1}}};
}

class MeshWriter {
public:
  MeshWriter(RenderPacket &packet, const PymolBackboneInput &input)
      : packet_(packet), input_(input) {}

  std::uint32_t vertex(V position, V normal, ColorRgba color) {
    packet_.mesh_vertices.push_back(MeshVertex{position, unit(normal), color});
    include(packet_.bounds, position);
    return static_cast<std::uint32_t>(packet_.mesh_vertices.size() - 1U);
  }
  void triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::size_t atom) {
    const auto residue = input_.topology->atoms()[atom].residue;
    packet_.mesh_triangles.push_back(MeshTriangle{a, b, c, input_.residue_pick(residue)});
  }
  void quad(std::uint32_t a0, std::uint32_t a1, std::uint32_t b0, std::uint32_t b1,
            std::size_t atom) {
    triangle(a0, b0, a1, atom);
    triangle(a1, b0, b1, atom);
  }

private:
  RenderPacket &packet_;
  const PymolBackboneInput &input_;
};

V shape_point(const Frame &frame, std::pair<double, double> offset) {
  return frame.normal * offset.first + frame.binormal * offset.second;
}

void flat_cap(MeshWriter &mesh, const Path &path, const std::vector<Frame> &frames,
              const Shape &shape, std::size_t a, bool start, double scale = 1.0) {
  const auto normal = start ? frames[a].tangent * -1.0 : frames[a].tangent;
  const auto center = mesh.vertex(path.point[a], normal, path.color[a]);
  std::vector<std::uint32_t> ring;
  for (const auto &offset : shape.vertex)
    ring.push_back(mesh.vertex(
        path.point[a] + shape_point(frames[a], {offset.first * scale, offset.second * scale}),
        normal, path.color[a]));
  for (std::size_t b = 0; b < ring.size(); ++b)
    mesh.triangle(center, ring[b], ring[(b + 1U) % ring.size()], path.atom[a]);
}

void round_cap(MeshWriter &mesh, const Path &path, const std::vector<Frame> &frames,
               const Shape &shape, double radius, std::size_t a, bool start) {
  const auto pole = start ? frames[a].tangent * -1.0 : frames[a].tangent;
  const auto rings = std::max<std::size_t>(2U, shape.vertex.size() / 2U);
  const auto count = shape.vertex.size();
  std::vector<std::vector<std::uint32_t>> grid;
  for (std::size_t ring = 0; ring <= rings; ++ring) {
    const auto polar = std::numbers::pi / 2.0 * static_cast<double>(ring) / static_cast<double>(rings);
    std::vector<std::uint32_t> row;
    for (std::size_t b = 0; b < count; ++b) {
      const auto angle = 2.0 * std::numbers::pi * static_cast<double>(b) / static_cast<double>(count);
      const auto direction = (frames[a].normal * std::cos(angle) +
                              frames[a].binormal * std::sin(angle)) *
                                 std::sin(polar) +
                             pole * std::cos(polar);
      row.push_back(mesh.vertex(path.point[a] + direction * radius, direction, path.color[a]));
    }
    grid.push_back(std::move(row));
  }
  for (std::size_t ring = 0; ring < rings; ++ring)
    for (std::size_t b = 0; b < count; ++b)
      mesh.quad(grid[ring][b], grid[ring][(b + 1U) % count], grid[ring + 1U][b],
                grid[ring + 1U][(b + 1U) % count], path.atom[a]);
}

enum class Cap { none, flat, round };

void tube(MeshWriter &mesh, const Path &path, const std::vector<Frame> &frames,
          const Shape &shape, Cap cap, double radius, std::size_t dash = 0U) {
  const auto count = path.point.size();
  const auto sides = shape.vertex.size();
  if (count < 2U || sides == 0U)
    return;
  std::vector<std::vector<std::uint32_t>> grid(sides, std::vector<std::uint32_t>(count));
  for (std::size_t b = 0; b < sides; ++b)
    for (std::size_t a = 0; a < count; ++a)
      grid[b][a] = mesh.vertex(path.point[a] + shape_point(frames[a], shape.vertex[b]),
                               shape_point(frames[a], shape.normal[b]), path.color[a]);
  const auto step = dash != 0U ? dash : count;
  for (std::size_t first = 0; first + 1U < count; first += step) {
    const auto end = std::min(first + step, count);
    for (std::size_t b = 0; b < sides; ++b)
      for (std::size_t a = first; a + 1U < end; ++a)
        mesh.quad(grid[b][a], grid[(b + 1U) % sides][a], grid[b][a + 1U],
                  grid[(b + 1U) % sides][a + 1U], path.atom[a]);
    if (cap == Cap::flat) {
      flat_cap(mesh, path, frames, shape, first, true);
      flat_cap(mesh, path, frames, shape, end - 1U, false);
    }
  }
  if (cap == Cap::round) {
    round_cap(mesh, path, frames, shape, radius, 0U, true);
    round_cap(mesh, path, frames, shape, radius, count - 1U, false);
  }
}

// Tube whose cross-section is scaled by `factor[a]` at each path point (putty). The side normals tilt toward
// the thinner end by the local rate of change of the radius, as in PyMOL's variable tube.
void variable_tube(MeshWriter &mesh, const Path &path, const std::vector<Frame> &frames,
                   const Shape &shape, const std::vector<double> &factor) {
  const auto count = path.point.size();
  const auto sides = shape.vertex.size();
  if (count < 2U || sides == 0U || factor.size() != count)
    return;
  const auto radius = [&](std::size_t b, std::size_t a) {
    return std::hypot(shape.vertex[b].first, shape.vertex[b].second) * factor[a];
  };
  std::vector<std::vector<std::uint32_t>> grid(sides, std::vector<std::uint32_t>(count));
  for (std::size_t b = 0; b < sides; ++b) {
    std::vector<double> lean(count, 0.0);
    for (std::size_t a = 1U; a + 1U < count; ++a) {
      const auto r0 = radius(b, a);
      const auto before = length(path.point[a - 1U] - path.point[a]);
      const auto after = length(path.point[a + 1U] - path.point[a]);
      const auto x1 = before > 0.0 ? (radius(b, a - 1U) - r0) / before : 0.0;
      const auto x2 = after > 0.0 ? -(radius(b, a + 1U) - r0) / after : 0.0;
      lean[a] = (x1 + x2) / 2.0;
      if (a == 1U)
        lean[0] = x1;
      if (a + 2U == count)
        lean[count - 1U] = x2;
    }
    for (std::size_t a = 0; a < count; ++a) {
      const auto side = shape_point(frames[a], shape.normal[b]);
      const auto normal = unit(frames[a].tangent * lean[a] + side);
      const auto offset = shape_point(frames[a], {shape.vertex[b].first * factor[a],
                                                  shape.vertex[b].second * factor[a]});
      grid[b][a] = mesh.vertex(path.point[a] + offset, normal, path.color[a]);
    }
  }
  for (std::size_t b = 0; b < sides; ++b)
    for (std::size_t a = 0; a + 1U < count; ++a)
      mesh.quad(grid[b][a], grid[(b + 1U) % sides][a], grid[b][a + 1U],
                grid[(b + 1U) % sides][a + 1U], path.atom[a]);
  flat_cap(mesh, path, frames, shape, 0U, true, factor.front());
  flat_cap(mesh, path, frames, shape, count - 1U, false, factor.back());
}

// Paired-face polygon (flat rectangle) with flat end caps.
void polygon(MeshWriter &mesh, const Path &path, const std::vector<Frame> &frames,
             const Shape &shape) {
  const auto count = path.point.size();
  if (count < 2U)
    return;
  for (std::size_t b = 0; b + 1U < shape.vertex.size(); b += 2U) {
    std::vector<std::uint32_t> left(count), right(count);
    for (std::size_t a = 0; a < count; ++a) {
      left[a] = mesh.vertex(path.point[a] + shape_point(frames[a], shape.vertex[b]),
                            shape_point(frames[a], shape.normal[b]), path.color[a]);
      right[a] = mesh.vertex(path.point[a] + shape_point(frames[a], shape.vertex[b + 1U]),
                             shape_point(frames[a], shape.normal[b + 1U]), path.color[a]);
    }
    for (std::size_t a = 0; a + 1U < count; ++a)
      mesh.quad(left[a], right[a], left[a + 1U], right[a + 1U], path.atom[a]);
  }
  flat_cap(mesh, path, frames, shape, 0U, true);
  flat_cap(mesh, path, frames, shape, count - 1U, false);
}

// Dumbbell web: the two faces of a flat polygon whose length shrinks to nothing over `sampling` points at
// each end (PyMOL's tapered polygon); no end caps.
void polygon_taper(MeshWriter &mesh, const Path &path, const std::vector<Frame> &frames,
                   const Shape &shape, std::size_t sampling) {
  const auto count = path.point.size();
  if (count < 2U)
    return;
  const auto taper = [&](std::size_t a) {
    if (a < sampling)
      return smooth(static_cast<double>(a) / static_cast<double>(sampling), 2.0);
    if (a >= count - sampling)
      return smooth(static_cast<double>(count - a - 1U) / static_cast<double>(sampling), 2.0);
    return 1.0;
  };
  for (std::size_t b = 0; b + 1U < shape.vertex.size(); b += 2U) {
    std::vector<std::uint32_t> left(count), right(count);
    for (std::size_t a = 0; a < count; ++a) {
      const auto f = taper(a);
      const auto place = [&](std::size_t vertex) {
        return path.point[a] + shape_point(frames[a], {shape.vertex[vertex].first,
                                                       shape.vertex[vertex].second * f});
      };
      left[a] = mesh.vertex(place(b), shape_point(frames[a], shape.normal[b]), path.color[a]);
      right[a] = mesh.vertex(place(b + 1U), shape_point(frames[a], shape.normal[b + 1U]), path.color[a]);
    }
    for (std::size_t a = 0; a + 1U < count; ++a)
      mesh.quad(left[a], right[a], left[a + 1U], right[a + 1U], path.atom[a]);
  }
}

// Sheet strand: flat body, then an arrow head over the last residue that
// starts 1.5 times wider and narrows to the tip.
void strand(MeshWriter &mesh, const Path &path, const std::vector<Frame> &frames,
            const Shape &shape, std::size_t sampling) {
  const auto count = path.point.size();
  if (count < 2U)
    return;
  const auto body_end = count > sampling ? count - sampling : 0U; // subN
  const auto face = [&](std::size_t from, std::size_t to,
                        const std::function<double(std::size_t)> &scale, bool tilt) {
    for (std::size_t b = 0; b + 1U < shape.vertex.size(); b += 2U) {
      std::vector<std::uint32_t> left, right;
      for (std::size_t a = from; a < to; ++a) {
        const auto widened = [&](std::pair<double, double> offset) {
          return std::pair{offset.first, offset.second * scale(a)};
        };
        auto normal_b = shape.normal[b];
        auto normal_c = shape.normal[b + 1U];
        auto tangent_part = 0.0;
        if (tilt && std::abs(normal_b.second) > 1.0e-4)
          tangent_part = 0.4;
        const auto normal = [&](std::pair<double, double> value) {
          return frames[a].tangent * tangent_part + shape_point(frames[a], value);
        };
        left.push_back(mesh.vertex(path.point[a] + shape_point(frames[a], widened(shape.vertex[b])),
                                   normal(normal_b), path.color[a]));
        right.push_back(mesh.vertex(
            path.point[a] + shape_point(frames[a], widened(shape.vertex[b + 1U])),
            normal(normal_c), path.color[a]));
      }
      for (std::size_t k = 0; k + 1U < left.size(); ++k)
        mesh.quad(left[k], right[k], left[k + 1U], right[k + 1U], path.atom[from + k]);
    }
  };
  face(0U, body_end, [](std::size_t) { return 1.0; }, false);
  // Start cap of the body.
  {
    const auto normal = frames[0].tangent * -1.0;
    std::array<std::uint32_t, 4> corner{};
    const std::array<std::size_t, 4> order{6U, 4U, 0U, 2U};
    for (std::size_t k = 0; k < 4U; ++k)
      corner[k] = mesh.vertex(path.point[0] + shape_point(frames[0], shape.vertex[order[k]]),
                              normal, path.color[0]);
    mesh.triangle(corner[0], corner[1], corner[2], path.atom[0]);
    mesh.triangle(corner[1], corner[2], corner[3], path.atom[0]);
  }
  const auto head = body_end > 0U ? body_end - 1U : 0U;
  face(head, count,
       [&](std::size_t a) {
         return 1.5 * static_cast<double>((count - 1U) - a) / static_cast<double>(sampling);
       },
       true);
  if (body_end == 0U)
    return;
  // Back faces where the arrow head overhangs the body on both sides.
  const auto normal = frames[head].tangent * -1.0;
  for (const auto side : {1.0, -1.0}) {
    std::array<std::uint32_t, 4> corner{};
    const std::array<std::size_t, 4> order{6U, 4U, 0U, 2U};
    for (std::size_t k = 0; k < 4U; ++k) {
      auto offset = shape.vertex[order[k]];
      offset.second = offset.second < 0.0 ? -offset.second * side
                                          : offset.second * 1.5 * side;
      corner[k] = mesh.vertex(path.point[head] + shape_point(frames[head], offset), normal,
                              path.color[head]);
    }
    mesh.triangle(corner[0], corner[1], corner[2], path.atom[head]);
    mesh.triangle(corner[1], corner[2], corner[3], path.atom[head]);
  }
}

struct PuttyStatistics {
  double mean{10.0};
  double stdev{0.0};
  double minimum{0.0};
  double maximum{10.0};
};

// Mean, deviation and range of the B-factor over the atoms this cartoon shows.
PuttyStatistics putty_statistics(const PymolBackboneInput &input) {
  double sum = 0.0, square = 0.0;
  double minimum = std::numeric_limits<double>::max(), maximum = std::numeric_limits<double>::lowest();
  std::size_t count = 0;
  for (std::size_t atom = 0; atom < input.visible.size() && atom < input.b_factor.size(); ++atom) {
    if (input.visible[atom] == 0U)
      continue;
    const auto value = input.b_factor[atom];
    sum += value;
    square += value * value;
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
    ++count;
  }
  if (count == 0U)
    return {};
  const auto n = static_cast<double>(count);
  return {sum / n, std::sqrt(std::max(0.0, (square - sum * sum / n) / n)), minimum, maximum};
}

// Radius factor along a putty path: the B-factor of each point's atom mapped by cartoon_putty_transform,
// clamped to [scale_min, scale_max] (negative: unbounded) and averaged over +-`window` points.
std::vector<double> putty_factors(const PymolBackboneInput &input, const PymolCartoonParameters &parameters,
                                  const PuttyStatistics &statistics, const Path &path, int window) {
  const auto count = path.point.size();
  std::vector<double> factor(count, 0.5);
  const auto transform = parameters.putty_transform;
  const auto range = parameters.putty_range;
  const auto data_range = statistics.maximum - statistics.minimum;
  const bool normalized = transform == 0 || transform == 4;
  const bool relative = transform == 1 || transform == 5;
  const bool invalid = (normalized && statistics.stdev < 1e-8) ||
                       (transform != 3 && transform != 7 && transform != 8 && std::abs(range) < 1e-8) ||
                       (relative && std::abs(data_range) < 1e-8);
  if (invalid)
    return factor;
  const bool nonlinear = transform <= 3;
  for (std::size_t a = 0; a < count; ++a) {
    const auto b = path.atom[a] < input.b_factor.size() ? input.b_factor[path.atom[a]] : 0.0;
    double scale = 1.0;
    if (transform == 8) {
      scale = std::sqrt(b / 8.0) / std::numbers::pi;
    } else {
      if (normalized)
        scale = (range + (b - statistics.mean) / statistics.stdev) / range;
      else if (relative)
        scale = (b - statistics.minimum) / (data_range * range);
      else if (transform == 2 || transform == 6)
        scale = b / range;
      else
        scale = b;
      scale = std::max(0.0, scale);
      if (nonlinear)
        scale = std::pow(scale, parameters.putty_scale_power);
    }
    if (parameters.putty_scale_min >= 0.0 && scale < parameters.putty_scale_min)
      scale = parameters.putty_scale_min;
    if (parameters.putty_scale_max >= 0.0 && scale > parameters.putty_scale_max)
      scale = parameters.putty_scale_max;
    factor[a] = scale;
  }
  if (count > 2U) {
    const auto source = factor;
    for (std::size_t a = 1U; a + 1U < count; ++a) {
      double sum = 0.0;
      for (int w = -window; w <= window; ++w) {
        const auto index = std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(a) + w, 0,
                                                      static_cast<std::ptrdiff_t>(count) - 1);
        sum += source[static_cast<std::size_t>(index)];
      }
      factor[a] = sum / static_cast<double>(2 * window + 1);
    }
  }
  return factor;
}

} // namespace

std::vector<std::int32_t> append_pymol_cartoon(const PymolBackboneInput &input,
                                               const PymolCartoonParameters &parameters,
                                               RenderPacket &packet) {
  std::vector<std::int32_t> fallbacks;
  const auto atoms = residue_atoms(input);
  auto guides = cartoon_guides(input, atoms, parameters.gap_cutoff, parameters.fancy_helices,
                               parameters.fancy_sheets);
  const auto count = guides.size();
  if (count < 2U)
    return fallbacks;
  auto path = spans(guides);
  round_helices(guides, path.tangent);
  refine_normals(guides, path);
  if (parameters.flat_sheets)
    flatten_sheets(guides, parameters.flat_cycles);
  if (parameters.smooth_loops)
    smooth_loops(guides, parameters.smooth_first, parameters.smooth_last, parameters.smooth_cycles);
  path = spans(guides);
  if (parameters.flat_sheets)
    refine_tips(guides, path.tangent, parameters.refine_tips);

  const auto scale = input.unit_scale;
  const auto sampling = static_cast<std::size_t>(std::max(1, parameters.sampling));
  MeshWriter mesh{packet, input};
  Path points;
  PuttyStatistics putty;
  bool putty_ready = false;
  const auto extrude = [&](int car) {
    if (points.point.size() < 2U || car == kSkip)
      return;
    if (car == kCylinder) {
      if (std::find(fallbacks.begin(), fallbacks.end(), car) == fallbacks.end())
        fallbacks.push_back(car);
    }
    switch (car) {
    case kLoop:
    case kDash:
      tube(mesh, points, transported_frames(points),
           circle(parameters.loop_quality, parameters.loop_radius * scale), Cap::flat,
           parameters.loop_radius * scale, car == kDash ? 2U : 0U);
      break;
    case kRect:
      polygon(mesh, points, oriented_frames(points),
              rectangle(parameters.rect_width * scale, parameters.rect_length * scale));
      break;
    case kDumbbell: {
      const auto width = parameters.dumbbell_width * scale;
      const auto length = parameters.dumbbell_length * scale;
      const auto w = std::cos(std::numbers::pi / 4.0) * width;
      const auto l = std::sin(std::numbers::pi / 4.0) * length;
      const Shape web{{{w, -l}, {w, l}, {-w, l}, {-w, -l}}, {{1, 0}, {1, 0}, {-1, 0}, {-1, 0}}};
      const auto frames = oriented_frames(points);
      polygon_taper(mesh, points, frames, web, sampling);
      // A rod along each edge of the web, pulled in over the first and last `sampling` points.
      const auto reach = std::sin(std::numbers::pi / 4.0) * length;
      const auto count = points.point.size();
      for (const double sign : {-1.0, 1.0}) {
        Path edge = points;
        for (std::size_t a = 0; a < count; ++a) {
          double f = sign * reach;
          if (a <= sampling)
            f *= smooth(static_cast<double>(a) / static_cast<double>(sampling), 2.0);
          else if (a + sampling >= count)
            f *= smooth(static_cast<double>(count - a - 1U) / static_cast<double>(sampling), 2.0);
          edge.point[a] = edge.point[a] + frames[a].binormal * f;
        }
        tube(mesh, edge, transported_frames(edge),
             circle(parameters.loop_quality, parameters.dumbbell_radius * scale), Cap::flat,
             parameters.dumbbell_radius * scale);
      }
      break;
    }
    case kOval:
      tube(mesh, points, oriented_frames(points),
           oval(parameters.oval_quality, parameters.oval_width * scale,
                parameters.oval_length * scale),
           Cap::flat, 0.0);
      break;
    case kPutty: {
      if (!putty_ready) {
        putty = putty_statistics(input);
        putty_ready = true;
      }
      variable_tube(mesh, points, transported_frames(points),
                    circle(parameters.putty_quality, parameters.putty_radius * scale),
                    putty_factors(input, parameters, putty, points, static_cast<int>(sampling) / 2));
      break;
    }
    case kTube:
    case kCylinder:
      tube(mesh, points, transported_frames(points),
           circle(parameters.tube_quality, parameters.tube_radius * scale), Cap::round,
           parameters.tube_radius * scale);
      break;
    case kArrow:
      strand(mesh, points, oriented_frames(points),
             rectangle(parameters.rect_width * scale, parameters.rect_length * scale), sampling);
      break;
    default:
      break;
    }
  };
  const auto sample = [&](std::size_t a) {
    const auto &first = guides[a];
    const auto &second = guides[a + 1U];
    const auto p1 = first.position;
    const auto p2 = second.position;
    const auto t1 = path.tangent[a];
    const auto t2 = path.tangent[a + 1U];
    const auto o1 = first.orientation;
    const auto o2 = second.orientation;
    const auto c1 = input.visuals[first.atom].color;
    const auto c2 = input.visuals[second.atom].color;
    const auto deviation = parameters.throw_factor * path.distance[a];
    const auto point_at = [&](double fraction) {
      const auto f0 = smooth(fraction, parameters.power);
      const auto f1 = 1.0 - f0;
      const auto f2 = smooth(f0, parameters.power_b);
      const auto f3 = smooth(f1, parameters.power_b);
      const auto f4 = deviation * f2 * f3;
      return std::tuple{p1 * f1 + p2 * f0 + (t1 * f3 - t2 * f2) * f4, f0, f1, f2, f3};
    };
    for (std::size_t b = 0; b < sampling; ++b) {
      if (points.point.empty()) {
        const auto fraction = static_cast<double>(b) / static_cast<double>(sampling);
        points.point.push_back(std::get<0>(point_at(fraction)));
        points.orientation.push_back(o1);
        points.color.push_back(fraction <= 0.5 ? c1 : c2);
        points.atom.push_back(fraction <= 0.5 ? first.atom : second.atom);
      }
      const auto fraction = static_cast<double>(b + 1U) / static_cast<double>(sampling);
      const auto [position, f0, f1, f2, f3] = point_at(fraction);
      points.point.push_back(position);
      points.orientation.push_back(b + 1U == sampling ? o2 : o1 * (f1 * f2) + o2 * (f0 * f3));
      points.color.push_back(fraction <= 0.5 ? c1 : c2);
      points.atom.push_back(fraction <= 0.5 ? first.atom : second.atom);
    }
    // Smooth the span along the axis normal to both orientation vectors.
    const auto axis = cross(o1, o2);
    if (sampling > 1U && length(axis) > 1.0e-4) {
      const auto normal = unit(axis);
      const auto base = points.point.size() - sampling - 1U;
      std::vector<V> buffer(sampling - 1U);
      for (int cycle = 0; cycle < parameters.refine; ++cycle) {
        for (std::size_t b = 0; b + 1U < sampling; ++b) {
          const auto &q0 = points.point[base + b];
          const auto &q1 = points.point[base + b + 1U];
          const auto &q2 = points.point[base + b + 2U];
          const auto target = (dot(normal, q2) + dot(normal, q0)) / 2.0;
          buffer[b] = q1 + normal * (target - dot(normal, q1));
        }
        for (std::size_t b = 0; b + 1U < sampling; ++b)
          points.point[base + b + 1U] = buffer[b];
      }
    }
  };

  int current = kSkip;
  std::size_t a = 0;
  bool running = true;
  while (running) {
    bool flush = false;
    int next = kSkip;
    if (a + 1U < count && guides[a].segment == guides[a + 1U].segment)
      next = prioritize(guides[a].out(), guides[a + 1U].car_in);
    if (current != next) {
      if (!points.point.empty()) {
        flush = true;
      } else {
        current = next;
        points.clear();
      }
    }
    if (!flush && a + 1U < count && guides[a].segment == guides[a + 1U].segment)
      sample(a);
    ++a;
    if (a == count) {
      running = false;
      if (!points.point.empty())
        flush = true;
    }
    if (flush) {
      extrude(current);
      --a;
      points.clear();
    }
  }
  return fallbacks;
}

void append_pymol_ribbon(const PymolBackboneInput &input,
                         const PymolRibbonParameters &parameters,
                         RenderPacket &packet) {
  const auto atoms = residue_atoms(input);
  std::vector<Guide> guides;
  int segment = 0;
  std::optional<std::size_t> last;
  for (std::size_t residue = 0; residue < atoms.size(); ++residue) {
    const auto &entry = atoms[residue];
    if (!entry.ca || input.visible[*entry.ca] == 0U)
      continue;
    if (!last || !connected(input, atoms, *last, residue))
      ++segment;
    guides.push_back(Guide{input.positions[*entry.ca], {}, 'L', kLoop, 0, *entry.ca,
                           model::ResidueIndex{residue}, segment});
    last = residue;
  }
  if (guides.size() < 2U)
    return;
  const auto path = spans(guides);
  const auto sampling = static_cast<std::size_t>(std::max(1, parameters.sampling));
  const auto width = static_cast<float>(parameters.width_pixels);
  for (std::size_t a = 0; a + 1U < guides.size(); ++a) {
    if (guides[a].segment != guides[a + 1U].segment)
      continue;
    const auto &first = guides[a];
    const auto &second = guides[a + 1U];
    const auto deviation = parameters.throw_factor * path.distance[a];
    const auto point_at = [&](double fraction) {
      const auto f0 = smooth(fraction, parameters.power);
      const auto f1 = 1.0 - f0;
      const auto f2 = smooth(f0, parameters.power_b);
      const auto f3 = smooth(f1, parameters.power_b);
      const auto f4 = deviation * f2 * f3;
      return first.position * f1 + second.position * f0 +
             (path.tangent[a] * f3 - path.tangent[a + 1U] * f2) * f4;
    };
    const auto pick = input.residue_pick(first.residue);
    for (std::size_t b = 0; b < sampling; ++b) {
      const auto start = point_at(static_cast<double>(b) / static_cast<double>(sampling));
      const auto end = point_at(static_cast<double>(b + 1U) / static_cast<double>(sampling));
      const auto &color1 = input.visuals[b * 2U < sampling ? first.atom : second.atom].color;
      const auto &color2 = input.visuals[(b + 1U) * 2U > sampling ? second.atom : first.atom].color;
      if (color1 == color2) {
        packet.lines.push_back({start, end, color1, color1, width, pick});
      } else {
        const auto middle = (start + end) * 0.5;
        packet.lines.push_back({start, middle, color1, color1, width, pick});
        packet.lines.push_back({middle, end, color2, color2, width, pick});
      }
      include(packet.bounds, start);
      include(packet.bounds, end);
    }
  }
}

} // namespace molshredder::render::detail
