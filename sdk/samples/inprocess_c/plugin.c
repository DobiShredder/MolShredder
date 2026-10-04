/*
 * Sample in-process MolShredder plugin (C ABI 1.0). It shows the callbacks:
 * progress, cancellation and a host data request. JSON handling is kept
 * minimal on purpose; a real plugin would use a JSON library.
 *
 * Operations
 *   echo       {"text": "...", "times": N}  -> repeats the text
 *   sum        {"count": N}                 -> 1 + 2 + ... + N with progress
 *   spin       {}                           -> runs until the host cancels it
 *   atoms      {"selection": "..."}         -> the host's selection.atoms reply
 *   fail       {}                           -> an error result
 *
 * Compile-time switches (SAMPLE_*) build deliberately broken variants for the
 * compatibility test kit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "molshredder/plugin/molshredder_plugin.h"

#ifndef SAMPLE_ABI_MAJOR
#define SAMPLE_ABI_MAJOR MOLSHREDDER_PLUGIN_ABI_MAJOR
#endif
#ifndef SAMPLE_ABI_MINOR
#define SAMPLE_ABI_MINOR MOLSHREDDER_PLUGIN_ABI_MINOR
#endif

/* The broken test variants leave some helpers unused. */
#if defined(__GNUC__)
#define SAMPLE_UNUSED __attribute__((unused))
#else
#define SAMPLE_UNUSED
#endif

static const molshredder_host_api* g_host SAMPLE_UNUSED = NULL;

#ifdef SAMPLE_BAD_DESCRIBE
static const char kDescription[] =
    "{\"plugin\":{\"id\":\"org.example.native\",\"version\":\"1.0.0\"},\"operations\":[\"echo\"]}";
#else
static const char kDescription[] =
    "{\"plugin\":{\"id\":\"org.example.native\",\"version\":\"1.0.0\"},"
    "\"operations\":[\"echo\",\"sum\",\"spin\",\"atoms\",\"fail\"]}";
#endif

static SAMPLE_UNUSED char* duplicate(const char* text) {
  size_t size = strlen(text) + 1U;
  char* copy = (char*)malloc(size);
  if (copy != NULL) memcpy(copy, text, size);
  return copy;
}

static unsigned long long find_unsigned(const char* json, const char* key, unsigned long long fallback) {
  char pattern[64];
  const char* at;
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  at = strstr(json, pattern);
  if (at == NULL) return fallback;
  at = strchr(at + strlen(pattern), ':');
  if (at == NULL) return fallback;
  ++at;
  while (*at == ' ') ++at;
  return strtoull(at, NULL, 10);
}

/* Copies the string value of `key` into out (without escapes beyond \" and \\). */
static void find_text(const char* json, const char* key, char* out, size_t capacity) {
  char pattern[64];
  const char* at;
  size_t used = 0;
  out[0] = '\0';
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  at = strstr(json, pattern);
  if (at == NULL) return;
  at = strchr(at + strlen(pattern), ':');
  if (at == NULL) return;
  at = strchr(at, '"');
  if (at == NULL) return;
  ++at;
  while (*at != '\0' && *at != '"' && used + 2U < capacity) {
    if (*at == '\\' && at[1] != '\0') ++at;
    out[used++] = *at++;
  }
  out[used] = '\0';
}

static void append_escaped(char* out, size_t capacity, const char* text) {
  size_t used = strlen(out);
  while (*text != '\0' && used + 3U < capacity) {
    if (*text == '"' || *text == '\\') out[used++] = '\\';
    out[used++] = *text++;
  }
  out[used] = '\0';
}

static SAMPLE_UNUSED const char* describe(void* state) {
  (void)state;
  return kDescription;
}

static SAMPLE_UNUSED void free_string(void* state, char* text) {
  (void)state;
  free(text);
}

static SAMPLE_UNUSED void shutdown_plugin(void* state) { (void)state; }

static SAMPLE_UNUSED int invoke(void* state, const char* operation, const char* arguments, char** result) {
  (void)state;
#ifdef SAMPLE_BAD_RESULT
  *result = duplicate("this is not json");
  return 0;
#endif
  if (strcmp(operation, "echo") == 0) {
    char text[256];
    char out[1024] = "{\"summary\":\"echoed\",\"fields\":{\"text\":\"";
    unsigned long long times = find_unsigned(arguments, "times", 1U);
    unsigned long long i;
    find_text(arguments, "text", text, sizeof(text));
    for (i = 0; i < times && i < 4U; ++i) append_escaped(out, sizeof(out) - 8U, text);
    strcat(out, "\"}}");
    *result = duplicate(out);
    return 0;
  }
  if (strcmp(operation, "sum") == 0) {
    unsigned long long count = find_unsigned(arguments, "count", 4U);
    unsigned long long total = 0, i;
    char out[128];
    for (i = 1; i <= count; ++i) {
      total += i;
      if (g_host->report_progress != NULL) g_host->report_progress(g_host->host_context, (double)i / (double)count, "adding");
    }
    snprintf(out, sizeof(out), "{\"summary\":\"summed\",\"fields\":{\"total\":%llu}}", total);
    *result = duplicate(out);
    return 0;
  }
  if (strcmp(operation, "spin") == 0) {
    unsigned long long round;
    volatile unsigned long long sink = 0;
    for (round = 0; round < 4000ULL; ++round) {
      unsigned long long k;
      if (g_host->is_cancelled(g_host->host_context)) {
        *result = duplicate("{\"code\":\"cancelled\",\"message\":\"spin stopped on request\"}");
        return 1;
      }
      for (k = 0; k < 2000000ULL; ++k) sink += k;
    }
    *result = duplicate("{\"summary\":\"spin finished\",\"fields\":{}}");
    return 0;
  }
  if (strcmp(operation, "atoms") == 0) {
    char selection[128];
    char params[256] = "{\"selection\":\"";
    char* reply = NULL;
    int rc;
    find_text(arguments, "selection", selection, sizeof(selection));
    append_escaped(params, sizeof(params) - 4U, selection[0] != '\0' ? selection : "all");
    strcat(params, "\"}");
    rc = g_host->request(g_host->host_context, "selection.atoms", params, &reply);
    if (rc != 0) {
      char out[512] = "{\"code\":\"failed\",\"message\":\"";
      append_escaped(out, sizeof(out) - 4U, reply != NULL ? reply : "host request failed");
      strcat(out, "\"}");
      if (reply != NULL) g_host->free_string(reply);
      *result = duplicate(out);
      return 1;
    } else {
      size_t size = strlen(reply) + 96U;
      char* out = (char*)malloc(size);
      snprintf(out, size, "{\"summary\":\"host data\",\"fields\":{\"host\":%s}}", reply);
      g_host->free_string(reply);
      *result = out;
      return 0;
    }
  }
  if (strcmp(operation, "fail") == 0) {
    *result = duplicate("{\"code\":\"invalid_argument\",\"message\":\"native boom\",\"suggestion\":\"retry\"}");
    return 1;
  }
  *result = duplicate("{\"code\":\"not_found\",\"message\":\"unknown operation\"}");
  return 1;
}

#ifndef SAMPLE_NO_ENTRY
MOLSHREDDER_PLUGIN_EXPORT int molshredder_plugin_entry(const molshredder_host_api* host,
                                                       molshredder_plugin_api* plugin) {
#ifdef SAMPLE_FAIL_ENTRY
  (void)host;
  (void)plugin;
  return 1;
#else
  g_host = host;
#ifdef SAMPLE_SMALL_STRUCT
  plugin->struct_size = 16U;
#else
  plugin->struct_size = (uint32_t)sizeof(*plugin);
#endif
  plugin->abi_major = SAMPLE_ABI_MAJOR;
  plugin->abi_minor = SAMPLE_ABI_MINOR;
  plugin->plugin_state = NULL;
  plugin->describe = describe;
  plugin->invoke = invoke;
  plugin->free_string = free_string;
  plugin->shutdown = shutdown_plugin;
  return 0;
#endif
}
#endif
