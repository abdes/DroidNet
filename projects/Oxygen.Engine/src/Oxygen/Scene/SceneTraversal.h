//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstdint>
#include <deque>
#include <ranges>
#include <span>
#include <vector>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

class Scene;
class SceneNodeImpl;

//=== Traversal Types ===-----------------------------------------------------//

//! Enumeration of supported traversal orders
enum class TraversalOrder : uint8_t {
    kDepthFirst, //!< Visit nodes depth-first (no sibling order guarantee)
    kBreadthFirst //!< Visit nodes level by level (no sibling order guarantee)
};

//! Result of a traversal operation
struct TraversalResult {
    std::size_t nodes_visited = 0; //!< Number of nodes visited
    std::size_t nodes_filtered = 0; //!< Number of nodes filtered out
    bool completed = true; //!< True if traversal completed, false if stopped early
};

//=== Traversal Control Enums ===---------------------------------------------//

//! Filter result controlling node visitation and subtree traversal
enum class FilterResult : uint8_t {
    kAccept, //!< Visit node and traverse children
    kReject, //!< Skip node, but traverse children
    kRejectSubTree //!< Skip node and skip its entire subtree
};

//! Visitor result controlling traversal continuation
enum class VisitResult : uint8_t {
    kContinue, //!< Continue traversal as normal
    kSkipSubtree, //!< Do not traverse this node's children
    kStop //!< Stop traversal entirely
};

//=== Concepts for Visitors and Filters ===-----------------------------------//

template <typename Visitor>
concept SceneVisitor = requires(Visitor v, SceneNodeImpl& node, const Scene& scene) {
    { v(node, scene) } -> std::convertible_to<VisitResult>;
};

// Update SceneFilter concept to accept parent_result
template <typename Filter>
concept SceneFilter = requires(Filter f, const SceneNodeImpl& node, FilterResult parent_result) {
    { f(node, parent_result) } -> std::convertible_to<FilterResult>;
};

//=== High-Performance Filters ===--------------------------------------------//

//! Filter that accepts all nodes.
struct AcceptAllFilter {
    constexpr auto operator()(
        const SceneNodeImpl& /*node*/,
        FilterResult /*parent_result*/) const noexcept
        -> FilterResult
    {
        return FilterResult::kAccept;
    }
};
static_assert(SceneFilter<AcceptAllFilter>);

//! Filter that accepts nodes based on the state of their transforms.
/*!
 This filter enables efficient traversal for transform updates in a scene graph.

 - Traversal proceeds as deep as possible, visiting all nodes that require
   transform updates.
 - If a parent node is accepted for visitation, its children must also accept,
   unless they have the kIgnoreParentTransform flag set.
 - This ensures that world transforms remain consistent throughout the
   hierarchy.
 - If a node is configured to ignore its parent transform, its entire subtree is
   excluded from traversal.
 - When a node is visited, it is expected that its parent transform is
   up-to-date, allowing it to compute its own world transform.
*/
struct DirtyTransformFilter {
    auto operator()(
        const SceneNodeImpl& node,
        const FilterResult parent_result) const noexcept
        -> FilterResult
    {
        // If parent was accepted and this node does not ignore parent transform, accept
        if (node.GetFlags().GetEffectiveValue(SceneNodeFlags::kIgnoreParentTransform)) {
            return FilterResult::kRejectSubTree;
        }
        // Otherwise, accept if this node is dirty
        return parent_result == FilterResult::kAccept || node.IsTransformDirty()
            ? FilterResult::kAccept
            : FilterResult::kReject;
    }
};
static_assert(SceneFilter<DirtyTransformFilter>);

//! Filter that accepts only visible SceneNodeImpl objects.
/*!
 This filter accepts only nodes that are marked as visible, and will block the
 entire sub-tree below a node if it's not visible.
*/
struct VisibleFilter {
    auto operator()(
        const SceneNodeImpl& node,
        FilterResult /*parent_result*/) const noexcept
        -> FilterResult
    {
        const auto& flags = node.GetFlags();
        return flags.GetEffectiveValue(SceneNodeFlags::kVisible)
            ? FilterResult::kAccept
            : FilterResult::kRejectSubTree;
    }
};
static_assert(SceneFilter<VisibleFilter>);

//=== High-Performance Scene Graph Traversal ===------------------------------//

//! High-performance scene graph traversal interface
/*!
 Provides optimized, non-recursive traversal algorithms working directly with
 SceneNodeImpl pointers for maximum performance in batch operations.

 Key features:
 - Non-recursive to avoid stack overflow on deep hierarchies
 - Direct implementation access bypassing SceneNode wrapper creation
 - Efficient with pre-allocated containers and minimal allocation
 - Cache-friendly sequential pointer processing

 \warning The Scene API does not guarantee any specific order for sibling nodes.
 \warning Modifying the scene graph (adding/removing nodes, changing
          parent/child relationships) during traversal is undefined behavior and
          may result in crashes or inconsistent results.
*/
class SceneTraversal {
public:
    OXYGEN_SCENE_API explicit SceneTraversal(const Scene& scene);

    //=== Core Traversal API ===----------------------------------------------//

    //! Traverse the entire scene graph from root nodes
    template <SceneVisitor VisitorFunc, SceneFilter FilterFunc = AcceptAllFilter>
    [[nodiscard]] auto Traverse(
        VisitorFunc&& visitor,
        TraversalOrder order = TraversalOrder::kDepthFirst,
        FilterFunc&& filter = AcceptAllFilter {}) const -> TraversalResult;

    //! Traverse from specific root nodes
    template <SceneVisitor VisitorFunc, SceneFilter FilterFunc = AcceptAllFilter>
    [[nodiscard]] auto TraverseFrom(
        std::span<const ResourceHandle> root_handles,
        VisitorFunc&& visitor,
        TraversalOrder order = TraversalOrder::kDepthFirst,
        FilterFunc&& filter = AcceptAllFilter {}) const -> TraversalResult;

    //! Traverse from a single root node
    template <SceneVisitor VisitorFunc, SceneFilter FilterFunc = AcceptAllFilter>
    [[nodiscard]] auto TraverseFrom(
        const ResourceHandle& root_handle,
        VisitorFunc&& visitor,
        TraversalOrder order = TraversalOrder::kDepthFirst,
        FilterFunc&& filter = AcceptAllFilter {}) const -> TraversalResult;

    //=== Convenience Methods ===---------------------------------------------//

    //! Update transforms for all dirty nodes using optimized traversal
    /*!
     Efficiently updates transforms for all nodes that have dirty transform state.
     This is equivalent to Scene::Update but uses the optimized traversal system.

     \return Number of nodes that had their transforms updated
    */
    [[nodiscard]] OXYGEN_SCENE_API auto UpdateTransforms() const -> std::size_t;

    //! Update transforms for dirty nodes from specific roots
    /*!
     \param root_handles Starting node handles for transform update traversal
     \return Number of nodes that had their transforms updated
    */
    [[nodiscard]] OXYGEN_SCENE_API auto UpdateTransformsFrom(
        std::span<const ResourceHandle> root_handles) const -> std::size_t;

private:
    const Scene* scene_;
    mutable std::vector<SceneNodeImpl*> children_buffer_; //!< Reused for child collection

    //! Calculate optimal stack capacity based on scene size
    [[nodiscard]] auto GetOptimalStackCapacity() const -> std::size_t
    {
        const auto node_count = scene_->GetNodeCount();
        if (node_count <= 64)
            return 32;
        if (node_count <= 256)
            return 64;
        if (node_count <= 1024)
            return 128;
        return 256; // Cap at reasonable size for deep scenes
    }

    //! Get valid node pointer from handle
    OXYGEN_SCENE_API auto GetNodeImpl(const ResourceHandle& handle) const -> SceneNodeImpl*;

    //! Collect children of a node into reused buffer
    void CollectChildren(SceneNodeImpl* node) const
    {
        children_buffer_.clear(); // Fast - just resets size for pointer vector

        auto child_handle = node->AsGraphNode().GetFirstChild();
        if (!child_handle.IsValid()) {
            return; // Early exit for leaf nodes
        }

        // Collect all children in a single pass
        while (child_handle.IsValid()) {
            auto* child_node = GetNodeImpl(child_handle);
            children_buffer_.push_back(child_node);
            child_handle = child_node->AsGraphNode().GetNextSibling();
        }
    }

    //! Initialize node pointers from handles
    OXYGEN_SCENE_API void InitializeNodes(std::span<const ResourceHandle> handles,
        std::vector<SceneNodeImpl*>& nodes) const;

    //! Depth-first traversal implementation
    /*!
     Traverses the scene graph in depth-first order.
     \warning Sibling order is not guaranteed and depends on Scene storage.
    */
    template <typename VisitorFunc, typename FilterFunc>
    auto TraverseDepthFirst(const std::vector<SceneNodeImpl*>& roots,
        VisitorFunc&& visitor,
        FilterFunc&& filter) const -> TraversalResult;

    //! Breadth-first traversal implementation
    /*!
     Traverses the scene graph in breadth-first order.
     \warning Sibling order is not guaranteed and depends on Scene storage.
    */
    template <typename VisitorFunc, typename FilterFunc>
    auto TraverseBreadthFirst(const std::vector<SceneNodeImpl*>& roots,
        VisitorFunc&& visitor,
        FilterFunc&& filter) const -> TraversalResult;

    //! Core traversal dispatcher
    template <typename VisitorFunc, typename FilterFunc>
    [[nodiscard]] auto TraverseInternal(
        std::span<const ResourceHandle> root_handles,
        VisitorFunc&& visitor,
        TraversalOrder order,
        FilterFunc&& filter) const -> TraversalResult;
};

//=== Template Implementation ===---------------------------------------------//

template <SceneVisitor VisitorFunc, SceneFilter FilterFunc>
auto SceneTraversal::Traverse(
    VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter) const
    -> TraversalResult
{
    const auto root_handles = scene_->GetRootNodes();
    return TraverseInternal(root_handles, std::forward<VisitorFunc>(visitor),
        order, std::forward<FilterFunc>(filter));
}

template <SceneVisitor VisitorFunc, SceneFilter FilterFunc>
auto SceneTraversal::TraverseFrom(std::span<const ResourceHandle> root_handles,
    VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter) const
    -> TraversalResult
{
    return TraverseInternal(root_handles, std::forward<VisitorFunc>(visitor),
        order, std::forward<FilterFunc>(filter));
}

template <SceneVisitor VisitorFunc, SceneFilter FilterFunc>
auto SceneTraversal::TraverseFrom(const ResourceHandle& root_handle,
    VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter) const
    -> TraversalResult
{
    const ResourceHandle root_handles[] = { root_handle };
    return TraverseInternal(root_handles, std::forward<VisitorFunc>(visitor),
        order, std::forward<FilterFunc>(filter));
}

template <typename VisitorFunc, typename FilterFunc>
auto SceneTraversal::TraverseInternal(
    const std::span<const ResourceHandle> root_handles,
    VisitorFunc&& visitor, const TraversalOrder order, FilterFunc&& filter) const
    -> TraversalResult
{
    if (root_handles.empty()) [[unlikely]] {
        return TraversalResult {};
    }

    // Initialize nodes from handles
    std::vector<SceneNodeImpl*> roots;
    InitializeNodes(root_handles, roots);

    if (roots.empty()) [[unlikely]] {
        return TraversalResult {};
    }

    // Dispatch to appropriate traversal algorithm
    switch (order) {
    case TraversalOrder::kDepthFirst:
        return TraverseDepthFirst(roots, std::forward<VisitorFunc>(visitor),
            std::forward<FilterFunc>(filter));
    case TraversalOrder::kBreadthFirst:
        return TraverseBreadthFirst(roots, std::forward<VisitorFunc>(visitor),
            std::forward<FilterFunc>(filter));
    }

    // This should never be reached with valid enum values
    [[unlikely]] return TraversalResult {};
}

template <typename VisitorFunc, typename FilterFunc>
auto SceneTraversal::TraverseDepthFirst(
    const std::vector<SceneNodeImpl*>& roots,
    VisitorFunc&& visitor, // NOLINT(cppcoreguidelines-missing-std-forward)
    FilterFunc&& filter) const -> TraversalResult // NOLINT(cppcoreguidelines-missing-std-forward)
{
    // We're calling them in a loop, create a local reference.
    auto& local_visitor = visitor;
    auto& local_filter = filter;

    TraversalResult result {};
    struct StackEntry {
        SceneNodeImpl* node;
        FilterResult parent_result;
    };
    std::vector<StackEntry> stack;
    stack.reserve(GetOptimalStackCapacity());

    // Initialize stack with roots, parent_result defaults to Reject
    for (auto root : std::ranges::reverse_view(roots)) {
        stack.push_back({ root, FilterResult::kReject });
    }

    auto add_children_to_stack = [&](SceneNodeImpl* node, FilterResult parent_filter_result) {
        CollectChildren(node);
        if (!children_buffer_.empty()) {
            for (auto& it : std::ranges::reverse_view(children_buffer_)) {
                stack.push_back({ it, parent_filter_result });
            }
        }
    };

    while (!stack.empty()) {
        const auto& entry = stack.back();
        auto* node = entry.node;
        auto parent_result = entry.parent_result;
        stack.pop_back();

        switch (const FilterResult filter_result = local_filter(*node, parent_result)) {
        case FilterResult::kAccept: {
            ++result.nodes_visited;
            const VisitResult visit_result = local_visitor(*node, *scene_);
            if (visit_result == VisitResult::kStop) [[unlikely]] {
                result.completed = false;
                return result;
            }
            if (visit_result != VisitResult::kSkipSubtree) {
                add_children_to_stack(node, filter_result);
            }
            break;
        }
        case FilterResult::kReject: {
            ++result.nodes_filtered;
            add_children_to_stack(node, filter_result);
            break;
        }
        case FilterResult::kRejectSubTree: {
            ++result.nodes_filtered;
            // Skip node and entire subtree
            break;
        }
        }
    }

    return result;
}

template <typename VisitorFunc, typename FilterFunc>
auto SceneTraversal::TraverseBreadthFirst(
    const std::vector<SceneNodeImpl*>& roots,
    VisitorFunc&& visitor, // NOLINT(cppcoreguidelines-missing-std-forward)
    FilterFunc&& filter) const -> TraversalResult // NOLINT(cppcoreguidelines-missing-std-forward)
{
    // We're calling them in a loop, create a local reference.
    auto& local_visitor = visitor;
    auto& local_filter = filter;

    TraversalResult result { .completed = true };
    struct QueueEntry {
        SceneNodeImpl* node;
        FilterResult parent_result;
    };
    std::deque<QueueEntry> queue;

    // Initialize queue with all roots, parent_result defaults to Reject
    for (auto* root : roots) {
        queue.push_back({ root, FilterResult::kReject });
    }

    auto add_children_to_queue = [&](SceneNodeImpl* node, FilterResult parent_filter_result) {
        CollectChildren(node);
        if (!children_buffer_.empty()) {
            for (auto& it : std::ranges::reverse_view(children_buffer_)) {
                queue.push_back({ it, parent_filter_result });
            }
        }
    };

    while (!queue.empty()) {
        const auto& entry = queue.front();
        auto* node = entry.node;
        auto parent_result = entry.parent_result;
        queue.pop_front();

        switch (const FilterResult filter_result = local_filter(*node, parent_result)) {
        case FilterResult::kAccept: {
            ++result.nodes_visited;
            const VisitResult visit_result = local_visitor(*node, *scene_);
            if (visit_result == VisitResult::kStop) [[unlikely]] {
                result.completed = false;
                return result;
            }
            if (visit_result != VisitResult::kSkipSubtree) {
                add_children_to_queue(node, filter_result);
            }
            break;
        }
        case FilterResult::kReject: {
            ++result.nodes_filtered;
            add_children_to_queue(node, filter_result);
            break;
        }
        case FilterResult::kRejectSubTree: {
            ++result.nodes_filtered;
            // Skip node and entire subtree
            break;
        }
        }
    }
    return result;
}

} // namespace oxygen::scene
