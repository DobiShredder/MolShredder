#pragma once

#include <cstddef>
#include <type_traits>

// Minimal declaration of the public VMD plugin ABI 18 prefix used by
// MolShredder. Field order and constants follow the official public headers:
// https://www.ks.uiuc.edu/Research/vmd/plugins/doxygen/vmdplugin_8h-source.html
// https://www.ks.uiuc.edu/Research/vmd/plugins/doxygen/molfile__plugin_8h-source.html
// The UIUC Open Source License notice is reproduced in THIRD_PARTY_NOTICES.md.

namespace molshredder::io::abi18 {

inline constexpr int kAbiVersion = 18;
inline constexpr int kSuccess = 0;
inline constexpr int kError = -1;
inline constexpr int kThreadUnsafe = 0;
inline constexpr int kThreadSafe = 1;
inline constexpr const char* kMolfilePluginType = "mol file reader";

inline constexpr int kInsertion = 0x0001;
inline constexpr int kOccupancy = 0x0002;
inline constexpr int kBFactor = 0x0004;
inline constexpr int kMass = 0x0008;
inline constexpr int kCharge = 0x0010;
inline constexpr int kRadius = 0x0020;
inline constexpr int kAltLoc = 0x0040;
inline constexpr int kAtomicNumber = 0x0080;

struct PluginHeader {
  int abi_version;
  const char* type;
  const char* name;
  const char* pretty_name;
  const char* author;
  int major_version;
  int minor_version;
  int is_reentrant;
};

struct Atom {
  char name[16];
  char type[16];
  char residue_name[8];
  int residue_id;
  char segment_id[8];
  char chain[2];
  char alternate_location[2];
  char insertion_code[2];
  float occupancy;
  float b_factor;
  float mass;
  float charge;
  float radius;
  int atomic_number;
};

struct Timestep {
  float* coordinates;
  float* velocities;
  float a;
  float b;
  float c;
  float alpha;
  float beta;
  float gamma;
  double physical_time;
};

using ReadBonds = int (*)(void*, int*, int**, int**, float**, int**, int*,
                          char***);

// This is the ABI-stable prefix through close_file_read. Later ABI 18 fields
// are intentionally not declared until their channel adapters are implemented.
struct MolfilePlugin {
  int abi_version;
  const char* type;
  const char* name;
  const char* pretty_name;
  const char* author;
  int major_version;
  int minor_version;
  int is_reentrant;
  const char* filename_extension;
  void* (*open_file_read)(const char*, const char*, int*);
  int (*read_structure)(void*, int*, Atom*);
  ReadBonds read_bonds;
  int (*read_next_timestep)(void*, int, Timestep*);
  void (*close_file_read)(void*);
};

using RegisterCallback = int (*)(void*, PluginHeader*);
using InitFunction = int (*)();
using RegisterFunction = int (*)(void*, RegisterCallback);
using FiniFunction = int (*)();

static_assert(std::is_standard_layout_v<PluginHeader>);
static_assert(std::is_standard_layout_v<MolfilePlugin>);
static_assert(std::is_standard_layout_v<Atom>);
static_assert(offsetof(MolfilePlugin, filename_extension) ==
              sizeof(PluginHeader));
static_assert(sizeof(Atom) == 84U);

}  // namespace molshredder::io::abi18
