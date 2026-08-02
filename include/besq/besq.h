#pragma once

/// @file besq/besq.h
/// BestEnchSeq Core — Public C ABI.
///
/// Usage (C):
///   #include <besq/besq.h>
///   BesqContext* ctx = besq_create();
///   besq_load_builtin(ctx);
///   char* result = besq_solve(ctx, "{...}");
///   besq_free_string(result);
///   besq_destroy(ctx);
///
/// Usage (C++):
///   #include <besq/besq.h>
///   // Same API; C++ consumers can also use the convenience wrappers
///   // in BesqContext.h (included if building C++).
///
/// Thread safety: BesqContext is NOT thread-safe.  Concurrent calls on
/// the same context from different threads is undefined behaviour.
/// Different contexts may be used concurrently.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Opaque context ──────────────────────────────────────────────────────────
#ifdef __cplusplus
class BesqContext;
#else
typedef struct BesqContext BesqContext;
#endif

// ── Version information ─────────────────────────────────────────────────────

/// Returns the library version string (semver, e.g. "0.0.1").
/// The pointer is statically allocated and never needs freeing.
const char* besq_get_version(void);

// ── Context lifecycle ───────────────────────────────────────────────────────

/// Create a new BesqContext.  Returns NULL on allocation failure.
BesqContext* besq_create(void);

/// Destroy a context previously created by besq_create().
void besq_destroy(BesqContext* ctx);

// ── Data loading ────────────────────────────────────────────────────────────

/// Load built-in (vanilla) data into the context.
/// Returns 0 on success, -1 on error (see besq_last_error).
int besq_load_builtin(BesqContext* ctx);

/// Load a data file (JSON or CSV) from @p path.
int besq_load_file(BesqContext* ctx, const char* path);

/// Load data files from a directory.
int besq_load_data(BesqContext* ctx, const char* path);

// ── Profile management ──────────────────────────────────────────────────────

/// Returns the name of the active profile.
/// The returned string is valid until the next call to any profile-modifying
/// function on the same context.  Do not free it.
const char* besq_active_profile(BesqContext* ctx);

/// List all profile names.  The caller must free the returned array with
/// besq_free_string_list().
/// @param[out] out_count  Set to the number of profile names.
/// @returns  An array of C strings, or NULL on allocation failure.
char** besq_list_profiles(BesqContext* ctx, int* out_count);

/// Free a string list previously returned by besq_list_profiles() or
/// besq_list_algorithms().
void besq_free_string_list(char** list, int count);

int besq_activate_profile(BesqContext* ctx, const char* name);

/// Fork a profile: copy @p source to @p dest.
int besq_fork_profile(BesqContext* ctx, const char* source, const char* dest);

/// Merge @p source into @p dest.
int besq_merge_profile(BesqContext* ctx, const char* source, const char* dest);

int besq_remove_profile(BesqContext* ctx, const char* name);

// ── Algorithm enumeration ───────────────────────────────────────────────────

/// List all registered algorithm names (built-in + plugin).
/// The caller must free the returned array with besq_free_string_list().
/// @param[out] out_count  Set to the number of algorithm names.
/// @returns  An array of C strings, or NULL on error.
char** besq_list_algorithms(BesqContext* ctx, int* out_count);

// ── Profile query ───────────────────────────────────────────────────────────

/// List all enchantments in the active profile as a JSON array.
/// Each element has fields: id, name, max_level, multiplier, etc.
/// Caller must free the returned string via besq_free_string().
char* besq_list_enchantments(BesqContext* ctx);

/// List all equipment in the active profile as a JSON array.
/// Each element has fields: id, name, category, max_durability, etc.
/// Caller must free the returned string via besq_free_string().
char* besq_list_equipment(BesqContext* ctx);

/// List all equipment categories as a JSON array of strings.
/// Caller must free the returned string via besq_free_string().
char* besq_list_categories(BesqContext* ctx);

// ── Profile editing (JSON bridge) ───────────────────────────────────────────

/// Add an enchantment from a JSON object.
/// @p json_info  e.g. {"id":"my:custom","max_level":3,"multiplier":10}
int besq_add_enchantment(BesqContext* ctx, const char* json_info);

int besq_remove_enchantment(BesqContext* ctx, const char* name_id);

/// Modify fields of an existing enchantment.
/// @p json_patch  e.g. {"id":"sharpness","max_level":10}
int besq_modify_enchantment(BesqContext* ctx, const char* json_patch);

/// Add an equipment from a JSON object.
/// @p json_eq  e.g. {"id":"my:weapon","category":"sword","max_durability":1561}
int besq_add_equipment(BesqContext* ctx, const char* json_eq);

int besq_remove_equipment(BesqContext* ctx, const char* name_id);

/// Add an equipment category.
int besq_add_category(BesqContext* ctx, const char* name);

// ── Solve (JSON input → JSON output) ────────────────────────────────────────

/// Solve for the optimal enchantment sequence.
///
/// @p json_input  JSON object with fields:
///   {
///     "target": {"equipment":"...", "enchantments":[...]},
///     "source": [{"id":"...","level":N}, ...],
///     "algorithm": "...",       // optional, default "dp_merge" (direct) / "hamming" (inventory)
///     "platform": "java|bedrock", // optional, default "java"
///     "max_solutions": N,        // optional
///     "mode": "direct|inventory"  // optional, default "direct"
///   }
///
/// @returns  A JSON string (caller must free via besq_free_string()),
///           or NULL on error (see besq_last_error).
char* besq_solve(BesqContext* ctx, const char* json_input);

void besq_free_string(char* str);

// ── Profile data import / export ────────────────────────────────────────────

int besq_export_profile(BesqContext* ctx, const char* path);
int besq_import_profile(BesqContext* ctx, const char* path);

// ── Error handling ──────────────────────────────────────────────────────────

/// Returns the last error message for the context.
/// The returned string is valid until the next call that produces an error
/// on the same context.  Do not free it.
const char* besq_last_error(BesqContext* ctx);

// ── Solver lifecycle (optional, requires backend support) ───────────────────

/// Abort an ongoing besq_solve() call (must be called from a different thread).
int besq_abort_solve(BesqContext* ctx);

#ifdef __cplusplus
} // extern "C"
#endif
