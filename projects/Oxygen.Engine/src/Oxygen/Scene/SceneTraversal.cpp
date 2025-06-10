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

//=== Transform Update Methods ===--------------------------------------------//

auto SceneTraversal::UpdateTransforms() -> std::size_t
{
  std::size_t updated_count
    = 0; // Batch process with dirty transform filter for efficiency
  [[maybe_unused]] auto result = Traverse(
    [&updated_count](const VisitedNode& node, const Scene& scene,
      bool dry_run) -> VisitResult {
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

auto SceneTraversal::UpdateTransforms(std::span<SceneNode> starting_nodes)
  -> std::size_t
{
  std::size_t updated_count
    = 0; // Batch process from specific roots with dirty transform filter
  [[maybe_unused]] auto result = TraverseHierarchies(
    starting_nodes,
    [&updated_count](const VisitedNode& node, const Scene& scene,
      bool dry_run) -> VisitResult {
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

auto oxygen::scene::to_string(FilterResult value) -> const char*
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

auto oxygen::scene::to_string(VisitResult value) -> const char*
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
