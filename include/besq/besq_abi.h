#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
class BesqContext;
#else
typedef struct BesqContext BesqContext;
#endif

// ── Context lifecycle ──────────────────────────────────────────────────────
BesqContext* besq_create(void);
void besq_destroy(BesqContext* ctx);

// ── Data loading ────────────────────────────────────────────────────────────
int besq_load_builtin(BesqContext* ctx);
int besq_load_file(BesqContext* ctx, const char* path);
int besq_load_data(BesqContext* ctx, const char* path);

// ── Profile management ──────────────────────────────────────────────────────
const char* besq_active_profile(BesqContext* ctx);
char** besq_list_profiles(BesqContext* ctx, int* out_count);
void besq_free_string_list(char** list, int count);
int besq_activate_profile(BesqContext* ctx, const char* name);
int besq_fork_profile(BesqContext* ctx, const char* source, const char* dest);
int besq_merge_profile(BesqContext* ctx, const char* source, const char* dest);
int besq_remove_profile(BesqContext* ctx, const char* name);

// ── Registry editing (JSON bridge) ─────────────────────────────────────────
int besq_add_enchantment(BesqContext* ctx, const char* json_info);
int besq_remove_enchantment(BesqContext* ctx, const char* name_id);
int besq_modify_enchantment(BesqContext* ctx, const char* json_patch);
int besq_add_equipment(BesqContext* ctx, const char* json_eq);
int besq_remove_equipment(BesqContext* ctx, const char* name_id);
int besq_add_category(BesqContext* ctx, const char* name);

// ── Solve (JSON input -> JSON output) ──────────────────────────────────────
char* besq_solve(BesqContext* ctx, const char* json_input);
void besq_free_string(char* str);

// ── Persistence ────────────────────────────────────────────────────────────
int besq_export_registry(BesqContext* ctx, const char* path);

// ── Error handling ──────────────────────────────────────────────────────────
const char* besq_last_error(BesqContext* ctx);

#ifdef __cplusplus
}
#endif
