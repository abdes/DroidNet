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
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

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
  kBreadthFirst, //!< Visit nodes level by level (first child to last sibling)
  kPreOrder, //!< Visit nodes before their children (depth-first pre-order)
  kPostOrder //!< Visit nodes after their children (depth-first post-order)
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
OXGN_SCN_API auto to_string(FilterResult value) -> const char*;

//! Visitor result controlling traversal continuation
enum class VisitResult : uint8_t {
  kContinue, //!< Continue traversal as normal
  kSkipSubtree, //!< Do not traverse this node's children
  kStop //!< Stop traversal entirely
};
OXGN_SCN_API auto to_string(VisitResult value) -> const char*;

//=== Concepts for Visitors and Filters ===-----------------------------------//

template <typename Visitor>
concept SceneVisitor = requires(Visitor v, const VisitedNode& visited_node,
    const Scene& scene, bool dry_run) {
  { v(visited_node, scene, dry_run) } -> std::convertible_to<VisitResult>;
};

// Update SceneFilter concept to accept VisitedNode and parent_filter_result
template <typename Filter>
concept SceneFilter = requires(Filter f, const VisitedNode& visited_node,
    FilterResult parent_filter_result) {
  {
    f(visited_node, parent_filter_result)
  } -> std::convertible_to<FilterResult>;
};

//=== High-Performance Filters ===--------------------------------------------//

//! Filter that accepts all nodes.
struct AcceptAllFilter {
  constexpr auto operator()(const VisitedNode& /*visited_node*/,
      FilterResult /*parent_filter_result*/) const noexcept -> FilterResult
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
  OXGN_SCN_NDAPI auto operator()(const VisitedNode& visited_node,
      FilterResult parent_filter_result) const noexcept -> FilterResult;
};
static_assert(SceneFilter<DirtyTransformFilter>);

//! Filter that accepts only visible SceneNodeImpl objects.
/*!
 This filter accepts only nodes that are marked as visible, and will block the
 entire sub-tree below a node if it's not visible.
*/
struct VisibleFilter {
  OXGN_SCN_NDAPI auto operator()(const VisitedNode& visited_node,
      FilterResult /*parent_filter_result*/) const noexcept -> FilterResult;
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
 Traversal order details:
 - kBreadthFirst: Level-by-level traversal using a queue
 - kPreOrder: Visit parent before children (ideal for transform updates)
 - kPostOrder: Visit children before parent (ideal for cleanup/destruction)

 \warning The Scene API does not guarantee any specific order for sibling nodes.
 \warning Modifying the scene graph (adding/removing nodes, changing
          parent/child relationships) during traversal is undefined behavior and
          may result in crashes or inconsistent results.
*/
class SceneTraversal {
public:
  OXGN_SCN_API explicit SceneTraversal(const Scene& scene);

  //=== Core Traversal API ===----------------------------------------------//

  //! Traverse the entire scene graph from root nodes, using by default, a
  //! depth-first, pre-order traversal, visiting all nodes.
  template <SceneVisitor VisitorFunc, SceneFilter FilterFunc = AcceptAllFilter>
  [[nodiscard]] auto Traverse(VisitorFunc&& visitor,
      TraversalOrder order = TraversalOrder::kPreOrder,
      FilterFunc&& filter = AcceptAllFilter {}) -> TraversalResult;

  //! Traverse from a single root node, using by default, a depth-first,
  //! pre-order traversal, visiting all nodes.
  template <SceneVisitor VisitorFunc, SceneFilter FilterFunc = AcceptAllFilter>
  [[nodiscard]] auto TraverseHierarchy(SceneNode& starting_node,
      VisitorFunc&& visitor, TraversalOrder order = TraversalOrder::kPreOrder,
      FilterFunc&& filter = AcceptAllFilter {}) -> TraversalResult;

  //! Traverse from specific root nodes, using by default, a depth-first,
  //! pre-order traversal, visiting all nodes.
  template <SceneVisitor VisitorFunc, SceneFilter FilterFunc = AcceptAllFilter>
  [[nodiscard]] auto TraverseHierarchies(std::span<SceneNode> starting_nodes,
      VisitorFunc&& visitor, TraversalOrder order = TraversalOrder::kPreOrder,
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

  //! Collect children of a node into reused buffer
  OXGN_SCN_API void CollectChildrenToBuffer(SceneNodeImpl* node) const;

  //! Get valid node pointer from handle
  OXGN_SCN_API auto GetNodeImpl(const NodeHandle& handle) const
      -> SceneNodeImpl*;

  //! Calculate optimal stack capacity based on scene size
  OXGN_SCN_NDAPI auto GetOptimalStackCapacity() const -> std::size_t;

  //! Container type selection based on traversal order. Will select a
  //! std::vector (better memory locality) for depth-first traversals and a
  //! std::deque (more efficient front removal) for breadth-first traversal.
  template <TraversalOrder Order> struct ContainerTraits;

  struct TraversalEntry {
    VisitedNode visited_node;
    FilterResult parent_filter_result { FilterResult::kAccept };

    // Used by PostOrder to track if children have been processed
    enum class ProcessingState : uint8_t {
      kPending, //!< Node children not yet processed
      kChildrenProcessed //!< Node children already visited (post-order only)
    } state { ProcessingState::kPending };
  };

  template <TraversalOrder Order, typename Container>
  void InitializeContainerWithRoots(
      std::span<VisitedNode> roots, Container& container) const;

  template <TraversalOrder Order, typename Container>
  void QueueChildrenForTraversal(
      FilterResult parent_filter_result, Container& container) const;

  // Helper to apply the filter and update result
  template <typename FilterFunc>
  auto ApplyNodeFilter(FilterFunc& filter, const TraversalEntry& entry,
      TraversalResult& result) const -> FilterResult;

  template <TraversalOrder Order, typename VisitorFunc, typename Container>
  auto PerformNodeVisit(VisitorFunc& visitor, Container& container,
      TraversalResult& result, bool dry_run = false) const -> VisitResult;

  //! Update the entry for the current node with the node implementation.
  OXGN_SCN_NDAPI auto UpdateNodeImpl(TraversalEntry& entry) const -> bool;

  //! Unified traversal implementation
  template <TraversalOrder Order, typename VisitorFunc, typename FilterFunc>
  auto TraverseImpl(std::span<VisitedNode> roots, VisitorFunc&& visitor,
      FilterFunc&& filter) const -> TraversalResult;

  template <typename VisitorFunc, typename FilterFunc>
  auto TraverseDispatch(std::span<VisitedNode> root_impl_nodes,
      VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter) const
      -> TraversalResult;
};

//=== Template Specializations ===--------------------------------------------//

template <>
struct SceneTraversal::ContainerTraits<TraversalOrder::kBreadthFirst> {
  template <typename T> using container_type = std::deque<T>;

  template <typename Container>
  static constexpr void push(
      Container& container, const typename Container::value_type& value)
  {
    container.push_back(value);
  }

  template <typename Container>
  static constexpr auto pop(Container& container) ->
      typename Container::value_type
  {
    auto item = container.front();
    container.pop_front();
    return item;
  }

  template <typename Container>
  static constexpr auto peek(Container& container) ->
      typename Container::value_type&
  {
    return container.front();
  }

  template <typename Container>
  static constexpr auto empty(const Container& container) -> bool
  {
    return container.empty();
  }
};

// Pre-order and post-order use stack-like behavior (vector)
template <> struct SceneTraversal::ContainerTraits<TraversalOrder::kPreOrder> {
  template <typename T> using container_type = std::vector<T>;

  template <typename Container>
  static constexpr void push(
      Container& container, const typename Container::value_type& value)
  {
    container.push_back(value);
  }

  template <typename Container>
  static constexpr auto pop(Container& container) ->
      typename Container::value_type
  {
    auto item = container.back();
    container.pop_back();
    return item;
  }

  template <typename Container>
  static constexpr auto peek(Container& container) ->
      typename Container::value_type&
  {
    return container.back();
  }

  template <typename Container>
  static constexpr auto empty(const Container& container) -> bool
  {
    return container.empty();
  }
};

template <>
struct SceneTraversal::ContainerTraits<TraversalOrder::kPostOrder>
  : ContainerTraits<TraversalOrder::kPreOrder> { };

//=== Template Implementations ===--------------------------------------------//

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
        return VisitedNode { .handle = handle,
          .node_impl = GetNodeImpl(handle) };
      });

  return TraverseDispatch(root_impl_nodes, std::forward<VisitorFunc>(visitor),
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

  std::array root_impl_nodes { VisitedNode { .handle
      = starting_node.GetHandle(),
      .node_impl = GetNodeImpl(starting_node.GetHandle()) } };

  return TraverseDispatch(root_impl_nodes, std::forward<VisitorFunc>(visitor),
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

  return TraverseDispatch(root_impl_nodes, std::forward<VisitorFunc>(visitor),
      order, std::forward<FilterFunc>(filter));
}

template <TraversalOrder Order, typename Container>
void SceneTraversal::InitializeContainerWithRoots(
    std::span<VisitedNode> roots, Container& container) const
{
  using Traits = ContainerTraits<Order>;

  for (const auto& root : roots) {
    Traits::push(container,
        TraversalEntry {
            .visited_node = root,
            // For consistency, we set the parent result for root nodes as
            // 'accepted'. Filters should handle any additional logic for
            // determining if the node should accept appropriately.
            .parent_filter_result = FilterResult::kAccept,
            .state = TraversalEntry::ProcessingState::kPending,
        });
  }
}

template <TraversalOrder Order, typename Container>
void SceneTraversal::QueueChildrenForTraversal(
    const FilterResult parent_filter_result, Container& container) const
{
  using Traits = ContainerTraits<Order>;

  if (!children_buffer_.empty()) {
    for (auto& child : children_buffer_) {
      Traits::push(container,
          TraversalEntry {
              .visited_node = child,
              .parent_filter_result = parent_filter_result,
              .state = TraversalEntry::ProcessingState::kPending,
          });
    }
  }
  DLOG_F(2, "queued: {}", container.size());
}

template <typename FilterFunc>
auto SceneTraversal::ApplyNodeFilter(FilterFunc& filter,
    const TraversalEntry& entry, TraversalResult& result) const -> FilterResult
{
  DLOG_SCOPE_FUNCTION(2);

  DCHECK_NOTNULL_F(entry.visited_node.node_impl);
  DLOG_F(2, "node: {}", entry.visited_node.node_impl->GetName());

  const auto filter_result
      = filter(entry.visited_node, entry.parent_filter_result);
  if (filter_result != FilterResult::kAccept) {
    ++result.nodes_filtered;
  }
  DLOG_F(2, "-> {}", nostd::to_string(filter_result));
  return filter_result;
}

template <TraversalOrder Order, typename VisitorFunc, typename Container>
auto SceneTraversal::PerformNodeVisit(VisitorFunc& visitor,
    Container& container, TraversalResult& result, bool dry_run) const
    -> VisitResult
{
  using Traits = ContainerTraits<Order>;

  // Update the entry for the current node with the node implementation. Peek
  // only, entries will be removed when processed. Skip the child if it became
  // invalid due to mutations in the previous siblings visits.
  auto& entry_ref = Traits::peek(container);
  DCHECK_NOTNULL_F(entry_ref.visited_node.node_impl);

  DLOG_SCOPE_FUNCTION(2);
  DLOG_F(2, "node : {}", entry_ref.visited_node.node_impl->GetName());

  VisitResult visit_result;
  if (dry_run) {
    DLOG_SCOPE_F(2, "Dry-Run");

    visit_result = visitor(entry_ref.visited_node, *scene_, dry_run);
    DLOG_F(2, "result: {}", nostd::to_string(visit_result));

    if (visit_result == VisitResult::kContinue) [[likely]] {
      return visit_result; // Continue traversal
    }

    // If not kContinue, then fall through to actually visit the node as
    // if it were not a dry run
  }
  DLOG_SCOPE_F(2, "Real-Run");

  // Remove it now - visiting
  auto entry = Traits::pop(container); // Do not use node_ref - invalidated

  visit_result = visitor(entry.visited_node, *scene_, false);
  DLOG_F(2, "-> {}", nostd::to_string(visit_result));
  ++result.nodes_visited;

  if (visit_result == VisitResult::kStop) [[unlikely]] {
    result.completed = false;
  }
  return visit_result;
}

// NOLINTBEGIN(cppcoreguidelines-missing-std-forward)
template <TraversalOrder Order, typename VisitorFunc, typename FilterFunc>
auto SceneTraversal::TraverseImpl(std::span<VisitedNode> roots,
    VisitorFunc&& visitor, FilterFunc&& filter) const -> TraversalResult
// NOLINTEND(cppcoreguidelines-missing-std-forward)
{
  if (roots.empty()) [[unlikely]] {
    return TraversalResult {};
  }

  DLOG_SCOPE_F(2, "Scene Traversal");

  // Store in local refs because they are used in a loop
  auto& local_visitor = visitor;
  auto& local_filter = filter;

  using Traits = ContainerTraits<Order>;
  typename Traits::template container_type<TraversalEntry> container;

  // Optimize for stack-based traversals with pre-allocation
  if constexpr (Order == TraversalOrder::kPreOrder
      || Order == TraversalOrder::kPostOrder) {
    container.reserve(GetOptimalStackCapacity());
  }

  InitializeContainerWithRoots<Order>(roots, container);

  TraversalResult result {};

  while (!Traits::empty(container)) {
    // Peek at the entry without removing it
    auto& entry_ref = Traits::peek(container);

    // Update the entry for the current node with the node implementation.
    // Peek only, entries will be removed when processed. Skip the node if it
    // became invalid due to mutations in the previous siblings visits.
    if (!UpdateNodeImpl(entry_ref)) [[unlikely]] {
      const auto& handle = entry_ref.visited_node.handle;
      DLOG_F(2, "skipping, no longer in scene", to_string_compact(handle));
      Traits::pop(container);
      continue;
    }

    // Keep the direct pointer to the node implementation, it will not be
    // invalidated by the container operations
    auto* node = entry_ref.visited_node.node_impl;

    const auto filter_result = ApplyNodeFilter(local_filter, entry_ref, result);

    // Handle filtering
    if (filter_result == FilterResult::kRejectSubTree) {
      Traits::pop(container); // Remove the entry since we're skipping it
      continue;
    }
    if (filter_result == FilterResult::kReject) {
      // Remove the entry since we're not visiting it
      Traits::pop(container);
      // Still traverse children for rejected nodes
      CollectChildrenToBuffer(node);
      QueueChildrenForTraversal<Order>(filter_result, container);
      continue;
    }

    // Post-order first time seeing this node:
    // - dry-run to check the visitor intent
    // - children first
    if constexpr (Order == TraversalOrder::kPostOrder) {
      if (entry_ref.state == TraversalEntry::ProcessingState::kPending) {
        // First time seeing this node - dry run to check visitor intent
        auto visit_result
            = PerformNodeVisit<Order>(local_visitor, container, result, true);
        if (visit_result == VisitResult::kStop) [[unlikely]] {
          return result;
        }
        if (visit_result == VisitResult::kSkipSubtree) {
          // Skip children and continue
          continue;
        }
        // Continue with children - mark as processed and add children
        entry_ref.state = TraversalEntry::ProcessingState::kChildrenProcessed;
        node = Traits::peek(container).visited_node.node_impl;
        CollectChildrenToBuffer(node);
        QueueChildrenForTraversal<Order>(filter_result, container);
        continue;
      }
    }

    if constexpr (Order == TraversalOrder::kPostOrder) {
      DCHECK_F(entry_ref.state
              == TraversalEntry::ProcessingState::kChildrenProcessed,
          "post-order first pass should not fall through");
    }

    // Post-order second time seeing this node, or non-post-order cases ->
    // actual visit of the node
    auto visit_result
        = PerformNodeVisit<Order>(local_visitor, container, result, false);
    if (visit_result == VisitResult::kStop) [[unlikely]] {
      return result;
    }

    // Breadth-first and pre-order -> add children if not skipping subtree
    if constexpr (Order != TraversalOrder::kPostOrder) {
      if (visit_result != VisitResult::kSkipSubtree) {
        CollectChildrenToBuffer(node);
        QueueChildrenForTraversal<Order>(filter_result, container);
      }
    }
  }
  return result;
}

template <typename VisitorFunc, typename FilterFunc>
auto SceneTraversal::TraverseDispatch(std::span<VisitedNode> root_impl_nodes,
    VisitorFunc&& visitor, const TraversalOrder order,
    FilterFunc&& filter) const -> TraversalResult
{
  if (root_impl_nodes.empty()) [[unlikely]] {
    return TraversalResult {};
  }

  // Dispatch to appropriate traversal algorithm
  switch (order) {
  case TraversalOrder::kBreadthFirst:
    return TraverseImpl<TraversalOrder::kBreadthFirst>(root_impl_nodes,
        std::forward<VisitorFunc>(visitor), std::forward<FilterFunc>(filter));
  case TraversalOrder::kPreOrder:
    return TraverseImpl<TraversalOrder::kPreOrder>(root_impl_nodes,
        std::forward<VisitorFunc>(visitor), std::forward<FilterFunc>(filter));
  case TraversalOrder::kPostOrder:
    return TraverseImpl<TraversalOrder::kPostOrder>(root_impl_nodes,
        std::forward<VisitorFunc>(visitor), std::forward<FilterFunc>(filter));
  }

  // This should never be reached with valid enum values
  [[unlikely]] return TraversalResult {};
}

} // namespace oxygen::scene
