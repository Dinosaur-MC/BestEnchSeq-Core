# JSON Output Schema

> **Version:** 1.1  
> **Status:** Stable  
> **Last updated:** 2026-08-06

This document describes the JSON output format produced by `besq --format json`. It is intended for frontend/CLI consumers who parse the output programmatically.

---

## Overview

The JSON output is a single root object containing metadata and an array of solutions. All object keys are sorted alphabetically (`std::map`) for stable, predictable output ordering.

```json
{
    "algorithm": "dp_merge",
    "computation_time_ms": 0,
    "mode": "direct",
    "schema_version": "1.1",
    "solutions": [ ... ],
    "success": true
}
```

---

## Top-Level Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `schema_version` | `string` | always | Schema version string (`"1.1"`). Incremented on breaking changes; 1.1 was non-breaking but changed the shape — it added the additive `result` field to each Step (see Step Object) and marked the switch to schema-driven output assembly. |
| `mode` | `string` | always | Operating mode: `"direct"` or `"inventory"`. |
| `success` | `bool` | always | Whether the solve completed successfully. Shared with the C ABI `besq_solve` root. |
| `algorithm` | `string` | always | Name of the algorithm used. Derived from the first solution's metadata when a bare solution set is formatted without a `SolveResult`. |
| `computation_time_ms` | `int64` | always | Total wall-clock search time in milliseconds. |
| `solutions` | `array<Solution>` | always | Array of zero or more forge solutions, sorted by cost ascending. |

The root metadata (`schema_version`, `mode`, `success`, `algorithm`, `computation_time_ms`) is produced by `OutputFormatter::build_json_root`, which is shared with the C ABI `besq_solve` so the CLI `--format json` and the C ABI output cannot drift.

---

## Solution Object

Each solution represents one complete forge sequence.

| Field | Type | Required | Description |
|---|---|---|---|
| `available_items` | `array<ItemStack>` | always | Items available for forging (books + equipment). |
| `is_success` | `bool` | always | Whether the forge sequence is feasible. |
| `max_cost_step_index` | `int64` | always | Index into `steps[]` of the step with highest level cost (0-based). Value is -1 for empty step lists or infeasible solutions. |
| `metadata` | `Metadata` | always | Execution metadata. |
| `original_ench` | `array<Enchantment>` | always | Source enchantments already present on target. |
| `peak_exp_cost` | `int32` | always | Experience POINT cost of the peak (most expensive) step. |
| `peak_level_cost` | `int32` | always | Experience LEVEL cost of the peak step. |
| `platform` | `string` | always | Target platform: `"Java"`, `"Bedrock"`, `"All"`, or `"None"`. |
| `rank` | `int32` | always | Solution rank (1-based, 1 = lowest cost). |
| `steps` | `array<Step>` | always | Forge steps to execute in order. |
| `target_item` | `ItemStack` | always | The target item after all forge operations. |
| `total_exp_cost` | `int32` | always | Total experience POINTS consumed. |
| `total_exp_level_cost` | `int32` | always | Total experience LEVELS consumed. |

### Metadata Object

| Field | Type | Required | Description |
|---|---|---|---|
| `algorithm_name` | `string` | always | Name of the algorithm that produced this solution. |
| `algorithm_version` | `string` | always | Algorithm version string. |
| `computation_time` | `int64` | always | Wall-clock time in milliseconds spent searching. |
| `created_at` | `int64` | always | Timestamp when the solution was generated; precision follows the algorithm layer (typically nanoseconds since epoch). |

---

## Step Object

Each step defines one forge operation.

| Field | Type | Required | Description |
|---|---|---|---|
| `exp_cost` | `int32` | always | Experience POINTS consumed by this step. |
| `exp_level_cost` | `int32` | always | Experience LEVELS consumed by this step. |
| `item_a` | `ItemStack` | always | Base item (placed in left anvil slot). |
| `item_b` | `ItemStack` | always | Sacrifice item (placed in right anvil slot). |
| `result` | `ItemStack` | always | Forged result item (A+B=C): the base item after the forge operation, carrying the merged enchantments and the updated `prior_penalty`. Added in 1.1 (additive — consumers using key-based lookup are unaffected). |

The forge operation is: `item_a + item_b → item_a (modified)`.

---

## ItemStack Object

Represents an item with its enchantments.

| Field | Type | Required | Description |
|---|---|---|---|
| `durability` | `int32` | always | Current durability. 0 for books, -1 if unknown. |
| `enchantments` | `array<Enchantment>` | always | Enchantments on this item. |
| `equipment` | `Equipment \| null` | always | Equipment definition; `null` for books. |
| `is_book` | `bool` | always | `true` if this is an enchanted book, `false` if equipment. |
| `prior_penalty` | `int32` | always | Prior Work Penalty count (0-31). |

### Equipment Object (when `equipment` is not null)

| Field | Type | Required | Description |
|---|---|---|---|
| `category` | `string` | always | Equipment category name (e.g., `"sword"`, `"chestplate"`). |
| `id` | `string` | always | Namespaced equipment ID (e.g., `"minecraft:diamond_sword"`). |
| `max_durability` | `int32` | always | Maximum durability of this equipment type. |
| `name` | `string` | always | Human-readable name (e.g., `"Diamond Sword"`). |

### Enchantment Object

| Field | Type | Required | Description |
|---|---|---|---|
| `id` | `string` | always | Namespaced enchantment ID (e.g., `"minecraft:sharpness"`). |
| `level` | `int32` | always | Enchantment level (1 to `max_level`). |

---

## Field Ordering

All object keys are sorted **alphabetically** (via `std::map<std::string, Json>`). This ensures:

- **Deterministic output** — identical inputs always produce identical JSON text
- **Easy diffing** — reordering code does not change field order
- **Predictable parsing** — consumers can rely on stable key positions

---

## Error States

### No solutions found
When no feasible forge sequence exists, `solutions` is an empty array:
```json
{
    "algorithm": "dp_merge",
    "computation_time_ms": 0,
    "mode": "direct",
    "schema_version": "1.1",
    "solutions": [],
    "success": true
}
```

### Infeasible solution
A solution may have `is_success: false` if the target cannot be reached:
```json
{
    "rank": 1,
    "is_success": false,
    "steps": [],
    "total_exp_level_cost": 0,
    "total_exp_cost": 0,
    "peak_level_cost": 0,
    "peak_exp_cost": 0,
    "max_cost_step_index": 0,
    ...
}
```

### No --target (profile export)
When `profile export --file <path>` is used without a solve target, no JSON output is produced. Instead, the profile data file is written.

---

## Parsing Guide

### Key rules for consumers

1. **Use `schema_version` for format detection** — always check this field before parsing
2. **Never assume field order matters** — although keys are alphabetically sorted, always use key-based lookup
3. **Handle empty `solutions` arrays** — valid when no forge sequence is possible
4. **Check `is_success`** before using step data
5. **Enchantment IDs are fully namespaced** — `"minecraft:sharpness"`, not `"sharpness"`; bare IDs without `:` are in the `minecraft` namespace
6. **`equipment` can be `null`** — this indicates the item is an enchanted book

### Round-trip compatibility

JSON output can be deserialized back into `EnchSolution` objects via `OutputFormatter::parse_json()`. The round-trip preserves:

- All enchantments with levels
- All equipment with categories
- Forge step sequence
- Metadata (algorithm, timing)

Limitations:
- Enchantment IDs must exist in the registry; unknown enchantments are silently skipped during deserialization
- Equipment category must exist in the category registry; unknown categories fall back to `ID_ANY`

---

## Changelog

| Schema Version | Changes |
|---|---|
| 1.0 | Initial stable schema |
| 1.0 (B-T23) | Root now carries `success` / `algorithm` / `computation_time_ms`, aligned with the C ABI `besq_solve` root via a shared `OutputFormatter::build_json_root`; `equipment.category` / `equipment.max_durability` in `ItemStack` now emit real registry data instead of `"unknown"` / `0`. |
| 1.1 | Additive field `result` added to each Step (non-breaking — consumers using key-based lookup are unaffected). Output assembly moved to the project's ds DSL schema (`OutputSchema.h`): the whole root — metadata + solutions — is encoded by `ds::json::Schema<RootSchema>::serialize`, and `build_json_root` (shared with the C ABI) by `RootMetaSchema`. The five root-meta fields are declared once (`kRootMetaFields<T>` in `OutputSchema.h`) and shared by both schemas — `RootMetaSchema` directly, `RootSchema` via `std::tuple_cat` — so the two roots' meta field set cannot drift. `schema_version` bumped 1.0 → 1.1 to mark the schema-ized generation. |
