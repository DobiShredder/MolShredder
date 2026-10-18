#include "structure_reader_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/core/parse_number.hpp"

// CML (Chemical Markup Language) and PDBML (PDBx/XML) readers. A minimal XML tree parser is
// included so the readers do not need an external dependency. It rejects DOCTYPE internal subsets
// and entity declarations (no XXE or entity-expansion attacks), supports only the five predefined
// entities and numeric character references, and enforces depth and node limits.
// Implemented from the public CML/PDBx descriptions and checked against Open-Source PyMOL 3.1.0
// (`lazyio.load_cml`, `lazyio.load_pdbml`) in SEQ-319 S4.
namespace molshredder::io::detail {
namespace {

using operation::Result;

constexpr std::size_t kMaxDepth = 128U;
constexpr std::size_t kMaxNodes = 20'000'000U;

struct XmlError {
  std::string message;
  std::size_t line{};
};

struct XmlNode {
  std::string name;  // local name, namespace prefix removed
  std::vector<std::pair<std::string, std::string>> attributes;
  std::vector<XmlNode> children;
  std::string text;
  std::size_t line{};

  [[nodiscard]] const std::string *attribute(std::string_view key) const {
    for (const auto &[name_, value] : attributes)
      if (name_ == key) return &value;
    return nullptr;
  }
};

std::string local_name(std::string_view qualified) {
  const auto colon = qualified.rfind(':');
  return std::string{colon == std::string_view::npos ? qualified : qualified.substr(colon + 1U)};
}

class XmlParser {
public:
  explicit XmlParser(std::string_view text) : text_{text} {}

  std::optional<XmlError> parse(XmlNode &root) {
    try {
      if (text_.starts_with("\xEF\xBB\xBF")) position_ = 3U;
      skip_prolog();
      if (position_ >= text_.size() || text_[position_] != '<') fail("no XML root element found");
      root = parse_element(0U);
      skip_misc();
      if (position_ < text_.size()) fail("unexpected content after the root element");
    } catch (const XmlError &error) {
      return error;
    }
    return std::nullopt;
  }

private:
  std::string_view text_;
  std::size_t position_{};
  std::size_t line_{1U};
  std::size_t nodes_{};

  [[noreturn]] void fail(std::string message) const { throw XmlError{std::move(message), line_}; }

  [[nodiscard]] bool starts_with(std::string_view token) const { return text_.substr(position_).starts_with(token); }

  void advance(std::size_t count) {
    for (std::size_t i = 0; i < count && position_ < text_.size(); ++i)
      if (text_[position_++] == '\n') ++line_;
  }

  void skip_space() {
    while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_])) != 0) advance(1U);
  }

  void skip_until(std::string_view terminator, std::string_view what) {
    const auto end = text_.find(terminator, position_);
    if (end == std::string_view::npos) fail(std::string{"unterminated "} + std::string{what});
    advance(end + terminator.size() - position_);
  }

  void skip_misc() {
    for (;;) {
      skip_space();
      if (starts_with("<!--")) skip_until("-->", "comment");
      else if (starts_with("<?")) skip_until("?>", "processing instruction");
      else return;
    }
  }

  void skip_prolog() {
    for (;;) {
      skip_misc();
      if (!starts_with("<!DOCTYPE")) return;
      // Only an external-free, subset-free declaration is tolerated.
      const auto end = text_.find('>', position_);
      const auto bracket = text_.find('[', position_);
      if (end == std::string_view::npos) fail("unterminated DOCTYPE declaration");
      if (bracket != std::string_view::npos && bracket < end)
        fail("DOCTYPE internal subsets and entity declarations are not supported");
      const auto declaration = text_.substr(position_, end - position_);
      if (declaration.find("ENTITY") != std::string_view::npos) fail("entity declarations are not supported");
      advance(end + 1U - position_);
    }
  }

  std::string parse_name() {
    const auto start = position_;
    while (position_ < text_.size()) {
      const auto c = static_cast<unsigned char>(text_[position_]);
      if (std::isalnum(c) != 0 || c == '_' || c == ':' || c == '-' || c == '.' || c >= 0x80U) ++position_;
      else break;
    }
    if (position_ == start) fail("expected an XML name");
    return std::string{text_.substr(start, position_ - start)};
  }

  void append_reference(std::string &out) {
    // text_[position_] == '&'
    const auto semicolon = text_.find(';', position_);
    if (semicolon == std::string_view::npos || semicolon - position_ > 12U) fail("malformed entity reference");
    const auto body = text_.substr(position_ + 1U, semicolon - position_ - 1U);
    if (body == "lt") out.push_back('<');
    else if (body == "gt") out.push_back('>');
    else if (body == "amp") out.push_back('&');
    else if (body == "quot") out.push_back('"');
    else if (body == "apos") out.push_back('\'');
    else if (!body.empty() && body.front() == '#') {
      std::uint32_t code{};
      bool ok = body.size() > 1U;
      if (ok && (body[1] == 'x' || body[1] == 'X')) {
        ok = body.size() > 2U;
        for (std::size_t i = 2; ok && i < body.size(); ++i) {
          const auto c = static_cast<unsigned char>(body[i]);
          if (std::isxdigit(c) == 0) ok = false;
          else code = code * 16U + static_cast<std::uint32_t>(std::isdigit(c) != 0 ? c - '0' : (std::tolower(c) - 'a' + 10));
          if (code > 0x10FFFFU) ok = false;
        }
      } else {
        for (std::size_t i = 1; ok && i < body.size(); ++i) {
          const auto c = static_cast<unsigned char>(body[i]);
          if (std::isdigit(c) == 0) ok = false;
          else code = code * 10U + static_cast<std::uint32_t>(c - '0');
          if (code > 0x10FFFFU) ok = false;
        }
      }
      if (!ok || code == 0U) fail("invalid numeric character reference");
      if (code < 0x80U) out.push_back(static_cast<char>(code));
      else if (code < 0x800U) {
        out.push_back(static_cast<char>(0xC0U | (code >> 6U)));
        out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
      } else if (code < 0x10000U) {
        out.push_back(static_cast<char>(0xE0U | (code >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
      } else {
        out.push_back(static_cast<char>(0xF0U | (code >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((code >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
      }
    } else {
      fail("undefined entity '&" + std::string{body} + ";' (only predefined and numeric references are supported)");
    }
    advance(semicolon + 1U - position_);
  }

  XmlNode parse_element(std::size_t depth) {
    if (depth >= kMaxDepth) fail("XML nesting is deeper than the supported limit");
    if (++nodes_ > kMaxNodes) fail("XML contains more elements than the supported limit");
    XmlNode node;
    node.line = line_;
    advance(1U);  // '<'
    const auto qualified = parse_name();
    node.name = local_name(qualified);
    for (;;) {
      skip_space();
      if (position_ >= text_.size()) fail("unterminated start tag");
      if (text_[position_] == '/' || text_[position_] == '>') break;
      const auto attribute_name = parse_name();
      skip_space();
      if (position_ >= text_.size() || text_[position_] != '=') fail("attribute without a value");
      advance(1U);
      skip_space();
      if (position_ >= text_.size() || (text_[position_] != '"' && text_[position_] != '\'')) fail("attribute value is not quoted");
      const auto quote = text_[position_];
      advance(1U);
      std::string value;
      while (position_ < text_.size() && text_[position_] != quote) {
        if (text_[position_] == '<') fail("'<' is not allowed in an attribute value");
        if (text_[position_] == '&') append_reference(value);
        else {
          value.push_back(text_[position_]);
          advance(1U);
        }
      }
      if (position_ >= text_.size()) fail("unterminated attribute value");
      advance(1U);
      node.attributes.emplace_back(local_name(attribute_name), std::move(value));
    }
    if (text_[position_] == '/') {
      advance(1U);
      if (position_ >= text_.size() || text_[position_] != '>') fail("malformed empty-element tag");
      advance(1U);
      return node;
    }
    advance(1U);  // '>'
    for (;;) {
      if (position_ >= text_.size()) fail("unterminated element <" + qualified + ">");
      if (text_[position_] == '&') {
        append_reference(node.text);
      } else if (starts_with("<!--")) {
        skip_until("-->", "comment");
      } else if (starts_with("<![CDATA[")) {
        advance(9U);
        const auto end = text_.find("]]>", position_);
        if (end == std::string_view::npos) fail("unterminated CDATA section");
        node.text.append(text_.substr(position_, end - position_));
        advance(end + 3U - position_);
      } else if (starts_with("<?")) {
        skip_until("?>", "processing instruction");
      } else if (starts_with("</")) {
        advance(2U);
        const auto closing = parse_name();
        skip_space();
        if (closing != qualified) fail("mismatched end tag </" + closing + "> for <" + qualified + ">");
        if (position_ >= text_.size() || text_[position_] != '>') fail("malformed end tag");
        advance(1U);
        return node;
      } else if (text_[position_] == '<') {
        if (starts_with("<!")) fail("unsupported markup declaration inside an element");
        node.children.push_back(parse_element(depth + 1U));
      } else {
        node.text.push_back(text_[position_]);
        advance(1U);
      }
    }
  }
};

Result<XmlNode> parse_xml(std::string_view content, const std::string &source_name) {
  XmlNode root;
  XmlParser parser{content};
  if (const auto error = parser.parse(root); error.has_value())
    return Result<XmlNode>::failure(parse_error(source_name, error->line, "invalid XML: " + error->message,
                                                "provide a well-formed XML document without DOCTYPE entities"));
  return Result<XmlNode>::success(std::move(root));
}

std::string trimmed(std::string_view value) { return trim(value); }

std::string lowered(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::vector<std::string> split_words(std::string_view text) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) ++i;
    const auto start = i;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) == 0) ++i;
    if (i > start) out.emplace_back(text.substr(start, i - start));
  }
  return out;
}

std::optional<double> number(std::string_view text) {
  const auto value = trimmed(text);
  double result{};
  const auto parsed = molshredder::core::from_chars(value.data(), value.data() + value.size(), result);
  if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return std::nullopt;
  return result;
}

std::optional<long> integer(std::string_view text) {
  const auto value = trimmed(text);
  long result{};
  const auto parsed = molshredder::core::from_chars(value.data(), value.data() + value.size(), result);
  if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return std::nullopt;
  return result;
}

// =================================================================== CML

struct CmlAtom {
  std::string id;
  std::string symbol;
  int charge{};
  model::Vec3d position{};
};

struct CmlBond {
  std::string first;
  std::string second;
  model::BondOrder order{model::BondOrder::single};
};

std::optional<model::BondOrder> cml_order(std::string_view text) {
  const auto value = trimmed(text);
  if (value == "1" || value == "S" || value == "s") return model::BondOrder::single;
  if (value == "2" || value == "D" || value == "d") return model::BondOrder::double_bond;
  if (value == "3" || value == "T" || value == "t") return model::BondOrder::triple;
  if (value == "A" || value == "a" || value == "1.5") return model::BondOrder::aromatic;
  return std::nullopt;
}

void collect_molecules(const XmlNode &node, std::vector<const XmlNode *> &molecules) {
  for (const auto &child : node.children) {
    if (child.name != "molecule") {
      collect_molecules(child, molecules);
      continue;
    }
    const bool has_atoms = std::any_of(child.children.begin(), child.children.end(),
                                       [](const XmlNode &part) { return part.name == "atomArray"; });
    if (has_atoms) molecules.push_back(&child);
    else collect_molecules(child, molecules);
  }
}

Result<StructureData> build_cml_molecule(const XmlNode &molecule, const std::string &source_name, std::size_t index) {
  std::vector<CmlAtom> atoms;
  std::vector<CmlBond> bonds;
  for (const auto &array : molecule.children) {
    if (array.name == "atomArray") {
      // Array form: whitespace separated lists held in attributes of atomArray.
      if (const auto *ids = array.attribute("atomID"); ids != nullptr) {
        const auto id_list = split_words(*ids);
        const auto symbols = array.attribute("elementType") != nullptr ? split_words(*array.attribute("elementType")) : std::vector<std::string>{};
        const auto get_list = [&](const char *key) {
          return array.attribute(key) != nullptr ? split_words(*array.attribute(key)) : std::vector<std::string>{};
        };
        const auto x3 = get_list("x3"), y3 = get_list("y3"), z3 = get_list("z3"), x2 = get_list("x2"), y2 = get_list("y2");
        const auto charges = get_list("formalCharge");
        const bool three = x3.size() == id_list.size() && y3.size() == id_list.size() && z3.size() == id_list.size();
        const bool two = x2.size() == id_list.size() && y2.size() == id_list.size();
        if (!three && !two)
          return Result<StructureData>::failure(parse_error(source_name, array.line, "CML atom arrays need matching x3/y3/z3 (or x2/y2) lists"));
        for (std::size_t i = 0; i < id_list.size(); ++i) {
          const auto x = number(three ? x3[i] : x2[i]);
          const auto y = number(three ? y3[i] : y2[i]);
          const auto z = three ? number(z3[i]) : std::optional<double>{0.0};
          if (!x || !y || !z)
            return Result<StructureData>::failure(parse_error(source_name, array.line, "CML atom array has an invalid coordinate"));
          CmlAtom atom;
          atom.id = id_list[i];
          atom.symbol = i < symbols.size() ? symbols[i] : std::string{};
          if (i < charges.size()) atom.charge = static_cast<int>(integer(charges[i]).value_or(0));
          atom.position = {*x, *y, *z};
          atoms.push_back(std::move(atom));
        }
      }
      for (const auto &node : array.children) {
        if (node.name != "atom") continue;
        CmlAtom atom;
        if (const auto *id = node.attribute("id"); id != nullptr) atom.id = *id;
        if (const auto *symbol = node.attribute("elementType"); symbol != nullptr) atom.symbol = trimmed(*symbol);
        if (const auto *charge = node.attribute("formalCharge"); charge != nullptr) {
          const auto parsed = integer(*charge);
          if (!parsed) return Result<StructureData>::failure(parse_error(source_name, node.line, "CML atom has an invalid formalCharge"));
          atom.charge = static_cast<int>(*parsed);
        }
        const auto *x3 = node.attribute("x3");
        const auto *x2 = node.attribute("x2");
        if (x3 != nullptr) {
          const auto *y3 = node.attribute("y3");
          const auto *z3 = node.attribute("z3");
          const auto x = number(*x3);
          const auto y = y3 != nullptr ? number(*y3) : std::nullopt;
          const auto z = z3 != nullptr ? number(*z3) : std::nullopt;
          if (!x || !y || !z) return Result<StructureData>::failure(parse_error(source_name, node.line, "CML atom has an invalid x3/y3/z3 coordinate"));
          atom.position = {*x, *y, *z};
        } else if (x2 != nullptr) {
          const auto *y2 = node.attribute("y2");
          const auto x = number(*x2);
          const auto y = y2 != nullptr ? number(*y2) : std::nullopt;
          if (!x || !y) return Result<StructureData>::failure(parse_error(source_name, node.line, "CML atom has an invalid x2/y2 coordinate"));
          atom.position = {*x, *y, 0.0};
        } else {
          return Result<StructureData>::failure(parse_error(source_name, node.line, "CML atom has no coordinates",
                                                            "provide x3/y3/z3 or x2/y2 attributes"));
        }
        atoms.push_back(std::move(atom));
      }
    } else if (array.name == "bondArray") {
      if (const auto *refs1 = array.attribute("atomRef1"); refs1 != nullptr && array.attribute("atomRef2") != nullptr) {
        const auto first = split_words(*refs1);
        const auto second = split_words(*array.attribute("atomRef2"));
        const auto orders = array.attribute("order") != nullptr ? split_words(*array.attribute("order")) : std::vector<std::string>{};
        if (first.size() != second.size())
          return Result<StructureData>::failure(parse_error(source_name, array.line, "CML bond array atomRef1/atomRef2 lists differ in length"));
        for (std::size_t i = 0; i < first.size(); ++i) {
          auto order = model::BondOrder::single;
          if (i < orders.size()) {
            const auto parsed = cml_order(orders[i]);
            if (!parsed) return Result<StructureData>::failure(parse_error(source_name, array.line, "CML bond has an unsupported order '" + orders[i] + "'"));
            order = *parsed;
          }
          bonds.push_back({first[i], second[i], order});
        }
      }
      for (const auto &node : array.children) {
        if (node.name != "bond") continue;
        const auto *refs = node.attribute("atomRefs2");
        if (refs == nullptr) refs = node.attribute("atomsRefs2");  // spelling used by PyMOL's loader
        if (refs == nullptr) continue;
        const auto pair = split_words(*refs);
        if (pair.size() != 2U)
          return Result<StructureData>::failure(parse_error(source_name, node.line, "CML bond atomRefs2 must name two atoms"));
        auto order = model::BondOrder::single;
        if (const auto *text = node.attribute("order"); text != nullptr) {
          const auto parsed = cml_order(*text);
          if (!parsed) return Result<StructureData>::failure(parse_error(source_name, node.line, "CML bond has an unsupported order '" + *text + "'"));
          order = *parsed;
        }
        bonds.push_back({pair[0], pair[1], order});
      }
    }
  }
  if (atoms.empty())
    return Result<StructureData>::failure(parse_error(source_name, molecule.line, "CML molecule contains no atoms"));

  model::TopologyBuilder builder;
  const auto residue = builder.add_residue({"UNK", 0, "", "", ""});
  if (!residue.has_value()) return Result<StructureData>::failure(residue.error());
  std::map<std::string, std::size_t> by_id;
  std::vector<model::Vec3d> positions;
  for (std::size_t i = 0; i < atoms.size(); ++i) {
    const auto &atom = atoms[i];
    const auto element = atomic_number(atom.symbol);
    const auto name = atom.id.empty() ? (atom.symbol.empty() ? std::string{"X"} : atom.symbol) : atom.id;
    const auto added = builder.add_atom({name, element.value_or(0U), residue.value(), "", atom.charge});
    if (!added.has_value()) return Result<StructureData>::failure(added.error());
    if (!atom.id.empty() && !by_id.emplace(atom.id, i).second)
      return Result<StructureData>::failure(parse_error(source_name, molecule.line, "CML atom id '" + atom.id + "' is duplicated"));
    positions.push_back(atom.position);
  }
  std::set<std::pair<std::size_t, std::size_t>> seen;
  for (const auto &bond : bonds) {
    const auto first = by_id.find(bond.first);
    const auto second = by_id.find(bond.second);
    if (first == by_id.end() || second == by_id.end())
      return Result<StructureData>::failure(parse_error(source_name, molecule.line, "a CML bond references an unknown atom id"));
    const auto lo = std::min(first->second, second->second), hi = std::max(first->second, second->second);
    if (lo == hi || !seen.insert({lo, hi}).second) continue;
    const auto error = builder.add_bond({model::AtomIndex{lo}, model::AtomIndex{hi}, bond.order, model::BondQuery::none,
                                         model::BondStereo::none, model::ChemicalAnnotationOrigin::explicit_input});
    if (error.has_value()) return Result<StructureData>::failure(*error);
  }
  builder.set_source_metadata("format", "cml");
  const auto topology = builder.build();
  if (!topology.has_value()) return Result<StructureData>::failure(topology.error());

  model::FrameMetadata metadata;
  metadata.source_step = 1U;
  metadata.coordinate_unit = operation::LengthUnit::angstrom;
  const auto frame = model::CoordinateFrame::create(model::CoordinateBuffer{std::move(positions)}, std::nullopt, {}, std::move(metadata));
  if (!frame.has_value()) return Result<StructureData>::failure(frame.error());
  const auto coordinates = model::InMemoryCoordinateSource::create(topology.value()->atom_count(), {frame.value()});
  if (!coordinates.has_value()) return Result<StructureData>::failure(coordinates.error());
  StructureData structure;
  if (const auto *id = molecule.attribute("id"); id != nullptr && !id->empty()) structure.name = *id;
  else if (const auto *title = molecule.attribute("title"); title != nullptr && !title->empty()) structure.name = *title;
  else structure.name = "cml_structure" + (index == 0U ? std::string{} : "_" + std::to_string(index + 1U));
  structure.topology = topology.value();
  structure.coordinates = coordinates.value();
  structure.metadata.emplace("format", "cml");
  return Result<StructureData>::success(std::move(structure));
}

// =================================================================== PDBML

std::string pdbml_value(const XmlNode &element) {
  if (const auto *nil = element.attribute("nil"); nil != nullptr && (*nil == "true" || *nil == "1")) return "?";
  const auto value = trimmed(element.text);
  return value.empty() ? std::string{"?"} : value;
}

} // namespace

Result<StructureDocument> read_cml(std::string_view content, std::string source_name) {
  const auto root = parse_xml(content, source_name);
  if (!root.has_value()) return Result<StructureDocument>::failure(root.error());
  if (root.value().name != "cml" && root.value().name != "molecule")
    return Result<StructureDocument>::failure(parse_error(source_name, root.value().line, "XML root element is not <cml>",
                                                          "provide a Chemical Markup Language document"));
  std::vector<const XmlNode *> molecules;
  if (root.value().name == "molecule") {
    const bool has_atoms = std::any_of(root.value().children.begin(), root.value().children.end(),
                                       [](const XmlNode &part) { return part.name == "atomArray"; });
    if (has_atoms) molecules.push_back(&root.value());
    else collect_molecules(root.value(), molecules);
  } else {
    collect_molecules(root.value(), molecules);
  }
  if (molecules.empty())
    return Result<StructureDocument>::failure(parse_error(source_name, root.value().line, "CML document contains no <molecule> with an <atomArray>"));
  StructureDocument document;
  document.format = StructureFormat::cml;
  document.source_name = source_name;
  for (std::size_t i = 0; i < molecules.size(); ++i) {
    auto structure = build_cml_molecule(*molecules[i], source_name, i);
    if (!structure.has_value()) return Result<StructureDocument>::failure(structure.error());
    document.structures.push_back(std::move(structure.value()));
  }
  return Result<StructureDocument>::success(std::move(document));
}

Result<StructureDocument> read_pdbml(std::string_view content, std::string source_name) {
  const auto root = parse_xml(content, source_name);
  if (!root.has_value()) return Result<StructureDocument>::failure(root.error());
  if (root.value().name != "datablock")
    return Result<StructureDocument>::failure(parse_error(source_name, root.value().line, "XML root element is not a PDBx datablock",
                                                          "provide a PDBML (PDBx/XML) document"));
  CifBlock block;
  if (const auto *name = root.value().attribute("datablockName"); name != nullptr) block.name = *name;
  block.line = root.value().line;
  constexpr std::string_view suffix = "Category";
  for (const auto &category : root.value().children) {
    if (category.name.size() <= suffix.size() || !category.name.ends_with(suffix)) continue;
    const auto category_name = "_" + lowered(category.name.substr(0U, category.name.size() - suffix.size()));
    if (category.children.empty()) continue;
    std::vector<std::string> columns;
    std::map<std::string, std::size_t> column_index;
    std::vector<std::map<std::size_t, std::string>> rows;
    const auto column_of = [&](const std::string &name) {
      const auto key = category_name + "." + lowered(name);
      const auto found = column_index.find(key);
      if (found != column_index.end()) return found->second;
      column_index.emplace(key, columns.size());
      columns.push_back(key);
      return columns.size() - 1U;
    };
    for (const auto &row : category.children) {
      std::map<std::size_t, std::string> values;
      for (const auto &[name, value] : row.attributes) {
        if (name == "xmlns" || name == "nil" || name == "schemaLocation") continue;
        values[column_of(name)] = trimmed(value).empty() ? std::string{"?"} : trimmed(value);
      }
      for (const auto &item : row.children) values[column_of(item.name)] = pdbml_value(item);
      rows.push_back(std::move(values));
    }
    const bool as_loop = category_name == "_atom_site" || category_name == "_struct_conn" || rows.size() > 1U;
    if (!as_loop) {
      for (const auto &[index, value] : rows.front())
        block.scalars.emplace(columns[index], CifToken{value, block.line, true});
      continue;
    }
    CifLoop loop;
    loop.columns = columns;
    loop.line = block.line;
    for (const auto &row : rows)
      for (std::size_t column = 0; column < columns.size(); ++column) {
        const auto found = row.find(column);
        loop.values.push_back(CifToken{found == row.end() ? std::string{"?"} : found->second, block.line, true});
      }
    block.loops.push_back(std::move(loop));
  }
  const bool has_atoms = std::any_of(block.loops.begin(), block.loops.end(), [](const CifLoop &loop) {
    return !loop.columns.empty() && loop.columns.front().starts_with("_atom_site.");
  });
  if (!has_atoms)
    return Result<StructureDocument>::failure(parse_error(source_name, root.value().line, "no PDBx:atom_site rows found in the PDBML document"));
  return build_cif_document({block}, std::move(source_name), StructureFormat::pdbml, "PDBML");
}

} // namespace molshredder::io::detail
