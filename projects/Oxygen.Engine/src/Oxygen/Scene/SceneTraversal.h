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

//! Enumeration of supported traversal orders
enum class TraversalOrder : uint8_t {
  kBreadthFirst, //!< Visit nodes level by level (first child to last sibling)
  kPreOrder, //!< Visit nodes before their children (depth-first pre-order)
  kPostOrder //!< Visit nodes after their children (depth-first post-order)
};
OXGN_SCN_API auto to_string(TraversalOrder value) -> const char*;

//=== Traversal Data Structure ===--------------------------------------------//

//! Helper for conditional constness. Adds const qualifier to T if B is true.
template <typename T, bool B>
using add_const_if_t = std::conditional_t<B, const T, T>;

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
template <bool IsConst> struct VisitedNodeT {
  //! Handle to the node being visited
  NodeHandle handle;
  //! Reference to the node implementation
  add_const_if_t<SceneNodeImpl, IsConst>* node_impl { nullptr };
};
using MutableVisitedNode = VisitedNodeT<false>;
using ConstVisitedNode = VisitedNodeT<true>;

//! Result of a traversal operation
struct TraversalResult {
  std::size_t nodes_visited = 0; //!< Number of nodes visited
  std::size_t nodes_filtered = 0; //!< Number of nodes filtered out
  bool completed = true; //!< true if fully completed, false if stopped early
};

//=== Concepts for Visitors and Filters ===-----------------------------------//

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

//! Concept for a filter that can be used in scene traversal. Automatically
//! deduces the constness of the VisitedNode based on the Scene type.
template <typename Filter, typename SceneT>
concept SceneFilterT = requires(Filter f,
  const VisitedNodeT<std::is_const_v<SceneT>>& visited_node, // const-correct
  FilterResult parent_result) /* filter result from parent node */ {
  { f(visited_node, parent_result) } -> std::convertible_to<FilterResult>;
};
template <typename Filter>
concept MutatingSceneFilter = SceneFilterT<Filter, Scene>;
template <typename Filter>
concept NonMutatingSceneFilter = SceneFilterT<Filter, const Scene>;

//=== Visited Nodes Container Template & Specializations //===--------------//

//! Container type selection based on traversal order. Will select a
//! std::vector (better memory locality) for depth-first traversals and a
//! std::deque (more efficient front removal) for breadth-first traversal.
template <TraversalOrder Order> struct ContainerTraits;

template <> struct ContainerTraits<TraversalOrder::kBreadthFirst> {
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
template <> struct ContainerTraits<TraversalOrder::kPreOrder> {
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
struct ContainerTraits<TraversalOrder::kPostOrder>
  : ContainerTraits<TraversalOrder::kPreOrder> { };

//=== High-Performance Filters ===--------------------------------------------//

//! Non-mutating filter that accepts all nodes.
struct AcceptAllFilter {
  constexpr auto operator()(const auto& /*visited_node*/,
    FilterResult /*parent_filter_result*/) const noexcept -> FilterResult
  {
    return FilterResult::kAccept;
  }
};
static_assert(NonMutatingSceneFilter<AcceptAllFilter>);

//! Non-mutating filter that accepts nodes based on the state of their
//! transforms.
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
  [[nodiscard]] auto operator()(const auto& visited_node,
    FilterResult parent_filter_result) const noexcept -> FilterResult
  {
    using enum FilterResult;

    const auto& node = *visited_node.node_impl;

    // If parent was accepted and this node does not ignore parent transform,
    // accept
    if (node.GetFlags().GetEffectiveValue(
          SceneNodeFlags::kIgnoreParentTransform)) {
      DLOG_F(2, "Rejecting subtree for node {} due to IgnoreParentTransform",
        node.GetName());
      return kRejectSubTree;
    }
    // Otherwise, accept if this node is dirty, or its parent accepted, but for
    // root nodes, we only use our own verdict.
    const auto parent_accepted
      = node.AsGraphNode().IsRoot() ? false : parent_filter_result == kAccept;
    const auto should_accept = parent_accepted || node.IsTransformDirty();
    const auto verdict = should_accept ? kAccept : kReject;
    DLOG_F(2, "Node {} is {}", node.GetName(),
      verdict == kAccept ? "accepted" : "rejected");
    return verdict;
  }
};
static_assert(NonMutatingSceneFilter<DirtyTransformFilter>);

//! Non-mutating filter that accepts only visible SceneNodeImpl objects.
/*!
 This filter accepts only nodes that are marked as visible, and will block the
 entire sub-tree below a node if it's not visible.
*/
struct VisibleFilter {
  [[nodiscard]] auto operator()(const auto& visited_node,
    FilterResult /*parent_filter_result*/) const noexcept -> FilterResult
  {
    const auto& flags = visited_node.node_impl->GetFlags();
    return flags.GetEffectiveValue(SceneNodeFlags::kVisible)
      ? FilterResult::kAccept
      : FilterResult::kRejectSubTree;
  }
};
static_assert(NonMutatingSceneFilter<VisibleFilter>);

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
template <typename SceneT> class SceneTraversal {
public:
  using Node = add_const_if_t<SceneNode, std::is_const_v<SceneT>>;
  using NodeImpl = add_const_if_t<SceneNodeImpl, std::is_const_v<SceneT>>;
  using VisitedNode = VisitedNodeT<std::is_const_v<SceneT>>;

  explicit SceneTraversal(std::shared_ptr<SceneT> scene_weak)
    : scene_weak_(std::move(scene_weak))
  {
    // Pre-allocate children buffer to avoid repeated small reservations
    children_buffer_.reserve(8);
  }

  SceneTraversal(const SceneTraversal& other)
    : scene_weak_(other.scene_weak_)
  {
    // Pre-allocate children buffer to avoid repeated small reservations
    children_buffer_.reserve(8);
  }

  //=== Core Traversal API ===----------------------------------------------//

  //! Traverse the entire scene graph from root nodes, using by default, a
  //! depth-first, pre-order traversal.
  template <typename VisitorFunc, typename FilterFunc = AcceptAllFilter>
    requires SceneVisitorT<VisitorFunc, SceneT>
    && SceneFilterT<FilterFunc, SceneT>
  [[nodiscard]] auto Traverse(VisitorFunc&& visitor,
    TraversalOrder order = TraversalOrder::kPreOrder,
    FilterFunc&& filter = AcceptAllFilter {}) const -> TraversalResult;

  //! Traverse from a single root node, using by default, a depth-first,
  //! pre-order traversal.
  template <typename VisitorFunc, typename FilterFunc = AcceptAllFilter>
    requires SceneVisitorT<VisitorFunc, SceneT>
    && SceneFilterT<FilterFunc, SceneT>
  [[nodiscard]] auto TraverseHierarchy(Node& starting_node,
    VisitorFunc&& visitor, TraversalOrder order = TraversalOrder::kPreOrder,
    FilterFunc&& filter = AcceptAllFilter {}) const -> TraversalResult;

  //! Traverse from specific root nodes, using by default, a depth-first,
  //! pre-order traversal.
  template <typename VisitorFunc, typename FilterFunc = AcceptAllFilter>
    requires SceneVisitorT<VisitorFunc, SceneT>
    && SceneFilterT<FilterFunc, SceneT>
  [[nodiscard]] auto TraverseHierarchies(std::span<Node> starting_nodes,
    VisitorFunc&& visitor, TraversalOrder order = TraversalOrder::kPreOrder,
    FilterFunc&& filter = AcceptAllFilter {}) const -> TraversalResult;

  //=== Convenience Methods ===---------------------------------------------//

  //! Update transforms for all dirty nodes using optimized traversal
  /*!
   Efficiently updates transforms for all nodes that have dirty transform
   state. This is equivalent to Scene::Update but uses the optimized traversal
   system.

   \return Number of nodes that had their transforms updated
  */
  auto UpdateTransforms() -> std::size_t;

  //! Update transforms for dirty nodes from specific roots
  /*!
   \param starting_nodes Starting nodes for transform update traversal
   \return Number of nodes that had their transforms updated
  */
  auto UpdateTransforms(std::span<SceneNode> starting_nodes) -> std::size_t;

private:
  std::weak_ptr<SceneT> scene_weak_;
  mutable std::vector<VisitedNode> children_buffer_;

  //=== Private Helper Methods ===--------------------------------------------//

  //! Collect children of a node into reused buffer
  void CollectChildrenToBuffer(NodeImpl* node) const;

  //! Get valid node pointer from handle
  auto GetNodeImpl(const NodeHandle& handle) const;

  //! Calculate optimal stack capacity based on scene size
  auto GetOptimalStackCapacity() const -> std::size_t;

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
  auto UpdateNodeImpl(TraversalEntry& entry) const -> bool;

  //! Unified traversal implementation
  template <TraversalOrder Order, typename VisitorFunc, typename FilterFunc>
  auto TraverseImpl(std::span<VisitedNode> roots, VisitorFunc&& visitor,
    FilterFunc&& filter) const -> TraversalResult;

  template <typename VisitorFunc, typename FilterFunc>
  auto TraverseDispatch(std::span<VisitedNode> root_impl_nodes,
    VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter) const
    -> TraversalResult;
};

//=== Template Deduction Guides ===-------------------------------------------//

SceneTraversal(std::shared_ptr<Scene>) -> SceneTraversal<Scene>;
SceneTraversal(std::shared_ptr<const Scene>) -> SceneTraversal<const Scene>;

//=== Helper Methods ===------------------------------------------------------//

template <typename SceneT>
auto SceneTraversal<SceneT>::GetNodeImpl(const NodeHandle& handle) const
{
  DCHECK_F(!scene_weak_.expired());
  DCHECK_F(handle.IsValid());

  // Define an alias for the return type to improve readability
  using ReturnType = add_const_if_t<SceneNodeImpl, std::is_const_v<SceneT>>*;

  try {
    // Breaks const-correctness but some visitors need mutation.
    // TODO: Need better solution for mutating traversal.
    auto scene = scene_weak_.lock();
    auto& impl_ref = scene->GetNodeImplRefUnsafe(handle);
    return &impl_ref;
  } catch (const std::exception&) {
    DLOG_F(ERROR, "node no longer in scene: {}", to_string_compact(handle));
    return ReturnType { nullptr };
  }
}

template <typename SceneT>
auto SceneTraversal<SceneT>::GetOptimalStackCapacity() const -> std::size_t
{
  DCHECK_F(!scene_weak_.expired());
  // NOLINTBEGIN(*-magic-numbers)
  const auto node_count = scene_weak_.lock()->GetNodeCount();
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

template <typename SceneT>
void SceneTraversal<SceneT>::CollectChildrenToBuffer(NodeImpl* node) const
{
  children_buffer_.clear(); // Fast - just resets size for pointer vector

  LOG_SCOPE_F(2, "Collect Children");
  DLOG_F(2, "node: {}", node->GetName());

  auto child_handle = node->AsGraphNode().GetFirstChild();
  if (!child_handle.IsValid()) {
    DLOG_F(2, "no children");
    return; // Early exit for leaf nodes
  }

  // Collect all children in a single pass.
  while (child_handle.IsValid()) {
    auto* child_node = GetNodeImpl(child_handle);

    // Sanity checks
    DCHECK_NOTNULL_F(child_node,
      "corrupted scene graph, child `{}` of `{}` is no longer in the scene",
      to_string_compact(child_handle), node->GetName());
    DLOG_F(2, " + {}", child_node->GetName());

    children_buffer_.push_back(VisitedNode {
      .handle = child_handle, // The handle is the only stable thing
      .node_impl = nullptr, // Do not update the node_impl here, it will be
      // updated during traversal because the table may
      // change
    });
    child_handle = child_node->AsGraphNode().GetNextSibling();
  }

  DLOG_F(2, "total: {}", children_buffer_.size());
}

/*!
 The implementation for the node being traversed is updated before a node is
 visited (or revisited in post-order traversal) to make the traversal algorithm
 resilient to visitors that mutate the scene graph during traversal.
*/
template <typename SceneT>
auto SceneTraversal<SceneT>::UpdateNodeImpl(TraversalEntry& entry) const -> bool
{
  // Refresh the node impl from handle ALWAYS even if it is not null. Mutations
  // during child visits will invalidate the pointers
  entry.visited_node.node_impl = GetNodeImpl(entry.visited_node.handle);
  return entry.visited_node.node_impl != nullptr;
}

//=== Transform Update Methods ===--------------------------------------------//

/*!
 Efficiently updates transforms for all nodes that have dirty transform state.
 This is equivalent to Scene::Update but uses the optimized traversal system.

 @return Number of nodes that had their transforms updated
*/
template <typename SceneT>
auto SceneTraversal<SceneT>::UpdateTransforms() -> std::size_t
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

      node.node_impl->UpdateTransforms(*scene_weak_.lock());
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
auto SceneTraversal<SceneT>::UpdateTransforms(
  const std::span<SceneNode> starting_nodes) -> std::size_t
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
      node.node_impl->UpdateTransforms(*scene_weak_.lock());
      ++updated_count;
      return VisitResult::kContinue;
    },
    TraversalOrder::kPreOrder, DirtyTransformFilter {});

  return updated_count;
}

//=== Template Implementations ===--------------------------------------------//

template <typename SceneT>
template <typename VisitorFunc, typename FilterFunc>
  requires SceneVisitorT<VisitorFunc, SceneT>
  && SceneFilterT<FilterFunc, SceneT>
auto SceneTraversal<SceneT>::Traverse(VisitorFunc&& visitor,
  TraversalOrder order, FilterFunc&& filter) const -> TraversalResult
{
  DCHECK_F(!scene_weak_.expired());
  auto root_handles = scene_weak_.lock()->GetRootHandles();
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

  return TraverseDispatch(root_impl_nodes, std::forward<VisitorFunc>(visitor),
    order, std::forward<FilterFunc>(filter));
}

template <typename SceneT>
template <typename VisitorFunc, typename FilterFunc>
  requires SceneVisitorT<VisitorFunc, SceneT>
  && SceneFilterT<FilterFunc, SceneT>
auto SceneTraversal<SceneT>::TraverseHierarchy(Node& starting_node,
  VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter) const
  -> TraversalResult
{

  if (!starting_node.IsValid()) {
    DLOG_F(WARNING, "TraverseHierarchy starting from an invalid node.");
    return TraversalResult {};
  }
  DCHECK_F(!scene_weak_.expired());
  CHECK_F(scene_weak_.lock()->Contains(starting_node),
    "Starting node for traversal must be part of this scene");

  using VisitorNode = VisitedNodeT<std::is_const_v<SceneT>>;
  std::array root_impl_nodes {
    VisitorNode {
      .handle = starting_node.GetHandle(),
      .node_impl = GetNodeImpl(starting_node.GetHandle()),
    },
  };

  return TraverseDispatch(root_impl_nodes, std::forward<VisitorFunc>(visitor),
    order, std::forward<FilterFunc>(filter));
}

template <typename SceneT>
template <typename VisitorFunc, typename FilterFunc>
  requires SceneVisitorT<VisitorFunc, SceneT>
  && SceneFilterT<FilterFunc, SceneT>
auto SceneTraversal<SceneT>::TraverseHierarchies(std::span<Node> starting_nodes,
  VisitorFunc&& visitor, TraversalOrder order, FilterFunc&& filter) const
  -> TraversalResult
{
  if (starting_nodes.empty()) [[unlikely]] {
    return TraversalResult {};
  }
  DCHECK_F(!scene_weak_.expired());

  std::vector<VisitedNode> root_impl_nodes;
  root_impl_nodes.reserve(starting_nodes.size());
  std::ranges::transform(starting_nodes, std::back_inserter(root_impl_nodes),
    [this](const SceneNode& node) {
      CHECK_F(scene_weak_.lock()->Contains(node),
        "Starting nodes for traversal must be part of this scene");
      return VisitedNode { .handle = node.GetHandle(),
        .node_impl = GetNodeImpl(node.GetHandle()) };
    });

  return TraverseDispatch(root_impl_nodes, std::forward<VisitorFunc>(visitor),
    order, std::forward<FilterFunc>(filter));
}

template <typename SceneT>
template <TraversalOrder Order, typename Container>
void SceneTraversal<SceneT>::InitializeContainerWithRoots(
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

template <typename SceneT>
template <TraversalOrder Order, typename Container>
void SceneTraversal<SceneT>::QueueChildrenForTraversal(
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

template <typename SceneT>
template <typename FilterFunc>
auto SceneTraversal<SceneT>::ApplyNodeFilter(FilterFunc& filter,
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

template <typename SceneT>
template <TraversalOrder Order, typename VisitorFunc, typename Container>
auto SceneTraversal<SceneT>::PerformNodeVisit(VisitorFunc& visitor,
  Container& container, TraversalResult& result, bool dry_run) const
  -> VisitResult
{
  using Traits = ContainerTraits<Order>;

  DCHECK_F(!scene_weak_.expired());
  auto scene = scene_weak_.lock();

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

// NOLINTBEGIN(cppcoreguidelines-missing-std-forward)
template <typename SceneT>
template <TraversalOrder Order, typename VisitorFunc, typename FilterFunc>
auto SceneTraversal<SceneT>::TraverseImpl(std::span<VisitedNode> roots,
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
      DCHECK_F(
        entry_ref.state == TraversalEntry::ProcessingState::kChildrenProcessed,
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

template <typename SceneT>
template <typename VisitorFunc, typename FilterFunc>
auto SceneTraversal<SceneT>::TraverseDispatch(
  std::span<VisitedNode> root_impl_nodes, VisitorFunc&& visitor,
  const TraversalOrder order, FilterFunc&& filter) const -> TraversalResult
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
