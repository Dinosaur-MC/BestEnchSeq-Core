# Business Domain Design

> Version: 1.0
> Last updated: 2026-07-25

---

## Table of Contents

1. [Overview](#1-overview)
2. [Directory Structure](#2-directory-structure)
3. [Types](#3-types)
   - 3.1 [Value Types](#31-value-types)
   - 3.2 [DTO Types](#32-dto-types)
   - 3.3 [Profile — First-Class Citizen](#33-profile--first-class-citizen)
4. [Registries](#4-registries)
5. [Parsers](#5-parsers)
6. [Loaders](#6-loaders)
   - 6.1 [RegistryLoader](#61-registryloader)
   - 6.2 [ProfileLoader](#62-profileloader)
7. [Managers](#7-managers)
   - 7.1 [RegistryManager](#71-registrymanager)
   - 7.2 [ProfileManager](#72-profilemanager)
8. [Components](#8-components)
9. [Cross-Domain Cleanup](#9-cross-domain-cleanup)
10. [Calling Examples](#10-calling-examples)
11. [Implementation Plan](#11-implementation-plan)

---

## 1. Overview

The business domain (`src/domain/business/`) encapsulates all game-concept types, registries, and their high-level operations. It is **self-contained** — it depends only on `common/` and on its own submodules. No dependency on `interface/`, `algorithm/`, or `orchestration/` leaks into business domain code.

### Core Principles

- **Profile is the first-class citizen** — all normal operations take and return `Profile` as the unit of input/output. Low-level detail handling is delegated through Profile proxy methods or obtained from Profile resource access.
- **Cross-registry and cross-profile operations must be done through Profile**, not by manually manipulating individual registries.
- **Self-contained core** — the domain owns its types, registries, parsers, loaders, and managers. External domains consume business types through public headers only.
- **Profile is the complete serialization unit** — its JSON format corresponds directly to the existing `vanilla.json` structure.

### Architecture

```
common/ ──→ business/ ──→ interface/ ──→ orchestration/
               ↑                              ↑
               └── (self-contained) ──────────┘
                                    (orchestration adapts business types for algorithm domain)
```

---

## 2. Directory Structure

```
src/domain/business/
├── business.h                                  ← Umbrella header
│
├── types/                                      ← All data types
│   ├── Ench.h                                  Enchantment value object
│   ├── EnchInfo.h                              Enchantment definition
│   ├── EnchSet.h                               Enchantment set
│   ├── Equipment.h                             Equipment definition
│   ├── EquipmentTag.h                          Equipment tag (category)
│   ├── Item.h                                  Forgeable item stack
│   ├── Solution.h                              Forge solution
│   ├── Enchantment.h                           ← Backward-compat umbrella for Ench+EnchInfo+EnchSet
│   ├── Profile.h                               Profile + ProfileMetadata
│   └── dto/                                    Data transfer objects (parser intermediate types)
│       ├── EnchantmentData.h
│       └── EquipmentData.h
│
├── registries/                                 ← Pure data containers
│   ├── IRegistry.h                             Generic registry template
│   ├── EnchantmentRegistry.h/cpp
│   ├── EquipmentRegistry.h/cpp
│   └── EquipmentTagRegistry.h/cpp
│
├── parsers/                                    ← File format → DTO
│   ├── NativeJsonParser.h/cpp                  vanilla.json format parser
│   ├── NativeCsvParser.h/cpp                   CSV format parser
│   └── McOfficialParser.h/cpp                  MC official data-driven format parser
│
├── loaders/                                    ← DTO ↔ Profile / Registry
│   ├── RegistryLoader.h/cpp                    Low-level: DTO ↔ Registry
│   └── ProfileLoader.h/cpp                     External: Profile I/O (primary entry point)
│
├── managers/                                   ← Profile-centric operations
│   ├── RegistryManager.h/cpp                   Filter, set operations, validation, diff
│   └── ProfileManager.h/cpp                    Lifecycle, snapshot, branch, merge, activation
│
└── components/
    ├── FormatDetector.h/cpp                     File format detection + dispatch
    └── Serializer.h/cpp                         Json streaming serialization (extended for Profile)
```

---

## 3. Types

### 3.1 Value Types

Existing value types remain unchanged in their data layout. Only namespace/header path adjustments where needed.

| Type | File | Description |
|------|------|-------------|
| `Ench` | `Ench.h` | Enchantment value object: `NSID id` + `name` + `level` |
| `EnchInfo` | `EnchInfo.h` | Enchantment definition: `max_level`, `multiplier`, `exclusive_set`, `applicable_equipments` |
| `EnchSet` | `EnchSet.h` | `unordered_set<Ench>` with `NSID` find helper |
| `Equipment` | `Equipment.h` | Equipment definition: `NSID id` + `name` + `category` + `max_durability` |
| `EquipmentTag` | `EquipmentTag.h` | Equipment category tag: `NSID` (e.g. `#minecraft:sword`) + `name` |
| `Item` | `Item.h` | Forgeable item stack: `NSID id` + `EnchSet` + `prior_penalty` + `durability` |
| `Solution` | `Solution.h` | Forge solution: `EnchStep[]` + `MetaData` + costs |
| `Enchantment` | `Enchantment.h` | Transitional backward‑compat header including `Ench.h` + `EnchInfo.h` + `EnchSet.h` |

### 3.2 DTO Types

DTOs are the intermediate representation produced by parsers and consumed by `RegistryLoader`. They replace the former `interface/types/RawTypes.h` (which is removed).

```cpp
// types/dto/EnchantmentData.h
namespace business::loader {

struct EnchantmentData {
    std::string id;                          // "minecraft:sharpness"
    std::string display_name;
    int32_t multiplier;
    int32_t max_level;
    int32_t limited_level;                   // 0 = treasure
    std::vector<std::string> exclusive_with; // conflicting enchantment IDs
    std::vector<std::string> applicable_to;  // applicable equipment category names
};

} // namespace business::loader
```

```cpp
// types/dto/EquipmentData.h
namespace business::loader {

struct EquipmentData {
    std::string id;                          // "minecraft:diamond_sword"
    std::string display_name;
    std::string category;                    // "sword"
    int32_t max_durability;
};

} // namespace business::loader
```

Key differences from the removed `RawTypes.h`:
- `exclusive_set` (unordered_set) → `exclusive_with` (vector, preserves order)
- `applicable_items` → `applicable_to` (clearer semantics)
- These are now **domain-owned** types, not interface types

### 3.3 Profile — First-Class Citizen

Profile is the **primary unit of business logic**. All normal operations take and return Profile. Low-level details are handled through Profile proxy methods.

```cpp
// types/Profile.h
#pragma once
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "common/CommonTypes.h"
#include <chrono>
#include <string>

// ── Profile Metadata ───────────────────────────────────────────────────

struct ProfileMetadata {
    NSID name;                               // "builtin:vanilla", "profile:my_custom"
    std::string description;
    std::string author;
    std::string version;
    std::string parent;                      // branch source profile name
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;

    // JSON serialization keys (for vanilla.json compatibility)
    static constexpr std::string_view KEY_NAME        = "name";
    static constexpr std::string_view KEY_DESCRIPTION = "description";
    static constexpr std::string_view KEY_AUTHOR      = "author";
    static constexpr std::string_view KEY_VERSION     = "version";
    static constexpr std::string_view KEY_ENCHANTMENTS = "enchantments";
    static constexpr std::string_view KEY_EQUIPMENTS   = "equipments";
    static constexpr std::string_view KEY_TAGS         = "tags";
};

// ── Profile ────────────────────────────────────────────────────────────

class Profile {
public:
    Profile() = default;
    explicit Profile(NSID name);

    // -- Metadata -------------------------------------------------------

    const ProfileMetadata& metadata() const noexcept;
    const NSID& name() const noexcept;
    void set_description(std::string desc);
    void set_version(std::string version);

    // -- Registry read access (lenient, for trusted downstream) ----------

    /// Enchantment registry (for CompactAdapter, OutputFormatter, etc.)
    const EnchantmentRegistry& ench() const noexcept;
    /// Equipment registry
    const EquipmentRegistry& eq() const noexcept;
    /// Equipment tag registry
    const EquipmentTagRegistry& tags() const noexcept;

    // -- Profile proxy queries (preferred over manual registry access) ---

    /// Are two enchantments mutually exclusive?
    bool is_compatible(const NSID& a, const NSID& b) const;
    /// Which equipment does this enchantment apply to?
    std::vector<Equipment> applicable_equipment(const NSID& ench_id) const;
    /// Does this enchantment exist in the profile?
    bool has_enchantment(const NSID& id) const;
    /// Does this equipment exist in the profile?
    bool has_equipment(const NSID& id) const;

    // -- Profile proxy mutation (controlled entry points) ----------------

    bool add_enchantment(const EnchInfo& info);
    bool update_enchantment(const EnchInfo& info);
    bool remove_enchantment(const NSID& id);
    bool add_equipment(const Equipment& eq);
    bool remove_equipment(const NSID& id);
    bool add_tag(const EquipmentTag& tag);
    bool remove_tag(const NSID& id);

    // -- Validation -----------------------------------------------------

    bool validate() const;

    // -- Clone ----------------------------------------------------------

    /// Deep copy with new name (supports snapshot/branch)
    Profile clone(const NSID& new_name) const;

    // -- Serialization --------------------------------------------------

    Json to_json() const;
    static Profile from_json(const Json& json);

private:
    friend class ProfileManager;             // for lifecycle management
    friend class RegistryManager;            // for set operation result construction

    ProfileMetadata _meta;
    EnchantmentRegistry _ench;
    EquipmentRegistry _eq;
    EquipmentTagRegistry _tags;
};
```

**Design notes**:
- `name` uses `NSID` — profiles are identified by namespaced identifiers like `"builtin:vanilla"`, `"profile:experimental"`, `"backup:pre_test_0725"`.
- `tags` in `ProfileMetadata` is **not present** — the `tags` field in `vanilla.json` maps to `EquipmentTagRegistry`, not metadata.
- Profile exposes `const&` access to registries for trusted downstream (e.g., `CompactAdapter` needs raw `EnchantmentRegistry` to build `EnchReg`). Normal business logic uses proxy methods instead.

---

## 4. Registries

Pure data containers. Unchanged except for directory rename (`registries/` remains as-is — no rename needed in this design; the directory stays `registries/`).

| Registry | Key | Value |
|----------|-----|-------|
| `EnchantmentRegistry` | `NSID` | `EnchInfo` |
| `EquipmentRegistry` | `NSID` | `Equipment` |
| `EquipmentTagRegistry` | `NSID` | `EquipmentTag` |

All inherit from `IRegistry<T>` which provides: `insert`, `erase`, `update`, `find`, `at`, `contains`, `create_subset`, value iteration.

`EnchantmentRegistry` additionally maintains an `incompatible_table_` (built from each `EnchInfo.exclusive_set`) and provides `is_incompatible(a, b)` for O(1) conflict checks.

---

## 5. Parsers

Parsers convert file formats into domain DTOs. They are format-aware but filesystem-aware only where necessary (McOfficialParser traverses directories).

Each parser is **stateless** — static methods only.

### NativeJsonParser

Parses the `vanilla.json` format (or any compatible JSON).

```cpp
class NativeJsonParser {
public:
    /// Parse from a pre-parsed Json DOM
    static std::pair<std::vector<EnchantmentData>, std::vector<EquipmentData>>
    parse(const Json& json);

    /// Parse from a JSON string
    static std::pair<std::vector<EnchantmentData>, std::vector<EquipmentData>>
    parse_string(const std::string& content);
};
```

### NativeCsvParser

Parses CSV format (enchantment data only; equipment cannot be represented in CSV).

```cpp
class NativeCsvParser {
public:
    static std::vector<EnchantmentData>
    parse(const std::string& content);

    static std::vector<EnchantmentData>
    parse_file(const std::filesystem::path& path);
};
```

### McOfficialParser

Parses MC 1.21+ data-driven format (`data/<ns>/enchantment/<id>.json` with tag resolution).

```cpp
class McOfficialParser {
public:
    static std::pair<std::vector<EnchantmentData>, std::vector<EquipmentData>>
    parse(const std::filesystem::path& directory);
};
```

---

## 6. Loaders

Loaders bridge between DTOs (from parsers) and registries/Profile. `ProfileLoader` is the **primary external entry point** for data I/O.

### 6.1 RegistryLoader

Low-level: converts DTOs to registries and vice versa. Absorbs the logic of the former `RawTypeAdapter`.

```cpp
class RegistryLoader {
public:
    // ── DTO → Registry ────────────────────────────────────────────────

    /// Convert EnchantmentData[] → EnchantmentRegistry.
    /// Inlines string→NSID resolution (replaces RawTypeAdapter::resolve_ench_info).
    void from_dto(EnchantmentRegistry& reg,
                  const std::vector<EnchantmentData>& data);

    /// Convert EquipmentData[] → EquipmentRegistry.
    void from_dto(EquipmentRegistry& reg,
                  const std::vector<EquipmentData>& data);

    // ── Json → Registry ───────────────────────────────────────────────

    bool from_json(EnchantmentRegistry& reg, const Json& json);
    bool from_json(EquipmentRegistry& reg, const Json& json);
    bool from_json(EquipmentTagRegistry& reg, const Json& json);

    // ── Registry → Json ───────────────────────────────────────────────

    Json to_json(const EnchantmentRegistry& reg);
    Json to_json(const EquipmentRegistry& reg);
    Json to_json(const EquipmentTagRegistry& reg);

    // ── Registry → DTO (for export pipeline) ──────────────────────────

    std::vector<EnchantmentData> to_dto(const EnchantmentRegistry& reg);
    std::vector<EquipmentData> to_dto(const EquipmentRegistry& reg);
};
```

**Resolution logic (in `from_dto`)**:
- Category name strings → `NSID("#minecraft:" + name)` for `EquipmentTagRegistry`
- Equipment category strings → resolve against `EquipmentTagRegistry` IDs
- Enchantment `exclusive_with` strings → `NSID` set lookup (with `"minecraft:"` namespace fallback)
- Enchantment `applicable_to` strings → resolved to `NSID` equipment category references

### 6.2 ProfileLoader

The **primary external entry point** for loading and saving data. All operations take or return Profile.

```cpp
class ProfileLoader {
public:
    // ── Load ───────────────────────────────────────────────────────────

    /// Load from file → Profile (auto-detect format via FormatDetector).
    Profile load(const std::filesystem::path& path);
    bool load_into(Profile& profile, const std::filesystem::path& path);

    /// Load from JSON DOM → Profile.
    Profile from_json(const Json& json);
    bool from_json(Profile& profile, const Json& json);

    /// Load built-in vanilla data (delegates to builtin/DataLoader).
    Profile load_builtin();
    bool load_builtin(Profile& profile);

    // ── Save ───────────────────────────────────────────────────────────

    /// Profile → JSON DOM.
    Json to_json(const Profile& profile);

    /// Profile → JSON string.
    std::string to_json_string(const Profile& profile);

    /// Profile → file.
    bool save(const Profile& profile, const std::filesystem::path& path);
};
```

**Loading pipeline**:
```
File → FormatDetector::detect(path) → [NativeJsonParser | NativeCsvParser | McOfficialParser]
     → {EnchantmentData[], EquipmentData[]}
     → RegistryLoader::from_dto()
     → EnchantmentRegistry + EquipmentRegistry + EquipmentTagRegistry
     → Profile
```

**Builtin pipeline**:
```
ProfileLoader::load_builtin()
  → besq::data::load_builtin_data()          (project-level resource tool, stays in src/builtin/)
  → RegistryLoader::from_dto()
  → Profile
```

---

## 7. Managers

### 7.1 RegistryManager

Provides set operations, filtering, validation, and diff on Profiles. Supports both **single-shot static methods** and a **chainable builder** for multi-step operations.

```cpp
class RegistryManager {
public:
    // ── Chainable Builder ─────────────────────────────────────────────
    //
    // Allows multi-round operations before finalizing:
    //
    //   Profile result = RegistryManager{}
    //       .load(base)
    //       .filter_platform(MCE::Java)
    //       .filter_equipment(NSID("#minecraft:sword"))
    //       .unite(other_profile)
    //       .build(NSID("result:java_swords"));

    RegistryManager& load(const Profile& from);
    RegistryManager& filter(std::function<bool(const EnchInfo&)> pred);
    RegistryManager& filter_platform(MCE platform);
    RegistryManager& filter_equipment(const NSID& category);
    RegistryManager& unite(const Profile& other);
    RegistryManager& intersect(const Profile& other);

    /// Finalize and produce result Profile.
    Profile build(const NSID& result_name) const;

    // ── Static Single Operations ──────────────────────────────────────

    /// Union: all entries from both profiles.
    static Profile unite(const NSID& name,
                         const Profile& a, const Profile& b);
    /// Intersection: entries present in both profiles.
    static Profile intersect(const NSID& name,
                             const Profile& a, const Profile& b);
    /// Subtraction: entries in base but not in other.
    static Profile subtract(const NSID& name,
                            const Profile& base, const Profile& other);
    /// Merge: insert_or_assign from other into base (overwrite semantics).
    static Profile merge(const NSID& name,
                         const Profile& base, const Profile& other);

    // ── Diff ──────────────────────────────────────────────────────────

    struct DiffEntry {
        NSID id;
        enum Status { Added, Removed, Modified } status;
    };
    struct DiffResult {
        std::vector<DiffEntry> enchantments;
        std::vector<DiffEntry> equipment;
        std::vector<DiffEntry> tags;
    };

    static DiffResult diff(const Profile& a, const Profile& b);

    // ── Validation ────────────────────────────────────────────────────

    static bool validate(const Profile& profile);

private:
    std::optional<EnchantmentRegistry> _ench;
    std::optional<EquipmentRegistry> _eq;
    std::optional<EquipmentTagRegistry> _tags;
};

// ── Operator Overloads ────────────────────────────────────────────────

Profile operator|(const Profile& a, const Profile& b);  // unite
Profile operator&(const Profile& a, const Profile& b);  // intersect
Profile operator+(const Profile& a, const Profile& b);  // merge (overwrite)
Profile operator-(const Profile& a, const Profile& b);  // subtract
```

### 7.2 ProfileManager

Manages Profile lifecycle: creation, activation, snapshot, branching, merging.

**Implementation note**: Built by extending the existing `interface/ProfileSet` (which already has CRUD + activate + fork/merge). The new version adds snapshot, version metadata, NSID-based naming, and diff delegation.

```cpp
class ProfileManager {
public:
    ProfileManager() = default;

    // ── CRUD ──────────────────────────────────────────────────────────

    /// Create an empty profile.
    Profile& create(const NSID& name);

    /// Create a profile from an existing source (deep copy).
    Profile& create_from(const NSID& source, const NSID& dest);

    /// Remove a profile. Returns false if not found.
    bool remove(const NSID& name);

    /// Check if profile exists.
    bool exists(const NSID& name) const;

    /// Find profile by name (nullptr if not found).
    Profile* find(const NSID& name);
    const Profile* find(const NSID& name) const;

    /// List all profile names.
    std::vector<NSID> list() const;

    /// Number of managed profiles.
    size_t size() const noexcept;

    // ── Activation ────────────────────────────────────────────────────

    /// Set the active profile. Throws if not found.
    void activate(const NSID& name);

    /// Get active profile. Throws if manager is empty.
    Profile& active();
    const Profile& active() const;

    // ── Snapshot (immutable point-in-time copy) ───────────────────────

    /// Create an immutable copy of a profile at this point in time.
    Profile& snapshot(const NSID& source, const NSID& snapshot_name);

    // ── Branch (independently evolvable copy) ─────────────────────────

    /// Create a fork of a profile that can evolve independently.
    /// The branch inherits all data from source but tracks parent metadata.
    Profile& branch(const NSID& source, const NSID& branch_name);

    // ── Merge ─────────────────────────────────────────────────────────

    /// Merge source profile into dest (insert_or_assign semantics).
    /// Source entries overwrite dest entries on conflict.
    void merge(const NSID& source, const NSID& dest);

private:
    std::unordered_map<NSID, std::unique_ptr<Profile>> _profiles;
    NSID _active;
};
```

---

## 8. Components

### FormatDetector

File format detection and dispatch. Wraps the three parsers behind a single `parse(path)` entry point.

```cpp
class FormatDetector {
public:
    /// Detect file/data format.
    static DataFormat detect(const std::filesystem::path& path);

    /// Parse with auto-detect (dispatches to the appropriate parser).
    static std::pair<std::vector<EnchantmentData>, std::vector<EquipmentData>>
    parse(const std::filesystem::path& path);
};

enum class DataFormat {
    NativeJson,
    NativeCsv,
    McOfficial,
    Auto,  // detect from path/contents
};
```

**Detection heuristics**:
- Directory with `data/` subdirectory → `McOfficial`
- `.json` extension → `NativeJson`
- `.csv` extension → `NativeCsv`
- Other → attempt `NativeJson` (embedded data path)

### Serializer

Business types now inherit `IJsonSerializable` (defined in `common/serialization/`) and implement `to_json()` / `from_json()` directly:

```cpp
struct EnchInfo : IJsonSerializable {
    // ... fields ...
    Json to_json() const override { /* in EnchInfo.cpp */ }
    void from_json(const Json& json) override { /* in EnchInfo.cpp */ }
};
```

The `Serializer.h` still provides `operator<<` / `operator>>` free functions for ADL compatibility, but they are now thin delegates that call the member functions:

```cpp
Json& operator<<(Json& json, const EnchInfo& info) {
    json = info.to_json();
    return json;
}
```

### Serialization Module

The `common/serialization/` module defines three interfaces in an inheritance hierarchy:

| Interface | File | Purpose | Methods |
|-----------|------|---------|---------|
| `ISerializable` | `ISerializable.h` | Generic base (format-independent) | `virtual ~ISerializable()` |
| `IJsonSerializable` | `IJsonSerializable.h` | JSON serialization | `to_json()`, `from_json()` |
| `IBinarySerializable` | `IBinarySerializable.h` | Binary (ByteStream) serialization | `serialize()`, `deserialize()` |

Template helpers in `json::` namespace:
```cpp
#include "common/serialization/IJsonSerializable.h"

auto j = json::serialize(obj);                              // → obj.to_json()
auto obj = json::deserialize<MyType>(json);                  // → construct + from_json
json::deserialize(existing_obj, json);                       // → existing.from_json()
auto arr = json::serialize_vector(vec);                      // → array of to_json()
```

Registry types (`EnchantmentRegistry`, `EquipmentRegistry`, `EquipmentTagRegistry`) keep their operator pairs in Serializer.cpp, as they iterate and delegate to element serialization.

---

## 9. Cross-Domain Cleanup

### Files to Remove

| File | Reason | Replaced By |
|------|--------|-------------|
| `interface/parsers/EnchInfoParser.h/cpp` | Format parsing belongs in business/ | `business/parsers/NativeJsonParser`, `NativeCsvParser`, `McOfficialParser` |
| `interface/types/RawTypes.h` | DTO types now owned by business/ | `business/types/dto/EnchantmentData.h`, `EquipmentData.h` |
| `interface/ProfileSet.h/cpp` | Profile management belongs in business/ | `business/managers/ProfileManager` |
| `orchestration/components/RawTypeAdapter.h/cpp` | String→ID resolve is a loading concern | `RegistryLoader::from_dto()` internal logic |
| `business/registries/RegistryManager.h/cpp` | Loading logic splits into loaders + managers | `RegistryLoader`, `ProfileLoader`, `RegistryManager`, `ProfileManager` |

### Files to Keep (unaffected)

| File | Reason |
|------|--------|
| `builtin/DataLoader.h/cpp` | Project-level resource tool; `ProfileLoader::load_builtin()` calls it internally |
| `builtin/EmbeddedData.h` | Same as above |
| `business/registries/IRegistry.h` | Core registry template — unchanged |
| `business/registries/EnchantmentRegistry.h/cpp` | Core registry — unchanged |
| `business/registries/EquipmentRegistry.h/cpp` | Core registry — unchanged |
| `business/registries/EquipmentTagRegistry.h/cpp` | Core registry — unchanged |
| `business/components/Serializer.h/cpp` | JSON serialization — extended for Profile but otherwise unchanged |
| `business/types/*` | Core value types — unchanged |

### Files to Update

| File | Change |
|------|--------|
| `business/business.h` | Update umbrella includes: add `Profile.h`, `RegistryLoader.h`, `ProfileLoader.h`, `RegistryManager.h`, `ProfileManager.h`; remove old `RegistryManager.h` |
| `interface/BesqContext.cpp` | Delegate loading to `ProfileLoader`, management to `ProfileManager` |
| `interface/interface.h` | Remove `ProfileSet` and `RawTypes` includes |
| `orchestration/orchestration.h` | Remove `RawTypeAdapter` include |
| `main.cpp` | Update include paths |
| `src/domain/business/CMakeLists.txt` | GLOB_RECURSE will auto-detect new files; add explicit sources if needed |
| `src/domain/interface/CMakeLists.txt` | Remove `EnchInfoParser.cpp`, `ProfileSet.cpp` |
| `src/domain/orchestration/CMakeLists.txt` | Remove `RawTypeAdapter.cpp` |

---

## 10. Calling Examples

### Initialization & Loading

```cpp
// Load built-in vanilla data
ProfileLoader loader;
Profile vanilla = loader.load_builtin();         // NSID("builtin:vanilla")

// Load custom data file (auto-detect format)
Profile custom = loader.load("mods/custom.json");// → Profile("mods:custom")
```

### Set Operations via Operators

```cpp
// Union: all enchantments and equipment from both
Profile combined = vanilla | custom;

// Difference: custom additions only
Profile delta = custom - vanilla;

// Intersection: shared entries
Profile common = vanilla & custom;

// Merge: custom overwrites vanilla on conflict
Profile working = vanilla + custom;
```

### Builder for Complex Filtering

```cpp
Profile java_swords = RegistryManager{}
    .load(vanilla)                              // start from vanilla
    .filter_platform(MCE::Java)                 // Java-only enchantments
    .filter_equipment(NSID("#minecraft:sword")) // sword-applicable only
    .build(NSID("subset:java_swords"));
```

### Profile Manager Lifecycle

```cpp
ProfileManager mgr;
mgr.create_from(vanilla.name(), NSID("profile:experimental"));
mgr.activate(NSID("profile:experimental"));

auto& active = mgr.active();
active.add_enchantment(custom_ench);
active.remove_enchantment(NSID("minecraft:vanishing_curse"));

// Snapshot before risky changes
mgr.snapshot(NSID("profile:experimental"), NSID("backup:pre_test"));

// Branch for parallel exploration
mgr.branch(NSID("profile:experimental"), NSID("branch:alt_config"));

// Merge branch back
mgr.merge(NSID("branch:alt_config"), NSID("profile:experimental"));
```

### Diff Comparison

```cpp
auto diff = RegistryManager::diff(
    *mgr.find(NSID("profile:experimental")),
    *mgr.find(NSID("backup:pre_test"))
);
// diff.enchantments → {Added/Removed/Modified entries}
```

### Export

```cpp
// Profile → file
loader.save(mgr.active(), "output/experimental.json");

// Profile → JSON string
std::string json = loader.to_json_string(mgr.active());
```

---

## 11. Implementation Plan

### Phase 1: Foundation (S1–S2)

| Step | Files | Description |
|------|-------|-------------|
| **S1** | `types/Profile.h/cpp`, `types/dto/EnchantmentData.h`, `types/dto/EquipmentData.h` | Create core types and DTOs |
| **S2** | `parsers/NativeJsonParser.h/cpp`, `NativeCsvParser.h/cpp`, `McOfficialParser.h/cpp` | Split from `EnchInfoParser` |

### Phase 2: Loaders (S3)

| Step | Files | Description |
|------|-------|-------------|
| **S3a** | `loaders/RegistryLoader.h/cpp` | DTO ↔ Registry (absorb `RawTypeAdapter` logic) |
| **S3b** | `loaders/ProfileLoader.h/cpp` | Profile I/O entry point |

### Phase 3: Managers (S4–S5)

| Step | Files | Description |
|------|-------|-------------|
| **S4** | `managers/ProfileManager.h/cpp` | Profile lifecycle (based on existing `ProfileSet`) |
| **S5a** | `managers/RegistryManager.h/cpp` | Set operations + operator overloads + builder |
| **S5b** | *Add operator overloads in global ns* | `|`, `&`, `+`, `-` for Profile |

### Phase 4: Components & Cleanup (S6–S8)

| Step | Files | Description |
|------|-------|-------------|
| **S6** | `components/FormatDetector.h/cpp` | Format detection + dispatch |
| **S7** | *Remove old files* | `EnchInfoParser`, `RawTypeAdapter`, `RegistryManager`, `ProfileSet`, `RawTypes` |
| **S8a** | *Update consumers* | `main.cpp`, `BesqContext`, `SolvePipeline` |
| **S8b** | *Update tests* | Migrate and adapt test files |
| **S8c** | *Update CMakeLists* | All affected build files |
