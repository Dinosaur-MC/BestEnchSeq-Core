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
7. [Profile Management](#7-profile-management)
   - 7.1 [RegistryHelper](#71-registryhelper)
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
- **Self-contained core** — the domain owns its types, registries, parsers, loaders, `ProfileManager`（业务域入口）, and components (`RegistryHelper` 等). External domains consume business types through public headers only.
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
│   └── TagRegistry.h/cpp
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
├── ProfileManager.h/cpp                        ← Profile 生命周期 + 依赖图/有效视图/稳定 CRUD/发布/datapack（业务域入口）
│
└── components/
    ├── RegistryHelper.h/cpp                     Filter, set operations, validation, diff（原 RegistryManager）
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
| `EnchInfo` | `EnchInfo.h` | Enchantment definition: `max_level`, `multiplier`, `exclusive_set`, `supported_items`（原始 `#tag` 引用或具体物品 NSID） |
| `EnchSet` | `EnchSet.h` | `unordered_set<Ench>` with `NSID` find helper |
| `Equipment` | `Equipment.h` | Equipment definition: `NSID id` + `name` + `category`（显示短名）+ `max_durability` |
| `EquipmentTag` | `EquipmentTag.h` | MC tag 定义: `NSID`（真实 MC 物品/附魔 tag，如 `#minecraft:swords`、`#minecraft:enchantable/sharp_weapon`）+ `name` |
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
    std::vector<std::string> applicable_to;  // 原始 supported_items 引用（`#tag` 或具体物品 ID，透传不展开）
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
#include "domain/business/registries/TagRegistry.h"
#include "common/CommonTypes.h"
#include <chrono>
#include <memory>
#include <string>

class TagResolver;  // fwd — Profile stores a shared_ptr; complete type only needed at call sites

// ── Profile Metadata ───────────────────────────────────────────────────

struct ProfileMetadata {
    std::string name;                        // 任意可读名（可含空格/点，verbatim 保留，非 NSID）
    std::string description;
    std::string author;
    std::string version;
    std::string parent;                      // branch source profile name
    std::vector<std::string> dependencies;   // 声明的直接依赖（传递解析）
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;

    // JSON serialization keys (for vanilla.json compatibility)
    static constexpr std::string_view KEY_NAME        = "name";
    static constexpr std::string_view KEY_DESCRIPTION = "description";
    static constexpr std::string_view KEY_AUTHOR      = "author";
    static constexpr std::string_view KEY_VERSION     = "version";
    static constexpr std::string_view KEY_DEPENDENCIES = "dependencies";
    static constexpr std::string_view KEY_ENCHANTMENTS = "enchantments";
    static constexpr std::string_view KEY_EQUIPMENTS   = "equipments";
    static constexpr std::string_view KEY_TAGS         = "tags";
};

// ── Profile ────────────────────────────────────────────────────────────

class Profile {
public:
    Profile() = default;
    explicit Profile(std::string name);

    // -- Metadata -------------------------------------------------------

    const ProfileMetadata& metadata() const noexcept;
    const std::string& name() const noexcept;
    void set_description(std::string desc);
    void set_version(std::string version);

    /// Declared direct dependencies (transitively resolved at load).
    const std::vector<std::string>& dependencies() const noexcept;
    void set_dependencies(std::vector<std::string> deps);

    // -- Registry read access (lenient, for trusted downstream) ----------

    /// Enchantment registry (for CompactAdapter, OutputFormatter, etc.)
    const EnchantmentRegistry& ench() const noexcept;
    /// Equipment registry
    const EquipmentRegistry& eq() const noexcept;
    /// Tag registry (real MC item/enchantment tag definitions)
    const TagRegistry& tags() const noexcept;

    // -- Tag resolver (runtime-derived; not serialized) ------------------

    /// Attach the TagResolver used at the business→algorithm boundary to
    /// compute an item's tag membership for enchantment applicability
    /// (`supported_items` ∩ `tags_of(item)`).  Populated during load; the
    /// profile JSON does not serialize it.
    void set_tag_resolver(std::shared_ptr<TagResolver> r);

    /// Accessor — returns nullptr when no resolver has been attached.
    const TagResolver* tag_resolver() const noexcept;

    // -- Profile proxy queries (preferred over manual registry access) ---

    /// Are two enchantments mutually exclusive?
    bool is_compatible(const NSID& a, const NSID& b) const;

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
    Profile clone(const std::string& new_name) const;

    // -- Serialization --------------------------------------------------

    Json to_json() const;
    void from_json(const Json& json);
    static Profile from_json_static(const Json& json);

private:
    ProfileMetadata _meta;
    EnchantmentRegistry _ench;
    EquipmentRegistry _eq;
    TagRegistry _tags;
    std::shared_ptr<TagResolver> _tag_resolver;  // runtime-derived, not serialized
};
```

> **Note**: `Profile` 无 `friend` 声明 — `ProfileManager`/`RegistryHelper` 通过公开 proxy 方法与 `RegistryHelper::merge` 操作 Profile。mod profile 若要显式依赖内置根，可声明 `dependencies: ["builtin:vanilla"]`。

**Design notes**:
- `name` is a **`std::string` profile key** — 任意可读名（可含空格/点，verbatim 保留），**不是 NSID**（B-T13：Profile key 与 NSID 解耦）。NSID 仅用于 MC 内容类型（魔咒/装备/tag id、datapack 内命名空间）。根 key 固定为 `builtin:vanilla`。`--profile` 匹配任意字符串 key。
- `dependencies` 声明直接依赖（字符串列表），由 `ProfileManager` 传递解析（见 7.2）。
- `tags` in `ProfileMetadata` is **not present** — the `tags` field in `vanilla.json` maps to `TagRegistry`, not metadata。
- **内容 id 仍是 NSID** — `has_enchantment(const NSID&)`、`remove_enchantment(const NSID&)`、注册表 key 等全部保持不变；只有 profile 身份（name/key）改为字符串。
- Profile exposes `const&` access to registries for trusted downstream (e.g., `CompactAdapter` needs raw `EnchantmentRegistry` to build `EnchReg`). Normal business logic uses proxy methods instead.

---

## 4. Registries

Pure data containers. Unchanged except for directory rename (`registries/` remains as-is — no rename needed in this design; the directory stays `registries/`).

| Registry | Key | Value |
|----------|-----|-------|
| `EnchantmentRegistry` | `NSID` | `EnchInfo` |
| `EquipmentRegistry` | `NSID` | `Equipment` |
| `TagRegistry` | `NSID` | `EquipmentTag` |

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

Parses the real MC 1.21+ data-driven format (`data/<ns>/enchantment/<id>.json` with tag resolution). 支持真实格式特性：`supported_items`/`exclusive_set` 单字符串或数组、`slots`、`tag replace`、`anvil_cost`（→ 费用倍率）。也支持内存解析（`parse_files`，无文件系统访问）与单条解析（`parse_single_enchantment`）。

```cpp
class McOfficialParser {
public:
    static std::pair<std::vector<EnchantmentData>, std::vector<EquipmentData>>
    parse(const std::filesystem::path& directory);

    static std::pair<std::vector<EnchantmentData>, std::vector<EquipmentData>>
    parse_files(const std::unordered_map<std::string, std::string>& files);

    static business::loader::EnchantmentData parse_single_enchantment(
        const std::string& ns, const std::string& filename,
        const std::string& content, TagResolver& tag_resolver);
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
    bool from_json(TagRegistry& reg, const Json& json);

    // ── Registry → Json ───────────────────────────────────────────────

    Json to_json(const EnchantmentRegistry& reg);
    Json to_json(const EquipmentRegistry& reg);
    Json to_json(const TagRegistry& reg);

    // ── Registry → DTO (for export pipeline) ──────────────────────────

    std::vector<EnchantmentData> to_dto(const EnchantmentRegistry& reg);
    std::vector<EquipmentData> to_dto(const EquipmentRegistry& reg);
};
```

**Resolution logic (in `from_dto`)**:
- 装备 `category` 只是**显示短名**（如 `"sword"`），不再从中推导合成 tag（T10）——真实 MC 物品 tag（`#minecraft:swords`、`#minecraft:enchantable/*`）才是适用性判定来源
- 魔咒 `applicable_to`（原始 `supported_items` 引用）**透传不展开**，交叉验证：`#tag` 引用需在 tag 定义中存在、具体物品 ID 需在装备注册表中存在，否则丢弃；引用全部丢弃则整条魔咒被跳过（T6）
- 魔咒 `exclusive_with` 字符串 → `NSID` set lookup（带 `"minecraft:"` 命名空间回退）

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
     → RegistryLoader::resolve_with_base()    (两阶段：以 vanilla tag/装备为基准做交叉验证，T7)
     → EnchantmentRegistry + EquipmentRegistry + TagRegistry
     → Profile + set_tag_resolver(内置 vanilla tag resolver)
```

**Builtin pipeline**:
```
ProfileLoader::load_builtin()
  → besq::data::load_builtin_data()          (project-level resource tool, stays in src/builtin/)
  → RegistryLoader::from_dto()
  → Profile + set_tag_resolver(内置 vanilla tag resolver)
```

**两阶段加载（vanilla fallback）**：`ProfileLoader::load_into` 先把 vanilla 全宇宙（tags + equipments + enchantments）装进临时注册表，再把 profile 自身的 DTO 在并集上交叉验证。Profile 只保留自己的 enchantments/equipments；vanilla tag 宇宙保留，使 profile 的 `#tag` supported_items 引用在业务→算法边界仍可解析。vanilla profile 本身是内置根（`builtin:vanilla`）。

---

## 7. Profile Management

`managers/` 目录已解散（B-T12 结构重组）：`ProfileManager` 上移到 `src/domain/business/` 顶层（业务域入口），原 `RegistryManager` 改名 **`RegistryHelper`** 并移入 `src/domain/business/components/`。

### 7.1 RegistryHelper

位于 `src/domain/business/components/RegistryHelper.h/cpp`（原 `RegistryManager`）。提供 Profile 上的集合运算、筛选、验证与 diff，支持**单发静态方法**与**链式 Builder** 两种用法。

```cpp
class RegistryHelper {
public:
    // ── Chainable Builder ─────────────────────────────────────────────
    //
    // Allows multi-round operations before finalizing:
    //
    //   Profile result = RegistryHelper{}
    //       .load(base)
    //       .filter_platform(MCE::Java)
    //       .filter_equipment(NSID("#minecraft:sword"))
    //       .unite(other_profile)
    //       .build("result:java_swords");

    RegistryHelper& load(const Profile& from);
    RegistryHelper& filter(std::function<bool(const EnchInfo&)> pred);
    RegistryHelper& filter_platform(MCE platform);
    RegistryHelper& filter_equipment(const NSID& category);
    RegistryHelper& unite(const Profile& other);
    RegistryHelper& intersect(const Profile& other);

    /// Finalize and produce result Profile.
    Profile build(const std::string& result_name) const;

    // ── Static Single Operations ──────────────────────────────────────

    /// Union: all entries from both profiles.
    static Profile unite(const std::string& name,
                         const Profile& a, const Profile& b);
    /// Intersection: entries present in both profiles.
    static Profile intersect(const std::string& name,
                             const Profile& a, const Profile& b);
    /// Subtraction: entries in base but not in other.
    static Profile subtract(const std::string& name,
                            const Profile& base, const Profile& other);
    /// Merge: insert_or_assign from other into base (overwrite semantics).
    static Profile merge(const std::string& name,
                         const Profile& base, const Profile& other);
    /// Merge `src` into `dest` IN PLACE (overwrite semantics; used by ProfileManager::merge).
    static void merge(Profile& dest, const Profile& src);

    /// Build a TagResolver covering the merged tag universe of an effective
    /// view (every `#tag` in `eff.tags()` is registered).  Used by
    /// ProfileManager::resolve_effective so the merged view is `tags_of`-queryable.
    static std::shared_ptr<TagResolver> build_tag_resolver(
        const Profile& eff, const std::vector<const Profile*>& sources);

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
    std::optional<TagRegistry> _tags;
};

// ── Operator Overloads ────────────────────────────────────────────────

Profile operator|(const Profile& a, const Profile& b);  // unite
Profile operator&(const Profile& a, const Profile& b);  // intersect
Profile operator+(const Profile& a, const Profile& b);  // merge (overwrite)
Profile operator-(const Profile& a, const Profile& b);  // subtract
```

### 7.2 ProfileManager

位于 `src/domain/business/ProfileManager.h/cpp`（业务域入口）。管理 Profile 生命周期（CRUD、激活、快照、分支、合并），并在其上叠加**依赖图 / 有效视图 / 稳定 CRUD+快照 / 版本化发布 / datapack 加载**。

**Implementation note**: Built by extending the former `interface/ProfileSet` (CRUD + activate + fork/merge). Profile key 是 `std::string`（非 NSID）；内容 id 仍是 NSID。

```cpp
class ProfileManager {
public:
    ProfileManager() = default;

    // ── CRUD ──────────────────────────────────────────────────────────

    Profile& create(const std::string& name);
    Profile& create_from(const std::string& source, const std::string& dest);
    bool remove(const std::string& name);
    bool exists(const std::string& name) const;
    Profile* find(const std::string& name);
    const Profile* find(const std::string& name) const;
    std::vector<std::string> list() const;
    size_t size() const noexcept;

    // ── 稳定 CRUD（实时校验 + 自动快照/undo）─────────────────────────

    bool add_enchantment(const std::string& profile, const EnchInfo& info);
    bool update_enchantment(const std::string& profile, const EnchInfo& patch);
    bool remove_enchantment(const std::string& profile, const NSID& id);
    bool add_equipment(const std::string& profile, const Equipment& eq);
    bool remove_equipment(const std::string& profile, const NSID& id);
    bool add_tag(const std::string& profile, const EquipmentTag& tag);
    bool remove_tag(const std::string& profile, const NSID& id);
    bool undo(const std::string& profile);   // 回滚最近一次成功变更

    // ── Activation ────────────────────────────────────────────────────

    void activate(const std::string& name);
    Profile& active();
    const Profile& active() const;
    const std::string& active_name() const noexcept;

    // ── Snapshot / Branch / Merge ─────────────────────────────────────

    Profile& snapshot(const std::string& source, const std::string& snapshot_name);
    Profile& branch(const std::string& source, const std::string& branch_name);
    void merge(const std::string& source, const std::string& dest);

    // ── 依赖图 ────────────────────────────────────────────────────────

    std::vector<std::string> resolve_dependencies(const std::string& profile) const;  // 拓扑序（依赖在前），环→空
    const Profile& resolve_effective(const std::string& profile) const;               // 拓扑合并有效视图 + TagResolver + 缓存
    void load_directory(const std::filesystem::path& dir);                            // native JSON/CSV + datapack 子目录
    bool load_datapack(const std::filesystem::path& dir);                             // pack.mcmeta 检测 + McOfficialParser
    size_t cross_validate(const std::string& profile);                                // 对 (vanilla ∪ 依赖链) 校验，返回移除数
    void notify_mutated() const;                                                      // 直接改 Profile 后使有效视图缓存失效

    // ── Publish（拍平有效视图 + version/tag）─────────────────────────

    bool publish(const std::string& profile, const std::string& version,
                 const std::string& tag, const std::filesystem::path& out);

private:
    bool _mutate(const std::string& profile, std::function<bool(Profile&)> op);
    void _build_graph() const;
    std::unordered_map<std::string, std::vector<Snapshot>> _undo_log;  // 每个 profile 的变更历史
    std::unordered_map<std::string, std::unique_ptr<Profile>> _profiles;
    mutable std::unordered_map<std::string, std::vector<std::string>> _dep_graph;    // 邻接表
    mutable std::unordered_map<std::string, std::unique_ptr<Profile>> _effective_cache;
    std::string _active;
};
```

**关键设计**：

- **依赖图**：`dependencies` 声明直接依赖；`resolve_dependencies` 用 DFS 求传递闭包（拓扑序，依赖在前，不含目标自身）；检测到环返回空。
- **有效视图**：`resolve_effective` 按拓扑序 merge 依赖链 + 自身（上层覆盖下层），构造覆盖合并 tag 宇宙的 `TagResolver`，并缓存；任何 manager 级 mutation 使缓存失效，直接改 Profile 后须 `notify_mutated()`。
- **稳定 CRUD**：`_mutate` 在应用前/后各校验一次，成功后记录变更前快照（`_undo_log`），失败回滚不留脏状态；`undo()` 回滚最近一次成功变更。回滚/撤销通过 JSON round-trip 恢复，同时保留 `TagResolver`。
- **版本化发布**：`publish(profile, version, tag, out)` 拍平有效视图为自包含 profile JSON，内嵌 `version` / `release_tag`。
- **datapack**：`load_datapack` 要求目录含 `pack.mcmeta`，用 `McOfficialParser` 解析真实 MC 1.21+ 格式（单字符串/数组 `supported_items`、`slots`、`tag replace`、`anvil_cost`），经与 `ProfileLoader` 相同的两阶段 `RegistryLoader` 路径构建，仅保留 datapack 自身内容（含自身 item tag 并入 tag 宇宙与 TagResolver，`replace` 语义覆盖 vanilla tag）。datapack `dependencies()` 保持为空：内置 `builtin:vanilla` 是 `cross_validate` 无条件收集的**隐式基座**，而非写入依赖链（B-T14 M-5）。`load_directory` 加载目录下 native JSON/CSV 文件并把含 `pack.mcmeta` 的子目录当作 datapack 加载。datapack profile key = **文件夹名 verbatim**（`pack.id` 仅回退，B-T14 M-4）；命名为 `builtin:vanilla`/`vanilla` 时改写为 `vanilla_datapack` 防止覆盖根。

---

## 8. Components

### RegistryHelper

集合运算助手（原 `RegistryManager`，B-T12 改名并移入 `components/`）。提供链式 Builder 与静态集合运算（unite/intersect/subtract/merge）、diff、validate，以及 `build_tag_resolver`（为有效视图构造覆盖合并 tag 宇宙的 `TagResolver`）。详见 7.1。

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

Registry types (`EnchantmentRegistry`, `EquipmentRegistry`, `TagRegistry`) keep their operator pairs in Serializer.cpp, as they iterate and delegate to element serialization.

---

## 9. Cross-Domain Cleanup

### Files to Remove

| File | Reason | Replaced By |
|------|--------|-------------|
| `interface/parsers/EnchInfoParser.h/cpp` | Format parsing belongs in business/ | `business/parsers/NativeJsonParser`, `NativeCsvParser`, `McOfficialParser` |
| `interface/types/RawTypes.h` | DTO types now owned by business/ | `business/types/dto/EnchantmentData.h`, `EquipmentData.h` |
| `interface/ProfileSet.h/cpp` | Profile management belongs in business/ | `business/ProfileManager` |
| `orchestration/components/RawTypeAdapter.h/cpp` | String→ID resolve is a loading concern | `RegistryLoader::from_dto()` internal logic |
| `business/managers/RegistryManager.h/cpp`（B-T12 改名 + 移位） | `managers/` 解散；集合运算下沉到 components | `business/components/RegistryHelper` |
| `business/managers/ProfileManager.h/cpp`（B-T12 移位） | `managers/` 解散；上移到业务域顶层 | `business/ProfileManager` |

### Files to Keep (unaffected)

| File | Reason |
|------|--------|
| `builtin/DataLoader.h/cpp` | Project-level resource tool; `ProfileLoader::load_builtin()` calls it internally |
| `builtin/EmbeddedData.h` | Same as above |
| `business/registries/IRegistry.h` | Core registry template — unchanged |
| `business/registries/EnchantmentRegistry.h/cpp` | Core registry — unchanged |
| `business/registries/EquipmentRegistry.h/cpp` | Core registry — unchanged |
| `business/registries/TagRegistry.h/cpp` | Core registry — unchanged |
| `business/components/Serializer.h/cpp` | JSON serialization — extended for Profile but otherwise unchanged |
| `business/types/*` | Core value types — unchanged |

### Files to Update

| File | Change |
|------|--------|
| `business/business.h` | Update umbrella includes: add `Profile.h`, `RegistryLoader.h`, `ProfileLoader.h`, `ProfileManager.h`, `components/RegistryHelper.h`；移除旧 `managers/` 引用 |
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
Profile vanilla = loader.load_builtin();         // name "builtin:vanilla"（根 key）

// Load custom data file (auto-detect format)
Profile custom = loader.load("mods/custom.json");// → Profile("custom")（name 为文件内 name 或文件名）
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
Profile java_swords = RegistryHelper{}
    .load(vanilla)                              // start from vanilla
    .filter_platform(MCE::Java)                 // Java-only enchantments
    .filter_equipment(NSID("#minecraft:sword")) // sword-applicable only
    .build("subset:java_swords");
```

### Profile Manager Lifecycle

```cpp
ProfileManager mgr;
mgr.create_from(vanilla.name(), "profile:experimental");  // name 是字符串 key
mgr.activate("profile:experimental");

auto& active = mgr.active();
active.add_enchantment(custom_ench);
active.remove_enchantment(NSID("minecraft:vanishing_curse"));  // 内容 id 仍是 NSID

// Snapshot before risky changes
mgr.snapshot("profile:experimental", "backup:pre_test");

// Branch for parallel exploration
mgr.branch("profile:experimental", "branch:alt_config");

// Merge branch back
mgr.merge("branch:alt_config", "profile:experimental");

// 依赖图 / 有效视图 / 发布
auto chain = mgr.resolve_dependencies("profile:experimental");  // 拓扑序依赖链
const Profile& eff = mgr.resolve_effective("profile:experimental");  // 有效视图（含依赖）
mgr.publish("profile:experimental", "1.0.0", "stable", "out/experimental.json");
```

### Diff Comparison

```cpp
auto diff = RegistryHelper::diff(
    *mgr.find("profile:experimental"),
    *mgr.find("backup:pre_test")
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

### Phase 3: Profile 管理（S4–S5）

| Step | Files | Description |
|------|-------|-------------|
| **S4** | `ProfileManager.h/cpp` | Profile lifecycle（基于既有 `ProfileSet`）+ 依赖图/有效视图/稳定 CRUD/发布/datapack |
| **S5a** | `components/RegistryHelper.h/cpp` | Set operations + operator overloads + builder（原 `RegistryManager`） |
| **S5b** | *Add operator overloads in global ns* | `|`, `&`, `+`, `-` for Profile |

### Phase 4: Components & Cleanup (S6–S8)

| Step | Files | Description |
|------|-------|-------------|
| **S6** | `components/FormatDetector.h/cpp` | Format detection + dispatch |
| **S7** | *Remove old files* | `EnchInfoParser`, `RawTypeAdapter`, `managers/`, `ProfileSet`, `RawTypes`；`RegistryManager` → `components/RegistryHelper` |
| **S8a** | *Update consumers* | `main.cpp`, `BesqContext`, `SolvePipeline` |
| **S8b** | *Update tests* | Migrate and adapt test files |
| **S8c** | *Update CMakeLists* | All affected build files |
