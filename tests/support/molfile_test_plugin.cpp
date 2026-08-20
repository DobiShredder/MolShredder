#include "molfile_abi18.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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

namespace {

using namespace molshredder::io;

struct ReadState {
  std::filesystem::path path;
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
  *atom_count = filename.find("oversized") == std::string::npos ? 2 : 6'000'000;
  last_path = path;
  return new (std::nothrow) ReadState{{path}};
}

int read_structure(void* handle, int* options, abi18::Atom* atoms) {
  if (handle == nullptr || options == nullptr || atoms == nullptr) {
    return abi18::kError;
  }
  const auto* state = static_cast<ReadState*>(handle);
  if (state->path.filename().string().find("structure_fail") !=
      std::string::npos) {
    return abi18::kError;
  }
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

int read_next_timestep(void* handle, int atom_count, abi18::Timestep* step) {
  if (handle == nullptr || atom_count != 2 || step == nullptr ||
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
      slow ? std::chrono::milliseconds{300} : std::chrono::milliseconds{15});
  if (slow) {
    std::error_code ignored;
    std::filesystem::remove(state->path.string() + ".active", ignored);
  }
  if (state->path.filename().string().find("timestep_fail") !=
      std::string::npos) {
    return abi18::kError;
  }
  const float coordinates[]{0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
  std::copy(std::begin(coordinates), std::end(coordinates),
            step->coordinates);
  step->a = 10.0F;
  step->b = 11.0F;
  step->c = 12.0F;
  step->alpha = 90.0F;
  step->beta = 90.0F;
  step->gamma = 90.0F;
  return abi18::kSuccess;
}

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
  plugin.pretty_name = "Synthetic PQR";
  plugin.author = "MolShredder test suite";
  plugin.major_version = 0;
  plugin.minor_version = 6;
  plugin.is_reentrant = abi18::kThreadUnsafe;
  plugin.filename_extension = "pqr";
  plugin.open_file_read = open_file_read;
  plugin.read_structure = read_structure;
  plugin.read_next_timestep = read_next_timestep;
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
