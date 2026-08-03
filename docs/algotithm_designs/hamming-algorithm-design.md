# HammingAlgorithm — Popcount-Balanced Merge Tree

**Name:** `hamming`  
**Version:** `1.0.0`  
**Source:** `src/domain/algorithm/_strategies/hamming/HammingAlgorithm.{h,cpp}`  
**Port of:** `Alg_Hamming()` from BestEnchSeq v2.x (Qt/C++)

---

## Table of contents

1. [Motivation](#motivation)
2. [Core idea: popcount as merge depth](#core-idea-popcount-as-merge-depth)
3. [Algorithm walk-through](#algorithm-walk-through)
4. [Worked example](#worked-example)
5. [Comparison with other algorithms](#comparison-with-other-algorithms)
6. [Comparison with the old Qt Alg_Hamming()](#comparison-with-the-old-qt-alg_hamming)
7. [Applicability and limitations](#applicability-and-limitations)
8. [Performance](#performance)
9. [Implementation notes](#implementation-notes)

---

## Motivation

Minecraft anvil forging cost has a term that grows exponentially with
the item's prior-penalty number (PPN):

    penalty_cost(ppn) = 2^ppn - 1    (capped at 39 in survival; ±∞ in modded)

Each forge operation increments the result's PPN by 1.  Over a sequence
that merges `n` items into one, a poorly chosen merge order can push
some items through O(n) forges, incurring exponential penalty cost on
that branch.  The optimal merge tree (for penalty alone) is a **balanced
binary tree** where each item participates in roughly ⌈log₂(n)⌉ forges
(at most ⌊log₂(n-1)⌋ + 1 for the deepest leaf).

The Hamming algorithm is a deterministic O(n log n) construction that
produces this balanced tree without search.  It was the primary fast
algorithm in BestEnchSeq v2.x and has been ported to the current C++20
architecture.

---

## Core idea: popcount as merge depth

For a sequence of `n` items indexed `0 … n-1`, define:

    popcount(i) = number of set bits in the binary representation of i

The popcount values across `0 … n-1` form a "merge-level" assignment:

```
i      binary   popcount(i)   role
──     ──────   ───────────   ────
0      000      0             root (equipment, merged 0 times)
1      001      1             tier-1 leaf (merged 1 time)
2      010      1             tier-1 leaf
3      011      2             tier-2 leaf
4      100      1             tier-1 leaf
5      101      2             tier-2 leaf
6      110      2             tier-2 leaf
7      111      3             tier-3 leaf (deepest)
```

The arrangement places items so that adjacent pairs `(0,1), (2,3), …`
merge together.  Because `popcount(i)` approximates the depth of `i` in
a complete binary tree, each item goes through roughly `popcount(i)`
merge operations — ensuring an overall balanced merge tree.

### Why this matters for enchanting

The dominant variable cost in anvil forging is:

    total_cost = Σ(penalty_cost) + Σ(ench_cost) + conflicts + repair

`penalty_cost` is exponential in PPN.  A balanced tree keeps each item's
PPN growth at `⌈log2(n)⌉` instead of `O(n)`.  For 8 items:

| Merge strategy | max PPN | penalty_cost for that item |
|:---------------|:-------:|:--------------------------:|
| Linear chain   | 7       | 127 |
| Balanced tree  | 3       |   7 |
| **Hamming**    | **3**   | **7** |

The difference grows rapidly: at 16 items, linear chain → max PPN = 15,
penalty = 2¹⁵ − 1 = **32767**; balanced tree → max PPN = 4,
penalty = 2⁴ − 1 = **15**.  Over 2000× cheaper.

---

## Algorithm walk-through

```
Input:  equipment + N books  (all with compact-type Item / EnchReg)
Output: ordered list of forge Steps, or "no solution"
```

### Phase 1 — Seed

Copy all items into a `tiers` vector indexed by PPN:

```
tiers[0] = [equip, book₁, book₂, …]    # most items start at PPN 0
tiers[1] = […]                          # items that already have PPN 1
tiers[2] = […]                          # items that already have PPN 2
```

### Phase 2 — Cascade (the "Hamming triangle")

```
for tier = 0, 1, 2, … while tiers[tier] exists:

    if tiers[tier] has ≤ 1 item:
        carry it to tier+1 (if tier+1 exists)
        continue

    sort items by forge cost descending
    → equipment first, then highest-cost book, etc.
    (inventory 模式：装备保持 resolver 进入顺序——位置 0 恒为 resolver
     选定的无冲突 base（平衡树根）；仅书按成本降序。direct 模式单装备，
     装备排序无差别，保持原行为。)

    arrange_by_popcount(items)
    → position k → item that should go through ~popcount(k) merges

    while items ≥ 2:
        a = pop_front(items)    // base
        b = pop_front(items)    // sacrifice
        forge_into(a, b)        // a absorbs b
        steps.push(pre-forge state of a, b, cost)
        next_items.push(a)      // forged result preserved

    if 1 item remains:
        next_items.push(it)     // odd leftover, carried forward

    clear tiers[tier]
    push ALL next_items to tiers[tier + 1]
    → guarantees convergence
```

### Phase 3 — Resolution

Scan tiers from highest to lowest for any Equipment item
whose enchantments meet the target.  If found, report the
forge steps with accumulated cost.

```
If no Equipment meets target → "no solution"
```

---

## Worked example

**netherite_sword (8 enchantments)** — 1 equipment + 8 books = 9 items.
Each book is single-enchantment.  Local IDs and book-multipliers (`mul_b`)
from the compact registry determine the cost-sort order.

| ID | mul_b | Level | Product | Item |
|:--:|:-----:|:-----:|:-------:|:-----|
| 37 | 2 | 3 | 6 | sweeping_edge |
| 18 | 2 | 3 | 6 | looting |
| 33 | 1 | 5 | 5 | sharpness |
| 10 | 2 | 2 | 4 | fire_aspect |
| 17 | 2 | 2 | 4 | knockback |
| 40 | 1 | 3 | 3 | unbreaking |
| 23 | 2 | 1 | 2 | mending |
| 41 | 1 | 1 | 1 | (other) |

### Tier 0 — arrange and forge (n=9)

Sorted by `estimate_forge_cost` descending (multiplier × level):
```
[equip, 37(6), 18(6), 33(5), 10(4), 17(4), 40(3), 23(2), 41(1)]
```

Popcount arrangement — fill positions by ascending popcount level `j`:

```
pos:  0    1    2    3    4    5    6    7    8
pop:  0    1    1    2    1    2    2    3    1
─────────────────────────────────────────────────
      eq   37   18   17   33   40   23   41   10
           ← j=1 →    ← j=2 →        ←j=3 ←j=1
```

| Pair | Forge | Result (PPN=1) |
|:-----|:------|:----------------|
| (0,1) | equip + 37 | equip + sweeping |
| (2,3) | 18 + 17 | book(looting + knockback) |
| (4,5) | 33 + 40 | book(sharpness + unbreaking) |
| (6,7) | 23 + 41 | book(mending + other) |
| (8,-) | 10 (fire_aspect) | leftover, carried |

All 5 results → tier 1.

### Tier 1 — arrange (n=5)

```
[equip+37, book(18+17), book(33+40), book(23+41), 10]
               cost=9*      cost=8      cost=3    cost=4
```

Popcount arranges → `[eq, book(18+17), book(33+40), 10, book(23+41)]`

| Pair | Result (PPN=2) |
|:-----|:----------------|
| equip + book(18+17) | equip(+sweeping+looting+knockback) |
| book(33+40) + 10 | book(sharpness+unbreaking+fire) |
| leftover book(23+41) | carried |

All 3 results → tier 2.

### Tier 2 — arrange (n=3)

```
[equip(3 ench), book(33+40+10), book(23+41)]
```

Popcount arranges → `[eq, book(33+40+10), book(23+41)]`

| Pair | Result (PPN=3) |
|:-----|:----------------|
| equip + book(33+40+10) | equip + 6 enchants (all but 23+41) |
| leftover book(23+41) | carried |

2 results → tier 3.

### Tier 3

```
[equip(6 of 8), book(23+41)]
```

Forge → equip gains mending + (other) → **all 8 target enchants achieved**.
PPN = 4.  8 steps, 9 items → minimum possible.

*\* Estimated cost of a multi-enchant book is the sum of its individual
enchantment costs (`mul_b × level`), plus the doubled penalty term
which is uniform for items at the same tier and thus doesn't affect
ordering.*

---

## Comparison with other algorithms

### Versus other built-in strategies

| Property | Greedy | penalty_balance | Hierarchical | Hamming | A*/IDA* |
|:---------|:------:|:---------------:|:------------:|:-------:|:-------:|
| Complexity | O(n²) | O(n²) | O(n²) | **O(n log n)** | O(n!) |
| Search | No | No | No | **No** | Yes |
| Optimality | Low | Medium | Medium | **High** | Exact |
| Bounded backtrack | No | No | No | **No** | Yes |
| Deterministic | Yes | Yes | Yes | **Yes** | No |

**GreedyAlgorithm** — merges every book directly into the equipment in
cost order.  Fast but very sub-optimal: the equipment's PPN grows
linearly with the number of books, incurring high penalty.

**DynamicPenaltyBalancingAlgorithm** — at each step selects the pair with
the smallest PPN difference, then the smallest estimated cost.
PPN-aware but still entirely local-greedy; no global tree structure.
Has no explicit left-over carry; odd-count cases can strand items.

**HierarchicalMergeAlgorithm** — groups books by enchantment multiplier
tier, merges within groups, then merges groups into equipment.
Effective for same-enchantment dedup but the tier grouping is coarse and
does not guarantee balanced internal merge trees.

**HammingAlgorithm** — globally arranges the merge order via popcount.
Achieves the balanced tree property for the entire item set in one
deterministic pass.  Matches A* optimal on 79 % of the benchmark suite
and is within 2 L on the remaining 21 %.

**AStarAlgorithm / IDAStarAlgorithm** — exhaustive search with
admissible heuristic + transposition table.  Guarantees optimality but
cost grows factorially.  At 8+ enchantments wall time jumps from
milliseconds to seconds.

### Solution quality (14-case benchmark)

| Metric | Greedy | penalty_bal | Hierarchical | **Hamming** | A* |
|:-------|:------:|:-----------:|:------------:|:-----------:|:--:|
| Optimal matches (of 14) | 2 | 0 | 0 | **11** | 14 |
| Total sum of costs | 926 | 642 | 846 | **524** | 517 |
| vs A* baseline | +79 % | +24 % | +64 % | **+1.4 %** | — |
| Worst-case overhead | +122 % | +17 % | +52 % | **+3.3 %** | — |

Hamming is within 2 levels of A* on all cases and delivers the same cost
as A* on 11/14 cases, while being **3–4 orders of magnitude faster**.

---

## Comparison with the old Qt Alg_Hamming()

### Structural changes

| Aspect | Old Qt code (v2.x) | Current implementation |
|:-------|:------------------:|:----------------------:|
| Language | C++14 + Qt (QVector, QObject) | C++20, pure STL |
| Types | `Item{name, ench[64]}` fixed array | `compact::Item` with `EnchSet` (uint64_t bitmask + uint8_t[64] levels) |
| Cost model | Hard-coded edition 0/1 | `IForgeEngine` virtual dispatch (JE/BE) |
| Forge mode | 4x `ForgeMode` enum | `ForgeConfig` (ignore_penalty, etc.) |
| Tier routing | PPN-based (`tm_item_triangle[tm.penalty]`) | Sequential tier+1 (proven correct by debugging) |
| Node tracking | Global static arrays | `AlgorithmInput` per-call, `EnchStep` vector |
| Diagnostics | `qDebug` lines | `AlgorithmDiagnostics` + `DiagnosticsWriter` |

### Algorithmic differences

**Cost sorting** — the old code used `preForge(item, Normal).cost`
(which included penalty cost).  The new code uses
`estimate_forge_cost(item, item, reg)` which doubles the penalty term.
Since all items at the same tier share the same PPN, the penalty terms
are equal and the sort order is driven only by enchantment value — the
same effective ordering.

**Popcount iteration bound** — the old code iterated `j` from 1 to
`n` (the number of items), relying on `dupFloorMembers(j, n)` returning
an empty list for `j > max_popcount(n-1)`.  The current code uses
`while (src < n)` to loop, which is equivalent but avoids the
`max_popcount(n-1)` computation that was buggy (`popcount(n-1)`
undercounts when `n-1` itself has few bits set but lower indices have
more, e.g. n=5, n-1=4, popcount(4)=1 but index 3 has popcount=2).

**Leftover carry** — the old code had two separate triangles
(`tm_item_triangle` for actual forging, `item_triangle` for step
recording) and a subtle flow between them.  The current code unifies
this into a single sequential cascade: every item (forged results AND
leftovers) always moves to `tier+1`.  This was discovered to be the
correct invariant after debugging several routing bugs.

**Material types** — the old code knew only `ID_ECB` (book) and
everything else (equipment).  The current code uses the three-way
`ItemType::Book / Equip / Material` enum and validates forge direction
with `is_forgeable()`, falling back to swapping or carrying un-forgeable
pairs forward.

---

## Applicability and limitations

### When to use

- **Default recommended strategy** — Hamming matches A* on 79 % of
  benchmarks at near-zero cost.  Use it whenever a single fast solution
  is sufficient (most daily-driver scenarios).
- **Initial upper bound** — A* and IDA* currently use a greedy bound
  then a limited DFS to tighten it.  Hamming could replace both as the
  pre-search bound, giving a much tighter starting estimate and
  dramatically reducing the number of nodes explored.
- **Large inventories** — O(n log n) scaling means it handles 20+ item
  scenarios that A* cannot touch.

### When to use A* / IDA* instead

- **True optimality required** — A* still finds strictly cheaper
  solutions on 3 of the 14 benchmark cases (by 1–2 levels).
- **Modded content** — the balanced tree assumption holds only when
  enchantment books are the primary forgeable items.  Exotic material
  types or forge rules (e.g. mods that allow multi-enchant combining
  with different multipliers) may degrade the approximation.

### Known limitations

- **Does not attempt level-up merges** — The Hamming arrangement pairs
  books by forge-cost order, not by enchantment ID.  Two books with
  the same enchantment at the same level (which would combine to the
  next level) may be placed in different branches of the tree and never
  get paired.  This is the primary source of the 1–2 L gap versus A*.
- **Deterministic construction — no backtracking** — The algorithm
  commits to the popcount arrangement once per tier.  It cannot undo a
  bad pairing and try alternatives.  Cases where a different first
  merge produces a better total cost are missed.
- **Odd-item leftover chain** — When an odd number of items reaches a
  tier, one item passes through un-forged to the next tier.  This
  "leftover strand" may rejoin the main forge chain later but will
  always have gone through one fewer merge than optimal.
- **Inventory conflicting retained equipment** — In inventory mode the
  resolver may retain an equipment that carries a target enchant AND an
  enchant conflicting with the target (e.g. thorns III + blast_protection
  IV on one chestplate).  `arrange_by_popcount` preserves the resolver's
  equipment order (base at position 0 = the balanced-tree root), so a
  same-tier conflicting equipment merges into the base as a sacrifice and
  its conflicting enchant is dropped by `forge_into` (Java +1 penalty) —
  correct.  Residual (pre-existing): if the conflicting retained
  equipment sits in a **lower PPN tier** than the base, it forges with the
  books in its own tier first and can absorb a needed book, wasting its
  enchant via silent conflict-drop → false "unreachable".  Follow-up
  candidates: make `is_forgeable` reject book→conflicting-equipment, or
  bias tier processing so the base's tier resolves first.

---

## Performance

All measurements from the 14-case benchmark suite on a debug build
(Windows, Clang 19, no LTO).  Hamming times are consistently **<1 ms**
— below the measurement precision of `std::chrono::milliseconds`.

| Test | Items | Steps | A* time | IDA* time | Hamming time |
|:-----|:-----:|:-----:|:-------:|:---------:|:------------:|
| sword_basic | 4 | 3 | 4 ms | 0 ms | **<1 ms** |
| sword_combat_5 | 6 | 5 | 43 ms | 42 ms | **<1 ms** |
| sword_combat_7 | 8 | 7 | 357 ms | 428 ms | **<1 ms** |
| netherite_sword | 9 | 8 | 1989 ms | 3016 ms | **<1 ms** |
| netherite_boots | 10 | 9 | 16725 ms | 27263 ms | **<1 ms** |

The O(n log n) scaling is visible in the step count, which always
equals `items − 1` (the minimum possible, since each forge consumes one
item).  The arrangement and merge overhead scales with `n log n` via the
`std::sort` and the `while (src < n)` popcount loop.

---

## Implementation notes

- **`arrange_by_popcount(preserve_equip_order)`** — sorts by
  `estimate_forge_cost()` (a virtual call on `IForgeEngine`; subclasses
  that override the cost model automatically affect Hamming's ordering).
  In inventory mode (`preserve_equip_order=true`) equipment is kept in
  resolver order via `std::stable_sort` (position 0 = resolver base), and
  only books are cost-sorted — the resolver, not cost, decides the base.
  In direct mode (`false`) equipment keeps the cost-descending sort
  (single equipment, no effect).
- **`dup_floor_members()`** — called once per popcount level per tier.
  Each call allocates a small `std::vector<int>`.  For n ≲ 32 this is
  negligible; for extremely large n a pre-computed LUT could replace it.
- **Cancellation** — checked after every forge and before every
  `tiers.resize()`.  The algorithm is short enough that cancellation
  granularity is finer than in A* or IDA*.
- **Memory** — no persistent heap allocations beyond the initial item
  copy and the `tiers` / `next_items` vectors (all RAII, freed on
  return).  Peak memory = `O(items)`.

```
docs/hamming-algorithm-design.md — 2026-07-14
```
