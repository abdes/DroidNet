//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversal.h>

using oxygen::scene::DirtyTransformFilter;
using oxygen::scene::FilterResult;
using oxygen::scene::SceneTraversal;
using oxygen::scene::VisitedNode;

//=== SceneTraversal Implementation ===---------------------------------------//

auto DirtyTransformFilter::operator()(const VisitedNode& visited_node,
  const FilterResult parent_result) const noexcept -> FilterResult
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
  // Otherwise, accept if this node is dirty, or its parent accepted
  const auto parent_accepted = parent_result == kAccept;
  const auto should_accept = parent_accepted || node.IsTransformDirty();
  const auto verdict = should_accept ? kAccept : kReject;
  DLOG_F(2, "Node {} is {}", node.GetName(),
    verdict == kAccept ? "accepted" : "rejected");
  return verdict;
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
  // Private stuff to Scene, and should not be checked here.
  // if (!scene_->Contains(handle)) {
  //     return nullptr;
  // }

  // TODO: Breaks const-correctness but some visitors need mutation.
  // Need better solution for mutable access.
  return &const_cast<Scene*>(scene_)->GetNodeImplRef(handle);
}

//=== Transform Update Methods ===--------------------------------------------//

auto SceneTraversal::UpdateTransforms() -> std::size_t
{
  std::size_t updated_count = 0;

  // Batch process with dirty transform filter for efficiency
  [[maybe_unused]] auto result = Traverse(
    [&updated_count](
      const VisitedNode& node, const Scene& scene) -> VisitResult {
      DCHECK_NOTNULL_F(node.node_impl);
      LOG_SCOPE_F(2, "For Node");
      LOG_F(2, "name = {}", node.node_impl->GetName());
      LOG_F(2, "is root: {}", node.node_impl->AsGraphNode().IsRoot());

      node.node_impl->UpdateTransforms(scene);
      ++updated_count;
      return VisitResult::kContinue;
    },
    TraversalOrder::kDepthFirst, DirtyTransformFilter {});

  return updated_count;
}

auto SceneTraversal::UpdateTransforms(std::span<SceneNode> starting_nodes)
  -> std::size_t
{
  std::size_t updated_count = 0;

  // Batch process from specific roots with dirty transform filter
  [[maybe_unused]] auto result = TraverseHierarchies(
    starting_nodes,
    [&updated_count](
      const VisitedNode& node, const Scene& scene) -> VisitResult {
      DCHECK_NOTNULL_F(node.node_impl);
      node.node_impl->UpdateTransforms(scene);
      ++updated_count;
      return VisitResult::kContinue;
    },
    TraversalOrder::kDepthFirst, DirtyTransformFilter {});

  return updated_count;
}
