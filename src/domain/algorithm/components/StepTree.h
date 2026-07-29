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
/// Serialisation helpers:
///   auto flat = tree.materialize();       // → std::vector<EnchStep>
///   auto tree = StepTree::from_flat(vec); // ← rebuild linear chain

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

    /// Build a linear chain from a flat step vector (deserialisation).
    static StepTree from_flat(const std::vector<EnchStep>& flat) {
        std::shared_ptr<Node> cur;
        for (auto it = flat.rbegin(); it != flat.rend(); ++it) {
            cur = std::make_shared<Node>(
                *it, std::move(cur), nullptr, flat.size());
        }
        return StepTree{std::move(cur)};
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
