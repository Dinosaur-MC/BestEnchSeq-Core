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
Input (domain) → CompactAdapter → Algorithm (compact) → CompactAdapter → Output (domain)
```

### Source Layout

```
src/
├── main.cpp                     ← Entrypoint: init registries → parse input → run algorithm → format output
├── types/
│   ├── common.h                 ← platform::MCE enum (Java/Bedrock/All)
│   ├── AlgorithmInput.h         ← Domain boundary input struct
│   ├── CompactedTypes.h/.cpp    ← Compact types: Ench, EnchSet, Item, EnchStep (namespace compact)
│   └── ...domain types          ← EnchInfo, Ench, EnchSet, ItemStack, EnchSolution, Equipment
├── registries/
│   ├── AlgorithmRegistry.h/.cpp ← Algorithm factory (singleton)
│   ├── CompactedRegistries.h/.cpp ← EnchReg: compact registry with O(1) conflict matrix
│   ├── EnchantmentRegistry.h/.cpp
│   ├── EquipmentRegistry.h/.cpp
│   └── PlatformConfig.h/.cpp
├── algorithm/                   ← Zero domain types
│   ├── IAlgorithm.h             ← Pure compact execute() interface + AlgorithmOutput
│   ├── AlgorithmExecutor.h/.cpp ← Async execution engine
│   ├── ExecutionContext.h/.cpp  ← Cancel/pause/progress
│   ├── AlgorithmObserver.h      ← Streaming callbacks
│   ├── forge/
│   │   ├── IForgeEngine.h       ← Virtual interface + ForgeConfig
│   │   └── ForgeEngine.h/.cpp   ← Vanilla implementation
│   └── strategies/
│       ├── GreedyAlgorithm.*
│       ├── DFSAlgorithm.*
│       ├── AStarAlgorithm.*
│       ├── DynamicPenaltyBalancing.*
│       └── HierarchicalMergeStrategy.*
├── utils/
│   ├── CompactAdapter.hpp/.cpp  ← Domain ↔ compact boundary conversions
│   ├── ExpCalculator.hpp        ← Level ↔ XP conversion
│   └── SolutionFactory.hpp      ← Assemble EnchSolution from AlgorithmOutput
├── parser/                      ← CLI, JSON/CSV data parsing, output formatting
├── io/                          ← JSON library, CSV primitives
└── data/
    ├── builtin/vanilla.json     ← Vanilla Minecraft enchantment data
    └── examples/                ← Example inventory files
```

### Key Design Decisions

**Compact types** (`Ench`, `EnchSet`, `Item`): smaller (4-16 bytes), cache-friendly, sorted-vector-based `EnchSet` with O(log N) lookup. No pointer indirection, no virtual dispatch.

**Flat conflict matrix**: `EnchReg` stores an N×N `vector<char>` for O(1) incompatibility checks — single allocation, contiguous memory.

**IForgeEngine virtual interface**: all forge sub-operations (`penalty_cost`, `book_multiplier`, `apply_cap`, `estimate_forge_cost`) have default vanilla implementations. Subclass only what you need to change for modded rules.

**Boundary isolation**: `CompactAdapter` is the only bridge between domain and compact types — used only in `main.cpp`, tests, and benchmarks.

## Persistence

*(Coming soon)*

## Workflow

*(Coming soon)*

## Contributing

*(Coming soon)*

## License

> MIT License
