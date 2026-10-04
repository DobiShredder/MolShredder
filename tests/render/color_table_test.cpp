#include <cmath>
#include <iostream>
#include <string_view>

#include "molshredder/render/color_table.hpp"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool near(const molshredder::render::ColorRgb &actual, float red, float green,
          float blue) {
  return std::abs(actual.red - red) < 1.0e-7F &&
         std::abs(actual.green - green) < 1.0e-7F &&
         std::abs(actual.blue - blue) < 1.0e-7F;
}

std::int32_t index_of(const molshredder::render::ColorTable &table,
                      std::string_view token) {
  const auto resolved = table.resolve(token);
  return resolved.has_value() ? resolved.value() : -1000;
}

} // namespace

int main() {
  using namespace molshredder::render;
  bool passed = true;
  ColorTable table;

  // Reference: docs/research/pymol-color-system-blackbox_20261002T090000Z.
  passed &= expect(ColorTable::builtin_size() == 5388U && table.size() == 5388U,
                   "built-in catalog must hold the 5388 PyMOL 3.1.0 entries");
  passed &= expect(table.list(ColorListScope::named).size() == 178U &&
                       table.list(ColorListScope::all).size() == 5388U &&
                       table.list(ColorListScope::custom).empty(),
                   "named/all/custom list scopes must match the reference");
  passed &= expect(index_of(table, "white") == 0 && index_of(table, "red") == 4 &&
                       index_of(table, "RED") == 4 &&
                       index_of(table, "carbon") == 26 &&
                       index_of(table, "lightmagenta") == 154 &&
                       index_of(table, "s000") >= 0,
                   "exact lookup must be case-insensitive and index-stable");
  passed &= expect(index_of(table, "bl") == 1 && index_of(table, "re") == 4 &&
                       index_of(table, "gre") == 3 &&
                       index_of(table, "cya") == 5 &&
                       index_of(table, "lightm") == 154 &&
                       index_of(table, "oran") == 13 &&
                       index_of(table, "salm") == 9 &&
                       !table.resolve("xyz").has_value(),
                   "prefix lookup must pick the lowest matching index");
  passed &= expect(index_of(table, "0xff8000") == 1090486272 &&
                       index_of(table, "#FF8000") == 1090486272 &&
                       !table.resolve("0xff80").has_value() &&
                       near(table.rgb(1090486272).value(), 1.0F,
                            128.0F / 255.0F, 0.0F),
                   "0xRRGGBB and #RRGGBB must encode literal colors");
  passed &= expect(index_of(table, "4") == 4 && index_of(table, "-1") == -1 &&
                       !table.resolve("5388").has_value() &&
                       !table.resolve("-2").has_value(),
                   "numeric tokens must address existing indices or codes");
  passed &= expect(index_of(table, "default") == kColorDefault &&
                       index_of(table, "atomic") == kColorAtomic &&
                       index_of(table, "atom") == kColorAtomic &&
                       index_of(table, "object") == kColorObject &&
                       index_of(table, "front") == kColorFront &&
                       index_of(table, "back") == kColorBack &&
                       !table.rgb(kColorObject).has_value() &&
                       table.name(kColorAtomic) == "atomic",
                   "special tokens must map to PyMOL codes");
  const auto assignment = [&table](std::string_view token) {
    const auto resolved = table.resolve_assignment(token);
    return resolved.has_value() ? resolved.value() : -1000;
  };
  // Reference: pymol-spectrum-background-blackbox_20261002T150000Z/ambiguity_capture.json
  passed &= expect(assignment("bl") == -1000 && assignment("blu") == -1000 &&
                       assignment("wh") == -1000 && assignment("mag") == -1000 &&
                       assignment("r") == -1000 && assignment("light") == -1000 &&
                       assignment("re") == 4 && assignment("RE") == 4 &&
                       assignment("bluew") == 5278 && assignment("carb") == 26 &&
                       assignment("lightm") == 154 && assignment("dash") == 7 &&
                       assignment("s0") == 155 && assignment("grey0") == 54 &&
                       assignment("c08") == 2235 && assignment("o89") == 5146 &&
                       assignment("w0") == 3155 && assignment("br1") == 39 &&
                       assignment("gray5") == 4206 && assignment("4") == 4 &&
                       assignment("0xff8000") == 1090486272,
                   "assignment lookup must reject prefixes shared by named colors");
  passed &= expect(near(table.background_rgb(), kDefaultBackgroundRgb.red,
                        kDefaultBackgroundRgb.green, kDefaultBackgroundRgb.blue),
                   "unset background must use the MolShredder default");
  {
    ColorTable with_background;
    passed &= expect(!with_background.set_background(index_of(table, "0x336699"))
                          .has_value() &&
                         near(with_background.background_rgb(), 0.2F, 0.4F, 0.6F) &&
                         near(with_background.front_rgb(), 0.8F, 0.6F, 0.4F) &&
                         with_background.set_background(kColorAtomic).has_value() &&
                         !(with_background == table),
                     "background and front colors must follow the reference");
  }
  passed &= expect(!table.resolve("").has_value() &&
                       !table.resolve("not-a-color").has_value(),
                   "unknown colors must fail");
  passed &= expect(near(table.rgb(4).value(), 1.0F, 0.0F, 0.0F) &&
                       near(table.rgb(26).value(), 0.2F, 1.0F, 0.2F) &&
                       table.name(26) == "carbon" &&
                       table.name(1090486272) == "0xff8000",
                   "built-in RGB and names must match the capture");

  const auto mycolor = table.define("mycolor", {1.0, 0.5, 0.0});
  const auto replaced = table.define("MyColor", {0.0, 0.25, 1.0});
  const auto big = table.define("big", {255.0, 128.0, 0.0});
  passed &= expect(mycolor.has_value() && mycolor.value() == 5388 &&
                       replaced.has_value() && replaced.value() == 5388 &&
                       big.has_value() && big.value() == 5389 &&
                       near(table.rgb(5388).value(), 0.0F, 0.25F, 1.0F) &&
                       near(table.rgb(5389).value(), 1.0F, 128.0F / 255.0F,
                            0.0F) &&
                       index_of(table, "MYCOLOR") == 5388 &&
                       index_of(table, "myc") == 5388,
                   "custom definitions must follow the reference index rules");
  const auto red = table.define("red", {0.5, 0.0, 0.0});
  passed &= expect(red.has_value() && red.value() == 4 &&
                       near(table.rgb(4).value(), 0.5F, 0.0F, 0.0F) &&
                       table.list(ColorListScope::custom).size() == 3U &&
                       table.list(ColorListScope::named).size() == 180U,
                   "built-in replacement must keep its index");
  passed &= expect(!table.define("default", {1.0, 1.0, 1.0}).has_value() &&
                       !table.define("1abc", {1.0, 1.0, 1.0}).has_value() &&
                       !table.define("bad", {300.0, 0.0, 0.0}).has_value() &&
                       !table.define("bad", {-0.1, 0.0, 0.0}).has_value() &&
                       table.size() == 5390U,
                   "invalid definitions must not mutate the table");
  ColorTable copy = table;
  passed &= expect(copy == table && !(copy == ColorTable{}),
                   "tables must compare by value");

  passed &= expect(ColorTable::auto_cycle().size() == 40U &&
                       ColorTable::auto_cycle()[0] == 26 &&
                       ColorTable::auto_cycle()[1] == 5 &&
                       ColorTable::auto_cycle()[39] == 51,
                   "auto cycle must match the reference sequence");
  passed &= expect(ColorTable::element_color(6U) == 26 &&
                       ColorTable::element_color(7U) == 27 &&
                       ColorTable::element_color(8U) == 28 &&
                       ColorTable::element_color(1U) == 29 &&
                       ColorTable::element_color(16U) == 31 &&
                       ColorTable::element_color(15U) == 5291 &&
                       ColorTable::element_color(110U) == kColorDefault &&
                       ColorTable::element_color(0U) == 0 &&
                       ColorTable::element_color(200U) == 0,
                   "element colors must match color atomic");
  return passed ? 0 : 1;
}
