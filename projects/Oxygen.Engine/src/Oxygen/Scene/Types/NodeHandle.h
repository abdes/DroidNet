//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>

#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Core/Resources.h>

namespace oxygen::scene {

/*!
 A specialized ResourceHandle for scene graph nodes that uses the Custom bits to
 store Scene ID information.

 NodeHandle is a zero-overhead wrapper around ResourceHandle that provides
 semantic meaning to the Custom field by interpreting it as a Scene ID. This
 allows nodes to be associated with specific scenes while maintaining full
 compatibility with the base ResourceHandle infrastructure.

 ## Key Features:
 - **Unique Identification**: Because it carries within it the Scene ID,
   NodeHandle uniquely identifies a SceneNode resource in the engine.
 - **Zero Memory Overhead**: Same size and alignment as ResourceHandle
 - **Scene Association**: Custom bits store which scene owns this node
 - **Full Compatibility**: Can be used anywhere ResourceHandle is expected
 - **Type Safety**: Provides scene-specific APIs while maintaining handle
   semantics

 ## Scene ID Management:
 - Scene IDs range from 1 to kMaxSceneId (0 is reserved as invalid)
 - kInvalidSceneId (0) indicates no scene association
 - Scene IDs are automatically managed by the Scene class

 ## Usage Examples:
 ```cpp
 // Create a node handle with a specific scene ID
 NodeHandle handle(node_index, scene_id);

 // Check if node belongs to a scene
 if (handle.BelongsToScene(my_scene_id)) {
     // Process node...
 }

 // Convert from ResourceHandle with scene assignment
 ResourceHandle base_handle = GetSomeHandle();
 NodeHandle node_handle(base_handle, scene_id);

 // Use in unordered containers (hash specialization provided)
 std::unordered_map<NodeHandle, NodeData> node_map;
 ```

 ## Thread Safety:
 NodeHandle itself is not thread-safe for modification, but can be safely copied
 and read from multiple threads. Scene ID management is handled thread-safely by
 the Scene class.

 @note Conversion from ResourceHandle always sets Scene ID to kInvalidSceneId
       unless explicitly provided via constructor parameter.

 @see ResourceHandle for base handle functionality
 @see Scene for scene ID allocation and management
*/
class NodeHandle : public ResourceHandle {
public:
  static constexpr ResourceHandle::ResourceTypeT ResourceTypeId
    = oxygen::IndexOf<SceneNode, ResourceTypeList>::value;
  using SceneId = CustomT;

  //! Maximum valid Scene ID value (255 for 8-bit Custom field).
  constexpr static SceneId kMaxSceneId = kCustomMax;

  //! Invalid/unassigned Scene ID (0 is reserved as invalid).
  constexpr static SceneId kInvalidSceneId = 0;

  //! Default constructor creates an invalid node handle with no scene
  //! association
  constexpr NodeHandle()
    : ResourceHandle(kInvalidIndex)
  {
  }

  //! Creates a node handle with the given \p index and invalid scene ID.
  constexpr NodeHandle(IndexT index)
    : NodeHandle(index, kInvalidSceneId)
  {
  }
  //! Creates a node handle with the given \p index and \p scene_id.
  constexpr NodeHandle(IndexT index, SceneId scene_id)
    : ResourceHandle(index, ResourceTypeId)
  {
    SetSceneId(scene_id);
  }

  //! Explicit conversion from ResourceHandle with scene ID assignment.
  /*!
   Creates a NodeHandle from an existing ResourceHandle while assigning the
   specified scene ID. The resource type of the original handle must match this
   handle expected resource type.
  */
  constexpr NodeHandle(const ResourceHandle& handle, SceneId scene_id)
    : ResourceHandle(handle)
  {
    assert(handle.ResourceType() == ResourceTypeId);

    SetSceneId(scene_id);
  }

  //! Explicit move conversion from ResourceHandle with scene ID assignment.
  /*!
   Moves an existing ResourceHandle into a new NodeHandle, while assigning the
   specified scene ID. The resource type of the original handle must match this
   handle expected resource type.
  */
  constexpr NodeHandle(ResourceHandle&& handle, SceneId scene_id) noexcept
    : ResourceHandle(std::move(handle))
  {
    assert(ResourceType() == ResourceTypeId);

    SetSceneId(scene_id);
  }

  //! Assignment from ResourceHandle (sets Scene ID to invalid). The resource
  //! type of the original handle must match this handle expected resource type.
  constexpr auto operator=(const ResourceHandle& handle) -> NodeHandle&
  {
    assert(handle.ResourceType() == ResourceTypeId);

    ResourceHandle::operator=(handle);
    SetSceneId(kInvalidSceneId);
    return *this;
  }

  //! Assignment from ResourceHandle (sets Scene ID to invalid). The resource
  //! type of the original handle must match this handle expected resource type.
  constexpr auto operator=(ResourceHandle&& handle) noexcept -> NodeHandle&
  {
    assert(handle.ResourceType() == ResourceTypeId);

    ResourceHandle::operator=(std::move(handle));
    SetSceneId(kInvalidSceneId);
    return *this;
  }

  //! Gets the Scene ID stored in the Custom bits (0 indicates no scene
  //! association).
  [[nodiscard]] constexpr auto GetSceneId() const noexcept { return Custom(); }

  //! Sets the Scene ID in the Custom bits (must be <= kMaxSceneId, 0 indicates
  //! no scene association).
  constexpr auto SetSceneId(SceneId scene_id) noexcept -> void
  {
    SetCustom(scene_id);
  }

  //! Checks if this node handle belongs to the specified scene (\p scene_id).
  [[nodiscard]] constexpr auto BelongsToScene(SceneId scene_id) const noexcept
  {
    return GetSceneId() == scene_id;
  }
};

} // namespace oxygen::scene

// Hash specialization for NodeHandle to enable use in unordered containers
template <> struct std::hash<oxygen::scene::NodeHandle> {
  auto operator()(const oxygen::scene::NodeHandle& handle) const noexcept
  {
    // Delegate to ResourceHandle's hash function since NodeHandle has the exact
    // layout of ResourceHandle
    return std::hash<oxygen::ResourceHandle> {}(handle);
  }
};

//===----------------------------------------------------------------------===//
// Ensure NodeHandle is compatible with ResourceHandle, and does not add
// anything to its memory layout.
//===----------------------------------------------------------------------===//

static_assert(
  sizeof(oxygen::scene::NodeHandle) == sizeof(oxygen::ResourceHandle),
  "NodeHandle must have the same size as ResourceHandle");

static_assert(
  alignof(oxygen::scene::NodeHandle) == alignof(oxygen::ResourceHandle),
  "NodeHandle must have the same alignment as ResourceHandle");

static_assert(std::is_standard_layout_v<oxygen::scene::NodeHandle>,
  "NodeHandle must maintain standard layout");

static_assert(std::is_trivially_destructible_v<oxygen::scene::NodeHandle>,
  "NodeHandle must be trivially destructible like ResourceHandle");

static_assert(
  std::is_convertible_v<oxygen::scene::NodeHandle*, oxygen::ResourceHandle*>,
  "NodeHandle pointer must be convertible to ResourceHandle pointer");

static_assert(std::is_convertible_v<const oxygen::scene::NodeHandle*,
                const oxygen::ResourceHandle*>,
  "const NodeHandle pointer must be convertible to const ResourceHandle "
  "pointer");

// Ensure NodeHandle doesn't add any virtual functions
static_assert(!std::is_polymorphic_v<oxygen::scene::NodeHandle>,
  "NodeHandle must not be polymorphic (no virtual functions)");

// Ensure both types have the same object representation
static_assert(
  std::has_unique_object_representations_v<oxygen::scene::NodeHandle>
    == std::has_unique_object_representations_v<oxygen::ResourceHandle>,
  "NodeHandle and ResourceHandle must have same object representation "
  "guarantees");
