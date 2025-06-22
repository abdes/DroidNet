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
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Core/Resources.h>
#include <Oxygen/Core/SafeCall.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

class SceneNode;
class SceneNodeImpl;

namespace detail {
  class TransformComponent;
} // namespace detail

class Scene;

//! Lightweight handle to a scene graph node, providing safe API for scene
//! hierarchy, transformation, etc.
/*!
 Scene's hierarchy. All actual node data and relationships are stored in
 SceneNode is a non-owning handle/view that provides access to nodes in a
 SceneNodeImpl objects managed by the Scene's resource table.

 ### Key Characteristics

 - **Mutation Routing**: All scene hierarchy modifications (creation,
   destruction, re-parenting) must go through the Scene class.
 - **No-data policy**: SceneNode does not own the underlying data. SceneNodeImpl
   does, and this allows efficient processing of the data by the engine.

 ### Lazy Invalidation

 When a SceneNodeImpl is removed from the Scene, existing SceneNode handles are
 not immediately invalidated due to the complexity of tracking all copies.
 Instead, handles become invalid lazily when accessed. Operations on invalid
 handles will fail safely, and its validity can be verified using IsValid().

 @note SceneNode is the primary user-facing API for scene graph operations. Use
 Scene methods for creating, destroying, or re-parenting nodes.
*/
class SceneNode : public Object, public Resource<SceneNode, ResourceTypeList> {
  OXYGEN_TYPED(SceneNode)

public:
  using Flags = SceneNodeImpl::Flags;

  using OptionalRefToImpl
    = std::optional<std::reference_wrapper<SceneNodeImpl>>;
  using OptionalConstRefToImpl
    = std::optional<std::reference_wrapper<const SceneNodeImpl>>;

  using OptionalRefToFlags = std::optional<std::reference_wrapper<Flags>>;
  using OptionalConstRefToFlags
    = std::optional<std::reference_wrapper<const Flags>>;

  //! Forward declaration for Transform interface.
  //! @see SceneNode::Transform for full documentation.
  class Transform;

  // We make the Scene a friend, so it can invalidate a SceneNode when its
  // data is erased.
  friend class Scene;

  //! Default constructor. Creates an \b invalid SceneNode with an \b invalid
  //! handle, and not associated with any scene.
  OXGN_SCN_API SceneNode();

  //! Creates an \b invalid SceneNode, associated with the given \b valid scene,
  //! and an invalid handle.
  explicit OXGN_SCN_API SceneNode(std::weak_ptr<const Scene> scene_weak);

  //! Creates a SceneNode, associated with the given, \b valid scene and with
  //! the given \b valid \p handle.
  OXGN_SCN_API SceneNode(
    std::weak_ptr<const Scene> scene_weak, const NodeHandle& handle);

  ~SceneNode() override = default;

  OXYGEN_DEFAULT_COPYABLE(SceneNode)
  OXYGEN_DEFAULT_MOVABLE(SceneNode)

  //=== Scene Hierarchy ===---------------------------------------------------//

  OXGN_SCN_NDAPI auto IsAlive() const noexcept -> bool;

  OXGN_SCN_NDAPI auto GetParent() noexcept -> std::optional<SceneNode>;

  //! Gets the first child of this node in the scene hierarchy.
  OXGN_SCN_NDAPI auto GetFirstChild() noexcept -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto GetNextSibling() noexcept -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto GetPrevSibling() noexcept -> std::optional<SceneNode>;
  OXGN_SCN_NDAPI auto HasParent() noexcept -> bool;
  OXGN_SCN_NDAPI auto HasChildren() noexcept -> bool;
  OXGN_SCN_NDAPI auto IsRoot() noexcept -> bool;

  //=== NodeHandle Access ===-------------------------------------------------//

  auto GetHandle() const noexcept -> const NodeHandle&
  {
    return static_cast<const NodeHandle&>(Resource::GetHandle());
  }

  //=== SceneNodeImpl Access ===----------------------------------------------//

  //! Gets a mutable reference to the underlying SceneNodeImpl object if it
  //! exists.
  OXGN_SCN_NDAPI auto GetObject() noexcept -> OptionalRefToImpl;

  //=== Scene Node Flags Access ===-------------------------------------------//

  //! Gets a reference to the flags for this SceneNode if the node is alive.
  OXGN_SCN_NDAPI auto GetFlags() noexcept -> OptionalRefToFlags;

  //=== Transform Access ===--------------------------------------------------//

  //! Gets a Transform interface for safe transform operations.
  OXGN_SCN_NDAPI auto GetTransform() noexcept -> Transform;
  OXGN_SCN_NDAPI auto GetTransform() const noexcept -> Transform;

  //=== Name Access ===-------------------------------------------------------//

  //! Gets the name of this SceneNode, or an empty string if invalid.
  OXGN_SCN_NDAPI auto GetName() const noexcept -> std::string;

  //! Sets the name of this SceneNode. Returns true if successful.
  OXGN_SCN_NDAPI auto SetName(const std::string& name) noexcept -> bool;

private:
  // We need this to allow Scene to lazily invalidate the node even when it is
  // const.
  void Invalidate() const
  {
    // NOLINTNEXTLINE(*-pro-type-const-cast)
    const_cast<SceneNode*>(this)->Resource::Invalidate();
  }

  // Logging for SafeCall errors using DLOG_F
  void LogSafeCallError(const char* reason) const noexcept;

  std::weak_ptr<const Scene> scene_weak_ {};

  //=== Validation Helpers ===------------------------------------------------//

  // SafeCallState struct to hold validated pointers and eliminate redundant
  // lookups
  struct SafeCallState {
    const Scene* scene { nullptr };
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

  [[nodiscard]] auto NodeIsValid() -> NodeIsValidValidator;
  [[nodiscard]] auto NodeIsValidAndInScene() -> NodeIsValidAndInSceneValidator;
};

auto OXGN_SCN_API to_string(const SceneNode& node) noexcept -> std::string;

//==============================================================================
// SceneNode::Transform Implementation
//==============================================================================

/*!
 Scene-aware Transform interface providing safe access to node transformations.

 SceneNode::Transform is a lightweight wrapper that provides convenient,
 type-safe access to a node's TransformComponent while respecting the scene's
 caching mechanisms and hierarchy. Unlike direct component access, Transform
 operations are scene-aware and provide additional convenience methods for
 common transform operations.

 ### Key Design Principles

 - **Respect Caching**: Does not force immediate world matrix computation;
   respects the existing dirty marking and caching system
 - **Scene-Aware**: Provides operations that understand scene hierarchy and
   coordinate space conversions
 - **Error Resilient**: Gracefully handles missing components and invalid nodes
   using the SafeCall pattern
 - **Value Semantics**: Lightweight wrapper suitable for temporary usage
 - **Future-Proof**: Designed to accommodate animation, physics, and advanced
   transform operations

 ### Usage Examples

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

 @note This class is designed as a nested class of SceneNode to provide strong
 encapsulation while maintaining clean public APIs.
*/
class SceneNode::Transform {
public:
  using Vec3 = glm::vec3;
  using Quat = glm::quat;
  using Mat4 = glm::mat4;

  //! Constructs a Transform interface for the given SceneNode.
  /*!
   Creates a Transform interface that operates on the specified SceneNode. This
   allows safe access to the node's TransformComponent and provides convenient
   methods for local and world transformations.

   @param node Reference to the SceneNode this Transform will operate on.

   @note The node reference must remain valid for the lifetime of this
   Transform.
  */
  explicit Transform(SceneNode& node) noexcept
    : node_(&node)
  {
  }

  //=== Local Transform Operations (Forward to TransformComponent) ===--------//

  //! Sets all local transformation components atomically.
  OXGN_SCN_API auto SetLocalTransform(const Vec3& position,
    const Quat& rotation, const Vec3& scale) noexcept -> bool;

  //! Sets the local position (translation component).
  OXGN_SCN_API auto SetLocalPosition(const Vec3& position) noexcept -> bool;

  //! Sets the local rotation (rotation component).
  OXGN_SCN_API auto SetLocalRotation(const Quat& rotation) noexcept -> bool;

  //! Sets the local scale (scale component).
  OXGN_SCN_API auto SetLocalScale(const Vec3& scale) noexcept -> bool;

  //=== Local Transform Getters ===-------------------------------------------//

  //! Gets the local position (translation component).
  OXGN_SCN_NDAPI auto GetLocalPosition() const noexcept -> std::optional<Vec3>;

  //! Gets the local rotation (rotation component).
  OXGN_SCN_NDAPI auto GetLocalRotation() const noexcept -> std::optional<Quat>;

  //! Gets the local scale (scale component).
  OXGN_SCN_NDAPI auto GetLocalScale() const noexcept -> std::optional<Vec3>;

  //=== Transform Operations ===----------------------------------------------//

  //! Applies a translation (movement) to the current position.
  OXGN_SCN_API auto Translate(const Vec3& offset, bool local = true) noexcept
    -> bool;

  //! Applies a rotation to the current orientation.
  OXGN_SCN_API auto Rotate(const Quat& rotation, bool local = true) noexcept
    -> bool;

  //! Applies a scaling factor to the current scale.
  OXGN_SCN_API auto Scale(const Vec3& scale_factor) noexcept -> bool;

  //=== World Transform Access (Respects Caching) ===-------------------------//

  //! Gets the world transformation matrix.
  OXGN_SCN_NDAPI auto GetWorldMatrix() const noexcept -> std::optional<Mat4>;

  //! Extracts the world-space position from the cached world transformation
  //! matrix.
  OXGN_SCN_NDAPI auto GetWorldPosition() const noexcept -> std::optional<Vec3>;

  //! Extracts the world-space rotation from the cached world transformation
  //! matrix.
  OXGN_SCN_NDAPI auto GetWorldRotation() const noexcept -> std::optional<Quat>;

  //! Extracts the world-space scale from the cached world transformation
  //! matrix.
  OXGN_SCN_NDAPI auto GetWorldScale() const noexcept -> std::optional<Vec3>;

  //=== Scene-Aware Transform Operations ===----------------------------------//

  //! Orients the node to look at a target position.
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
