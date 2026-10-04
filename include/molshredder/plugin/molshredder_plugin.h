/*
 * MolShredder plugin SDK, in-process C ABI (version 1.0).
 *
 * An in-process plugin is a shared library that exports
 * `molshredder_plugin_entry`. It runs inside the MolShredder process with full
 * process authority and no crash isolation, so the host loads it only after
 * the user approved that exact file with --allow-in-process. Untrusted or
 * unreviewed code must use the out-of-process worker protocol instead.
 *
 * All strings are UTF-8 and JSON where noted. Memory is freed by the side that
 * allocated it: strings the plugin returns are released with the plugin's own
 * `free_string`, strings the host returns with the host's `free_string`.
 *
 * Host callbacks are valid only while `invoke` is running, on the thread that
 * called it. A plugin must not keep or call them afterwards.
 *
 * Versioning: `abi_major` changes break the ABI, `abi_minor` adds fields at the
 * end of the structs. Check `struct_size` before reading a field.
 */
#ifndef MOLSHREDDER_PLUGIN_H
#define MOLSHREDDER_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOLSHREDDER_PLUGIN_ABI_MAJOR 1u
#define MOLSHREDDER_PLUGIN_ABI_MINOR 0u
#define MOLSHREDDER_PLUGIN_ENTRY_SYMBOL "molshredder_plugin_entry"

#if defined(_WIN32)
#define MOLSHREDDER_PLUGIN_EXPORT __declspec(dllexport)
#else
#define MOLSHREDDER_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef struct molshredder_host_api {
  uint32_t struct_size;
  uint32_t abi_major;
  uint32_t abi_minor;
  void* host_context;
  /* Nonzero when the running operation was cancelled; poll it in long loops. */
  int (*is_cancelled)(void* host_context);
  /* fraction in [0, 1]; message may be NULL. Needs capability "progress". */
  void (*report_progress)(void* host_context, double fraction, const char* message);
  /*
   * Capability-gated read-only data request, for example
   * method "selection.atoms" with params {"selection":"all"} (capability
   * "host.selection.atoms"). Returns 0 and a host-allocated JSON result, or a
   * nonzero value and a host-allocated plain error message in *result_json.
   * Release with `free_string`.
   */
  int (*request)(void* host_context, const char* method, const char* params_json,
                 char** result_json);
  void (*free_string)(char* text);
} molshredder_host_api;

typedef struct molshredder_plugin_api {
  uint32_t struct_size;
  uint32_t abi_major;
  uint32_t abi_minor;
  void* plugin_state;
  /*
   * Static JSON owned by the plugin:
   *   {"plugin":{"id":"...","version":"..."},"operations":["name", ...]}
   * It must match the manifest exactly.
   */
  const char* (*describe)(void* plugin_state);
  /*
   * arguments_json is a JSON object. Return 0 and set *result_json to a
   * plugin-allocated JSON object {"summary":..., "fields":{...}, "table":{...}}
   * or return nonzero and set it to {"code":"...","message":"...",
   * "suggestion":"..."}. Release with the plugin's `free_string`.
   */
  int (*invoke)(void* plugin_state, const char* operation, const char* arguments_json,
                char** result_json);
  void (*free_string)(void* plugin_state, char* text);
  void (*shutdown)(void* plugin_state);
} molshredder_plugin_api;

/*
 * Exported by the plugin library. Fills `plugin` and returns 0. `host` stays
 * valid until `shutdown`.
 */
MOLSHREDDER_PLUGIN_EXPORT int molshredder_plugin_entry(const molshredder_host_api* host,
                                                       molshredder_plugin_api* plugin);

#ifdef __cplusplus
}
#endif

#endif /* MOLSHREDDER_PLUGIN_H */
