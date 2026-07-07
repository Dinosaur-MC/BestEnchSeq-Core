# BestEnchSeq-Core

## Overview

A C++20 tool to calculate the best enchanting order for your enchantments and enchanted books, which will try reducing your enchanting cost on anvil as far as possible. It not only supports enchantments in Vanilla Minecraft, but also those in various mods, as it uses extensible enchantment sheets and a data-driven registry system to maintain enchantment information and can easily add third-party/custom enchantments.

### Key Features

- [x] Calculate the best enchanting order/forging sequence by your given needs
- [ ] Support inventory management, providing well handling of complex enchanted items/situations (applicability, upgrade, confliction, override, prior work penalty, durability, etc.)
- [x] Support third-party/custom enchantments by editing custom enchantment sheet
- [x] Support third-party/custom equipments by editing custom equipment sheet
- [x] Pluggable algorithm strategies (Greedy, with more to come)
- [x] Asynchronous execution with pause/resume/cancel and streaming progress
- [x] Easily export to share calculation results with others
- [ ] Optionally hosting a RESTful API service for external applications

### Quick Start

#### From binary distribution

*(Coming soon)*

#### From source code

```bash
cmake -S . -B build
cmake --build build
./build/bin/BestEnchSeq-Core.exe --target diamond_sword --wanted "sharpness=5,knockback=2"
```

CMake options:

```bash
cmake -S . -B build -DENABLE_NETWORK=ON -DENABLE_API_SERVICE=OFF
```

- `ENABLE_NETWORK` defaults to `ON`.
- `ENABLE_API_SERVICE` defaults to `OFF`.

### Running tests

```bash
cd build && ctest --output-on-failure
```

## Architecture

The project is organized into four layers with clear separation of concerns:

```
src/
├── registries/         ← Data registry layer (singleton + isolated instances)
├── types/              ← Pure value types (no global state)
├── algorithm/          ← Abstract algorithm interface + forge helpers
├── parser/             ← Input/output parsing (CLI, JSON, CSV)
└── io/                 ← JSON library
```

### Registries Layer (`src/registries/`)

Manages the lifecycle and lookup of all game data. Each registry supports both global singleton access (`get_instance()`) for convenience and standalone instance construction for testing or algorithm subset derivation.

| Component | Responsibility |
|---|---|
| `EnchantmentRegistry` | Stores `EnchInfo` instances, provides index/name lookup, built-in incompatibility table, validation, and **subset derivation** (`create_subset()`) with dense index remapping for high-performance algorithm use |
| `EquipmentRegistry` | Stores `EquipmentType` instances, provides ID lookup, and manages custom equipment categories |
| `PlatformConfig` | Singleton configuration for active Minecraft platform (Java / Bedrock), providing `set_active()` and `get_active()` |

**Key design decisions:**

- **Subset derivation**: Algorithms typically only need a small subset of all enchantments. `EnchantmentRegistry::create_subset()` produces a new registry with dense indices `0..N-1` and a remapped incompatibility table, keeping working data in L1 cache.
- **Read-only during computation**: After initialization, all registry lookups are `const` and thread-safe without locks.
- **Performance**: `Ench` remains 8 bytes (two `int32_t`) with no vtable or per-instance registry pointer. Hot paths use the unchecked `Ench(id, level, Ench::unchecked)` constructor which skips bounds validation.

### Types Layer (`src/types/`)

Pure value types with **no global state**. All types are movable and copyable POD-like structs.

#### Common

| Type | Description |
|---|---|
| `MCE` | Platform enum: `Java`, `Bedrock`, `All` |
| `EquipmentCategory` | String-derived type for equipment classification (helmet, sword, etc.), extensible via `EquipmentRegistry` |

#### Enchantment

| Type | Description |
|---|---|
| `EnchInfo` | Static enchantment metadata: name, level limits, cost multiplier, exclusive set, applicable equipment. **No static members** — pure value type |
| `Ench` | Lightweight runtime value: `{id, level}` — 8 bytes. Supports two construction paths: checked (validates against global registry) and unchecked (`Ench::unchecked` tag, no validation, for hot paths) |
| `EnchSet` | Hash set of `Ench` with combining logic: incompatibility checks, level upgrade rules, platform-dependent cost calculation |

#### Equipment

| Type | Description |
|---|---|
| `EquipmentType` | Equipment metadata: durability, category, enchantment applicability filtering |

#### Item

| Type | Description |
|---|---|
| `ItemStack` | Forgeable item: optional `EquipmentType*`, `EnchSet`, prior-work penalty, durability, and cached evaluation cost |
| `ItemCollection` | `std::vector<ItemStack>` |

#### Solution

| Type | Description |
|---|---|
| `EnchSolution` | Computed result: ordered forge steps, per-step EXP cost, aggregate totals, algorithm metadata |
| `EnchStep` | One forge operation: source item A + source item B → resulting cost |

### Algorithm Layer (`src/algorithm/`)

The algorithm layer uses an **interface + strategy + executor** pattern:

- **`IAlgorithm`** — Pure virtual interface. Each strategy (Greedy, DFS, etc.) implements `execute(input, ctx)`.
- **`IForgeEngine` / `DefaultForgeEngine`** — Forge logic abstraction. `DefaultForgeEngine` implements vanilla Minecraft rules; subclass or reimplement for modded behavior. `ForgeConfig` (ignore penalty/repair/cost-cap) is constructor-injected and immutable.
- **`AlgorithmExecutor`** — Async engine managing thread lifecycle, state machine (`Idle → Running → Paused → Completed | Failed | Cancelled`), pause/resume/cancel, and streaming callbacks via `AlgorithmObserver`.
- **`AlgorithmRegistry`** — Singleton factory for registration and lookup of named algorithms.

**Legacy**: `BaseAlgorithm` is `[[deprecated]]`. New code should use `IAlgorithm` + `AlgorithmExecutor`.

### Parser Layer (`src/parser/`)

Handles all I/O:
- `EnchInfoParser` / `EquipmentParser` — read enchantment/equipment data from JSON, CSV, or Minecraft official data pack format
- `InputParser` — assemble CLI config + inventory files into algorithm Input
- `OutputFormatter` — format solutions as verbose, compact, or JSON output
- `TagResolver` — resolve Minecraft tag references

## Source Layout

```
src/
├── main.cpp                     ← Entrypoint: init registries → parse input → run algorithm → format output
├── BESQTypes.h                  ← Umbrella header re-exporting domain model types
├── registries/
│   ├── EnchantmentRegistry.h/cpp
│   ├── EquipmentRegistry.h/cpp
│   └── PlatformConfig.h
├── types/
│   ├── common.h                 ← MCE enum, EquipmentCategory (header-only, no static state)
│   ├── EnchInfo.h/cpp           ← Enchantment metadata value type
│   ├── Ench.h/cpp               ← Runtime enchantment (8 bytes)
│   ├── EnchSet.h/cpp            ← Enchantment set with combining logic
│   ├── EquipmentType.h/cpp      ← Equipment metadata
│   ├── ItemStack.h/cpp          ← Forgeable item stack
│   └── EnchSolution.h/cpp       ← Computed solution
├── algorithm/
│   ├── IAlgorithm.h              ← Pure algorithm interface + AlgorithmInput/Output
│   ├── IForgeEngine.h            ← Forge logic interface + ForgeConfig
│   ├── DefaultForgeEngine.h/cpp  ← Vanilla forge rules implementation
│   ├── AlgorithmExecutor.h/cpp   ← Async execution engine + ExecutionContext + Observer
│   ├── AlgorithmRegistry.h/cpp   ← Algorithm strategy factory
│   ├── strategies/
│   │   └── GreedyAlgorithm.h/cpp ← First concrete strategy
│   └── BaseAlgorithm.h/cpp       ← [deprecated] replaced by IAlgorithm + Executor
├── utils/
│   ├── ExpCalculator.h/cpp       ← EXP level ↔ experience point conversion
│   └── SolutionFactory.h/cpp     ← Assemble EnchSolution from AlgorithmOutput
├── parser/
│   ├── CLIParser.h/cpp          ← Command-line argument parsing
│   ├── InputParser.h/cpp        ← Input assembly (CLI + inventory → algorithm Input)
│   ├── OutputFormatter.h/cpp    ← Output formatting (verbose/compact/json)
│   ├── EnchInfoParser.h/cpp     ← Enchantment data parsing (JSON/CSV/MC official)
│   ├── EquipmentParser.h/cpp    ← Equipment data parsing
│   ├── ParserUtils.h/cpp        ← Shared parsing utilities
│   └── TagResolver.h/cpp        ← Minecraft tag reference resolution
├── io/
│   ├── json.h/cpp               ← JSON library
│   └── CsvIO.h/cpp              ← CSV read/write primitives (quoted fields, escaping)
└─ data/
    ├── builtin/
    │   └── vanilla.json         ← Built-in Vanilla Minecraft enchantment data
    └── examples/
        └── inventory_example.json
```

## Persistence

*(Coming soon)*

## Workflow

*(Coming soon)*

## Contributing

*(Coming soon)*

## License

> MIT License
