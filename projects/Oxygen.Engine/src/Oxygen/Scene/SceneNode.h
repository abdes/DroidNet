//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <glm/glm.hpp>

#include <Oxygen/Base/Resource.h>
#include <Oxygen/Base/ResourceHandle.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Core/Resources.h>
#include <Oxygen/Core/SafeCall.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

class SceneNode;
class SceneNodeImpl;

namespace detail {
  class TransformComponent; // Forward declaration
} // namespace detail

class Scene; // Forward declaration

//! Lightweight handle to a scene graph node, providing safe API for scene
//! hierarchy, transformation, etc.
/*!
 Scene's hierarchy. All actual node data and relationships are stored in
 SceneNode is a non-owning handle/view that provides access to nodes in a
 SceneNodeImpl objects managed by the Scene's resource table.

 **Key Characteristics**

 - **Mutation Routing**: All scene hierarchy modifications (creation,
   destruction, re-parenting) must go through the Scene class.
 - **No-data policy**: SceneNode does not own the underlying data. SceneNodeImpl
   does, and this allows efficient processing of the data by the engine.

 **Lazy Invalidation**

 When a SceneNodeImpl is removed from the Scene, existing SceneNode handles are
 not immediately invalidated due to the complexity of tracking all copies.
 Instead, handles become invalid lazily when accessed. Operations on invalid
 handles will fail safely, and its validity can be verified using IsValid().

 \note SceneNode is the primary user-facing API for scene graph operations. Use
       Scene methods for creating, destroying, or re-parenting nodes.
*/
class SceneNode : public Object, public Resource<resources::kSceneNode> {
  OXYGEN_TYPED(SceneNode)
public:
  using Flags = SceneNodeImpl::Flags;
  using NodeHandle = ResourceHandle;

  using OptionalRefToImpl
    = std::optional<std::reference_wrapper<SceneNodeImpl>>;
  using OptionalConstRefToImpl
    = std::optional<std::reference_wrapper<const SceneNodeImpl>>;

  using OptionalRefToFlags = std::optional<std::reference_wrapper<Flags>>;
  using OptionalConstRefToFlags
    = std::optional<std::reference_wrapper<const Flags>>;

  /*!
   Forward declaration for Transform interface.
   \see SceneNode::Transform for full documentation.
  */
  class Transform;

  // We make the Scene a friend, so it can invalidate a SceneNode when its
  // data is erased.
  friend class Scene;

  /*!
   Constructs a SceneNode handle for the given resource handle and scene.
   Only Scene should create SceneNode instances.
  */
  OXGN_SCN_API SceneNode(
    const ResourceHandle& handle, std::weak_ptr<Scene> scene_weak);

  ~SceneNode() override = default;

  OXYGEN_DEFAULT_COPYABLE(SceneNode)
  OXYGEN_DEFAULT_MOVABLE(SceneNode)

  //=== Scene Hierarchy ===-------------------------------------------------//

  OXGN_SCN_NDAPI auto GetParent() const noexcept -> std::optional<SceneNode>;

  //! Gets the first child of this node in the scene hierarchy.
  OXGN_SCN_NDAPI auto GetFirstChild() const noexcept
    -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto GetNextSibling() const noexcept
    -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto GetPrevSibling() const noexcept
    -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto HasParent() const noexcept -> bool;
  OXGN_SCN_NDAPI auto HasChildren() const noexcept -> bool;
  OXGN_SCN_NDAPI auto IsRoot() const noexcept -> bool;

  //=== SceneNodeImpl Access ===--------------------------------------------//

  //! Gets a reference to the underlying SceneNodeImpl object if it exists.
  /*!
   \return A reference (through unwrapping the std::reference_wrapper) to the
           underlying SceneNodeImpl object, or std::nullopt if the node is
           invalid or expired.
  */
  OXGN_SCN_NDAPI auto GetObject() const noexcept -> OptionalConstRefToImpl;
  OXGN_SCN_NDAPI auto GetObject() noexcept -> OptionalRefToImpl;

  //=== Scene Node Flags Access ===-----------------------------------------//

  //! Gets a reference to the flags for this SceneNode if the node is alive.
  /*!
   \return A reference (through unwrapping the std::reference_wrapper) to the
           node's Flags, or std::nullopt if the node is invalid or expired.
  */
  OXGN_SCN_NDAPI auto GetFlags() const noexcept -> OptionalConstRefToFlags;
  OXGN_SCN_NDAPI auto GetFlags() noexcept -> OptionalRefToFlags;

  //=== Transform Access ===------------------------------------------------//

  //! Gets a Transform interface for safe transform operations.
  /*!
   The Transform interface provides convenient, type-safe access to the node's
   TransformComponent while respecting the scene's caching and dirty marking
   systems. Unlike direct component access, Transform operations are aware of
   scene hierarchy and provide additional convenience methods.

   \return Transform interface wrapper for this node's transform operations.
   \note If the node has no TransformComponent, operations will be no-ops.
  */
  OXGN_SCN_NDAPI auto GetTransform() noexcept -> Transform;
  OXGN_SCN_NDAPI auto GetTransform() const noexcept -> Transform;

private:
  // We need this to allow Scene to lazily invalidate the node even when it is
  // const.
  void Invalidate() const
  {
    // NOLINTNEXTLINE(*-pro-type-const-cast)
    const_cast<SceneNode*>(this)->Resource::Invalidate();
  }

  [[nodiscard]] auto ValidateForSafeCall() const noexcept
    -> std::optional<std::string>;

  // Logging for SafeCall errors using DLOG_F
  void LogSafeCallError(const char* reason) const noexcept;

  std::weak_ptr<Scene> scene_weak_;

  //=== Validation Helpers ===------------------------------------------------//

  // SafeCallState struct to hold validated pointers and eliminate redundant
  // lookups
  struct SafeCallState {
    Scene* scene { nullptr };
    SceneNode* node = nullptr;
    SceneNodeImpl* node_impl = nullptr;
  };

  template <typename Self, typename Validator, typename Func>
  auto SafeCall(Self* self, Validator validator, Func&& func) const noexcept
  {
    SafeCallState state;
    auto result = oxygen::SafeCall(
      *self,
      [&](auto&& /*self_ref*/) -> std::optional<std::string> {
        return validator(state);
      },
      [func = std::forward<Func>(func), &state](
        auto&& /*self_ref*/) mutable noexcept { return func(state); });

    // Extract the actual value from the oxygen::SafeCall result. This would
    // work with operations that return std::options<T> or bool or a default
    // constructible type where the default constructed value indicates failure.
    if (result.has_value()) {
      return result.value();
    }
    return decltype(func(state)) {}; // Return default-constructed type
  }

  template <typename Validator, typename Func>
  auto SafeCall(Validator&& validator, Func&& func) const noexcept
  {
    return SafeCall(
      this, std::forward<Validator>(validator), std::forward<Func>(func));
  }

  template <typename Validator, typename Func>
  auto SafeCall(Validator&& validator, Func&& func) noexcept
  {
    return SafeCall(
      this, std::forward<Validator>(validator), std::forward<Func>(func));
  }

  // Validators for SafeCall operations
  class BaseNodeValidator;
  friend class BaseNodeValidator; // for access to the scene
  class NodeIsValidValidator;
  class NodeIsValidAndInSceneValidator;

  [[nodiscard]] auto NodeIsValid() const -> NodeIsValidValidator;
  [[nodiscard]] auto NodeIsValidAndInScene() const
    -> NodeIsValidAndInSceneValidator;
};

auto OXGN_SCN_API to_string(const SceneNode& node) noexcept -> std::string;

//=============================================================================
// SceneNode::Transform Implementation
//=============================================================================

/*!
 Scene-aware Transform interface providing safe access to node transformations.

 SceneNode::Transform is a lightweight wrapper that provides convenient,
 type-safe access to a node's TransformComponent while respecting the scene's
 caching mechanisms and hierarchy. Unlike direct component access, Transform
 operations are scene-aware and provide additional convenience methods for
 common transform operations.

 Key Design Principles:
 - **Respect Caching**: Does not force immediate world matrix computation;
   respects the existing dirty marking and caching system
 - **Scene-Aware**: Provides operations that understand scene hierarchy and
   coordinate space conversions
 - **Error Resilient**: Gracefully handles missing components and invalid nodes
   using the SafeCall pattern
 - **Value Semantics**: Lightweight wrapper suitable for temporary usage
 - **Future-Proof**: Designed to accommodate animation, physics, and advanced
   transform operations

 Usage Examples:
 ```cpp
 auto node = scene->CreateNode("MyNode");
 auto transform = node.GetTransform();

 // Basic transform operations
 transform.SetLocalPosition({1, 2, 3});
 transform.SetLocalRotation(glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1,
 0))); transform.SetLocalScale({2, 2, 2});
 // Scene-aware operations
 transform.LookAt({0, 0, 0});  // Point toward origin

 // Safe access to world transforms (computed lazily during scene updates)
 if (auto worldPos = transform.GetWorldPosition()) {
     // Use world position...
 }
 ```

 Thread Safety: Transform operations are not thread-safe. All transform
 operations should be performed on the main thread or properly synchronized.

 Performance: Transform creates minimal overhead as it's a simple reference
 wrapper. Most operations forward directly to TransformComponent methods.

 \note This class is designed as a nested class of SceneNode to provide
       strong encapsulation while maintaining clean public APIs.
*/
class SceneNode::Transform {
public:
  using Vec3 = glm::vec3;
  using Quat = glm::quat;
  using Mat4 = glm::mat4;

  //! Constructs a Transform interface for the given SceneNode.
  /*!
   Creates a Transform interface that operates on the specified SceneNode.
   This allows safe access to the node's TransformComponent and provides
   convenient methods for local and world transformations.

   \param node Reference to the SceneNode this Transform will operate on.

   \note The node reference must remain valid for the lifetime of this
         Transform.
  */
  explicit Transform(SceneNode& node) noexcept
    : node_(&node)
  {
  }

  //=== Local Transform Operations (Forward to TransformComponent) ===------//

  //! Sets all local transformation components atomically.
  /*!
   \param position New local position vector.
   \param rotation New local rotation quaternion (should be normalized).
   \param scale New local scale vector (positive values recommended).
   \return True if the operation succeeded, false if the node is no longer
   valid.
  */
  OXGN_SCN_API auto SetLocalTransform(const Vec3& position,
    const Quat& rotation, const Vec3& scale) noexcept -> bool;

  //! Sets the local position (translation component).
  /*!
   \param position New local position vector in local coordinate space.
   \return True if the operation succeeded, false if the node is no longer
   valid.
  */
  OXGN_SCN_API auto SetLocalPosition(const Vec3& position) noexcept -> bool;

  //! Sets the local rotation (rotation component).
  /*!
   \param rotation New local rotation quaternion (should be normalized).
   \return True if the operation succeeded, false if the node is no longer
   valid.
  */
  OXGN_SCN_API auto SetLocalRotation(const Quat& rotation) noexcept -> bool;

  //! Sets the local scale (scale component).
  /*!
   \param scale New local scale vector (positive values recommended).
   \return True if the operation succeeded, false if the node is no longer
   valid.
  */
  OXGN_SCN_API auto SetLocalScale(const Vec3& scale) noexcept -> bool;

  //=== Local Transform Getters ===---------------------------------------//

  //! Gets the local position (translation component).
  /*!
   \return Optional local position vector, or std::nullopt if the node is no
           longer valid.
  */
  OXGN_SCN_NDAPI auto GetLocalPosition() const noexcept -> std::optional<Vec3>;

  //! Gets the local rotation (rotation component).
  /*!
   \return Optional local rotation quaternion, or std::nullopt if the node is
   no longer valid.
  */
  OXGN_SCN_NDAPI auto GetLocalRotation() const noexcept -> std::optional<Quat>;

  //! Gets the local scale (scale component).
  /*!
   \return Optional local scale vector, or std::nullopt if the node is no
           longer valid.
  */
  OXGN_SCN_NDAPI auto GetLocalScale() const noexcept -> std::optional<Vec3>;

  //=== Transform Operations ===------------------------------------------//

  //! Applies a translation (movement) to the current position.
  /*!
   \param offset Distance to move along each axis.
   \param local If true, offset is rotated by current orientation; if false,
                offset is applied directly in world space.
   \return True if the operation succeeded, false if the node is no longer
   valid.
  */
  OXGN_SCN_API auto Translate(const Vec3& offset, bool local = true) noexcept
    -> bool;

  //! Applies a rotation to the current orientation.
  /*!
   \param rotation Quaternion rotation to apply (should be normalized).
   \param local If true, applies rotation after current rotation (local
   space); if false, applies rotation before current rotation (world space).
   \return True if the operation succeeded, false if the node is no longer
   valid.
  */
  OXGN_SCN_API auto Rotate(const Quat& rotation, bool local = true) noexcept
    -> bool;

  //! Applies a scaling factor to the current scale.
  /*!
   \param scale_factor Multiplicative scale factor for each axis.
   \return True if the operation succeeded, false if the node is no longer
   valid.
  */
  OXGN_SCN_API auto Scale(const Vec3& scale_factor) noexcept -> bool;

  //=== World Transform Access (Respects Caching) ===-----------------------//

  //! Gets the world transformation matrix.
  /*!
   Returns the cached world-space transformation matrix without forcing
   computation. The matrix is computed lazily during scene update passes
   and cached until marked dirty.

   \return Optional world transformation matrix, or std::nullopt if the node
           is no longer valid.
  */
  OXGN_SCN_NDAPI auto GetWorldMatrix() const noexcept -> std::optional<Mat4>;

  //! Extracts the world-space position from the cached world transformation
  //! matrix.
  /*!
   \return Optional world-space position vector, or std::nullopt if the node
           is no longer valid.
  */
  OXGN_SCN_NDAPI auto GetWorldPosition() const noexcept -> std::optional<Vec3>;

  //! Extracts the world-space rotation from the cached world transformation
  //! matrix.
  /*!
   \return Optional world-space rotation quaternion, or std::nullopt if the
           node is no longer valid.
  */
  OXGN_SCN_NDAPI auto GetWorldRotation() const noexcept -> std::optional<Quat>;

  //! Extracts the world-space scale from the cached world transformation
  //! matrix.
  /*!
   \return Optional world-space scale vector, or std::nullopt if the node is
           no longer valid.
  */
  OXGN_SCN_NDAPI auto GetWorldScale() const noexcept -> std::optional<Vec3>;

  //=== Scene-Aware Transform Operations ===--------------------------------//

  //! Orients the node to look at a target position.
  /*!
   Rotates the node so that its forward direction (-Z axis in local space)
   points toward the target position. This sets the local rotation directly
   without attempting to compute inverse parent transforms, as world transform
   computation is deferred and handled by the Scene.

   \param target_position World-space position to look at.
   \param up_direction World-space up direction (default: Y-up).
   \return True if the operation succeeded, false if the node is no longer
   valid.
   \note This computes rotation based on current cached world position.
         For accurate results, ensure scene transforms are up to date.
  */
  OXGN_SCN_API auto LookAt(const Vec3& target_position,
    const Vec3& up_direction = Vec3(0, 1, 0)) noexcept -> bool;

private:
  //! Pointer to the SceneNode this Transform operates on.
  SceneNode* node_;

  //=== Validation Helpers ===------------------------------------------------//

  // SafeCallState struct to hold validated pointers and eliminate redundant
  // lookups
  struct SafeCallState {
    SceneNode* node = nullptr;
    SceneNodeImpl* node_impl = nullptr;
    detail::TransformComponent* transform_component = nullptr;
  };

  // Logging for SafeCall errors using DLOG_F
  OXGN_SCN_API void LogSafeCallError(const char* reason) const noexcept;

  // Validators for SafeCall operations
  class BaseTransformValidator;
  class BasicTransformValidator;
  class CleanTransformValidator;

  // Factory methods for different validators
  [[nodiscard]] auto BasicValidator() const -> BasicTransformValidator;
  [[nodiscard]] auto CleanValidator() const -> CleanTransformValidator;

  template <typename Self, typename Validator, typename Func>
  auto SafeCall(Self* self, Validator validator, Func&& func) const noexcept
  {
    SafeCallState state;
    auto result = oxygen::SafeCall(
      *(self->node_),
      [&](auto&& /*self_ref*/) -> std::optional<std::string> {
        return validator(state);
      },
      [func = std::forward<Func>(func), &state](
        auto&& /*self_ref*/) mutable noexcept { return func(state); });

    // Extract the actual value from the oxygen::SafeCall result. This would
    // work with operations that return std::options<T> or bool or a default
    // constructible type where the default constructed value indicates failure.
    if (result.has_value()) {
      return result.value();
    }
    return decltype(func(state)) {}; // Return default-constructed type
  }

  template <typename Validator, typename Func>
  auto SafeCall(Validator validator, Func&& func) const noexcept
  {
    return SafeCall(this, validator, std::forward<Func>(func));
  }

  template <typename Validator, typename Func>
  auto SafeCall(Validator validator, Func&& func) noexcept
  {
    return SafeCall(this, validator, std::forward<Func>(func));
  }
};

} // namespace oxygen::scene
