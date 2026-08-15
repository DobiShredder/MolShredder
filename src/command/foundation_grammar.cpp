#include "molshredder/command/foundation_grammar.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace molshredder::command {
namespace {

ParameterSpec required_text(std::string name,
                            std::vector<std::string> allowed = {}) {
  return ParameterSpec{std::move(name), ParameterType::text, true, std::nullopt,
                       std::move(allowed)};
}

ParameterSpec optional_text(std::string name) {
  return ParameterSpec{std::move(name), ParameterType::text, false};
}

ParameterSpec defaulted_text(std::string name, std::string default_value,
                             std::vector<std::string> allowed = {}) {
  return ParameterSpec{std::move(name), ParameterType::text, false,
                       std::move(default_value), std::move(allowed)};
}

ParameterSpec precision() {
  return ParameterSpec{"precision",
                       ParameterType::integer,
                       false,
                       "6",
                       {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10",
                        "11", "12", "13", "14", "15"}};
}

ParameterSpec unit() {
  return defaulted_text("unit", "angstrom", {"angstrom", "nanometer"});
}

} // namespace

std::vector<Descriptor> foundation_command_descriptors() {
  return {
      Descriptor{"load",
                 "Load a molecular structure into a named object",
                 {required_text("path"), optional_text("name"),
                  ParameterSpec{"file-format",
                                ParameterType::text,
                                false,
                                "auto",
                                {"auto", "pdb", "mmcif", "cif", "bcif", "pqr",
                                 "mol", "mol2", "psf", "prmtop", "gro", "g96",
                                 "vtf", "sdf", "xyz"}}},
                 UndoPolicy::undoable},
      Descriptor{"select",
                 "Create or replace a named atom selection",
                 {required_text("name"), required_text("expression"),
                  ParameterSpec{"update",
                                ParameterType::boolean,
                                false,
                                "false",
                                {"false", "true"}}},
                 UndoPolicy::undoable},
      Descriptor{"show",
                 "Show a molecular representation for a selection",
                 {required_text("representation", {"lines", "sticks", "spheres",
                                                   "ribbon", "cartoon"}),
                  defaulted_text("selection", "all"),
                  ParameterSpec{"replace",
                                ParameterType::boolean,
                                false,
                                std::nullopt,
                                {"false", "true"}}},
                 UndoPolicy::undoable},
      Descriptor{"analyze center",
                 "Calculate a geometric centroid or center of mass",
                 {defaulted_text("selection", "all"),
                  defaulted_text("mode", "centroid", {"centroid", "com"}),
                  precision(), unit()},
                 UndoPolicy::not_applicable},
      Descriptor{
          "measure distance",
          "Measure a distance between atoms, selections, centroids or COMs",
          {required_text("from"), required_text("to"),
           defaulted_text("mode", "atom",
                          {"atom", "centroid", "com", "minimum", "maximum",
                           "mean", "closest"}),
           defaulted_text("pbc", "raw", {"raw", "minimum-image"}), precision(),
           unit()},
          UndoPolicy::undoable},
      Descriptor{"analyze contacts",
                 "Find non-bonded atom pairs within a distance cutoff",
                 {defaulted_text("first", "all"), optional_text("second"),
                  ParameterSpec{"cutoff", ParameterType::number, false, "4.0"},
                  defaulted_text("pbc", "raw", {"raw", "minimum-image"}),
                  ParameterSpec{"exclude-bonded",
                                ParameterType::boolean,
                                false,
                                "true",
                                {"false", "true"}},
                  precision(), unit()},
                 UndoPolicy::not_applicable},
      Descriptor{
          "analyze hbonds",
          "Find hydrogen bonds using donor-acceptor distance and D-H-A angle",
          {defaulted_text("donors", "all"), optional_text("acceptors"),
           ParameterSpec{"cutoff", ParameterType::number, false, "3.5"},
           ParameterSpec{"angle", ParameterType::number, false, "30.0"},
           defaulted_text("pbc", "raw", {"raw", "minimum-image"}), precision(),
           unit()},
          UndoPolicy::not_applicable},
      Descriptor{
          "analyze secondary-structure",
          "Assign protein secondary structure with the independent STRIDE "
          "method v0",
          {defaulted_text("selection", "all"),
           ParameterSpec{"energy-cutoff", ParameterType::number, false, "-0.5"},
           ParameterSpec{"helix-propensity", ParameterType::number, false,
                         "0.05"},
           ParameterSpec{"beta-propensity", ParameterType::number, false,
                         "0.02"},
           precision()},
          UndoPolicy::not_applicable},
  };
}

std::vector<AliasSpec> foundation_command_aliases() {
  return {
      AliasSpec{"center", "analyze center", {}},
      AliasSpec{"centroid", "analyze center", {{"mode", "centroid"}}},
      AliasSpec{"com", "analyze center", {{"mode", "com"}}},
      AliasSpec{"dist", "measure distance", {}},
      AliasSpec{"distance", "measure distance", {}},
      AliasSpec{"contacts", "analyze contacts", {}},
      AliasSpec{"hbonds", "analyze hbonds", {}},
      AliasSpec{"ss", "analyze secondary-structure", {}},
      AliasSpec{"open", "load", {}},
  };
}

std::vector<Descriptor> object_command_descriptors() {
  return {
      Descriptor{"object list",
                 "List molecular objects and runtime state",
                 {},
                 UndoPolicy::not_applicable},
      Descriptor{"object activate",
                 "Make one molecular object active",
                 {ParameterSpec{"id", ParameterType::integer, true}},
                 UndoPolicy::undoable},
      Descriptor{"object visibility",
                 "Show or hide a molecular object",
                 {ParameterSpec{"id", ParameterType::integer, true},
                  ParameterSpec{"visible",
                                ParameterType::boolean,
                                true,
                                std::nullopt,
                                {"false", "true"}}},
                 UndoPolicy::undoable},
  };
}

std::vector<Descriptor> file_command_descriptors() {
  return {
      Descriptor{"format list",
                 "List machine-readable native file format capabilities",
                 {defaulted_text("family", "all",
                                 {"all", "structure", "trajectory", "volume"}),
                  defaulted_text("direction", "all", {"all", "read", "write"})},
                 UndoPolicy::not_applicable},
      Descriptor{"volume load",
                 "Load a regular scalar volume into a named scene object",
                 {required_text("path"), optional_text("name"),
                  defaulted_text(
                      "file-format", "auto",
                      {"auto", "ccp4", "dx", "map", "mrc", "mrcs", "opendx"}),
                  defaulted_text("coordinate-unit", "angstrom",
                                 {"angstrom", "nanometer"})},
                 UndoPolicy::undoable},
      Descriptor{"volume list",
                 "List loaded scalar volume objects and grid metadata",
                 {},
                 UndoPolicy::not_applicable},
      Descriptor{"volume save",
                 "Write the active scalar volume with an explicit loss report",
                 {required_text("path"),
                  defaulted_text("file-format", "auto",
                                 {"auto", "ccp4", "dx", "map", "mrc",
                                  "mrcs", "opendx"}),
                  ParameterSpec{"overwrite", ParameterType::boolean, false,
                                "false", {"false", "true"}}},
                 UndoPolicy::not_applicable},
      Descriptor{"volume isosurface",
                 "Create an isosurface representation of the active volume",
                 {ParameterSpec{"level", ParameterType::number, true},
                  defaulted_text("color", "cyan",
                                 {"blue", "cyan", "green", "magenta",
                                  "orange", "red", "white", "yellow"}),
                  ParameterSpec{"opacity", ParameterType::number, false,
                                "1.0"},
                  ParameterSpec{"replace", ParameterType::boolean, false,
                                "true", {"false", "true"}}},
                 UndoPolicy::undoable},
      Descriptor{
          "save",
          "Write the active molecular object with an explicit loss report",
          {required_text("path"),
           defaulted_text("file-format", "auto",
                          {"auto", "g96", "gro", "mmcif", "cif", "mol", "mol2",
                           "pdb", "psf", "pqr", "sdf", "xyz"}),
           defaulted_text("frames", "current", {"current", "all"}), precision(),
           optional_text("comment"),
           ParameterSpec{"overwrite",
                         ParameterType::boolean,
                         false,
                         "false",
                         {"false", "true"}}},
          UndoPolicy::not_applicable},
  };
}

std::vector<Descriptor> trajectory_command_descriptors() {
  return {
      Descriptor{
          "analyze trajectory center",
          "Calculate centroid or center-of-mass values over trajectory frames",
          {defaulted_text("selection", "all"),
           defaulted_text("mode", "centroid", {"centroid", "com"}),
           ParameterSpec{"first", ParameterType::integer, false, "0"},
           ParameterSpec{"last", ParameterType::integer, false},
           ParameterSpec{"stride", ParameterType::integer, false, "1"},
           defaulted_text("missing", "error", {"error", "skip"}), precision(),
           unit()},
          UndoPolicy::not_applicable},
      Descriptor{"analyze trajectory distance",
                 "Calculate an atom distance over trajectory frames",
                 {required_text("from"), required_text("to"),
                  defaulted_text("pbc", "raw", {"raw", "minimum-image"}),
                  ParameterSpec{"first", ParameterType::integer, false, "0"},
                  ParameterSpec{"last", ParameterType::integer, false},
                  ParameterSpec{"stride", ParameterType::integer, false, "1"},
                  precision(), unit()},
                 UndoPolicy::not_applicable},
      Descriptor{
          "analyze trajectory rmsd",
          "Calculate trajectory RMSD against a reference frame",
          {defaulted_text("selection", "all"), optional_text("fit-selection"),
           ParameterSpec{"reference", ParameterType::integer, false, "0"},
           ParameterSpec{"first", ParameterType::integer, false, "0"},
           ParameterSpec{"last", ParameterType::integer, false},
           ParameterSpec{"stride", ParameterType::integer, false, "1"},
           defaulted_text("fit", "rigid", {"none", "rigid"}),
           defaulted_text("weight", "uniform", {"uniform", "mass"}),
           defaulted_text("missing", "error", {"error", "skip"}), precision(),
           unit()},
          UndoPolicy::not_applicable},
      Descriptor{
          "analyze trajectory rmsf",
          "Calculate per-atom trajectory RMSF",
          {defaulted_text("selection", "all"), optional_text("fit-selection"),
           ParameterSpec{"reference", ParameterType::integer, false, "0"},
           ParameterSpec{"first", ParameterType::integer, false, "0"},
           ParameterSpec{"last", ParameterType::integer, false},
           ParameterSpec{"stride", ParameterType::integer, false, "1"},
           defaulted_text("fit", "rigid", {"none", "rigid"}),
           defaulted_text("weight", "uniform", {"uniform", "mass"}),
           defaulted_text("missing", "error", {"error", "skip"}), precision(),
           unit()},
          UndoPolicy::not_applicable},
      Descriptor{"analyze trajectory contacts",
                 "Calculate trajectory contact counts or pair occupancy",
                 {defaulted_text("selection1", "all"),
                  optional_text("selection2"),
                  ParameterSpec{"cutoff", ParameterType::number, false, "4.0"},
                  defaulted_text("pbc", "raw", {"raw", "minimum-image"}),
                  ParameterSpec{"exclude-bonded",
                                ParameterType::boolean,
                                false,
                                "true",
                                {"false", "true"}},
                  defaulted_text("report", "frames", {"frames", "occupancy"}),
                  ParameterSpec{"first", ParameterType::integer, false, "0"},
                  ParameterSpec{"last", ParameterType::integer, false},
                  ParameterSpec{"stride", ParameterType::integer, false, "1"},
                  precision(), unit()},
                 UndoPolicy::not_applicable},
      Descriptor{
          "analyze trajectory hbonds",
          "Calculate trajectory hydrogen-bond counts or triple occupancy",
          {defaulted_text("donors", "all"), optional_text("acceptors"),
           ParameterSpec{"cutoff", ParameterType::number, false, "3.5"},
           ParameterSpec{"angle", ParameterType::number, false, "30.0"},
           defaulted_text("pbc", "raw", {"raw", "minimum-image"}),
           defaulted_text("report", "frames", {"frames", "occupancy"}),
           ParameterSpec{"first", ParameterType::integer, false, "0"},
           ParameterSpec{"last", ParameterType::integer, false},
           ParameterSpec{"stride", ParameterType::integer, false, "1"},
           precision(), unit()},
          UndoPolicy::not_applicable},
      Descriptor{
          "traj load",
          "Attach an indexed trajectory to the active topology",
          {required_text("path"),
           defaulted_text("file-format", "auto",
                          {"auto", "dcd", "trr", "xtc", "rst7", "mdcrd", "crd",
                           "netcdf", "nc", "ncdf", "lammps", "lammpstrj",
                           "dump", "binpos", "h5md"}),
           defaulted_text("coordinate-unit", "auto",
                          {"auto", "angstrom", "nanometer"}),
           optional_text("particle-group"),
           ParameterSpec{"cache-mib", ParameterType::integer, false, "256"},
           ParameterSpec{"prefetch-frames", ParameterType::integer, false,
                         "4"}},
          UndoPolicy::undoable},
      Descriptor{"traj save",
          "Write the current coordinate frame as a trajectory/restart file",
                 {required_text("path"),
                  defaulted_text("file-format", "auto",
                                 {"auto", "rst7", "trr"}),
                  optional_text("title"),
                  ParameterSpec{"overwrite", ParameterType::boolean, false,
                                "false", {"false", "true"}}},
                 UndoPolicy::not_applicable},
      Descriptor{"traj frame",
                 "Seek the active trajectory to a zero-based frame",
                 {ParameterSpec{"frame", ParameterType::integer, true}},
                 UndoPolicy::undoable},
      Descriptor{
          "traj play",
          "Advance deterministic trajectory playback",
          {defaulted_text("mode", "once", {"once", "loop", "rock"}),
           defaulted_text("direction", "forward", {"forward", "reverse"}),
           ParameterSpec{"steps", ParameterType::integer, false, "1"}},
          UndoPolicy::undoable},
      Descriptor{
          "traj range",
          "Configure a zero-based inclusive playback range",
          {ParameterSpec{"first", ParameterType::integer, false, "0"},
           ParameterSpec{"last", ParameterType::integer, false},
           ParameterSpec{"stride", ParameterType::integer, false, "1"},
           defaulted_text("mode", "once", {"once", "loop", "rock"}),
           defaulted_text("direction", "forward", {"forward", "reverse"})},
          UndoPolicy::undoable},
      Descriptor{
          "traj pause", "Pause trajectory playback", {}, UndoPolicy::undoable},
      Descriptor{"traj speed",
                 "Set trajectory playback frames per second",
                 {ParameterSpec{"fps", ParameterType::number, true}},
                 UndoPolicy::undoable},
      Descriptor{"traj tick",
                 "Advance playback from elapsed wall time",
                 {ParameterSpec{"elapsed-ms", ParameterType::number, true}},
                 UndoPolicy::undoable},
  };
}

} // namespace molshredder::command
