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

  // Skip preservation if transforms are dirty (never been updated)
  // This handles cases where newly created nodes are immediately reparented
  if (node_impl->IsTransformDirty()) {
    return;
  }

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

      if (node.IsRoot()) {
        return true;
      }

      // Mark the current parent's transform dirty since it's losing a child
      auto parent_opt = node.GetParent();
      if (parent_opt.has_value()) {
        auto parent_impl_opt = GetNodeImpl(*parent_opt);
        if (parent_impl_opt.has_value()) {
          parent_impl_opt->get().MarkTransformDirty();
        }
      }

      // Unlink the node from its current parent to make it an orphan
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

/*!
 Re-parents a node hierarchy to a new parent within this scene, preserving all
 internal relationships within the moved hierarchy.

 This operation moves the entire subtree rooted at the specified node from its
 current location to become a child of the new parent. The complete hierarchy
 is moved atomically while preserving all parent-child relationships within the
 moved subtree.

 ### Cycle Detection

 The operation includes cycle detection to prevent creating circular references
 in the hierarchy. If reparenting would result in a cycle (e.g., making a node
 a child of its own descendant), the operation fails safely.

 ### Failure Scenarios

 - If either \p node or \p new_parent handles are invalid or belong to different
 scenes
 - If the operation would create a cycle in the hierarchy
 - If either node was removed from the scene (triggers lazy invalidation)

 ### Post-Conditions

 - On success: Node becomes child of new parent, entire hierarchy moves as unit
 - On failure: Scene hierarchy remains unchanged, no nodes are modified
 - Transform dirty flags are updated for affected nodes

 @note This method will terminate the program if either node does not belong to
 this scene. For cross-scene operations, use the appropriate adoption APIs.

 @note **Atomicity:** Only hierarchy pointers are modified without
 destroying/recreating node data. Either fully succeeds or leaves scene
 unchanged.

 @note **Transform Preservation:** When \p preserve_world_transform is true,
 leverages Oxygen's cached transform system where world transforms remain valid
 until the next Update() cycle, enabling accurate preservation across hierarchy
 changes without expensive parent chain traversal.

 @param node Root of hierarchy to reparent (must be in this scene)
 @param new_parent New parent node (must be in this scene)
 @param preserve_world_transform If true, adjusts local transform to maintain
 world position using cached world transform values
 @return true if operation succeeded, false if invalid nodes or would create
 cycle

 @see MakeNodeRoot() for moving nodes to become root nodes
 @see AdoptNode() for adopting nodes from other scenes with new parents
 @see LinkChild() for the underlying hierarchy linking operation
 @see PreserveWorldTransform() for the transform preservation implementation
*/
auto Scene::ReparentNode(const SceneNode& node, const SceneNode& new_parent,
  bool preserve_world_transform) noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndMine(node), [&](const SafeCallState& node_state) -> bool {
      return SafeCall(NodeIsValidAndMine(new_parent),
        [&](const SafeCallState& parent_state) -> bool {
          // Check for potential cycles before making any changes
          if (WouldCreateCycle(node, new_parent)) {
            return false;
          } // Preserve world transform before hierarchy changes if requested
          if (preserve_world_transform) {
            PreserveWorldTransformForReparenting(
              node_state.node_impl, parent_state.node_impl);
          } // Unlink the node from its current parent (or root nodes if it's a
          // root)
          if (node.IsRoot()) {
            RemoveRootNode(node.GetHandle());
          } else {
            // Mark old parent transform dirty since it's losing a child
            auto old_parent_opt = node.GetParent();
            if (old_parent_opt.has_value()) {
              auto old_parent_impl_opt = GetNodeImpl(*old_parent_opt);
              if (old_parent_impl_opt.has_value()) {
                old_parent_impl_opt->get().MarkTransformDirty();
              }
            }
            UnlinkNode(node.GetHandle(), node_state.node_impl);
          }

          // Link the node as a child of the new parent
          LinkChild(new_parent.GetHandle(), parent_state.node_impl,
            node.GetHandle(), node_state.node_impl);

          // Mark new parent transform dirty since it's gaining a child
          parent_state.node_impl->MarkTransformDirty();

          // Mark transforms dirty if not preserving world transform
          if (!preserve_world_transform) {
            MarkSubtreeTransformDirty(node.GetHandle());
          }

          return true;
        });
    });
}

/*!
 Preserves world transform during reparenting by calculating the local transform
 needed to maintain the same world position under a new parent.

 This method uses Oxygen's cached transform system to capture the node's current
 world transform, then calculates what local transform is needed relative to the
 new parent to maintain the same world position. This ensures visual continuity
 during reparenting operations.

 The calculation uses inverse transforms:
 - local_position = inverse(parent_world_transform) * world_position
 - local_rotation = inverse(parent_world_rotation) * world_rotation
 - local_scale = world_scale / parent_world_scale
 @param node_impl Pointer to the node's implementation
 @param new_parent_impl Pointer to the new parent's implementation
*/
void Scene::PreserveWorldTransformForReparenting(
  SceneNodeImpl* node_impl, SceneNodeImpl* new_parent_impl) noexcept
{
  auto& transform_component = node_impl->GetComponent<TransformComponent>();
  auto& new_parent_transform
    = new_parent_impl->GetComponent<TransformComponent>();

  // Skip preservation if either node's transforms are dirty (never been
  // updated) This handles cases where newly created nodes are immediately
  // reparented
  if (node_impl->IsTransformDirty() || new_parent_impl->IsTransformDirty()) {
    return;
  }

  // Capture cached world transform (valid until next Update() cycle)
  const auto world_position = transform_component.GetWorldPosition();
  const auto world_rotation = transform_component.GetWorldRotation();
  const auto world_scale = transform_component.GetWorldScale();

  // Get new parent's world transform
  const auto parent_world_position = new_parent_transform.GetWorldPosition();
  const auto parent_world_rotation = new_parent_transform.GetWorldRotation();
  const auto parent_world_scale = new_parent_transform.GetWorldScale();

  // Calculate local transform needed to maintain world transform under new
  // parent local_position = inverse(parent_world_transform) * world_position
  const auto inverse_parent_rotation = glm::inverse(parent_world_rotation);
  const auto inverse_parent_scale = Vec3(1.0f) / parent_world_scale;

  // Transform world position to local space of new parent
  const auto relative_position = world_position - parent_world_position;
  const auto local_position
    = inverse_parent_rotation * (relative_position * inverse_parent_scale);

  // Calculate local rotation relative to parent
  const auto local_rotation = inverse_parent_rotation * world_rotation;

  // Calculate local scale relative to parent
  const auto local_scale = world_scale * inverse_parent_scale;

  // Set calculated local transform (automatically marks dirty)
  transform_component.SetLocalTransform(
    local_position, local_rotation, local_scale);
}

/*!
 Checks if reparenting a node would create a cycle in the hierarchy.

 This method traverses upward from the potential new parent to see if the node
 being reparented appears in the ancestor chain. If it does, then making the
 node a child of new_parent would create a cycle.

 For example, if we have: A -> B -> C and we try to reparent A under C,
 this would create a cycle: C -> A -> B -> C.

 @param node The node that would be reparented
 @param new_parent The potential new parent
 @return true if reparenting would create a cycle, false if safe
*/
auto Scene::WouldCreateCycle(
  const SceneNode& node, const SceneNode& new_parent) const noexcept -> bool
{
  // A node cannot be its own parent
  if (node.GetHandle() == new_parent.GetHandle()) {
    return true;
  }

  // Traverse up the ancestor chain from new_parent
  auto current_ancestor = new_parent;
  while (current_ancestor.HasParent()) {
    const auto parent_opt = current_ancestor.GetParent();
    if (!parent_opt.has_value()) {
      // Invalid parent found, break to avoid infinite loop
      break;
    }

    current_ancestor = *parent_opt;

    // If we find the node in the ancestor chain, it would create a cycle
    if (current_ancestor.GetHandle() == node.GetHandle()) {
      return true;
    }
  }

  return false;
}
