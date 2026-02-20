//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Detail/GraphData.h>
#include <Oxygen/Scene/Internal/IMutationCollector.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Types/Flags.h>
#include <stdexcept>

using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;
using oxygen::scene::detail::GraphData;

// Camera type aliases for brevity and clarity
using PerspectiveCamera = oxygen::scene::PerspectiveCamera;
using OrthographicCamera = oxygen::scene::OrthographicCamera;

using DirectionalLight = oxygen::scene::DirectionalLight;
using PointLight = oxygen::scene::PointLight;
using SpotLight = oxygen::scene::SpotLight;

// =============================================================================
// SceneNode Validator Implementations
// =============================================================================

auto SceneNode::LogSafeCallError(
  [[maybe_unused]] const char* reason) const noexcept -> void
{
  try {
    DLOG_F(ERROR, "Operation on SceneNode {} failed: {}",
      nostd::to_string(*this), reason);
  } catch (...) {
    // If logging fails, we can do nothing about it
    (void)0;
  }
}

//! Base validator for Transform operations, following Scene's pattern
class SceneNode::BaseNodeValidator {
public:
  explicit BaseNodeValidator(SceneNode& node) noexcept
    : node_(&node)
  {
  }

protected:
  [[nodiscard]] auto GetNode() const noexcept -> const SceneNode&
  {
    return *node_;
  }

  [[nodiscard]] auto GetScene() const noexcept -> const Scene*
  {
    DCHECK_F(!node_->scene_weak_.expired());
    return node_->scene_weak_.lock().get();
  }

  [[nodiscard]] auto GetResult() noexcept -> std::optional<std::string>
  {
    return std::move(result_);
  }

  template <typename State> auto CheckSceneNotExpired(State& state) -> bool
  {
    if (node_->scene_weak_.expired()) [[unlikely]] {
      result_ = fmt::format(
        "node({}) is invalid", nostd::to_string(GetNode().GetHandle()));
      return false;
    }
    state.scene = node_->scene_weak_.lock().get();
    result_.reset();
    return true;
  }

  template <typename State> auto CheckNodeIsValid(State& state) -> bool
  {
    if (!GetNode().IsValid()) [[unlikely]] {
      result_ = fmt::format(
        "node({}) is invalid", nostd::to_string(GetNode().GetHandle()));
      return false;
    }
    state.node = node_;
    result_.reset();
    return true;
  }

  template <typename State> auto PopulateStateWithNodeImpl(State& state) -> bool
  {
    // Then check if the node is still in the scene node table, and retrieve
    // its implementation object.
    try {
      state.scene = GetScene();
      auto* impl_ref = &state.scene->GetNodeImplRefUnsafe(node_->GetHandle());
      // Cast away const to allow modifying the SceneNodeImpl. All validators
      // for SceneNode are non-cost anyway due to lazy invalidation.
      state.node_impl = const_cast<SceneNodeImpl*>(impl_ref);
      result_.reset();
      return true;
    } catch (const std::exception&) {
      result_ = fmt::format("node({}) is no longer in scene",
        nostd::to_string(GetNode().GetHandle()));
      node_->Invalidate();
      return false;
    }
  }

private:
  std::optional<std::string> result_;
  SceneNode* node_;
};

//! A validator that checks if a SceneNode is valid and belongs to the \p scene.
class SceneNode::NodeIsValidValidator : public BaseNodeValidator {

public:
  explicit NodeIsValidValidator(SceneNode& node) noexcept
    : BaseNodeValidator(node)
  {
  }

  template <typename State>
  auto operator()(State& state) -> std::optional<std::string>
  {
    if (CheckSceneNotExpired(state) && CheckNodeIsValid(state)) [[likely]] {
      return std::nullopt;
    }
    return GetResult();
  }
};

//! A validator that checks if a SceneNode is valid and belongs to the \p scene.
class SceneNode::NodeIsValidAndInSceneValidator : public NodeIsValidValidator {
public:
  using NodeIsValidValidator::NodeIsValidValidator;

  template <typename State>
  auto operator()(State& state) -> std::optional<std::string>
  {
    if (auto result = NodeIsValidValidator::operator()(state)) {
      return result;
    }
    return PopulateStateWithNodeImpl(state) ? std::nullopt : GetResult();
  }
};

auto SceneNode::NodeIsValid() const -> NodeIsValidValidator
{
  // Cast away const to handle lazy invalidation (an internal detail).
  return NodeIsValidValidator {
    const_cast<SceneNode&>(*this) // NOLINT(*-const-cast)
  };
}

auto SceneNode::NodeIsValidAndInScene() const -> NodeIsValidAndInSceneValidator
{
  // Cast away const to handle lazy invalidation (an internal detail).
  return NodeIsValidAndInSceneValidator {
    const_cast<SceneNode&>(*this) // NOLINT(*-const-cast)
  };
}

// =============================================================================
// SceneNode Implementations
// =============================================================================

/*!
 This constructor creates an invalid SceneNode that cannot be used for any
 operations. It is primarily intended as a placeholder node, and for use by
 std:: containers.
*/
SceneNode::SceneNode()
  : Resource(
      NodeHandle { NodeHandle::kInvalidIndex, NodeHandle::kInvalidSceneId })
{
}

/*!
 This constructor creates an invalid SceneNode that cannot be used for any
 operations. It is primarily intended as a placeholder node, and for use by
 std:: containers. It is still associated with the given scene, which must not
 be expired.

 @param scene_weak A weak pointer (not expired) to the Scene this node is
 associated with.
*/
SceneNode::SceneNode(std::weak_ptr<const Scene> scene_weak)
  : Resource(
      NodeHandle { NodeHandle::kInvalidIndex, scene_weak.lock()->GetId() })
  , scene_weak_(std::move(scene_weak))
{
}

/*!
  This constructor creates a SceneNode that is associated with the given scene
  and has a valid handle. The node is expected to be valid and in the scene.

  @param scene_weak A weak pointer (not expired) to the Scene this node is
  @param handle A valid NodeHandle for this SceneNode.
  associated with.
*/
SceneNode::SceneNode(
  std::weak_ptr<const Scene> scene_weak, const NodeHandle& handle)
  : Resource(handle)
  , scene_weak_(std::move(scene_weak))
{
  DCHECK_F(handle.IsValid(), "expecting a valid NodeHandle");
  DCHECK_F(!scene_weak_.expired(), "expecting a non-expired Scene");
}

auto oxygen::scene::to_string(const SceneNode& node) noexcept -> std::string
{
  if (!node.IsValid()) {
    return "SN(invalid)";
  }
#if !defined(NDEBUG)
  // In debug mode, include the name of the node if it exists
  return fmt::format(
    "SN({}, n='{}')", to_string_compact(node.GetHandle()), node.GetName());
#else // defined(NDEBUG)
  return fmt::format("SN({})", to_string_compact(node.GetHandle()));
#endif // defined(NDEBUG)
}

/*!
 @return A mutable reference (through unwrapping the std::reference_wrapper) to
 the underlying SceneNodeImpl object, or std::nullopt if the node is invalid or
 expired.
*/
auto SceneNode::GetImpl() noexcept -> OptionalRefToImpl
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> OptionalRefToImpl {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      const auto& impl_ref = state.scene->GetNodeImplRefUnsafe(GetHandle());
      return {
        // Cast away const to handle lazy invalidation (an internal detail).
        // NOLINTNEXTLINE(*-const-cast)
        std::reference_wrapper(const_cast<SceneNodeImpl&>(impl_ref))
      };
    });
}

/*!
 @return A reference (through unwrapping the std::reference_wrapper) to the
 node's Flags, or std::nullopt if the node is invalid or expired.
*/
auto SceneNode::GetFlags() noexcept -> OptionalRefToFlags
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> OptionalRefToFlags {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.node_impl->GetFlags();
    });
}

auto SceneNode::IsAlive() const noexcept -> bool
{
  return IsValid() && !scene_weak_.expired()
    && scene_weak_.lock()->Contains(*this);
}

auto SceneNode::GetParent() noexcept -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->GetParentUnsafe(*this, state.node_impl);
    });
}

auto SceneNode::GetFirstChild() noexcept -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->GetFirstChildUnsafe(*this, state.node_impl);
    });
}

auto SceneNode::GetNextSibling() noexcept -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->GetNextSiblingUnsafe(*this, state.node_impl);
    });
}

auto SceneNode::GetPrevSibling() noexcept -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->GetPrevSiblingUnsafe(*this, state.node_impl);
    });
}

auto SceneNode::HasParent() noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->HasParentUnsafe(*this, state.node_impl);
    });
}

auto SceneNode::HasChildren() noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->HasChildrenUnsafe(*this, state.node_impl);
    });
}

auto SceneNode::IsRoot() noexcept -> bool { return !HasParent(); }

/*!
 The Transform interface provides convenient, type-safe access to the node's
 TransformComponent while respecting the scene's caching and dirty marking
 systems. Unlike direct component access, Transform operations are aware of
 scene hierarchy and provide additional convenience methods.

 @return Transform interface wrapper for this node's transform operations.
 @note If the node has no TransformComponent, operations will be no-ops.
*/

auto SceneNode::GetTransform() noexcept -> Transform
{
  return Transform(*this);
}

/*!
 @copydoc SceneNode::GetTransform() const
*/
auto SceneNode::GetTransform() const noexcept -> Transform
{
  return Transform(
    const_cast<SceneNode&>(*this)); // NOLINT(*-pro-type-const-cast)
}

/*! The Renderable interface provides safe access to geometry/submesh/LOD APIs
    without exposing component types. */
auto SceneNode::GetRenderable() noexcept -> Renderable
{
  return Renderable(*this);
}

/*! @copydoc SceneNode::GetRenderable() */
auto SceneNode::GetRenderable() const noexcept -> Renderable
{
  return Renderable(
    const_cast<SceneNode&>(*this)); // NOLINT(*-pro-type-const-cast)
}

/*! Gets the name of this SceneNode, or an empty string if invalid. */
auto SceneNode::GetName() const noexcept -> std::string
{
  // We do not use validators and SafeCall here, because that may trigger lazy
  // invalidation, would require a mutable SceneNode.
  try {
    const auto scene = scene_weak_.lock();
    if (!scene) [[unlikely]] {
      return "__not_in_scene__";
    }
    const auto& impl_ref = scene->GetNodeImplRefUnsafe(GetHandle());
    return std::string { impl_ref.GetName() };
  } catch (const std::exception&) {
    return "__not_in_scene__";
  }
}

/*! Sets the name of this SceneNode. Returns true if successful. */
auto SceneNode::SetName(const std::string& name) noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      state.node_impl->SetName(name);
      return true;
    });
}

//=== Camera Attachment ===---------------------------------------------------//

/*!
 Attaches a camera component to this SceneNode. Only one camera component
 (either PerspectiveCamera or OrthographicCamera) can be attached at a time.
 If a camera already exists, this method will fail and return false.

 @param camera Unique pointer to the camera component to attach. Must be a
 PerspectiveCamera or OrthographicCamera. Ownership is transferred.
 @return True if the camera was successfully attached; false if a camera
 already exists, the node is invalid, or the camera type is unsupported.

 ### Usage Examples
 ```cpp
 auto node = scene->CreateNode("CameraNode");
 auto camera = std::make_unique<PerspectiveCamera>(...);
 bool attached = node.AttachCamera(std::move(camera));
 ```

 @note Only one camera component can be attached at a time.
 @warning Passing a null pointer or an unsupported camera type will fail.
 @see DetachCamera, ReplaceCamera, GetCamera
*/
auto SceneNode::AttachCamera(std::unique_ptr<Component> camera) noexcept -> bool
{
  if (!camera) {
    LOG_F(ERROR, "Cannot attach a null camera. SceneNode: {}",
      nostd::to_string(*this));
    return false;
  }

  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      const auto type_id = camera->GetTypeId();
      bool already_exists = false;
      if (type_id == PerspectiveCamera::ClassTypeId()) {
        already_exists = state.node_impl->HasComponent<PerspectiveCamera>();
      } else if (type_id == OrthographicCamera::ClassTypeId()) {
        already_exists = state.node_impl->HasComponent<OrthographicCamera>();
      } else {
        // Only PerspectiveCamera and OrthographicCamera are supported
        LOG_F(ERROR, "Unsupported camera type: {}/{}. SceneNode: {}", type_id,
          camera->GetTypeNamePretty(), nostd::to_string(*this));
        return false;
      }
      if (already_exists) {
        LOG_F(ERROR,
          "SceneNode {} already has a camera component of type {}. Cannot "
          "attach another.",
          nostd::to_string(*this), camera->GetTypeNamePretty());
        return false;
      }

      if (type_id == PerspectiveCamera::ClassTypeId()) {
        state.node_impl->AddComponent<PerspectiveCamera>(std::move(camera));
      } else if (type_id == OrthographicCamera::ClassTypeId()) {
        state.node_impl->AddComponent<OrthographicCamera>(std::move(camera));
      }
      if (const auto collector = state.scene->AsMutationCollector();
        collector != nullptr) {
        collector->CollectCameraChanged(state.node->GetHandle());
      }
      return true;
    });
}

/*!
 Detaches the camera component from this SceneNode, if present.

 @return True if a camera component was detached; false if no camera was
 attached or the node is invalid.

 ### Usage Examples
 ```cpp
 auto node = scene->CreateNode("CameraNode");
 node.AttachCamera(std::make_unique<PerspectiveCamera>(...));
 bool detached = node.DetachCamera();
 ```

 @note Safe to call even if no camera is attached.
 @see AttachCamera, ReplaceCamera, GetCamera
*/
auto SceneNode::DetachCamera() noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      bool removed = false;
      if (state.node_impl->HasComponent<PerspectiveCamera>()) {
        state.node_impl->RemoveComponent<PerspectiveCamera>();
        removed = true;
      }
      if (state.node_impl->HasComponent<OrthographicCamera>()) {
        state.node_impl->RemoveComponent<OrthographicCamera>();
        removed = true;
      }
      if (removed) {
        if (const auto collector = state.scene->AsMutationCollector();
          collector != nullptr) {
          collector->CollectCameraChanged(state.node->GetHandle());
        }
      }
      return removed;
    });
}

/*!
 Replaces the current camera component with a new one. If no camera exists,
 this acts as an attach operation. Only one camera component can be present
 at a time.

 @param camera Unique pointer to the new camera component. Must be a
 PerspectiveCamera or OrthographicCamera. Ownership is transferred.
 @return True if the camera was successfully replaced or attached; false if
 the node is invalid, the camera is null, or the camera type is unsupported.

 ### Usage Examples
 ```cpp
 auto node = scene->CreateNode("CameraNode");
 node.AttachCamera(std::make_unique<PerspectiveCamera>(...));
 bool replaced = node.ReplaceCamera(std::make_unique<OrthographicCamera>(...));
 ```

 @note If no camera is attached, this method acts like AttachCamera.
 @warning Passing a null pointer or an unsupported camera type will fail.
 @see AttachCamera, DetachCamera, GetCamera
*/
auto SceneNode::ReplaceCamera(std::unique_ptr<Component> camera) noexcept
  -> bool
{
  if (!camera) {
    LOG_F(ERROR, "Cannot attach a null camera. SceneNode: {}",
      nostd::to_string(*this));
    return false;
  }

  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      // Remove any existing camera
      if (state.node_impl->HasComponent<PerspectiveCamera>()) {
        state.node_impl->ReplaceComponent<PerspectiveCamera>(std::move(camera));
        if (const auto collector = state.scene->AsMutationCollector();
          collector != nullptr) {
          collector->CollectCameraChanged(state.node->GetHandle());
        }
        return true;
      }
      if (state.node_impl->HasComponent<OrthographicCamera>()) {
        state.node_impl->ReplaceComponent<OrthographicCamera>(
          std::move(camera));
        if (const auto collector = state.scene->AsMutationCollector();
          collector != nullptr) {
          collector->CollectCameraChanged(state.node->GetHandle());
        }
        return true;
      }
      // If no camera exists, act as AttachCamera
      const auto type_id = camera->GetTypeId();
      if (type_id == PerspectiveCamera::ClassTypeId()) {
        state.node_impl->AddComponent<PerspectiveCamera>(std::move(camera));
      } else if (type_id == OrthographicCamera::ClassTypeId()) {
        state.node_impl->AddComponent<OrthographicCamera>(std::move(camera));
      }
      if (const auto collector = state.scene->AsMutationCollector();
        collector != nullptr) {
        collector->CollectCameraChanged(state.node->GetHandle());
      }
      return true;
    });
}

/*!
 Gets the attached camera component if present.

 @return An optional reference to the attached camera component (either
 PerspectiveCamera or OrthographicCamera), or std::nullopt if no camera is
 attached or the node is invalid.

 ### Usage Examples
 ```cpp
 auto node = scene->CreateNode("CameraNode");
 node.AttachCamera(std::make_unique<PerspectiveCamera>(...));
 auto camera_opt = node.GetCamera();
 if (camera_opt) {
   // Use camera_opt->get()
 }
 ```

 @see AttachCamera, DetachCamera, ReplaceCamera
*/
auto SceneNode::GetCamera() noexcept
  -> std::optional<std::reference_wrapper<Component>>
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept
      -> std::optional<std::reference_wrapper<Component>> {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      if (state.node_impl->HasComponent<PerspectiveCamera>()) {
        return std::ref(state.node_impl->GetComponent<PerspectiveCamera>());
      }
      if (state.node_impl->HasComponent<OrthographicCamera>()) {
        return std::ref(state.node_impl->GetComponent<OrthographicCamera>());
      }
      return std::nullopt;
    });
}

/*!
 Checks if a camera component is attached to this SceneNode.

 @return True if a camera component (PerspectiveCamera or OrthographicCamera) is
 attached; false otherwise or if the node is invalid.

 ### Usage Examples
 ```cpp
 auto node = scene->CreateNode("CameraNode");
 if (!node.HasCamera()) {
   node.AttachCamera(std::make_unique<PerspectiveCamera>(...));
 }
 ```

 @see AttachCamera, DetachCamera, ReplaceCamera, GetCamera
*/
auto SceneNode::HasCamera() noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.node_impl->HasComponent<PerspectiveCamera>()
        || state.node_impl->HasComponent<OrthographicCamera>();
    });
}

//=== Light Attachment ===----------------------------------------------------//

/*!
 Attaches a light component to this SceneNode. Only one light component can be
 attached at a time (DirectionalLight, PointLight, or SpotLight). If a light
 already exists, this method will fail and return false.

 @param light Unique pointer to the light component to attach. Must be a
 DirectionalLight, PointLight, or SpotLight. Ownership is transferred.
 @return True if the light was successfully attached; false if a light already
 exists, the node is invalid, or the light type is unsupported.

 @note Only one light component can be attached at a time.
 @warning Passing a null pointer or an unsupported light type will fail.
 @see DetachLight, ReplaceLight, GetLight
*/
auto SceneNode::AttachLight(std::unique_ptr<Component> light) noexcept -> bool
{
  if (!light) {
    LOG_F(ERROR, "Cannot attach a null light. SceneNode: {}",
      nostd::to_string(*this));
    return false;
  }

  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      const bool already_exists
        = state.node_impl->HasComponent<DirectionalLight>()
        || state.node_impl->HasComponent<PointLight>()
        || state.node_impl->HasComponent<SpotLight>();
      if (already_exists) {
        LOG_F(ERROR,
          "SceneNode {} already has a light component. Cannot attach another.",
          nostd::to_string(*this));
        return false;
      }

      const auto type_id = light->GetTypeId();
      if (type_id == DirectionalLight::ClassTypeId()) {
        state.node_impl->AddComponent<DirectionalLight>(std::move(light));
        if (const auto collector = state.scene->AsMutationCollector();
          collector != nullptr) {
          collector->CollectLightChanged(state.node->GetHandle());
        }
        return true;
      }
      if (type_id == PointLight::ClassTypeId()) {
        state.node_impl->AddComponent<PointLight>(std::move(light));
        if (const auto collector = state.scene->AsMutationCollector();
          collector != nullptr) {
          collector->CollectLightChanged(state.node->GetHandle());
        }
        return true;
      }
      if (type_id == SpotLight::ClassTypeId()) {
        state.node_impl->AddComponent<SpotLight>(std::move(light));
        if (const auto collector = state.scene->AsMutationCollector();
          collector != nullptr) {
          collector->CollectLightChanged(state.node->GetHandle());
        }
        return true;
      }

      LOG_F(ERROR, "Unsupported light type: {}/{}. SceneNode: {}", type_id,
        light->GetTypeNamePretty(), nostd::to_string(*this));
      return false;
    });
}

/*!
 Detaches the light component from this SceneNode, if present.

 @return True if a light component was detached; false if no light was attached
 or the node is invalid.

 @note Safe to call even if no light is attached.
 @see AttachLight, ReplaceLight, GetLight
*/
auto SceneNode::DetachLight() noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      bool removed = false;
      if (state.node_impl->HasComponent<DirectionalLight>()) {
        state.node_impl->RemoveComponent<DirectionalLight>();
        removed = true;
      }
      if (state.node_impl->HasComponent<PointLight>()) {
        state.node_impl->RemoveComponent<PointLight>();
        removed = true;
      }
      if (state.node_impl->HasComponent<SpotLight>()) {
        state.node_impl->RemoveComponent<SpotLight>();
        removed = true;
      }
      if (removed) {
        if (const auto collector = state.scene->AsMutationCollector();
          collector != nullptr) {
          collector->CollectLightChanged(state.node->GetHandle());
        }
      }
      return removed;
    });
}

/*!
 Replaces the current light component with a new one. If no light exists, this
 acts as an attach operation.

 @param light Unique pointer to the new light component. Must be a
 DirectionalLight, PointLight, or SpotLight. Ownership is transferred.
 @return True if the light was successfully replaced or attached; false if the
 node is invalid, the light is null, or the light type is unsupported.

 @note Replacement may change the concrete light type.
 @warning Passing a null pointer or an unsupported light type will fail.
 @see AttachLight, DetachLight, GetLight
*/
auto SceneNode::ReplaceLight(std::unique_ptr<Component> light) noexcept -> bool
{
  if (!light) {
    LOG_F(ERROR, "Cannot attach a null light. SceneNode: {}",
      nostd::to_string(*this));
    return false;
  }

  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      const auto type_id = light->GetTypeId();
      if (type_id != DirectionalLight::ClassTypeId()
        && type_id != PointLight::ClassTypeId()
        && type_id != SpotLight::ClassTypeId()) {
        LOG_F(ERROR, "Unsupported light type: {}/{}. SceneNode: {}", type_id,
          light->GetTypeNamePretty(), nostd::to_string(*this));
        return false;
      }

      if (state.node_impl->HasComponent<DirectionalLight>()) {
        state.node_impl->RemoveComponent<DirectionalLight>();
      }
      if (state.node_impl->HasComponent<PointLight>()) {
        state.node_impl->RemoveComponent<PointLight>();
      }
      if (state.node_impl->HasComponent<SpotLight>()) {
        state.node_impl->RemoveComponent<SpotLight>();
      }

      if (type_id == DirectionalLight::ClassTypeId()) {
        state.node_impl->AddComponent<DirectionalLight>(std::move(light));
        if (const auto collector = state.scene->AsMutationCollector();
          collector != nullptr) {
          collector->CollectLightChanged(state.node->GetHandle());
        }
        return true;
      }
      if (type_id == PointLight::ClassTypeId()) {
        state.node_impl->AddComponent<PointLight>(std::move(light));
        if (const auto collector = state.scene->AsMutationCollector();
          collector != nullptr) {
          collector->CollectLightChanged(state.node->GetHandle());
        }
        return true;
      }
      state.node_impl->AddComponent<SpotLight>(std::move(light));
      if (const auto collector = state.scene->AsMutationCollector();
        collector != nullptr) {
        collector->CollectLightChanged(state.node->GetHandle());
      }
      return true;
    });
}

/*!
 Gets the attached light component if present.

 @return An optional reference to the attached light component (Directional,
 Point, or Spot), or std::nullopt if no light is attached or the node is
 invalid.

 @see AttachLight, DetachLight, ReplaceLight
*/
auto SceneNode::GetLight() noexcept
  -> std::optional<std::reference_wrapper<Component>>
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept
      -> std::optional<std::reference_wrapper<Component>> {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      if (state.node_impl->HasComponent<DirectionalLight>()) {
        return std::ref(state.node_impl->GetComponent<DirectionalLight>());
      }
      if (state.node_impl->HasComponent<PointLight>()) {
        return std::ref(state.node_impl->GetComponent<PointLight>());
      }
      if (state.node_impl->HasComponent<SpotLight>()) {
        return std::ref(state.node_impl->GetComponent<SpotLight>());
      }
      return std::nullopt;
    });
}

/*!
 Checks if a light component is attached to this SceneNode.

 @return True if a light component (DirectionalLight, PointLight, or SpotLight)
 is attached; false otherwise or if the node is invalid.

 @see AttachLight, DetachLight, ReplaceLight, GetLight
*/
auto SceneNode::HasLight() noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.node_impl->HasComponent<DirectionalLight>()
        || state.node_impl->HasComponent<PointLight>()
        || state.node_impl->HasComponent<SpotLight>();
    });
}

//=== Renderable Component ===------------------------------------------------//
//=== Scripting Access ===--------------------------------------------------//

auto SceneNode::GetScripting() noexcept -> Scripting
{
  return Scripting(*this);
}

auto SceneNode::GetScripting() const noexcept -> Scripting
{
  // NOLINTNEXTLINE(*-pro-type-const-cast)
  return Scripting(const_cast<SceneNode&>(*this));
}

//=== Scripting Attachment ===----------------------------------------------//

auto SceneNode::AttachScripting() noexcept -> bool
{
  return SafeCall(NodeIsValidAndInScene(), [&](SafeCallState& state) {
    if (state.node_impl->HasComponent<ScriptingComponent>()) {
      return false;
    }
    auto* mutable_scene = const_cast<Scene*>(state.scene);
    DCHECK_NOTNULL_F(mutable_scene);
    state.node_impl->AddComponent<ScriptingComponent>(
      state.node->GetHandle(), mutable_scene->AsMutationCollector());
    return true;
  });
}

auto SceneNode::DetachScripting() noexcept -> bool
{
  return SafeCall(NodeIsValidAndInScene(), [&](SafeCallState& state) {
    if (!state.node_impl->HasComponent<ScriptingComponent>()) {
      return false;
    }
    state.node_impl->GetComponent<ScriptingComponent>()
      .EmitActiveSlotDeactivations();
    state.node_impl->RemoveComponent<ScriptingComponent>();
    return true;
  });
}

auto SceneNode::HasScripting() const noexcept -> bool
{
  return SafeCall(NodeIsValidAndInScene(), [&](SafeCallState& state) {
    return state.node_impl->HasComponent<ScriptingComponent>();
  });
}

//==============================================================================
// SceneNode::Scripting Implementation
//==============================================================================

class SceneNode::Scripting::RequiresScriptingValidator
  : public SceneNode::BaseNodeValidator {
public:
  using BaseNodeValidator::BaseNodeValidator;
  auto operator()(SafeCallState& state) -> std::optional<std::string>
  {
    if (CheckSceneNotExpired(state) && CheckNodeIsValid(state)
      && PopulateStateWithNodeImpl(state)) [[likely]] {
      if (!state.node_impl->HasComponent<ScriptingComponent>()) {
        return "node has no scripting component";
      }
      state.scripting = &state.node_impl->GetComponent<ScriptingComponent>();
      return std::nullopt;
    }
    return GetResult();
  }
};

auto SceneNode::Scripting::RequiresScripting() const
  -> RequiresScriptingValidator
{
  return RequiresScriptingValidator { *node_ };
}

auto SceneNode::Scripting::AddSlot(
  std::shared_ptr<const data::ScriptAsset> asset) noexcept -> bool
{
  return SafeCall(RequiresScripting(), [&](SafeCallState& state) {
    state.scripting->AddSlot(std::move(asset));
    return true;
  });
}

auto SceneNode::Scripting::RemoveSlot(const Slot& slot) noexcept -> bool
{
  return SafeCall(RequiresScripting(),
    [&](SafeCallState& state) { return state.scripting->RemoveSlot(slot); });
}

auto SceneNode::Scripting::Slots() const noexcept -> std::span<const Slot>
{
  return SafeCall(RequiresScripting(),
    [&](SafeCallState& state) { return state.scripting->Slots(); });
}

auto SceneNode::Scripting::SetParameter(const Slot& slot, std::string_view name,
  data::ScriptParam value) noexcept -> bool
{
  return SafeCall(RequiresScripting(), [&](SafeCallState& state) {
    return state.scripting->SetParameter(slot, name, std::move(value));
  });
}

auto SceneNode::Scripting::TryGetParameter(
  const Slot& slot, std::string_view name) const noexcept
  -> std::optional<std::reference_wrapper<const data::ScriptParam>>
{
  return SafeCall(RequiresScripting(), [&](SafeCallState& state) {
    return state.scripting->TryGetParameter(slot, name);
  });
}

auto SceneNode::Scripting::GetParameter(
  const Slot& slot, std::string_view name) const -> const data::ScriptParam&
{
  if (const auto value = TryGetParameter(slot, name)) {
    return value->get();
  }
  throw std::out_of_range(
    "Scripting parameter '" + std::string(name) + "' was not found");
}

auto SceneNode::Scripting::Parameters(const Slot& slot) const noexcept
  -> EffectiveParametersView
{
  return SafeCall(RequiresScripting(),
    [&](SafeCallState& state) { return state.scripting->Parameters(slot); });
}

auto SceneNode::Scripting::MarkSlotReady(const Slot& slot,
  std::shared_ptr<const scripting::ScriptExecutable> executable) noexcept
  -> bool
{
  return SafeCall(RequiresScripting(), [&](SafeCallState& state) {
    return state.scripting->MarkSlotReady(slot, std::move(executable));
  });
}

auto SceneNode::Scripting::MarkSlotCompilationFailed(
  const Slot& slot, std::string diagnostic_message) noexcept -> bool
{
  return SafeCall(RequiresScripting(), [&](SafeCallState& state) {
    return state.scripting->MarkSlotCompilationFailed(
      slot, std::move(diagnostic_message));
  });
}

auto SceneNode::Scripting::LogSafeCallError(const char* reason) const noexcept
  -> void
{
  node_->LogSafeCallError(reason);
}
