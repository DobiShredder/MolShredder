#include "molfile_abi18.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <string>
#include <thread>

#if defined(_WIN32)
#define MOLSHREDDER_TEST_EXPORT extern "C" __declspec(dllexport)
#else
#define MOLSHREDDER_TEST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#ifndef MOLSHREDDER_TEST_ABI
#define MOLSHREDDER_TEST_ABI 18
#endif

#ifndef MOLSHREDDER_TEST_NAME
#define MOLSHREDDER_TEST_NAME "pqr"
#endif

// 0: PQR charge/radius (default), 1: psf-like topology without coordinates,
// 2: pdb-like rich atom fields with coordinates, bonds and cell,
// 3: xyz-like coordinates without residue identity or bonds,
// 4: dcd-like multi-frame trajectory (no structure callback, plugin-reported
//    atom count, cell and physical time),
// 5: crd-like trajectory whose atom count is supplied by the host,
// 6: dx/ccp4-like single scalar volume (2 x 3 x 4, skewed, x-fastest).
#ifndef MOLSHREDDER_TEST_PROFILE
#define MOLSHREDDER_TEST_PROFILE 0
#endif

namespace {

using namespace molshredder::io;

struct ReadState {
  std::filesystem::path path;
  int frame{};
  int opens{};
};

abi18::MolfilePlugin plugin{};
std::atomic_flag callback_active = ATOMIC_FLAG_INIT;
std::string last_path;

void copy_text(char* target, std::size_t capacity, const char* value) {
  std::memset(target, 0, capacity);
  const auto count = std::min(std::strlen(value), capacity - 1U);
  std::copy_n(value, count, target);
}

void* open_file_read(const char* path, const char*, int* atom_count) {
  if (path == nullptr || atom_count == nullptr ||
      !std::filesystem::is_regular_file(path)) {
    return nullptr;
  }
  const auto filename = std::filesystem::path{path}.filename().string();
#if MOLSHREDDER_TEST_PROFILE == 5
  if (*atom_count <= 0) return nullptr;  // the file does not record it
#elif MOLSHREDDER_TEST_PROFILE == 6
  *atom_count = 0;  // volumes have no atoms
#else
  *atom_count = filename.find("oversized") == std::string::npos
                    ? (MOLSHREDDER_TEST_PROFILE == 0 ? 2 : 3)
                    : 6'000'000;
#endif
#if MOLSHREDDER_TEST_PROFILE == 4
  if (filename.find("traj_mismatch") != std::string::npos) *atom_count = 4;
#endif
  last_path = path;
  int opens = 1;
#if MOLSHREDDER_TEST_PROFILE >= 4 && MOLSHREDDER_TEST_PROFILE <= 5
  {
    std::ofstream marker{last_path + ".opens", std::ios::app};
    marker << "open\n";
  }
  std::ifstream counted{last_path + ".opens"};
  opens = static_cast<int>(
      std::count(std::istreambuf_iterator<char>{counted},
                 std::istreambuf_iterator<char>{}, '\n'));
#endif
  return new (std::nothrow) ReadState{{path}, 0, opens};
}


#if MOLSHREDDER_TEST_PROFILE != 0
void set_atom(abi18::Atom& atom, const char* name, const char* type,
              const char* residue, int residue_id, int atomic_number) {
  atom = {};
  copy_text(atom.name, sizeof(atom.name), name);
  copy_text(atom.type, sizeof(atom.type), type);
  copy_text(atom.residue_name, sizeof(atom.residue_name), residue);
  copy_text(atom.segment_id, sizeof(atom.segment_id), "");
  copy_text(atom.chain, sizeof(atom.chain), "A");
  copy_text(atom.alternate_location, sizeof(atom.alternate_location), "");
  copy_text(atom.insertion_code, sizeof(atom.insertion_code), "");
  atom.residue_id = residue_id;
  atom.atomic_number = atomic_number;
}

[[maybe_unused]] int fill_profile_atoms(int* options, abi18::Atom* atoms) {
#if MOLSHREDDER_TEST_PROFILE == 1
  *options = abi18::kMass | abi18::kCharge;
  set_atom(atoms[0], "N", "NH1", "ALA", 1, 0);
  set_atom(atoms[1], "CA", "CT1", "ALA", 1, 0);
  set_atom(atoms[2], "C", "C", "ALA", 1, 0);
  const float masses[]{14.007F, 12.011F, 12.011F};
  const float charges[]{-0.47F, 0.07F, 0.51F};
  for (int index = 0; index < 3; ++index) {
    atoms[index].mass = masses[index];
    atoms[index].charge = charges[index];
  }
#elif MOLSHREDDER_TEST_PROFILE == 2
  *options = abi18::kOccupancy | abi18::kBFactor | abi18::kAltLoc |
             abi18::kInsertion | abi18::kAtomicNumber | abi18::kRadius;
  set_atom(atoms[0], "N", "N", "GLY", 5, 7);
  set_atom(atoms[1], "CA", "C", "GLY", 5, 6);
  set_atom(atoms[2], "C", "C", "GLY", 5, 6);
  const float occupancy[]{1.0F, 0.5F, 0.0F};
  const float b_factor[]{10.0F, 20.0F, 30.0F};
  for (int index = 0; index < 3; ++index) {
    atoms[index].occupancy = occupancy[index];
    atoms[index].b_factor = b_factor[index];
    atoms[index].radius = 1.5F;
  }
  copy_text(atoms[1].alternate_location,
            sizeof(atoms[1].alternate_location), "B");
  copy_text(atoms[2].insertion_code, sizeof(atoms[2].insertion_code), "A");
#else
  *options = 0;
  set_atom(atoms[0], "C", "C", "", 0, 0);
  set_atom(atoms[1], "O", "O", "", 0, 0);
  set_atom(atoms[2], "H", "H", "", 0, 0);
#endif
  return abi18::kSuccess;
}

[[maybe_unused]] int read_bonds(void* handle, int* count, int** from, int** to, float** orders,
               int** types, int* type_count, char*** type_names) {
  if (handle == nullptr || count == nullptr || from == nullptr ||
      to == nullptr || orders == nullptr || types == nullptr ||
      type_count == nullptr || type_names == nullptr) {
    return abi18::kError;
  }
  const auto* state = static_cast<ReadState*>(handle);
  const auto name = state->path.filename().string();
  static int bond_from[]{1, 2, 2, 1};
  static int bond_to[]{2, 3, 1, 3};
  static float bond_orders[]{1.0F, 2.0F, 1.0F, 4.0F};
  if (name.find("bond_fail") != std::string::npos) return abi18::kError;
  if (name.find("bond_zero_based") != std::string::npos) {
    static int zero_from[]{0};
    static int zero_to[]{1};
    *count = 1;
    *from = zero_from;
    *to = zero_to;
    *orders = nullptr;
  } else if (name.find("bond_negative") != std::string::npos) {
    *count = -1;
    *from = nullptr;
    *to = nullptr;
    *orders = nullptr;
  } else {
    // Bond 3 repeats bond 1 with swapped endpoints; bond 4 has an order that
    // has no MolShredder equivalent.
    *count = 4;
    *from = bond_from;
    *to = bond_to;
    *orders = name.find("no_order") != std::string::npos ? nullptr
                                                          : bond_orders;
  }
  *types = nullptr;
  *type_count = 0;
  *type_names = nullptr;
  return abi18::kSuccess;
}
#endif

[[maybe_unused]] int read_structure(void* handle, int* options,
                                    abi18::Atom* atoms) {
  if (handle == nullptr || options == nullptr || atoms == nullptr) {
    return abi18::kError;
  }
  const auto* state = static_cast<ReadState*>(handle);
  if (state->path.filename().string().find("structure_fail") !=
      std::string::npos) {
    return abi18::kError;
  }
#if MOLSHREDDER_TEST_PROFILE != 0
  return fill_profile_atoms(options, atoms);
#endif
  *options = abi18::kCharge | abi18::kRadius;
  atoms[0] = {};
  copy_text(atoms[0].name, sizeof(atoms[0].name), "N");
  copy_text(atoms[0].type, sizeof(atoms[0].type), "N");
  copy_text(atoms[0].residue_name, sizeof(atoms[0].residue_name), "GLY");
  copy_text(atoms[0].segment_id, sizeof(atoms[0].segment_id), "PROA");
  copy_text(atoms[0].chain, sizeof(atoms[0].chain), "A");
  copy_text(atoms[0].alternate_location,
            sizeof(atoms[0].alternate_location), "");
  copy_text(atoms[0].insertion_code, sizeof(atoms[0].insertion_code), "");
  atoms[0].residue_id = 7;
  atoms[0].charge = -0.30F;
  atoms[0].radius = 1.55F;
  atoms[0].atomic_number = 7;
  std::memset(atoms[0].alternate_location, 'X',
              sizeof(atoms[0].alternate_location));
  std::memset(atoms[0].insertion_code, 'X',
              sizeof(atoms[0].insertion_code));

  atoms[1] = {};
  copy_text(atoms[1].name, sizeof(atoms[1].name), "CA");
  copy_text(atoms[1].type, sizeof(atoms[1].type), "CT");
  copy_text(atoms[1].residue_name, sizeof(atoms[1].residue_name), "GLY");
  copy_text(atoms[1].segment_id, sizeof(atoms[1].segment_id), "PROA");
  copy_text(atoms[1].chain, sizeof(atoms[1].chain), "A");
  copy_text(atoms[1].alternate_location,
            sizeof(atoms[1].alternate_location), "");
  copy_text(atoms[1].insertion_code, sizeof(atoms[1].insertion_code), "");
  atoms[1].residue_id = 7;
  atoms[1].charge = 0.10F;
  atoms[1].radius = 1.70F;
  atoms[1].atomic_number = 6;
  std::memset(atoms[1].alternate_location, 'X',
              sizeof(atoms[1].alternate_location));
  std::memset(atoms[1].insertion_code, 'X',
              sizeof(atoms[1].insertion_code));
  return abi18::kSuccess;
}

[[maybe_unused]] int read_next_timestep(void* handle, int atom_count,
                                        abi18::Timestep* step) {
  if (handle == nullptr || atom_count != (MOLSHREDDER_TEST_PROFILE == 0 ? 2 : 3) ||
      step == nullptr ||
      step->coordinates == nullptr) {
    return abi18::kError;
  }
  if (callback_active.test_and_set()) return abi18::kError;
  struct ActiveGuard {
    ~ActiveGuard() { callback_active.clear(); }
  } active_guard;
  const auto* state = static_cast<ReadState*>(handle);
  const auto slow =
      state->path.filename().string().find("slow") != std::string::npos;
  if (slow) {
    std::ofstream marker{state->path.string() + ".active"};
    marker << "active\n";
  }
  std::this_thread::sleep_for(
      slow ? std::chrono::milliseconds{1000} : std::chrono::milliseconds{15});
  if (slow) {
    std::error_code ignored;
    std::filesystem::remove(state->path.string() + ".active", ignored);
  }
  if (state->path.filename().string().find("timestep_fail") !=
      std::string::npos) {
    return abi18::kError;
  }
  const float coordinates[]{0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F};
  const auto coordinate_count = MOLSHREDDER_TEST_PROFILE == 0 ? 6 : 9;
  std::copy(std::begin(coordinates), std::begin(coordinates) + coordinate_count,
            step->coordinates);
  step->a = 10.0F;
  step->b = 11.0F;
  step->c = 12.0F;
  step->alpha = 90.0F;
  step->beta = 90.0F;
  step->gamma = 90.0F;
  return abi18::kSuccess;
}


#if MOLSHREDDER_TEST_PROFILE >= 4 && MOLSHREDDER_TEST_PROFILE <= 5
int read_trajectory_timestep(void* handle, int atom_count,
                             abi18::Timestep* step) {
  if (handle == nullptr || step == nullptr || step->coordinates == nullptr ||
      atom_count <= 0) {
    return abi18::kError;
  }
  auto* state = static_cast<ReadState*>(handle);
  const auto name = state->path.filename().string();
  const int total = name.find("traj_empty") != std::string::npos ? 0 : 5;
  if (state->frame >= total) return abi18::kError;  // EOF and error are both -1
  if (name.find("traj_replay_fail") != std::string::npos &&
      state->opens >= 2 && state->frame == 1) {
    return abi18::kError;
  }
  for (int atom = 0; atom < atom_count; ++atom) {
    for (int axis = 0; axis < 3; ++axis) {
      step->coordinates[atom * 3 + axis] =
          static_cast<float>(state->frame * 100 + atom * 3 + axis);
    }
  }
  if (name.find("traj_nan") != std::string::npos && state->frame == 1) {
    step->coordinates[0] = std::numeric_limits<float>::quiet_NaN();
  }
#if MOLSHREDDER_TEST_PROFILE == 4
  if (name.find("traj_nocell") == std::string::npos) {
    step->a = 10.0F + static_cast<float>(state->frame);
    step->b = 11.0F;
    step->c = 12.0F;
    step->alpha = 90.0F;
    step->beta = 90.0F;
    step->gamma = 90.0F;
  }
  step->physical_time = 0.5 * state->frame;
#endif
  ++state->frame;
  return abi18::kSuccess;
}
#endif

#if MOLSHREDDER_TEST_PROFILE == 6
abi18::VolumetricMetadata volume_metadata[2];

[[maybe_unused]] int read_volumetric_metadata(
    void* handle, int* set_count, abi18::VolumetricMetadata** metadata) {
  if (handle == nullptr || set_count == nullptr || metadata == nullptr) {
    return abi18::kError;
  }
  const auto name =
      static_cast<ReadState*>(handle)->path.filename().string();
  if (name.find("vol_meta_fail") != std::string::npos) return abi18::kError;
  volume_metadata[0] = {};
  copy_text(volume_metadata[0].dataname, sizeof(volume_metadata[0].dataname),
            "density");
  volume_metadata[0].origin[0] = 1.0F;
  volume_metadata[0].origin[1] = 2.0F;
  volume_metadata[0].origin[2] = 3.0F;
  // Extent from the first to the last sample: delta = extent / (count - 1).
  volume_metadata[0].xaxis[0] = 2.0F;
  volume_metadata[0].yaxis[0] = 0.5F;
  volume_metadata[0].yaxis[1] = 3.0F;
  volume_metadata[0].zaxis[2] = 6.0F;
  volume_metadata[0].xsize = 2;
  volume_metadata[0].ysize = 3;
  volume_metadata[0].zsize = 4;
  if (name.find("vol_single_axis") != std::string::npos) {
    volume_metadata[0].xsize = 1;
  }
  if (name.find("vol_oversized") != std::string::npos) {
    volume_metadata[0].xsize = 100000;
    volume_metadata[0].ysize = 100000;
    volume_metadata[0].zsize = 100000;
  }
  if (name.find("vol_nan_axis") != std::string::npos) {
    volume_metadata[0].xaxis[0] = std::numeric_limits<float>::quiet_NaN();
  }
  *set_count = name.find("vol_multi") != std::string::npos ? 2 : 1;
  *metadata = volume_metadata;
  return abi18::kSuccess;
}

[[maybe_unused]] int read_volumetric_data(void* handle, int set,
                                          float* datablock,
                                          float* colorblock) {
  if (handle == nullptr || set != 0 || datablock == nullptr ||
      colorblock != nullptr) {
    return abi18::kError;
  }
  const auto name =
      static_cast<ReadState*>(handle)->path.filename().string();
  if (name.find("vol_data_fail") != std::string::npos) return abi18::kError;
  // x-fastest memory order: value = x + 10 y + 100 z.
  for (int z = 0; z < 4; ++z) {
    for (int y = 0; y < 3; ++y) {
      for (int x = 0; x < 2; ++x) {
        datablock[(z * 3 + y) * 2 + x] = static_cast<float>(x + 10 * y + 100 * z);
      }
    }
  }
  if (name.find("vol_nan") != std::string::npos &&
      name.find("vol_nan_axis") == std::string::npos) {
    datablock[5] = std::numeric_limits<float>::quiet_NaN();
  }
  return abi18::kSuccess;
}
#endif

void close_file_read(void* handle) {
  const auto* state = static_cast<ReadState*>(handle);
  if (state != nullptr) {
    std::ofstream marker{state->path.string() + ".closed", std::ios::app};
    marker << "closed\n";
  }
  delete state;
}

}  // namespace

MOLSHREDDER_TEST_EXPORT int vmdplugin_init() {
#if defined(MOLSHREDDER_TEST_SLOW_INIT)
  std::this_thread::sleep_for(std::chrono::milliseconds{100});
#endif
  plugin = {};
  plugin.abi_version = MOLSHREDDER_TEST_ABI;
#if defined(MOLSHREDDER_TEST_WRONG_TYPE)
  plugin.type = "not a molfile plugin";
#else
  plugin.type = abi18::kMolfilePluginType;
#endif
  plugin.name = MOLSHREDDER_TEST_NAME;
  plugin.pretty_name = "Synthetic molfile";
  plugin.author = "MolShredder test suite";
  plugin.major_version = 0;
  plugin.minor_version = 6;
  plugin.is_reentrant = abi18::kThreadUnsafe;
  plugin.filename_extension = "pqr";
  plugin.open_file_read = open_file_read;
#if MOLSHREDDER_TEST_PROFILE < 4
  plugin.read_structure = read_structure;
#endif
#if MOLSHREDDER_TEST_PROFILE == 6
  plugin.read_volumetric_metadata = read_volumetric_metadata;
  plugin.read_volumetric_data = read_volumetric_data;
#endif
#if MOLSHREDDER_TEST_PROFILE == 6
  // Volume plugins have no timestep callback.
#elif MOLSHREDDER_TEST_PROFILE >= 4
  plugin.read_next_timestep = read_trajectory_timestep;
#elif MOLSHREDDER_TEST_PROFILE != 1
  plugin.read_next_timestep = read_next_timestep;
#endif
#if MOLSHREDDER_TEST_PROFILE == 1 || MOLSHREDDER_TEST_PROFILE == 2
  plugin.read_bonds = read_bonds;
#endif
  plugin.close_file_read = close_file_read;
  return abi18::kSuccess;
}

MOLSHREDDER_TEST_EXPORT int vmdplugin_register(
    void* context, abi18::RegisterCallback callback) {
  if (callback == nullptr) return abi18::kError;
  if (callback(context, reinterpret_cast<abi18::PluginHeader*>(&plugin)) !=
      abi18::kSuccess) {
    return abi18::kError;
  }
#if defined(MOLSHREDDER_TEST_DUPLICATE)
  return callback(context, reinterpret_cast<abi18::PluginHeader*>(&plugin));
#else
  return abi18::kSuccess;
#endif
}

#if !defined(MOLSHREDDER_TEST_OMIT_FINI)
MOLSHREDDER_TEST_EXPORT int vmdplugin_fini() {
  if (!last_path.empty()) {
    std::ofstream marker{last_path + ".fini", std::ios::app};
    marker << "fini\n";
  }
  return abi18::kSuccess;
}
#endif
