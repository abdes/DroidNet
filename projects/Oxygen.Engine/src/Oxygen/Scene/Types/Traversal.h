//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

class SceneNodeImpl;

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
  //! Hierarchical depth of this node (0 = root level)
  std::size_t depth { 0 };
};
using MutableVisitedNode = VisitedNodeT<false>;
using ConstVisitedNode = VisitedNodeT<true>;

//! Result of a traversal operation
struct TraversalResult {
  std::size_t nodes_visited = 0; //!< Number of nodes visited
  std::size_t nodes_filtered = 0; //!< Number of nodes filtered out
  bool completed = true; //!< true if fully completed, false if stopped early
};

//=== Concept for Traversal Filters ===---------------------------------------//

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

//=== Common Filters ===------------------------------------------------------//

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

//=== Visited Nodes Container Template & Specializations //===----------------//

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

} // namespace oxygen::scene
