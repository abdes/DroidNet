//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

class Scene;
class SceneNodeImpl;

//=== Traversal Types ===-----------------------------------------------------//

//! Context structure providing both handle and implementation for traversal
//! visitors.
/*!
 This structure provides visitors with access to both the NodeHandle and
 SceneNodeImpl for a node during traversal. This enables scenarios where the
 visitor needs the handle for operations like cloning, mapping, or external
 resource management, while still providing efficient access to the node data.

 Key use cases:
 - Node cloning operations that need to maintain handle mappings
 - External resource synchronization that uses handles as keys
 - Debugging and logging that benefits from handle identification
 - Custom operations that need both handle identity and node data
 */
struct VisitedNode {
  //! Handle to the node being visited
  NodeHandle handle;
  //! Reference to the node implementation
  SceneNodeImpl* node_impl { nullptr };
};

//! Enumeration of supported traversal orders
enum class TraversalOrder : uint8_t {
  kDepthFirst, //!< Visit nodes depth-first (no sibling order guarantee)
  kBreadthFirst //!< Visit nodes level by level (no sibling order guarantee)
};

//! Result of a traversal operation
struct TraversalResult {
  std::size_t nodes_visited = 0; //!< Number of nodes visited
  std::size_t nodes_filtered = 0; //!< Number of nodes filtered out
  bool completed = true; //!< true if fully completed, false if stopped early
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
concept SceneVisitor
  = requires(Visitor v, const VisitedNode& visited_node, const Scene& scene) {
      { v(visited_node, scene) } -> std::convertible_to<VisitResult>;
    };

// Update SceneFilter concept to accept VisitedNode and parent_result
template <typename Filter>
concept SceneFilter = requires(
  Filter f, const VisitedNode& visited_node, FilterResult parent_result) {
  { f(visited_node, parent_result) } -> std::convertible_to<FilterResult>;
};

//=== High-Performance Filters ===--------------------------------------------//

//! Filter that accepts all nodes.
struct AcceptAllFilter {
  constexpr auto operator()(const VisitedNode& /*visited_node*/,
    FilterResult /*parent_result*/) const noexcept -> FilterResult
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
  OXGN_SCN_API auto operator()(const VisitedNode& visited_node,
    FilterResult parent_result) const noexcept -> FilterResult;
};
static_assert(SceneFilter<DirtyTransformFilter>);

//! Filter that accepts only visible SceneNodeImpl objects.
/*!
 This filter accepts only nodes that are marked as visible, and will block the
 entire sub-tree below a node if it's not visible.
*/
struct VisibleFilter {
  auto operator()(const VisitedNode& visited_node,
    FilterResult /*parent_result*/) const noexcept -> FilterResult
  {
    const auto& flags = visited_node.node_impl->GetFlags();
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
  OXGN_SCN_API explicit SceneTraversal(const Scene& scene);

  //=== Core Traversal API ===----------------------------------------------//

  //! Traverse the entire scene graph from root nodes
  template <SceneVisitor VisitorFunc, SceneFilter FilterFunc = AcceptAllFilter>
  [[nodiscard]] auto Traverse(VisitorFunc&& visitor,
    TraversalOrder order = TraversalOrder::kDepthFirst,
    FilterFunc&& filter = AcceptAllFilter {}) -> TraversalResult;

  //! Traverse from a single root node
  template <SceneVisitor VisitorFunc, SceneFilter FilterFunc = AcceptAllFilter>
  [[nodiscard]] auto TraverseHierarchy(SceneNode& starting_node,
    VisitorFunc&& visitor, TraversalOrder order = TraversalOrder::kDepthFirst,
    FilterFunc&& filter = AcceptAllFilter {}) -> TraversalResult;

  //! Traverse from specific root nodes
  template <SceneVisitor VisitorFunc, SceneFilter FilterFunc = AcceptAllFilter>
  [[nodiscard]] auto TraverseHierarchies(std::span<SceneNode> starting_nodes,
    VisitorFunc&& visitor, TraversalOrder order = TraversalOrder::kDepthFirst,
    FilterFunc&& filter = AcceptAllFilter {}) -> TraversalResult;

  //=== Convenience Methods ===---------------------------------------------//

  //! Update transforms for all dirty nodes using optimized traversal
  /*!
   Efficiently updates transforms for all nodes that have dirty transform
   state. This is equivalent to Scene::Update but uses the optimized traversal
   system.

   \return Number of nodes that had their transforms updated
  */
  OXGN_SCN_API auto UpdateTransforms() -> std::size_t;

  //! Update transforms for dirty nodes from specific roots
  /*!
   \param starting_nodes Starting nodes for transform update traversal
   \return Number of nodes that had their transforms updated
  */
  OXGN_SCN_NDAPI auto UpdateTransforms(std::span<SceneNode> starting_nodes)
    -> std::size_t;

private:
  const Scene* scene_;
  mutable std::vector<VisitedNode> children_buffer_;

  //! Calculate optimal stack capacity based on scene size
  [[nodiscard]] auto GetOptimalStackCapacity() const -> std::size_t
  {
    // NOLINTBEGIN(*-magic-numbers)
    const auto node_count = scene_->GetNodeCount();
    if (node_count <= 64) {
      return 32;
    }
    if (node_count <= 256) {
      return 64;
    }
    if (node_count <= 1024) {
      return 128;
    }
    return 256; // Cap at reasonable size for deep scenes
    // NOLINTEND(*-magic-numbers)
  }

  //! Get valid node pointer from handle
  OXGN_SCN_API auto GetNodeImpl(const NodeHandle& handle) const
    -> SceneNodeImpl*;
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
      children_buffer_.push_back(
        VisitedNode { .handle = child_handle, .node_impl = child_node });
      child_handle = child_node->AsGraphNode().GetNextSibling();
    }
  }

  //! Depth-first traversal implementation
  /*!
   Traverses the scene graph in depth-first order.
   \warning Sibling order is not guaranteed and depends on Scene storage.
  */
  template <typename VisitorFunc, typename FilterFunc>
  auto TraverseDepthFirst(std::span<VisitedNode> roots, VisitorFunc&& visitor,
    FilterFunc&& filter) const -> TraversalResult;

  //! Breadth-first traversal implementation
  /*!
   Traverses the scene graph in breadth-first order.
   \warning Sibling order is not guaranteed and depends on Scene storage.
  */
  template <typename VisitorFunc, typename FilterFunc>
  auto TraverseBreadthFirst(std::span<VisitedNode> roots, VisitorFunc&& visitor,
    FilterFunc&& filter) const -> TraversalResult;

  //! Core traversal dispatcher
  template <typename VisitorFunc, typename FilterFunc>
  [[nodiscard]] auto TraverseInternal(std::span<VisitedNode> root_impl_nodes,
    VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter) const
    -> TraversalResult;
};

//=== Template Implementation ===---------------------------------------------//

template <SceneVisitor VisitorFunc, SceneFilter FilterFunc>
auto SceneTraversal::Traverse(VisitorFunc&& visitor, TraversalOrder order,
  FilterFunc&& filter) -> TraversalResult
{
  auto root_handles = scene_->GetRootHandles();
  if (root_handles.empty()) [[unlikely]] {
    return TraversalResult {};
  }

  // We're traversing the root nodes of our scene. No need to be paranoid with
  // checks for validity.
  std::vector<VisitedNode> root_impl_nodes;
  root_impl_nodes.reserve(root_handles.size());
  std::ranges::transform(root_handles, std::back_inserter(root_impl_nodes),
    [this](const NodeHandle& handle) {
      return VisitedNode { .handle = handle, .node_impl = GetNodeImpl(handle) };
    });

  return TraverseInternal(root_impl_nodes, std::forward<VisitorFunc>(visitor),
    order, std::forward<FilterFunc>(filter));
}

template <SceneVisitor VisitorFunc, SceneFilter FilterFunc>
auto SceneTraversal::TraverseHierarchy(SceneNode& starting_node,
  VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter)
  -> TraversalResult
{
  if (!starting_node.IsValid()) {
    DLOG_F(WARNING, "TraverseHierarchy starting from an invalid node.");
    return TraversalResult {};
  }
  CHECK_F(scene_->Contains(starting_node),
    "Starting node for traversal must be part of this scene");

  std::array root_impl_nodes { VisitedNode {
    .handle = starting_node.GetHandle(),
    .node_impl = GetNodeImpl(starting_node.GetHandle()) } };

  return TraverseInternal(root_impl_nodes, std::forward<VisitorFunc>(visitor),
    order, std::forward<FilterFunc>(filter));
}

template <SceneVisitor VisitorFunc, SceneFilter FilterFunc>
auto SceneTraversal::TraverseHierarchies(std::span<SceneNode> starting_nodes,
  VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter)
  -> TraversalResult
{
  if (starting_nodes.empty()) [[unlikely]] {
    return TraversalResult {};
  }

  std::vector<VisitedNode> root_impl_nodes;
  root_impl_nodes.reserve(starting_nodes.size());
  std::ranges::transform(starting_nodes, std::back_inserter(root_impl_nodes),
    [this](const SceneNode& node) {
      CHECK_F(scene_->Contains(node),
        "Starting nodes for traversal must be part of this scene");
      return VisitedNode { .handle = node.GetHandle(),
        .node_impl = GetNodeImpl(node.GetHandle()) };
    });

  return TraverseInternal(root_impl_nodes, std::forward<VisitorFunc>(visitor),
    order, std::forward<FilterFunc>(filter));
}

template <typename VisitorFunc, typename FilterFunc>
auto SceneTraversal::TraverseInternal(std::span<VisitedNode> root_impl_nodes,
  VisitorFunc&& visitor, const TraversalOrder order, FilterFunc&& filter) const
  -> TraversalResult
{
  DCHECK_F(!root_impl_nodes.empty(), "expecting root nodes to traverse");

  // Dispatch to appropriate traversal algorithm
  switch (order) {
  case TraversalOrder::kDepthFirst:
    return TraverseDepthFirst(root_impl_nodes,
      std::forward<VisitorFunc>(visitor), std::forward<FilterFunc>(filter));
  case TraversalOrder::kBreadthFirst:
    return TraverseBreadthFirst(root_impl_nodes,
      std::forward<VisitorFunc>(visitor), std::forward<FilterFunc>(filter));
  }

  // This should never be reached with valid enum values
  [[unlikely]] return TraversalResult {};
}

// NOLINTBEGIN(*-missing-std-forward)
template <typename VisitorFunc, typename FilterFunc>
auto SceneTraversal::TraverseDepthFirst(const std::span<VisitedNode> roots,
  VisitorFunc&& visitor, FilterFunc&& filter) const -> TraversalResult
// NOLINTEND(*-missing-std-forward)
{
  // We're calling them in a loop, create a local reference.
  auto& local_visitor = visitor;
  auto& local_filter = filter;

  TraversalResult result {};
  struct StackEntry {
    VisitedNode visited_node;
    FilterResult parent_result { FilterResult::kAccept };
  };
  std::vector<StackEntry> stack;
  stack.reserve(GetOptimalStackCapacity());
  // Initialize stack with roots, parent_result defaults to Reject
  for (const auto& root : roots) {
    stack.emplace_back(root, FilterResult::kReject);
  }

  auto add_children_to_stack
    = [&](SceneNodeImpl* node, FilterResult parent_filter_result) {
        CollectChildren(node);
        if (!children_buffer_.empty()) {
          for (auto& it : children_buffer_) {
            stack.push_back({ it, parent_filter_result });
          }
        }
      };
  while (!stack.empty()) {
    const auto entry = stack.back(); // copy the item to avoid invalidation
    stack.pop_back();

    auto* node = entry.visited_node.node_impl;
    auto parent_result = entry.parent_result;

    const FilterResult filter_result
      = local_filter(entry.visited_node, parent_result);

    switch (filter_result) {
    case FilterResult::kAccept: {
      ++result.nodes_visited;
      const VisitResult visit_result
        = local_visitor(entry.visited_node, *scene_);
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

// NOLINTBEGIN(*-missing-std-forward)
template <typename VisitorFunc, typename FilterFunc>
auto SceneTraversal::TraverseBreadthFirst(const std::span<VisitedNode> roots,
  VisitorFunc&& visitor, FilterFunc&& filter) const -> TraversalResult
// NOLINTEND(*-missing-std-forward)
{
  // We're calling them in a loop, create a local reference.
  auto& local_visitor = visitor;
  auto& local_filter = filter;

  TraversalResult result { .completed = true };
  struct QueueEntry {
    VisitedNode visited_node;
    FilterResult parent_result { FilterResult::kAccept };
  };
  std::deque<QueueEntry> queue;
  // Initialize queue with all roots, parent_result defaults to Reject
  for (const auto& root : roots) {
    queue.push_back({ root, FilterResult::kReject });
  }

  auto add_children_to_queue
    = [&](SceneNodeImpl* node, FilterResult parent_filter_result) {
        CollectChildren(node);
        if (!children_buffer_.empty()) {
          for (auto& it : children_buffer_) {
            queue.push_back({ it, parent_filter_result });
          }
        }
      };
  while (!queue.empty()) {
    const auto entry = queue.front(); // copy the item to avoid invalidation
    auto* node = entry.visited_node.node_impl;
    auto parent_result = entry.parent_result;
    queue.pop_front();

    const FilterResult filter_result
      = local_filter(entry.visited_node, parent_result);
    switch (filter_result) {
    case FilterResult::kAccept: {
      ++result.nodes_visited;
      const VisitResult visit_result
        = local_visitor(entry.visited_node, *scene_);
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
