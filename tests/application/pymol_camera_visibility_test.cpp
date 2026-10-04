#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "molshredder/application/default_registry.hpp"
#include "molshredder/application/dispatcher.hpp"
#include "molshredder/application/workspace.hpp"
#include "molshredder/operation/task_context.hpp"
#include "molshredder/scene/pymol_view.hpp"

namespace {

using namespace molshredder;

bool expect(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "FAIL: " << message << '\n';
  return condition;
}

// Expected values: PyMOL 3.1.0 black-box capture (SEQ-311 S7) on 1ubq.pdb,
// 640x480 viewport, field_of_view 20. Each operation starts from the
// orient("all") baseline; get_view() is compared within 1e-4.
struct CameraCase {
  std::string_view name;
  std::string_view command;
  command::Arguments arguments;
  std::array<double, 18> expected;
};

const std::array<double, 18> kBaseline{0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 145.1205291748047, -20.0};

const std::vector<CameraCase> &camera_cases() {
  static const std::vector<CameraCase> cases{
      {"orient_subset", "view orient", {{"selection", "resi 1-10"}},
       {0.20645129680633545, -0.1222289502620697, -0.9707924127578735, 0.7044366598129272, 0.7071604132652283, 0.06077137961983681, 0.6790779232978821, -0.6964080929756165, 0.23209673166275024, 0.0, 0.0, -66.19513702392578, 27.86815071105957, 35.29764938354492, 11.42301082611084, 52.188751220703125, 80.20152282714844, -20.0}},
      {"zoom_all", "view zoom", {},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 145.1205291748047, -20.0}},
      {"zoom_subset", "view zoom", {{"selection", "resi 1-10"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -66.19513702392578, 27.86815071105957, 35.29764938354492, 11.42301082611084, 52.188751220703125, 80.20152282714844, -20.0}},
      {"zoom_buffer", "view zoom", {{"buffer", "5"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -148.1331024169922, 30.295255661010742, 28.771011352539062, 15.13113784790039, 116.78926849365234, 179.47694396972656, -20.0}},
      {"zoom_complete", "view zoom", {{"complete", "true"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -147.2821044921875, 30.295255661010742, 28.771011352539062, 15.13113784790039, 116.11833190917969, 178.4458770751953, -20.0}},
      {"center_move_origin", "view center", {{"selection", "resi 30"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 36.16600036621094, 30.86487579345703, 13.368875503540039, 94.43284606933594, 145.1205291748047, -20.0}},
      {"center_keep_origin", "view center", {{"move-origin", "false"}, {"selection", "resi 30"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, -3.5212347507476807, -2.771435022354126, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43284606933594, 145.1205291748047, -20.0}},
      {"turn_x", "view turn", {{"angle", "30"}, {"axis", "x"}},
       {0.6016298532485962, 0.4044992923736572, -0.6887829303741455, 0.5113301277160645, 0.46743708848953247, 0.7211408019065857, 0.6136636734008789, -0.786055326461792, 0.07439149916172028, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 145.1205291748047, -20.0}},
      {"turn_y", "view turn", {{"angle", "-45"}, {"axis", "y"}},
       {0.990220308303833, 0.0059151966124773026, -0.13938722014427185, 0.08522221446037292, 0.7653828263282776, 0.6379078030586243, 0.11045791953802109, -0.6435481309890747, 0.7573935985565186, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 145.1205291748047, -20.0}},
      {"turn_z", "view turn", {{"angle", "90"}, {"axis", "z"}},
       {-0.005915222689509392, 0.6016298532485962, -0.7987530827522278, -0.7653828263282776, 0.5113300681114197, 0.3908076882362366, 0.6435481309890747, 0.6136636734008789, 0.45745256543159485, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 145.1205291748047, -20.0}},
      {"move_z", "view move", {{"axis", "z"}, {"distance", "10"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -109.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 84.43285369873047, 135.1205291748047, -20.0}},
      {"move_x", "view move", {{"axis", "x"}, {"distance", "2"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 2.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 145.1205291748047, -20.0}},
      {"clip_near", "view clip", {{"distance", "5"}, {"mode", "near"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 89.43285369873047, 145.1205291748047, -20.0}},
      {"clip_far", "view clip", {{"distance", "-5"}, {"mode", "far"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 150.1205291748047, -20.0}},
      {"clip_slab", "view clip", {{"distance", "20"}, {"mode", "slab"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 109.77668762207031, 129.7766876220703, -20.0}},
      {"clip_move", "view clip", {{"distance", "-3"}, {"mode", "move"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 97.43285369873047, 148.1205291748047, -20.0}},
      {"clip_atoms", "view clip", {{"distance", "2"}, {"mode", "atoms"}, {"selection", "resi 1-10"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 106.88313293457031, 126.83866882324219, -20.0}},
      {"origin_selection", "view origin", {{"selection", "resi 50"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, -0.9150403738021851, -6.1225996017456055, -116.06383514404297, 26.742874145507812, 25.06800079345703, 20.208250045776367, 94.43285369873047, 145.1205291748047, -20.0}},
      {"origin_position", "view origin", {{"position", "1,2,3"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, -38.75816345214844, -12.8563871383667, -112.38874816894531, 1.0, 2.0, 3.0, 94.43285369873047, 145.1205291748047, -20.0}},
      {"orthoscopic", "view projection", {{"mode", "orthographic"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 145.1205291748047, 20.0}},
      {"field_of_view", "view projection", {{"field-of-view-degrees", "30"}, {"mode", "perspective"}, {"preserve-scale", "false"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 145.1205291748047, -30.0}},
      {"reset", "view reset", {},
       {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 145.1205291748047, -20.0}},
  };
  return cases;
}

struct VisibilityStep {
  std::string_view command;
  std::string_view representation;
  std::string_view selection;
  // lines, sticks, spheres, ribbon, cartoon, labels (RepresentationKind order)
  std::array<std::size_t, 6> counts;
};

// PyMOL count_atoms("rep X") after each step, starting from hide everything;
// show lines.
constexpr std::array<VisibilityStep, 14> kVisibilitySteps{{
    {"show", "sticks", "resi 1-10", {660, 78, 0, 0, 0, 0}},
    {"hide", "sticks", "resi 5", {660, 71, 0, 0, 0, 0}},
    {"as", "spheres", "resi 1-3", {635, 46, 25, 0, 0, 0}},
    {"toggle", "sticks", "resi 1-20", {635, 0, 25, 0, 0, 0}},
    {"toggle", "sticks", "resi 1-20", {635, 155, 25, 0, 0, 0}},
    {"show", "licorice", "resi 40", {635, 164, 25, 0, 0, 0}},
    {"show", "wire", "resi 41", {635, 164, 25, 0, 0, 0}},
    {"hide", "everything", "resi 1-2", {635, 147, 8, 0, 0, 0}},
    {"show", "cartoon", "all", {635, 147, 8, 0, 660, 0}},
    {"as", "ribbon", "resi 60-70", {546, 147, 8, 89, 571, 0}},
    {"hide", "lines", "all", {0, 147, 8, 89, 571, 0}},
    {"toggle", "spheres", "resi 3-4", {0, 147, 0, 89, 571, 0}},
    {"show", "wire", "resi 41", {9, 147, 0, 89, 571, 0}},
    {"show", "licorice", "resi 42", {9, 158, 0, 89, 571, 0}},
}};


// Two-state capture: state 2 is state 1 translated by (12, -7, 5); baseline is
// orient("all", state=1). PyMOL zoom/center without state frame all states,
// while MolShredder keeps current-state framing by default (not compared).
const std::array<double, 18> kTwoStateBaseline{0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 145.1205291748047, -20.0};

const std::vector<CameraCase> &two_state_cases() {
  static const std::vector<CameraCase> cases{
      {"zoom_state_all", "view zoom", {{"state", "all"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -133.95484924316406, 36.29526138305664, 25.27099609375, 17.631145477294922, 105.61102294921875, 162.29867553710938, -20.0}},
      {"zoom_state_current", "view zoom", {{"state", "current"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 145.1205291748047, -20.0}},
      {"zoom_state_1", "view zoom", {{"state", "1"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43285369873047, 145.1205291748047, -20.0}},
      {"zoom_state_2", "view zoom", {{"state", "2"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77662658691406, 42.295257568359375, 21.771013259887695, 20.131149291992188, 94.43280792236328, 145.12045288085938, -20.0}},
      {"zoom_state_2_subset", "view zoom", {{"selection", "resi 1-10"}, {"state", "2"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -66.19512939453125, 39.86815643310547, 28.297651290893555, 16.423011779785156, 52.188743591308594, 80.2015151977539, -20.0}},
      {"center_state_all", "view center", {{"state", "0"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 36.29526138305664, 25.27099609375, 17.631145477294922, 94.43284606933594, 145.1205291748047, -20.0}},
      {"center_state_current", "view center", {{"state", "-1"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43284606933594, 145.1205291748047, -20.0}},
      {"center_state_2", "view center", {{"selection", "resi 30"}, {"state", "2"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, 0.0, 0.0, -119.77668762207031, 48.16600036621094, 23.86487579345703, 18.36887550354004, 94.43284606933594, 145.1205291748047, -20.0}},
      {"center_state_2_keep_origin", "view center", {{"move-origin", "false"}, {"selection", "resi 30"}, {"state", "2"}},
       {0.6016298532485962, 0.0059151966124773026, -0.7987531423568726, 0.5113301277160645, 0.7653828263282776, 0.39080771803855896, 0.6136636734008789, -0.6435481309890747, 0.45745259523391724, -10.229801177978516, 5.733002662658691, -119.77668762207031, 30.295255661010742, 28.771011352539062, 15.13113784790039, 94.43284606933594, 145.1205291748047, -20.0}},
  };
  return cases;
}

struct ClipCase {
  std::string_view mode;
  std::string_view distance;
  double near_clip;
  double far_clip;
};

// PyMOL get_clip() after clip from the two-state baseline.
const std::array<ClipCase, 3> kClipCases{{
    {"near", "5", 89.43285369873047, 145.1205291748047},
    {"slab", "20", 109.77668762207031, 129.7766876220703},
    {"move", "-3", 97.43285369873047, 148.1205291748047},
}};

// PyMOL count_atoms("rep X") over both 1ubq copies (lines, sticks, spheres)
// while the representation bits change independently of enable/disable.
constexpr std::array<std::array<std::size_t, 3>, 6> kIndependenceCounts{{
    {0, 78, 0}, // show sticks prot resi 1-10
    {0, 78, 0}, // disable prot
    {0, 78, 7}, // show spheres prot resi 5 while disabled
    {0, 61, 7}, // hide sticks prot resi 1-2 while disabled
    {0, 61, 7}, // enable prot
    {660, 61, 7}, // disable other; show lines other
}};

std::string view_text(const std::array<double, 18> &values) {
  std::ostringstream text;
  text << std::setprecision(17);
  for (std::size_t index = 0; index < values.size(); ++index)
    text << (index == 0U ? "" : ",") << values[index];
  return text.str();
}

std::size_t mask_count(const std::vector<std::uint64_t> &mask) {
  std::size_t count{};
  for (const auto word : mask)
    count += static_cast<std::size_t>(std::popcount(word));
  return count;
}

// Writes 1ubq as two MODEL records, the second translated by (12, -7, 5).
std::filesystem::path write_two_state(const std::filesystem::path &source) {
  std::ifstream input{source};
  std::vector<std::string> atoms;
  for (std::string line; std::getline(input, line);) {
    if (line.rfind("ATOM  ", 0) == 0 || line.rfind("HETATM", 0) == 0)
      atoms.push_back(line);
  }
  const auto path = std::filesystem::temp_directory_path() /
                    "molshredder_pymol_two_state_1ubq.pdb";
  std::ofstream output{path};
  for (int model = 1; model <= 2; ++model) {
    output << "MODEL        " << model << '\n';
    for (auto line : atoms) {
      if (model == 2) {
        const std::array<double, 3> shift{12.0, -7.0, 5.0};
        for (std::size_t axis = 0; axis < 3U; ++axis) {
          const auto column = 30U + 8U * axis;
          const auto value = std::stod(line.substr(column, 8U)) + shift[axis];
          char field[16];
          std::snprintf(field, sizeof(field), "%8.3f", value);
          line.replace(column, 8U, field);
        }
      }
      output << line << '\n';
    }
    output << "ENDMDL\n";
  }
  output << "END\n";
  return path;
}

std::size_t visible_total(const application::Workspace &workspace,
                          render::RepresentationKind kind) {
  std::size_t count{};
  for (const auto &object : workspace.objects())
  {
    const auto visible = object.representation_visibility.visible_count(kind);
    count += visible.has_value() ? visible.value() : 0U;
  }
  return count;
}

} // namespace

int main(int argc, char **argv) {
  bool passed = true;
  if (argc != 2) {
    std::cerr << "expected PDB fixture path\n";
    return 1;
  }
  auto workspace = std::make_shared<application::Workspace>();
  auto registry = application::make_default_registry(workspace);
  const application::Dispatcher dispatcher{registry};
  operation::TaskContext context;
  const auto run = [&](std::string_view name, command::Arguments arguments) {
    return dispatcher.dispatch({std::string{name}, std::move(arguments)},
                               context);
  };
  passed &= expect(run("load", {{"file-format", "pdb"},
                                {"path", std::filesystem::path{argv[1]}.string()}})
                       .succeeded(),
                   "1ubq fixture must load");

  const auto baseline = view_text(kBaseline);
  // set_view followed by get_view returns the same 18 values in PyMOL.
  const auto restored = run("view import-pymol", {{"values", baseline}});
  const auto round_trip = scene::to_pymol_view(workspace->camera());
  double round_trip_difference = 0.0;
  if (round_trip.has_value()) {
    for (std::size_t index = 0; index < kBaseline.size(); ++index)
      round_trip_difference =
          std::max(round_trip_difference,
                   std::abs(round_trip.value().values[index] - kBaseline[index]));
  }
  passed &= expect(restored.succeeded() && round_trip.has_value() &&
                       round_trip_difference < 1.0e-6,
                   "PyMOL 18-value view must round-trip through import/export");
  for (const auto &item : camera_cases()) {
    const auto imported = run("view import-pymol", {{"values", baseline}});
    const auto applied = run(item.command, item.arguments);
    const auto view = scene::to_pymol_view(workspace->camera());
    double difference = 0.0;
    if (view.has_value()) {
      for (std::size_t index = 0; index < item.expected.size(); ++index)
        difference = std::max(
            difference, std::abs(view.value().values[index] - item.expected[index]));
    }
    passed &= expect(imported.succeeded() && applied.succeeded() &&
                         view.has_value() && difference < 1.0e-4,
                     std::string{"PyMOL camera parity: "} +
                         std::string{item.name} +
                         " max difference " + std::to_string(difference));
  }

  passed &= expect(run("hide", {{"representation", "everything"},
                                {"selection", "all"}})
                           .succeeded() &&
                       run("show", {{"representation", "lines"},
                                    {"selection", "all"}})
                           .succeeded(),
                   "visibility sequence must start from lines only");
  for (const auto &step : kVisibilitySteps) {
    const auto applied =
        run(step.command, {{"representation", std::string{step.representation}},
                           {"selection", std::string{step.selection}}});
    const auto snapshot = workspace->representation_visibility_snapshot();
    bool matches = applied.succeeded() && snapshot.has_value();
    std::string observed;
    for (std::size_t kind = 0; matches && kind < step.counts.size(); ++kind) {
      const auto count = mask_count(snapshot.value().masks[kind]);
      observed += std::to_string(count) + ' ';
      matches &= count == step.counts[kind];
    }
    passed &= expect(matches, std::string{"PyMOL visibility parity: "} +
                                  std::string{step.command} + ' ' +
                                  std::string{step.representation} + ' ' +
                                  std::string{step.selection} +
                                  " observed " + observed);
  }

  // Explicit state scopes and get_clip on a two-state copy of 1ubq.
  auto states = std::make_shared<application::Workspace>();
  auto states_registry = application::make_default_registry(states);
  const application::Dispatcher states_dispatcher{states_registry};
  const auto run_states = [&](std::string_view name,
                              command::Arguments arguments) {
    return states_dispatcher.dispatch({std::string{name}, std::move(arguments)},
                                      context);
  };
  const auto two_state = write_two_state(argv[1]);
  passed &= expect(run_states("load", {{"file-format", "pdb"},
                                       {"path", two_state.string()}})
                           .succeeded() &&
                       states->active_object() != nullptr &&
                       states->active_object()->system->coordinates()
                               ->frame_count() == 2U,
                   "two-state 1ubq must load as two frames");
  const auto two_state_baseline = view_text(kTwoStateBaseline);
  for (const auto &item : two_state_cases()) {
    const auto imported =
        run_states("view import-pymol", {{"values", two_state_baseline}});
    const auto applied = run_states(item.command, item.arguments);
    const auto view = scene::to_pymol_view(states->camera());
    double difference = 0.0;
    if (view.has_value()) {
      for (std::size_t index = 0; index < item.expected.size(); ++index)
        difference = std::max(
            difference, std::abs(view.value().values[index] - item.expected[index]));
    }
    passed &= expect(imported.succeeded() && applied.succeeded() &&
                         view.has_value() && difference < 1.0e-4,
                     std::string{"PyMOL state-scope parity: "} +
                         std::string{item.name} +
                         " max difference " + std::to_string(difference));
  }
  for (const auto &item : kClipCases) {
    const auto imported =
        run_states("view import-pymol", {{"values", two_state_baseline}});
    const auto clipped = run_states(
        "view clip", {{"mode", std::string{item.mode}},
                      {"distance", std::string{item.distance}}});
    const auto queried = run_states("view get-clip", {});
    const auto *response =
        queried.succeeded()
            ? std::get_if<command::Response>(&queried.envelope.payload)
            : nullptr;
    const auto field = [&](std::string_view key) {
      const auto found = response != nullptr ? response->fields.find(key)
                                             : decltype(response->fields.end()){};
      return response != nullptr && found != response->fields.end() &&
                     std::holds_alternative<double>(found->second.data)
                 ? std::get<double>(found->second.data)
                 : std::numeric_limits<double>::quiet_NaN();
    };
    passed &= expect(imported.succeeded() && clipped.succeeded() &&
                         std::abs(field("near_clip") - item.near_clip) < 1.0e-4 &&
                         std::abs(field("far_clip") - item.far_clip) < 1.0e-4,
                     std::string{"PyMOL get_clip parity: "} +
                         std::string{item.mode});
  }
  std::error_code removed;
  std::filesystem::remove(two_state, removed);

  // Representation bits are independent of object enable/disable.
  auto pair = std::make_shared<application::Workspace>();
  auto pair_registry = application::make_default_registry(pair);
  const application::Dispatcher pair_dispatcher{pair_registry};
  const auto run_pair = [&](std::string_view name,
                            command::Arguments arguments) {
    return pair_dispatcher.dispatch({std::string{name}, std::move(arguments)},
                                    context);
  };
  const auto source = std::filesystem::path{argv[1]}.string();
  bool independence =
      run_pair("load", {{"file-format", "pdb"}, {"name", "prot"}, {"path", source}})
          .succeeded() &&
      run_pair("hide", {{"representation", "everything"}, {"selection", "all"}})
          .succeeded() &&
      run_pair("load",
               {{"file-format", "pdb"}, {"name", "other"}, {"path", source}})
          .succeeded() &&
      run_pair("hide", {{"representation", "everything"}, {"selection", "all"}})
          .succeeded() &&
      pair->objects().size() == 2U;
  const auto prot = independence ? std::to_string(pair->objects()[0].id) : "";
  const auto other = independence ? std::to_string(pair->objects()[1].id) : "";
  const auto counts_match = [&](std::size_t step) {
    const auto &expected = kIndependenceCounts[step];
    return visible_total(*pair, render::RepresentationKind::lines) == expected[0] &&
           visible_total(*pair, render::RepresentationKind::sticks) == expected[1] &&
           visible_total(*pair, render::RepresentationKind::spheres) == expected[2];
  };
  const auto object_visible = [&](std::size_t index) {
    return pair->list_objects()[index].visible;
  };
  independence &=
      run_pair("object activate", {{"id", prot}}).succeeded() &&
      run_pair("show", {{"representation", "sticks"}, {"selection", "resi 1-10"}})
          .succeeded() &&
      counts_match(0U);
  independence &=
      run_pair("object visibility", {{"id", prot}, {"visible", "false"}})
          .succeeded() &&
      counts_match(1U) && !object_visible(0U) && object_visible(1U);
  independence &=
      run_pair("show", {{"representation", "spheres"}, {"selection", "resi 5"}})
          .succeeded() &&
      counts_match(2U);
  independence &=
      run_pair("hide", {{"representation", "sticks"}, {"selection", "resi 1-2"}})
          .succeeded() &&
      counts_match(3U);
  independence &=
      run_pair("object visibility", {{"id", prot}, {"visible", "true"}})
          .succeeded() &&
      counts_match(4U) && object_visible(0U);
  independence &=
      run_pair("object visibility", {{"id", other}, {"visible", "false"}})
          .succeeded() &&
      run_pair("object activate", {{"id", other}}).succeeded() &&
      run_pair("show", {{"representation", "lines"}, {"selection", "all"}})
          .succeeded() &&
      counts_match(5U) && object_visible(0U) && !object_visible(1U);
  passed &= expect(independence,
                   "representation bits must stay independent of object "
                   "enable/disable like PyMOL");
  return passed ? 0 : 1;
}
