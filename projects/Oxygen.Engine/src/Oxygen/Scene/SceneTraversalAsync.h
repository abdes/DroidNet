//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// ReSharper disable CppClangTidyBugproneChainedComparison
#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversalBase.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/Types/Traversal.h>

namespace oxygen::scene {

//=== Concept for Async Visitors ===------------------------------------------//

//! Concept for an async visitor that can be used in scene traversal.
//! Automatically deduces the constness of the VisitedNode based on the Scene
//! type.
template <typename Visitor, typename SceneT>
concept AsyncSceneVisitorT = requires(Visitor v,
  const VisitedNodeT<std::is_const_v<SceneT>>& visited_node, bool dry_run) {
  { v(visited_node, dry_run) } -> std::same_as<co::Co<VisitResult>>;
};
template <typename Visitor>
concept MutatingAsyncSceneVisitor = AsyncSceneVisitorT<Visitor, Scene>;
template <typename Visitor>
concept NonMutatingAsyncSceneVisitor = AsyncSceneVisitorT<Visitor, const Scene>;

//=== High-Performance Scene Graph Async Traversal ===------------------------//

//! High-performance asynchronous scene graph traversal, supporting vsitors
//! implemented as co-routines.
/*!
 Provides optimized, non-recursive traversal algorithms working directly with
 SceneNodeImpl pointers for maximum performance in batch operations.

 ### Key features

 - Supports mutating and non-mutating visitors and filters, with auto-deduction
   of the VisitedNode constness based on the Scene type
 - Non-recursive to avoid stack overflow on deep hierarchies
 - Direct implementation access bypassing SceneNode wrapper creation
 - Efficient with pre-allocated containers and minimal allocation
 - Cache-friendly sequential pointer processing

 ### Traversal order details

 - kBreadthFirst: Level-by-level traversal using a queue
 - kPreOrder: Visit parent before children (ideal for transform updates)
 - kPostOrder: Visit children before parent (ideal for cleanup/destruction)

 @warning The Scene API does not guarantee that the order for sibling nodes is
 the same as the creation order.

 @warning Modifying the scene graph (adding/removing nodes, changing
 parent/child relationships) during traversal is undefined behavior and
 may result in crashes or inconsistent results.
*/
template <typename SceneT>
class AsyncSceneTraversal : public detail::SceneTraversalBase<SceneT> {
  using Base = detail::SceneTraversalBase<SceneT>;

public:
  using Node = typename Base::Node;
  using VisitedNode = typename Base::VisitedNode;

  explicit AsyncSceneTraversal(const std::shared_ptr<SceneT>& scene)
    : Base(scene)
  {
  }

  ~AsyncSceneTraversal() override = default;

  OXYGEN_DEFAULT_COPYABLE(AsyncSceneTraversal)
  OXYGEN_DEFAULT_MOVABLE(AsyncSceneTraversal)

  //=== Core Traversal API ===----------------------------------------------//

  //! Traverse the entire scene graph from root nodes, using by default, a
  //! depth-first, pre-order traversal.
  template <typename VisitorFunc, typename FilterFunc = AcceptAllFilter>
    requires AsyncSceneVisitorT<VisitorFunc, SceneT>
    && SceneFilterT<FilterFunc, SceneT>
  [[nodiscard]] co::Co<TraversalResult> TraverseAsync(VisitorFunc&& visitor,
    TraversalOrder order = TraversalOrder::kPreOrder,
    FilterFunc&& filter = AcceptAllFilter {}) const;

  //! Traverse from a single hierarchy starting at \p starting_node, using by
  //! default, a depth-first, pre-order traversal.
  template <typename VisitorFunc, typename FilterFunc = AcceptAllFilter>
    requires AsyncSceneVisitorT<VisitorFunc, SceneT>
    && SceneFilterT<FilterFunc, SceneT>
  [[nodiscard]] TraversalResult TraverseHierarchyAsync(
    const Node& starting_node, VisitorFunc&& visitor,
    TraversalOrder order = TraversalOrder::kPreOrder,
    FilterFunc&& filter = AcceptAllFilter {}) const;

  //! Traverse multiple hierarchies, starting at the nodes provided in \p
  //! starting_nodes, using by default, a depth-first, pre-order traversal.
  template <typename VisitorFunc, typename FilterFunc = AcceptAllFilter>
    requires AsyncSceneVisitorT<VisitorFunc, SceneT>
    && SceneFilterT<FilterFunc, SceneT>
  [[nodiscard]] co::Co<TraversalResult> TraverseHierarchiesAsync(
    std::span<const Node> starting_nodes, VisitorFunc&& visitor,
    TraversalOrder order = TraversalOrder::kPreOrder,
    FilterFunc&& filter = AcceptAllFilter {}) const;

private:
  using Base::ApplyNodeFilter;
  using Base::CollectChildrenToBuffer;
  using Base::GetNodeImpl;
  using Base::GetScene;
  using Base::InitializeTraversal;
  using Base::IsSceneExpired;
  using Base::QueueChildrenForTraversal;
  using Base::UpdateNodeImpl;

  using TraversalEntry = typename Base::TraversalEntry;

  //=== Private Helper Methods ===--------------------------------------------//

  template <TraversalOrder Order, typename VisitorFunc, typename Container>
  co::Co<VisitResult> PerformNodeVisitAsync(VisitorFunc& visitor,
    Container& container, TraversalResult& result, bool dry_run = false) const;

  //! Unified traversal implementation
  template <TraversalOrder Order, typename VisitorFunc, typename FilterFunc>
  co::Co<TraversalResult> TraverseImplAsync(std::span<VisitedNode> roots,
    VisitorFunc&& visitor, FilterFunc&& filter) const;

  template <typename VisitorFunc, typename FilterFunc>
  co::Co<TraversalResult> TraverseDispatchAsync(
    std::span<VisitedNode> root_impl_nodes, VisitorFunc&& visitor,
    TraversalOrder order, FilterFunc&& filter) const;
};

//=== Template Deduction Guides ===-------------------------------------------//

AsyncSceneTraversal(std::shared_ptr<Scene>) -> AsyncSceneTraversal<Scene>;
AsyncSceneTraversal(std::shared_ptr<const Scene>)
  -> AsyncSceneTraversal<const Scene>;

//=== Template Implementations ===--------------------------------------------//

template <typename SceneT>
template <typename VisitorFunc, typename FilterFunc>
  requires AsyncSceneVisitorT<VisitorFunc, SceneT>
  && SceneFilterT<FilterFunc, SceneT>
co::Co<TraversalResult> AsyncSceneTraversal<SceneT>::TraverseAsync(
  VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter) const
{
  if (IsSceneExpired()) [[unlikely]] {
    DLOG_F(ERROR, "SceneTraversal called on an expired scene");
    co_return TraversalResult {};
  }

  auto root_handles = GetScene().GetRootHandles();
  if (root_handles.empty()) [[unlikely]] {
    co_return TraversalResult {};
  }

  // We're traversing the root nodes of our scene. No need to be paranoid with
  // checks for validity.
  std::vector<VisitedNode> root_impl_nodes;
  root_impl_nodes.reserve(root_handles.size());
  std::ranges::transform(root_handles, std::back_inserter(root_impl_nodes),
    [this](const NodeHandle& handle) {
      return VisitedNode {
        .handle = handle, .node_impl = this->GetNodeImpl(handle), .depth = 0
      };
    });

  co_return co_await TraverseDispatchAsync(root_impl_nodes,
    std::forward<VisitorFunc>(visitor), order,
    std::forward<FilterFunc>(filter));
}

template <typename SceneT>
template <typename VisitorFunc, typename FilterFunc>
  requires AsyncSceneVisitorT<VisitorFunc, SceneT>
  && SceneFilterT<FilterFunc, SceneT>
TraversalResult AsyncSceneTraversal<SceneT>::TraverseHierarchyAsync(
  const Node& starting_node, VisitorFunc&& visitor, TraversalOrder order,
  FilterFunc&& filter) const
{

  if (!starting_node.IsValid()) {
    DLOG_F(WARNING, "TraverseHierarchy starting from an invalid node.");
    return TraversalResult {};
  }
  CHECK_F(GetScene().Contains(starting_node),
    "Starting node for traversal must be part of this scene");
  using VisitorNode = VisitedNodeT<std::is_const_v<SceneT>>;
  std::array root_impl_nodes {
    VisitorNode {
      .handle = starting_node.GetHandle(),
      .node_impl = this->GetNodeImpl(starting_node.GetHandle()),
      .depth = 0,
    },
  };

  return TraverseDispatchAsync(root_impl_nodes,
    std::forward<VisitorFunc>(visitor), order,
    std::forward<FilterFunc>(filter));
}

template <typename SceneT>
template <typename VisitorFunc, typename FilterFunc>
  requires AsyncSceneVisitorT<VisitorFunc, SceneT>
  && SceneFilterT<FilterFunc, SceneT>
co::Co<TraversalResult> AsyncSceneTraversal<SceneT>::TraverseHierarchiesAsync(
  std::span<const Node> starting_nodes, VisitorFunc&& visitor,
  TraversalOrder order, FilterFunc&& filter) const
{
  if (starting_nodes.empty()) [[unlikely]] {
    co_return TraversalResult {};
  }
  DCHECK_F(!IsSceneExpired());

  std::vector<VisitedNode> root_impl_nodes;
  root_impl_nodes.reserve(starting_nodes.size());
  std::ranges::transform(starting_nodes, std::back_inserter(root_impl_nodes),
    [this](const SceneNode& node) {
      CHECK_F(GetScene().Contains(node),
        "Starting nodes for traversal must be part of this scene");
      return VisitedNode { .handle = node.GetHandle(),
        .node_impl = this->GetNodeImpl(node.GetHandle()),
        .depth = 0 };
    });

  co_return co_await TraverseDispatchAsync(root_impl_nodes,
    std::forward<VisitorFunc>(visitor), order,
    std::forward<FilterFunc>(filter));
}

template <typename SceneT>
template <TraversalOrder Order, typename VisitorFunc, typename Container>
co::Co<VisitResult> AsyncSceneTraversal<SceneT>::PerformNodeVisitAsync(
  VisitorFunc& visitor, Container& container, TraversalResult& result,
  bool dry_run) const
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

    visit_result = co_await visitor(entry_ref.visited_node, dry_run);
    DLOG_F(2, "result: {}", nostd::to_string(visit_result));

    if (visit_result == VisitResult::kContinue) [[likely]] {
      co_return visit_result; // Continue traversal
    }

    // If not kContinue, then fall through to actually visit the node as
    // if it were not a dry run
  }
  DLOG_SCOPE_F(2, "Real-Run");

  // Remove it now - visiting
  auto entry = Traits::pop(container); // Do not use node_ref - invalidated

  visit_result = co_await visitor(entry.visited_node, false);
  DLOG_F(2, "-> {}", nostd::to_string(visit_result));
  ++result.nodes_visited;

  if (visit_result == VisitResult::kStop) [[unlikely]] {
    result.completed = false;
  }
  co_return visit_result;
}

// NOLINTBEGIN(*-missing-std-forward) - used inside loop
template <typename SceneT>
template <TraversalOrder Order, typename VisitorFunc, typename FilterFunc>
co::Co<TraversalResult> AsyncSceneTraversal<SceneT>::TraverseImplAsync(
  std::span<VisitedNode> roots, VisitorFunc&& visitor,
  FilterFunc&& filter) const
// NOLINTEND(*-missing-std-forward)
{
  if (roots.empty()) [[unlikely]] {
    co_return TraversalResult { .completed = true };
  }

  DLOG_SCOPE_F(2, "Scene Traversal");

  // Store in local refs because they are used in a loop
  auto& local_visitor = visitor;
  auto& local_filter = filter;

  TraversalResult result {};
  using Traits = ContainerTraits<Order>;
  typename Traits::template container_type<TraversalEntry> container;

  InitializeTraversal<Order>(roots, container);

  while (!Traits::empty(container)) {
    // Peek at the entry without removing it
    auto& entry_ref = Traits::peek(container);

    // Update the entry for the current node with the node implementation.
    // Peek only, entries will be removed when processed. Skip the node if it
    // became invalid due to mutations in the previous siblings visits.
    if (!UpdateNodeImpl(entry_ref)) [[unlikely]] {
      [[maybe_unused]] const auto& handle = entry_ref.visited_node.handle;
      DLOG_F(2, "skipping, no longer in scene", to_string_compact(handle));
      Traits::pop(container);
      continue;
    }

    // Keep the direct pointer to the node implementation and the current depth.
    // Visitors may mutate the scene graph while we traverse it, and make the
    // entry_ref invalid.
    auto* node = entry_ref.visited_node.node_impl;
    auto current_depth = entry_ref.visited_node.depth;

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
      CollectChildrenToBuffer(node, current_depth);
      QueueChildrenForTraversal<Order>(filter_result, container);
      continue;
    }

    // Post-order first time seeing this node:
    // - dry-run to check the visitor intent
    // - children first
    if constexpr (Order == TraversalOrder::kPostOrder) {
      if (entry_ref.state == TraversalEntry::ProcessingState::kPending) {
        // First time seeing this node - dry run to check visitor intent
        auto visit_result = co_await PerformNodeVisitAsync<Order>(
          local_visitor, container, result, true);
        if (visit_result == VisitResult::kStop) [[unlikely]] {
          co_return result;
        }
        if (visit_result == VisitResult::kSkipSubtree) {
          // Skip children and continue
          continue;
        }
        // Continue with children - mark as processed and add children
        entry_ref.state = TraversalEntry::ProcessingState::kChildrenProcessed;
        CollectChildrenToBuffer(node, current_depth);
        QueueChildrenForTraversal<Order>(filter_result, container);
        continue;
      }
    }

    if constexpr (Order == TraversalOrder::kPostOrder) {
      DCHECK_F(
        entry_ref.state == TraversalEntry::ProcessingState::kChildrenProcessed,
        "post-order first pass should not fall through");
    }

    // Post-order second time seeing this node, or non-post-order cases ->
    // actual visit of the node.
    // WARNING: This may mutate the scene graph, DO NOT use entry_ref or peek()
    // on the container after this point.
    auto visit_result = co_await PerformNodeVisitAsync<Order>(
      local_visitor, container, result, false);
    if (visit_result == VisitResult::kStop) [[unlikely]] {
      co_return result;
    }

    // Breadth-first and pre-order -> add children if not skipping subtree
    if constexpr (Order != TraversalOrder::kPostOrder) {
      if (visit_result != VisitResult::kSkipSubtree) {
        // Use the saved node pointer and current depth
        CollectChildrenToBuffer(node, current_depth);
        QueueChildrenForTraversal<Order>(filter_result, container);
      }
    }

    co_await oxygen::co::kYield; // Yield to allow other coroutines to run
  }
  co_return result;
}

template <typename SceneT>
template <typename VisitorFunc, typename FilterFunc>
co::Co<TraversalResult> AsyncSceneTraversal<SceneT>::TraverseDispatchAsync(
  std::span<VisitedNode> root_impl_nodes, VisitorFunc&& visitor,
  const TraversalOrder order, FilterFunc&& filter) const
{
  if (root_impl_nodes.empty()) [[unlikely]] {
    co_return TraversalResult {};
  }

  // Dispatch to appropriate traversal algorithm
  switch (order) {
  case TraversalOrder::kBreadthFirst:
    co_return co_await TraverseImplAsync<TraversalOrder::kBreadthFirst>(
      root_impl_nodes, std::forward<VisitorFunc>(visitor),
      std::forward<FilterFunc>(filter));
  case TraversalOrder::kPreOrder:
    co_return co_await TraverseImplAsync<TraversalOrder::kPreOrder>(
      root_impl_nodes, std::forward<VisitorFunc>(visitor),
      std::forward<FilterFunc>(filter));
  case TraversalOrder::kPostOrder:
    co_return co_await TraverseImplAsync<TraversalOrder::kPostOrder>(
      root_impl_nodes, std::forward<VisitorFunc>(visitor),
      std::forward<FilterFunc>(filter));
  }

  // This should never be reached with valid enum values
  [[unlikely]] co_return TraversalResult {};
}

} // namespace oxygen::scene
