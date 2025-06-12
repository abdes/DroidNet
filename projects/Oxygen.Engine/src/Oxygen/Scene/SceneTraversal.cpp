//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/SceneTraversal.h>

using oxygen::scene::DirtyTransformFilter;
using oxygen::scene::FilterResult;
using oxygen::scene::SceneTraversal;
using oxygen::scene::VisitedNode;

//=== SceneTraversal Implementation ===---------------------------------------//

auto DirtyTransformFilter::operator()(const VisitedNode& visited_node,
    const FilterResult parent_filter_result) const noexcept -> FilterResult
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

auto oxygen::scene::VisibleFilter::operator()(const VisitedNode& visited_node,
    FilterResult) const noexcept -> FilterResult
{
  const auto& flags = visited_node.node_impl->GetFlags();
  return flags.GetEffectiveValue(SceneNodeFlags::kVisible)
      ? FilterResult::kAccept
      : FilterResult::kRejectSubTree;
}

SceneTraversal::SceneTraversal(const Scene& scene)
  : scene_(&scene)
{
  // Pre-allocate children buffer to avoid repeated small reservations
  children_buffer_.reserve(8);
}

//=== Helper Methods ===------------------------------------------------------//

auto SceneTraversal::GetNodeImpl(const NodeHandle& handle) const
    -> SceneNodeImpl*
{
  DCHECK_F(handle.IsValid());
  try {
    // Breaks const-correctness but some visitors need mutation.
    // TODO: Need better solution for mutating traversal.
    return &const_cast<Scene*>(scene_)->GetNodeImplRef(handle);
  } catch (const std::exception&) {
    DLOG_F(ERROR, "node no longer in scene: {}", to_string_compact(handle));
    return nullptr;
  }
}

auto SceneTraversal::GetOptimalStackCapacity() const -> std::size_t
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

void SceneTraversal::CollectChildrenToBuffer(SceneNodeImpl* node) const
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
auto SceneTraversal::UpdateNodeImpl(TraversalEntry& entry) const -> bool
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
auto SceneTraversal::UpdateTransforms() -> std::size_t
{
  std::size_t updated_count
      = 0; // Batch process with dirty transform filter for efficiency
  [[maybe_unused]] auto result = Traverse(
      [&updated_count](const VisitedNode& node, const Scene& scene,
          const bool dry_run) -> VisitResult {
        DCHECK_F(!dry_run,
            "UpdateTransforms uses kPreOrder and should never receive "
            "dry_run=true");

        DCHECK_NOTNULL_F(node.node_impl);
        LOG_SCOPE_F(2, "For Node");
        LOG_F(2, "name = {}", node.node_impl->GetName());
        LOG_F(2, "is root: {}", node.node_impl->AsGraphNode().IsRoot());

        node.node_impl->UpdateTransforms(scene);
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
auto SceneTraversal::UpdateTransforms(const std::span<SceneNode> starting_nodes)
    -> std::size_t
{
  std::size_t updated_count
      = 0; // Batch process from specific roots with dirty transform filter
  [[maybe_unused]] auto result = TraverseHierarchies(
      starting_nodes,
      [&updated_count](const VisitedNode& node, const Scene& scene,
          const bool dry_run) -> VisitResult {
        DCHECK_F(!dry_run,
            "UpdateTransforms uses kPreOrder and should never receive "
            "dry_run=true");

        DCHECK_NOTNULL_F(node.node_impl);
        node.node_impl->UpdateTransforms(scene);
        ++updated_count;
        return VisitResult::kContinue;
      },
      TraversalOrder::kPreOrder, DirtyTransformFilter {});

  return updated_count;
}

auto oxygen::scene::to_string(const FilterResult value) -> const char*
{
  switch (value) {
  case FilterResult::kAccept:
    return "Accept";
  case FilterResult::kReject:
    return "Reject";
  case FilterResult::kRejectSubTree:
    return "Reject SubTree";
  }

  return "__NotSupported__";
}

auto oxygen::scene::to_string(const VisitResult value) -> const char*
{
  switch (value) {
  case VisitResult::kContinue:
    return "Continue";
  case VisitResult::kSkipSubtree:
    return "Skip SubTree";
  case VisitResult::kStop:
    return "Stop";
  }

  return "__NotSupported__";
}
