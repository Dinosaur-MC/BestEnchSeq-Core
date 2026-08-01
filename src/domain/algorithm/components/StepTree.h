#pragma once
#include "domain/algorithm/types/Solution.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace algorithm {

/// ─── StepTree — immutable DAG of forge steps ───────────────────────────
///
/// Instead of copying the full steps vector on every forge_pair, algorithm
/// strategies can store an entry's merge history as a StepTree that shares
/// its prefix nodes with other entries via shared_ptr.  The tree is only
/// materialised into a flat vector when the final solution is extracted
/// or when serialising a checkpoint.
///
/// Memory: one Node (~112 B + EnchStep with 2 Items) per forge operation,
///         shared across all entries that reference the same history.
///         Ref-counted; nodes are freed automatically when no entry
///         references them.
///
/// Usage:
///   // Leaf (first forge step)
///   auto node = std::make_shared<StepTree::Node>(
///       EnchStep{base, sacrifice, cost}, nullptr, nullptr, 1);
///   StepTree tree{std::move(node)};
///
///   // Internal node (combining two histories)
///   auto node = std::make_shared<StepTree::Node>(
///       EnchStep{base, sacrifice, cost},
///       left_tree.root_ptr(), right_tree.root_ptr(),
///       left_tree.size() + right_tree.size() + 1);
///   StepTree tree{std::move(node)};
///
/// Serialisation helpers (lossless inverse pair):
///   auto flat = tree.materialize();       // StepTree → std::vector<EnchStep>
///   auto tree = StepTree::from_steps(flat); // flat → StepTree (reconstructs
///                                            the merge tree, not a chain)

class StepTree {
public:
    struct Node final {
        EnchStep              step;    // the forge step at this node
        std::shared_ptr<Node> left;    // steps that produced the base item
        std::shared_ptr<Node> right;   // steps that produced the sacrifice
        std::size_t           depth;   // total steps in this sub-tree

        Node(EnchStep s, std::shared_ptr<Node> l,
             std::shared_ptr<Node> r, std::size_t d) noexcept
            : step(std::move(s)), left(std::move(l)),
              right(std::move(r)), depth(d) {}
    };

    StepTree() = default;
    explicit StepTree(std::shared_ptr<Node> root) noexcept
        : _root(std::move(root)) {}

    /// Total number of steps in the tree.
    std::size_t size() const noexcept { return _root ? _root->depth : 0; }
    bool empty() const noexcept { return !_root; }

    /// Materialise the tree into a flat vector in forge order.
    std::vector<EnchStep> materialize() const {
        std::vector<EnchStep> out;
        if (!_root) return out;
        out.reserve(_root->depth);
        _materialize(out, _root.get());
        return out;
    }

    /// Shared pointer to the root node (for constructing child trees).
    std::shared_ptr<Node> root_ptr() const noexcept { return _root; }

    /// Rebuild the merge tree from a flat post-order step list — the inverse
    /// of materialize().  Each step carries its own result, so a step's
    /// base/sacrifice is matched by value against the results of prior steps:
    /// a value produced by a prior step becomes that producing sub-tree, a
    /// value never produced is a leaf.  Lossless for trees whose intermediate
    /// results have distinct item values (duplicate-value results resolve to
    /// the first matching producer).  Returns an empty tree on malformed input.
    static StepTree from_steps(const std::vector<EnchStep>& flat) {
        struct Avail {
            Item item;
            std::shared_ptr<Node> tree;
        };
        std::vector<Avail> pool;
        pool.reserve(flat.size() + 1);
        for (const auto& step : flat) {
            auto take = [&](const Item& want) -> std::shared_ptr<Node> {
                for (auto it = pool.begin(); it != pool.end(); ++it) {
                    if (it->item == want) {
                        auto t = std::move(it->tree);
                        pool.erase(it);
                        return t;
                    }
                }
                return nullptr;  // leaf — not produced by any prior step
            };
            auto left  = take(step.base);
            auto right = take(step.sacrifice);
            size_t depth = (left ? left->depth : 0)
                         + (right ? right->depth : 0) + 1;
            pool.push_back({step.result,
                std::make_shared<Node>(step, std::move(left),
                                       std::move(right), depth)});
        }
        if (pool.size() != 1)
            return StepTree{};
        return StepTree{std::move(pool[0].tree)};
    }

private:
    std::shared_ptr<Node> _root;

    static void _materialize(std::vector<EnchStep>& out, const Node* n) {
        if (!n) return;
        _materialize(out, n->left.get());
        _materialize(out, n->right.get());
        out.push_back(n->step);
    }
};

} // namespace algorithm
