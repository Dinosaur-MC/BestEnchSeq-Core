# BestEnchSeq-Core

## Overview

A C++20 tool to calculate the best enchanting order for your enchantments and enchanted books, which will try reducing your enchanting cost on anvil as far as possible. It not only supports enchantments in Vanilla Minecraft, but also those in various mods, as it uses extensible enchantment sheets and a data-driven registry system to maintain enchantment information and can easily add third-party/custom enchantments.

### Key Features

- [x] Calculate the best enchanting order/forging sequence by your given needs
- [ ] Support inventory management, providing well handling of complex enchanted items/situations (applicability, upgrade, confliction, override, prior work penalty, durability, etc.)
- [x] Support third-party/custom enchantments by editing custom enchantment sheet
- [x] Support third-party/custom equipments by editing custom equipment sheet
- [x] Pluggable algorithm strategies: greedy, dfs, astar, penalty_balance, hierarchical
- [x] Asynchronous execution with pause/resume/cancel and streaming progress
- [x] Moddable forge engine via IForgeEngine virtual interface
- [x] Input validation + EnchReg pruning via CompactAdapter::apply()
- [x] Easily export to share calculation results with others
- [ ] Optionally hosting a RESTful API service for external applications

### Quick Start

#### From source code

```bash
cmake -S . -B build
cmake --build build
./build/bin/BestEnchSeq-Core.exe --target diamond_sword --wanted "sharpness=5,knockback=2"
./build/bin/BestEnchSeq-Core.exe --algorithm astar --target diamond_sword --wanted "sharpness=5,looting=3,unbreaking=3"
./build/bin/BestEnchSeq-Core.exe --algorithm penalty_balance --target diamond_chestplate --wanted "protection=4,thorns=3,unbreaking=3,mending=1"
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

### Benchmark

```bash
./build/bin/forge_benchmark --group sword --algo greedy,dfs
./build/bin/forge_benchmark --list
./build/bin/forge_benchmark --help
```

## Architecture

The project uses a **two-tier type system**: compact types (`namespace compact`) for algorithm hot paths, and domain types for I/O boundaries. The algorithm layer has zero domain type dependencies.

```
CLI → InputParser (domain)
  → CompactAdapter::apply() (validates + prunes EnchReg + converts)
  → AlgorithmInput (compact) → AlgorithmExecutor → IAlgorithm
  → AlgorithmOutput (compact steps)
  → CompactAdapter::recall() (restores IDs + builds solutions)
  → OutputFormatter (domain)
```

### Source Layout

```
src/
├── main.cpp                     ← Entrypoint: init registries → parse → CompactAdapter → executor → output
├── adapters/
│   └── CompactAdapter.h/.cpp    ← Domain ↔ compact boundary (apply / recall)
├── types/
│   ├── ForgeConfig.h            ← MCE enum class + ForgeConfig
│   ├── CompactedTypes.h/.cpp    ← Compact types: Ench, EnchSet, Item, EnchStep (namespace compact)
│   └── ...domain types          ← EnchInfo, Ench, EnchSet, ItemStack, EnchSolution (pure data)
├── registries/
│   ├── AlgorithmRegistry.h/.cpp ← Algorithm factory (singleton)
│   ├── CompactedRegistries.h/.cpp ← EnchReg: compact registry with O(1) conflict matrix
│   ├── EnchantmentRegistry.h/.cpp  ← Domain enchantment data with subset derivation
│   └── EquipmentRegistry.h/.cpp
├── algorithm/                   ← Zero domain types
│   ├── IAlgorithm.h             ← AlgorithmInput + AlgorithmOutput + IAlgorithm interface
│   ├── AlgorithmExecutor.h/.cpp ← Async execution engine
│   ├── ExecutionContext.h/.cpp  ← Cancel/pause/progress
│   ├── AlgorithmObserver.h      ← Streaming callbacks
│   ├── forge/
│   │   ├── IForgeEngine.h       ← Virtual interface + ForgeConfig + default sub-ops
│   │   └── ForgeEngine.h/.cpp   ← Vanilla implementation
│   └── strategies/
│       ├── GreedyAlgorithm.*    ← Fast approximate
│       ├── DFSAlgorithm.*       ← Exact search (branch-and-bound)
│       ├── AStarAlgorithm.*     ← Exact optimal (admissible heuristic)
│       ├── DynamicPenaltyBalancing.* ← High-quality approx
│       └── HierarchicalMergeStrategy.* ← Large-scale approx
├── utils/
│   ├── ExpCalculator.hpp        ← Level ↔ XP conversion
├── parser/                      ← CLI, JSON/CSV data parsing, output formatting
├── io/                          ← JSON library, CSV primitives
└── data/
    ├── builtin/vanilla.json     ← Vanilla Minecraft enchantment data
    └── examples/                ← Example inventory files
```

### Key Design Decisions

**Compact types**: `Ench` (4 bytes), `compact::EnchSet` (sorted vector, O(log N) lookup), `Item` (type + dur + ppn + enchs). No pointer indirection, no virtual dispatch, zero domain dependencies.

**Flat conflict matrix**: `EnchReg` stores an N×N `vector<char>` for O(1) incompatibility checks — single allocation, contiguous memory.

**EnchReg pruning**: `CompactAdapter::apply()` builds a subset of the global registry (via `EnchantmentRegistry::create_subset()`) that only includes enchantments applicable to the target equipment. Smaller conflict matrix, faster lookups.

**IForgeEngine virtual interface**: All forge sub-operations (`penalty_cost`, `book_multiplier`, `apply_cap`, `estimate_forge_cost`) have default vanilla implementations. Subclass only what you need for modded rules. `ForgeEngine` overrides to respect `ForgeConfig` flags.

**AlgorithmInput owns data**: `EnchReg`, `Equipment`, and item collections are stored by value — no pointers, no external lifetime dependencies. Once passed to `executor.start()`, ownership transfers completely.

**Domain types are pure data**: `EnchSet`, `Ench`, `ItemStack` are containers only. All combine/cost/penalty computation moved to compact forge engine.

**No global platform singleton**: Platform (`MCE::Java` / `MCE::Bedrock`) flows through `ForgeConfig` → `AlgorithmInput` → `IForgeEngine`. No global mutable state.

## License

> MIT License
