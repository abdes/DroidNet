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
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversalBase.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/Types/Traversal.h>

namespace oxygen::scene {

//=== Concept for Visitors ===------------------------------------------------//

//! Concept for a visitor that can be used in scene traversal. Automatically
//! deduces the constness of the VisitedNode based on the Scene type.
template <typename Visitor, typename SceneT>
concept SceneVisitorT = requires(Visitor v,
  const VisitedNodeT<std::is_const_v<SceneT>>& visited_node, // const-correct
  bool dry_run) {
  { v(visited_node, dry_run) } -> std::convertible_to<VisitResult>;
};
template <typename Visitor>
concept MutatingSceneVisitor = SceneVisitorT<Visitor, Scene>;
template <typename Visitor>
concept NonMutatingSceneVisitor = SceneVisitorT<Visitor, const Scene>;

//=== High-Performance Scene Graph Traversal ===------------------------------//

//! High-performance scene graph traversal interface
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
class SceneTraversal : public detail::SceneTraversalBase<SceneT> {
  using Base = detail::SceneTraversalBase<SceneT>;

public:
  using Node = typename Base::Node;
  using VisitedNode = typename Base::VisitedNode;

  explicit SceneTraversal(const std::shared_ptr<SceneT>& scene)
    : Base(scene)
  {
  }

  ~SceneTraversal() override = default;

  OXYGEN_DEFAULT_COPYABLE(SceneTraversal)
  OXYGEN_DEFAULT_MOVABLE(SceneTraversal)

  //=== Core Traversal API ===----------------------------------------------//

  //! Traverse the entire scene graph from root nodes, using by default, a
  //! depth-first, pre-order traversal.
  template <typename VisitorFunc, typename FilterFunc = AcceptAllFilter>
    requires SceneVisitorT<VisitorFunc, SceneT>
    && SceneFilterT<FilterFunc, SceneT>
  [[nodiscard]] TraversalResult Traverse(VisitorFunc&& visitor,
    TraversalOrder order = TraversalOrder::kPreOrder,
    FilterFunc&& filter = AcceptAllFilter {}) const;

  //! Traverse from a single hierarchy starting at \p starting_node, using by
  //! default, a depth-first, pre-order traversal.
  template <typename VisitorFunc, typename FilterFunc = AcceptAllFilter>
    requires SceneVisitorT<VisitorFunc, SceneT>
    && SceneFilterT<FilterFunc, SceneT>
  [[nodiscard]] TraversalResult TraverseHierarchy(const Node& starting_node,
    VisitorFunc&& visitor, TraversalOrder order = TraversalOrder::kPreOrder,
    FilterFunc&& filter = AcceptAllFilter {}) const;

  //! Traverse multiple hierarchies, starting at the nodes provided in \p
  //! starting_nodes, using by default, a depth-first, pre-order traversal.
  template <typename VisitorFunc, typename FilterFunc = AcceptAllFilter>
    requires SceneVisitorT<VisitorFunc, SceneT>
    && SceneFilterT<FilterFunc, SceneT>
  [[nodiscard]] TraversalResult TraverseHierarchies(
    std::span<const Node> starting_nodes, VisitorFunc&& visitor,
    TraversalOrder order = TraversalOrder::kPreOrder,
    FilterFunc&& filter = AcceptAllFilter {}) const;

  //=== Convenience Methods ===---------------------------------------------//

  //! Update transforms for all dirty nodes using optimized traversal
  /*!
   Efficiently updates transforms for all nodes that have dirty transform
   state. This is equivalent to Scene::Update but uses the optimized traversal
   system.

   \return Number of nodes that had their transforms updated
  */
  std::size_t UpdateTransforms();

  //! Update transforms for dirty nodes from specific roots
  /*!
   \param starting_nodes Starting nodes for transform update traversal
   \return Number of nodes that had their transforms updated
  */
  std::size_t UpdateTransforms(std::span<SceneNode> starting_nodes);

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
  VisitResult PerformNodeVisit(VisitorFunc& visitor, Container& container,
    TraversalResult& result, bool dry_run = false) const;

  //! Unified traversal implementation
  template <TraversalOrder Order, typename VisitorFunc, typename FilterFunc>
  TraversalResult TraverseImpl(std::span<VisitedNode> roots,
    VisitorFunc&& visitor, FilterFunc&& filter) const;

  template <typename VisitorFunc, typename FilterFunc>
  TraversalResult TraverseDispatch(std::span<VisitedNode> root_impl_nodes,
    VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter) const;
};

//=== Template Deduction Guides ===-------------------------------------------//

SceneTraversal(std::shared_ptr<Scene>) -> SceneTraversal<Scene>;
SceneTraversal(std::shared_ptr<const Scene>) -> SceneTraversal<const Scene>;

//=== Template Implementations ===--------------------------------------------//

template <typename SceneT>
template <typename VisitorFunc, typename FilterFunc>
  requires SceneVisitorT<VisitorFunc, SceneT>
  && SceneFilterT<FilterFunc, SceneT>
TraversalResult SceneTraversal<SceneT>::Traverse(
  VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter) const
{
  if (IsSceneExpired()) [[unlikely]] {
    DLOG_F(ERROR, "SceneTraversal called on an expired scene");
    return TraversalResult {};
  }

  auto root_handles = GetScene().GetRootHandles();
  if (root_handles.empty()) [[unlikely]] {
    return TraversalResult {};
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

  return TraverseDispatch(root_impl_nodes, std::forward<VisitorFunc>(visitor),
    order, std::forward<FilterFunc>(filter));
}

template <typename SceneT>
template <typename VisitorFunc, typename FilterFunc>
  requires SceneVisitorT<VisitorFunc, SceneT>
  && SceneFilterT<FilterFunc, SceneT>
TraversalResult SceneTraversal<SceneT>::TraverseHierarchy(
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

  return TraverseDispatch(root_impl_nodes, std::forward<VisitorFunc>(visitor),
    order, std::forward<FilterFunc>(filter));
}

template <typename SceneT>
template <typename VisitorFunc, typename FilterFunc>
  requires SceneVisitorT<VisitorFunc, SceneT>
  && SceneFilterT<FilterFunc, SceneT>
TraversalResult SceneTraversal<SceneT>::TraverseHierarchies(
  std::span<const Node> starting_nodes, VisitorFunc&& visitor,
  TraversalOrder order, FilterFunc&& filter) const
{
  if (starting_nodes.empty()) [[unlikely]] {
    return TraversalResult {};
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

  return TraverseDispatch(root_impl_nodes, std::forward<VisitorFunc>(visitor),
    order, std::forward<FilterFunc>(filter));
}

template <typename SceneT>
template <TraversalOrder Order, typename VisitorFunc, typename Container>
VisitResult SceneTraversal<SceneT>::PerformNodeVisit(VisitorFunc& visitor,
  Container& container, TraversalResult& result, bool dry_run) const
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

    visit_result = visitor(entry_ref.visited_node, dry_run);
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

  visit_result = visitor(entry.visited_node, false);
  DLOG_F(2, "-> {}", nostd::to_string(visit_result));
  ++result.nodes_visited;

  if (visit_result == VisitResult::kStop) [[unlikely]] {
    result.completed = false;
  }
  return visit_result;
}

// NOLINTBEGIN(*-missing-std-forward) - used inside loop
template <typename SceneT>
template <TraversalOrder Order, typename VisitorFunc, typename FilterFunc>
TraversalResult SceneTraversal<SceneT>::TraverseImpl(
  std::span<VisitedNode> roots, VisitorFunc&& visitor,
  FilterFunc&& filter) const
// NOLINTEND(*-missing-std-forward)
{
  if (roots.empty()) [[unlikely]] {
    return TraversalResult { .completed = true };
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
      const auto& handle = entry_ref.visited_node.handle;
      DLOG_F(
        2, "skipping, no longer in scene", to_string_compact(handle).c_str());
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
    auto visit_result
      = PerformNodeVisit<Order>(local_visitor, container, result, false);
    if (visit_result == VisitResult::kStop) [[unlikely]] {
      return result;
    }

    // Breadth-first and pre-order -> add children if not skipping subtree
    if constexpr (Order != TraversalOrder::kPostOrder) {
      if (visit_result != VisitResult::kSkipSubtree) {
        // Use the saved node pointer and current depth
        CollectChildrenToBuffer(node, current_depth);
        QueueChildrenForTraversal<Order>(filter_result, container);
      }
    }
  }
  return result;
}

template <typename SceneT>
template <typename VisitorFunc, typename FilterFunc>
TraversalResult SceneTraversal<SceneT>::TraverseDispatch(
  std::span<VisitedNode> root_impl_nodes, VisitorFunc&& visitor,
  const TraversalOrder order, FilterFunc&& filter) const
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

//=== Transform Update Methods ===--------------------------------------------//

/*!
 Efficiently updates transforms for all nodes that have dirty transform state.
 This is equivalent to Scene::Update but uses the optimized traversal system.

 @return Number of nodes that had their transforms updated
*/
template <typename SceneT>
std::size_t SceneTraversal<SceneT>::UpdateTransforms()
{
  std::size_t updated_count
    = 0; // Batch process with dirty transform filter for efficiency
  [[maybe_unused]] auto result = Traverse(
    [&](const auto& node, const bool dry_run) -> VisitResult {
      DCHECK_F(!dry_run,
        "UpdateTransforms uses kPreOrder and should never receive "
        "dry_run=true");

      DCHECK_NOTNULL_F(node.node_impl);
      LOG_SCOPE_F(2, "For Node");
      LOG_F(2, "name = {}", node.node_impl->GetName());
      LOG_F(2, "is root: {}", node.node_impl->AsGraphNode().IsRoot());

      node.node_impl->UpdateTransforms(GetScene());
      ++updated_count;
      return VisitResult::kContinue;
    },
    TraversalOrder::kPreOrder, DirtyTransformFilter {});

  return updated_count;
}

/*!
 @param starting_nodes Starting nodes for transform update traversal
 @return Number of nodes that had their transforms updated
*/
template <typename SceneT>
std::size_t SceneTraversal<SceneT>::UpdateTransforms(
  const std::span<SceneNode> starting_nodes)
{
  std::size_t updated_count
    = 0; // Batch process from specific roots with dirty transform filter
  [[maybe_unused]] auto result = TraverseHierarchies(
    starting_nodes,
    [this, &updated_count](
      const MutableVisitedNode& node, const bool dry_run) -> VisitResult {
      DCHECK_F(!dry_run,
        "UpdateTransforms uses kPreOrder and should never receive "
        "dry_run=true");

      DCHECK_NOTNULL_F(node.node_impl);
      node.node_impl->UpdateTransforms(GetScene());
      ++updated_count;
      return VisitResult::kContinue;
    },
    TraversalOrder::kPreOrder, DirtyTransformFilter {});

  return updated_count;
}

} // namespace oxygen::scene
