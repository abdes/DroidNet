//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <Oxygen/Core/Constants.h>

#include <Oxygen/Base/Resource.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Core/Resources.h>
#include <Oxygen/Core/SafeCall.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>
#include <Oxygen/Scene/Types/Strong.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::data {
class Mesh;
class GeometryAsset;
class MaterialAsset; // forward declaration for material override APIs
} // namespace oxygen::data

namespace oxygen::scene {

struct ActiveMesh;
class SceneNode;
class SceneNodeImpl;

namespace detail {
  class TransformComponent;
  class RenderableComponent;
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
  OXYGEN_POOLED_COMPONENT(SceneNode, ResourceTypeList)

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
  //! Forward declaration for Renderable interface.
  //! @see SceneNode::Renderable for full documentation.
  class Renderable;

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

  // NOLINTNEXTLINE(*-derived-method-shadowing-base-method)
  [[nodiscard]] auto GetHandle() const noexcept -> const NodeHandle&
  {
    // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
    return static_cast<const NodeHandle&>(Resource::GetHandle());
  }

  //=== SceneNodeImpl Access ===----------------------------------------------//

  //! Gets a mutable reference to the underlying SceneNodeImpl object if it
  //! exists.
  OXGN_SCN_NDAPI auto GetImpl() noexcept -> OptionalRefToImpl;

  //=== Scene Node Flags Access ===-------------------------------------------//

  //! Gets a reference to the flags for this SceneNode if the node is alive.
  OXGN_SCN_NDAPI auto GetFlags() noexcept -> OptionalRefToFlags;

  //=== Transform Access ===--------------------------------------------------//

  //! Gets a Transform interface for safe transform operations.
  OXGN_SCN_NDAPI auto GetTransform() noexcept -> Transform;
  OXGN_SCN_NDAPI auto GetTransform() const noexcept -> Transform;

  //=== Renderable Access ===-------------------------------------------------//

  //! Gets a Renderable interface for safe renderable operations (geometry,
  //! LOD, submeshes, overrides, bounds).
  OXGN_SCN_NDAPI auto GetRenderable() noexcept -> Renderable;
  OXGN_SCN_NDAPI auto GetRenderable() const noexcept -> Renderable;

  //=== Camera Attachment ===-------------------------------------------------//

  //! Attaches a camera component to this SceneNode. If a camera already exists,
  //! this will fail.
  OXGN_SCN_API auto AttachCamera(std::unique_ptr<Component> camera) noexcept
    -> bool;

  //! Detaches the camera component from this SceneNode, if present.
  OXGN_SCN_API auto DetachCamera() noexcept -> bool;

  //! Replaces the current camera component with a new one. If no camera exists,
  //! this acts as attach.
  OXGN_SCN_API auto ReplaceCamera(std::unique_ptr<Component> camera) noexcept
    -> bool;

  //! Checks if this SceneNode has an attached camera component.
  OXGN_SCN_NDAPI auto HasCamera() noexcept -> bool;

  //! Gets the attached camera as the specified type T (PerspectiveCamera or
  //! OrthographicCamera).
  /*!
   Returns the attached camera component as the specified type T if present and
   of the correct type. Throws std::runtime_error if the attached camera exists
   but is not of type T.

   @tparam T The camera type to cast to (PerspectiveCamera or
   OrthographicCamera).
   @return An optional reference to the attached camera as type T, or
   std::nullopt if no camera is attached or the requested type does not match
   the actual camera type.

   ### Usage Examples
   ```cpp
   auto cam = node.GetCameraAs<PerspectiveCamera>();
   if (cam) {
     // Use cam->get() as PerspectiveCamera
   }
   ```

   @see GetCamera, AttachCamera, DetachCamera
  */
  template <typename T>
  auto GetCameraAs() noexcept -> std::optional<std::reference_wrapper<T>>
  {
    const auto camera_opt = GetCamera();
    if (!camera_opt) {
      return std::nullopt;
    }
    return camera_opt->get().GetTypeId() == T::ClassTypeId()
      ? std::optional { std::ref(static_cast<T&>(camera_opt->get())) }
      : std::nullopt;
  }

  //=== Light Attachment ===--------------------------------------------------//

  //! Attaches a light component to this SceneNode. If a light already exists,
  //! this will fail.
  OXGN_SCN_API auto AttachLight(std::unique_ptr<Component> light) noexcept
    -> bool;

  //! Detaches the light component from this SceneNode, if present.
  OXGN_SCN_API auto DetachLight() noexcept -> bool;

  //! Replaces the current light component with a new one. If no light exists,
  //! this acts as attach.
  OXGN_SCN_API auto ReplaceLight(std::unique_ptr<Component> light) noexcept
    -> bool;

  //! Checks if this SceneNode has an attached light component.
  OXGN_SCN_NDAPI auto HasLight() noexcept -> bool;

  //! Gets the attached light as the specified type T.
  /*!
   Returns the attached light component as the specified type T if present and
   of the correct type.

   @tparam T The light type to cast to (DirectionalLight, PointLight, or
   SpotLight).
   @return An optional reference to the attached light as type T, or
   std::nullopt if no light is attached or the requested type does not match
   the actual light type.

   ### Usage Examples
   ```cpp
   auto light = node.GetLightAs<PointLight>();
   if (light) {
     // Use light->get() as PointLight
   }
   ```

   @see AttachLight, DetachLight, ReplaceLight
  */
  template <typename T>
  auto GetLightAs() noexcept -> std::optional<std::reference_wrapper<T>>
  {
    const auto light_opt = GetLight();
    if (!light_opt) {
      return std::nullopt;
    }
    return light_opt->get().GetTypeId() == T::ClassTypeId()
      ? std::optional { std::ref(static_cast<T&>(light_opt->get())) }
      : std::nullopt;
  }

  //=== Name Access ===-------------------------------------------------------//

  //! Gets the name of this SceneNode, or an empty string if invalid.
  OXGN_SCN_NDAPI auto GetName() const noexcept -> std::string;

  //! Sets the name of this SceneNode. Returns true if successful.
  OXGN_SCN_NDAPI auto SetName(const std::string& name) noexcept -> bool;

private:
  // We need this to allow Scene to lazily invalidate the node even when it is
  // const.
  auto Invalidate() const -> void
  {
    // NOLINTNEXTLINE(*-pro-type-const-cast)
    const_cast<SceneNode*>(this)->Resource::Invalidate();
  }

  //! Gets the attached camera component if present.
  OXGN_SCN_NDAPI auto GetCamera() noexcept
    -> std::optional<std::reference_wrapper<Component>>;

  //! Gets the attached light component if present.
  OXGN_SCN_NDAPI auto GetLight() noexcept
    -> std::optional<std::reference_wrapper<Component>>;

  // Logging for SafeCall errors
  auto LogSafeCallError(const char* reason) const noexcept -> void;

  std::weak_ptr<const Scene> scene_weak_;

  //=== Validation Helpers ===------------------------------------------------//

  // SafeCallState struct to hold validated pointers and eliminate redundant
  // lookups
  struct SafeCallState {
    const Scene* scene { nullptr };
    SceneNode* node = nullptr;
    SceneNodeImpl* node_impl = nullptr;
  };

  // Void-return overload
  template <typename Self, typename Validator, typename Func>
    requires std::is_void_v<std::invoke_result_t<Func, SafeCallState&>>
  auto SafeCall(Self* self, Validator validator, Func&& func) const noexcept
    -> void
  {
    SafeCallState state {};
    (void)oxygen::SafeCall(
      *self,
      [&](auto&& /*self_ref*/) -> std::optional<std::string> {
        return validator(state);
      },
      [func = std::forward<Func>(func), &state](
        auto&& /*self_ref*/) mutable noexcept -> auto { func(state); });
  }

  // Non-void-return overload
  template <typename Self, typename Validator, typename Func>
    requires(!std::is_void_v<std::invoke_result_t<Func, SafeCallState&>>)
  auto SafeCall(Self* self, Validator validator, Func&& func) const noexcept
    -> std::invoke_result_t<Func, SafeCallState&>
  {
    using ReturnT = std::invoke_result_t<Func, SafeCallState&>;
    SafeCallState state {};
    auto result = oxygen::SafeCall(
      *self,
      [&](auto&& /*self_ref*/) -> std::optional<std::string> {
        return validator(state);
      },
      [func = std::forward<Func>(func), &state](
        auto&& /*self_ref*/) mutable noexcept -> auto { return func(state); });

    if (result.has_value()) {
      return result.value();
    }
    return ReturnT {};
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

OXGN_SCN_NDAPI auto to_string(const SceneNode& node) noexcept -> std::string;

//==============================================================================
// Base class for SceneNode sub-interfaces
//==============================================================================

template <typename Derived> class SubInterfaceBase {
protected:
  explicit SubInterfaceBase(SceneNode& node) noexcept
    : node_(&node)
  {
  }

  // Access to owning node for derived classes
  SceneNode* node_ { nullptr };

  // Void-return overload
  template <typename Self, typename Validator, typename Func>
    requires std::is_void_v<std::invoke_result_t<Func,
      typename std::remove_pointer_t<Self>::SafeCallState&>>
  auto SafeCall(Self* self, Validator validator, Func&& func) const noexcept
    -> void
  {
    using DerivedT = std::remove_pointer_t<Self>;
    using State = DerivedT::SafeCallState;
    State state {};

    (void)oxygen::SafeCall(
      *(self->node_),
      [&](auto&& /*self_ref*/) -> std::optional<std::string> {
        return validator(state);
      },
      [func = std::forward<Func>(func), &state](
        auto&& /*self_ref*/) mutable noexcept -> auto { func(state); });
  }

  // Non-void-return overload
  template <typename Self, typename Validator, typename Func>
    requires(!std::is_void_v<std::invoke_result_t<Func,
        typename std::remove_pointer_t<Self>::SafeCallState&>>)
  auto SafeCall(Self* self, Validator validator, Func&& func) const noexcept
    -> std::invoke_result_t<Func,
      typename std::remove_pointer_t<Self>::SafeCallState&>
  {
    using DerivedT = std::remove_pointer_t<Self>;
    using State = DerivedT::SafeCallState;
    using ReturnT = std::invoke_result_t<Func, State&>;
    State state {};

    auto result = oxygen::SafeCall(
      *(self->node_),
      [&](auto&& /*self_ref*/) -> std::optional<std::string> {
        return validator(state);
      },
      [func = std::forward<Func>(func), &state](
        auto&& /*self_ref*/) mutable noexcept -> auto { return func(state); });

    if (result.has_value()) {
      return result.value();
    }
    return ReturnT {};
  }

  template <typename Validator, typename Func>
  auto SafeCall(Validator validator, Func&& func) const noexcept
  {
    return SafeCall(
      static_cast<const Derived*>(this), validator, std::forward<Func>(func));
  }

  template <typename Validator, typename Func>
  auto SafeCall(Validator validator, Func&& func) noexcept
  {
    return SafeCall(
      static_cast<Derived*>(this), validator, std::forward<Func>(func));
  }
};

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
 transform.SetLocalPosition({ 1, 2, 3 });
 transform.SetLocalRotation(
   glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0)));
 transform.SetLocalScale({ 2, 2, 2 });
 // Scene-aware operations
 transform.LookAt({ 0, 0, 0 }); // Point toward origin

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
class SceneNode::Transform : public SubInterfaceBase<SceneNode::Transform> {
public:
  // Bring SafeCall from base into scope
  using SubInterfaceBase::SafeCall;

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
    : SubInterfaceBase(node)
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
    const Vec3& up_direction = ::oxygen::space::look::Up) noexcept -> bool;

private:
  //=== Validation Helpers ===------------------------------------------------//

  // SafeCallState struct to hold validated pointers and eliminate redundant
  // lookups
public:
  struct SafeCallState {
    SceneNode* node = nullptr;
    SceneNodeImpl* node_impl = nullptr;
    detail::TransformComponent* transform_component = nullptr;
  };

  // Logging for SafeCall errors
  OXGN_SCN_API auto LogSafeCallError(const char* reason) const noexcept -> void;

  // Validators for SafeCall operations
  class BaseTransformValidator;
  class BasicTransformValidator;
  class CleanTransformValidator;

  // Factory methods for different validators
  [[nodiscard]] auto BasicValidator() const -> BasicTransformValidator;
  [[nodiscard]] auto CleanValidator() const -> CleanTransformValidator;
};

//==============================================================================
// SceneNode::Renderable Declaration
//==============================================================================

/*!
 Scene-aware Renderable interface providing safe access to geometry, LOD,
 submesh visibility and material overrides, and derived bounds.

 SceneNode::Renderable is a lightweight wrapper that gives convenient,
 type-safe access to a node's RenderableComponent, mirroring the Transform
 pattern. All operations validate node/component state using SafeCall and
 never throw; failures return false or empty optionals.

 ### Highlights

 - Encapsulated: keeps SceneNode API small; no component types leak.
 - Safe: resilient to invalid nodes or missing components.
 - Cohesive: groups geometry/LOD/submesh/bounds utilities in one place.

 Thread-safety: same as SceneNode; not thread-safe by default.
*/
class SceneNode::Renderable : public SubInterfaceBase<SceneNode::Renderable> {
public:
  // Bring SafeCall from base into scope
  using SubInterfaceBase<SceneNode::Renderable>::SafeCall;
  using GeometryAssetPtr = std::shared_ptr<const data::GeometryAsset>;
  using MaterialAssetPtr = std::shared_ptr<const data::MaterialAsset>;

  //! Constructs a Renderable interface for the given SceneNode.
  explicit Renderable(SceneNode& node) noexcept
    : SubInterfaceBase(node)
  {
  }

  //=== Geometry API ===-----------------------------------------------------//
  //! Detaches the RenderableComponent if present.
  OXGN_SCN_API auto Detach() noexcept -> bool;

  //! Sets/attaches geometry to the node's Renderable component.
  OXGN_SCN_API void SetGeometry(GeometryAssetPtr geometry);

  //! Gets attached geometry; returns empty shared_ptr if none.
  OXGN_SCN_NDAPI auto GetGeometry() const noexcept -> GeometryAssetPtr;

  //! Returns true if the node has a RenderableComponent.
  OXGN_SCN_NDAPI auto HasGeometry() const noexcept -> bool;

  //=== LOD policy and selection ============================================//

  OXGN_SCN_NDAPI auto UsesFixedPolicy() const noexcept -> bool;
  OXGN_SCN_NDAPI auto UsesDistancePolicy() const noexcept -> bool;
  OXGN_SCN_NDAPI auto UsesScreenSpaceErrorPolicy() const noexcept -> bool;

  // LOD policy setters (mirror RenderableComponent)
  OXGN_SCN_API void SetLodPolicy(FixedPolicy p);
  OXGN_SCN_API void SetLodPolicy(DistancePolicy p);
  OXGN_SCN_API void SetLodPolicy(ScreenSpaceErrorPolicy p);

  // Select active LOD using strong types (mirror RenderableComponent)
  OXGN_SCN_API void SelectActiveMesh(NormalizedDistance d) const noexcept;
  OXGN_SCN_API void SelectActiveMesh(ScreenSpaceError e) const noexcept;

  OXGN_SCN_NDAPI auto GetActiveMesh() const noexcept
    -> std::optional<ActiveMesh>;
  OXGN_SCN_NDAPI auto GetActiveLodIndex() const noexcept
    -> std::optional<std::size_t>;
  OXGN_SCN_NDAPI auto EffectiveLodCount() const noexcept -> std::size_t;

  //=== Bounds ===============================================================//

  //! Aggregated world bounding sphere (center.xyz, radius.w).
  OXGN_SCN_NDAPI auto GetWorldBoundingSphere() const noexcept -> ::oxygen::Vec4;

  //! Hook to notify the component that world transform changed.
  OXGN_SCN_API void OnWorldTransformUpdated(const Mat4& world);
  //! On-demand per-submesh world AABB for current LOD.
  OXGN_SCN_NDAPI auto GetWorldSubMeshBoundingBox(
    std::size_t submesh_index) const noexcept
    -> std::optional<std::pair<Vec3, Vec3>>;

  //=== Submesh visibility and materials ====================================//

  OXGN_SCN_NDAPI auto IsSubmeshVisible(
    std::size_t lod, std::size_t submesh_index) const noexcept -> bool;
  OXGN_SCN_API void SetSubmeshVisible(
    std::size_t lod, std::size_t submesh_index, bool visible) noexcept;
  OXGN_SCN_API void SetAllSubmeshesVisible(bool visible) noexcept;

  OXGN_SCN_API void SetMaterialOverride(std::size_t lod,
    std::size_t submesh_index, MaterialAssetPtr material) noexcept;
  OXGN_SCN_API void ClearMaterialOverride(
    std::size_t lod, std::size_t submesh_index) noexcept;
  OXGN_SCN_NDAPI auto ResolveSubmeshMaterial(std::size_t lod,
    std::size_t submesh_index) const noexcept -> MaterialAssetPtr;

  struct SafeCallState {
    SceneNode* node = nullptr;
    SceneNodeImpl* node_impl = nullptr;
    detail::RenderableComponent* renderable = nullptr;
  };

  // Logging for SafeCall errors
  OXGN_SCN_API auto LogSafeCallError(const char* reason) const noexcept -> void;

  // Validators for SafeCall operations
  class BaseRenderableValidator;
  class NodeInSceneValidator;
  class RequiresRenderableValidator;

  [[nodiscard]] auto NodeInScene() const -> NodeInSceneValidator;
  [[nodiscard]] auto RequiresRenderable() const -> RequiresRenderableValidator;
};

} // namespace oxygen::scene
