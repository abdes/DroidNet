//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Scene/Detail/Scene_safecall_impl.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Scene.h>

using oxygen::scene::Scene;
using oxygen::scene::detail::TransformComponent;
using Vec3 = TransformComponent::Vec3;
using Quat = TransformComponent::Quat;

//------------------------------------------------------------------------------
// Scene Node Reparenting Implementation
//------------------------------------------------------------------------------

/*!
 Preserves world transform during hierarchy changes by leveraging Oxygen's
 cached transform system.

 This method captures the cached world transform values (which remain valid
 until the next Scene::Update() cycle) and sets them as the new local transform.
 This ensures visual continuity when nodes are reparented, preventing objects
 from appearing to "jump" to new positions.

 The method works because Oxygen caches world transforms in SceneNodeImpl during
 the Update() cycle, so GetWorldPosition/Rotation/Scale() return accurate cached
 values even after hierarchy changes, until the next Update() recalculates them.

 @param node The node whose world transform should be preserved (used for root
 check)
 @param node_impl Pointer to the node's implementation containing cached
 transforms

 @note This is a general-purpose helper used by both MakeNodeRoot() and
       ReparentNode() operations
 @note SetLocalTransform() automatically marks transforms as dirty
 @note For root nodes, UpdateWorldTransformAsRoot() ensures immediate cache
 consistency
*/
void Scene::PreserveWorldTransform(
  const SceneNode& node, SceneNodeImpl* node_impl) noexcept
{
  auto& transform_component = node_impl->GetComponent<TransformComponent>();

  // Capture cached world transform (valid until next Update() cycle)
  const auto world_position = transform_component.GetWorldPosition();
  const auto world_rotation = transform_component.GetWorldRotation();
  const auto world_scale = transform_component.GetWorldScale();

  // Set captured world transform as new local transform (automatically marks
  // dirty)
  transform_component.SetLocalTransform(
    world_position, world_rotation, world_scale);

  // For root nodes, update world transform cache immediately for consistency
  if (node.IsRoot()) {
    transform_component.UpdateWorldTransformAsRoot();
  }
}

/*!
 Makes a node a root node within this scene, moving the entire hierarchy rooted
 at that node to become a top-level hierarchy.

 This operation unlinks the specified node from its current parent (if any) and
 adds it to the scene's root nodes collection. The entire subtree rooted at the
 node is moved as a unit, preserving all internal parent-child relationships
 within the hierarchy.

 ### Failure Scenarios

 - If the \p node handle is not valid (expired or invalidated)
 - If the \p node is valid but its corresponding node was removed from the scene
   (triggers lazy invalidation of the node handle)
 - If the \p node does not belong to this scene

 ### Post-Conditions

 - On success: Node becomes a root node with no parent, entire hierarchy moves
   to top level
 - On failure: Scene hierarchy remains unchanged, no nodes are modified
 - Transform dirty flags are updated for affected nodes when \p
   preserve_world_transform is true

 @note This method will terminate the program if the \p node does not belong to
 this scene. For cross-scene operations, use the appropriate adoption APIs.

 @note **Atomicity:** Only hierarchy pointers are modified without
 destroying/recreating node data. Either fully succeeds or leaves scene
 unchanged.

 @note **Transform Preservation:** When \p preserve_world_transform is true,
 leverages Oxygen's cached transform system where world transforms remain valid
 until the next Update() cycle, enabling accurate preservation across hierarchy
 changes without expensive parent chain traversal.

 @param node Root of hierarchy to make root (must be in this scene)
 @param preserve_world_transform If true, adjusts local transform to maintain
 world position using cached world transform values
 @return true if operation succeeded, false if invalid node

 @see ReparentNode() for moving nodes to specific parents
 @see AdoptNodeAsRoot() for adopting nodes from other scenes as roots
 @see UnlinkNode() for the underlying hierarchy unlinking operation
 @see PreserveWorldTransform() for the transform preservation implementation
*/
auto Scene::MakeNodeRoot(
  const SceneNode& node, bool preserve_world_transform) noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndMine(node), [&](const SafeCallState& state) -> bool {
      DCHECK_EQ_F(state.node, &node);

      // If the node is already a root node, operation succeeds immediately
      if (node.IsRoot()) {
        return true;
      } // Unlink the node from its current parent to make it an orphan
      UnlinkNode(node.GetHandle(), state.node_impl);

      // Add the node to the root nodes collection
      AddRootNode(node.GetHandle());

      // Apply transform preservation or mark for recalculation after hierarchy
      // changes
      if (preserve_world_transform) {
        PreserveWorldTransform(node, state.node_impl);
      } else {
        MarkSubtreeTransformDirty(node.GetHandle());
      }

      return true;
    });
}
