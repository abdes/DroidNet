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
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Types/Flags.h>

using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;
using oxygen::scene::detail::GraphData;

// Camera type aliases for brevity and clarity
using PerspectiveCamera = oxygen::scene::PerspectiveCamera;
using OrthographicCamera = oxygen::scene::OrthographicCamera;

// =============================================================================
// SceneNode Validator Implementations
// =============================================================================

auto SceneNode::LogSafeCallError(const char* reason) const noexcept -> void
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

  auto CheckSceneNotExpired(SafeCallState& state) -> bool
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

  auto CheckNodeIsValid() -> bool
  {
    // In debug mode, we can also explicitly check if the node is valid.
    // This is not strictly needed, as nodes_ table will check if the handle
    // is within bounds (i.e. valid), but it can help troubleshoot exactly
    // the reason why validation failed.
    if (!GetNode().IsValid()) [[unlikely]] {
      result_ = fmt::format(
        "node({}) is invalid", nostd::to_string(GetNode().GetHandle()));
      return false;
    }
    result_.reset();
    return true;
  }

  auto PopulateStateWithNodeImpl(SafeCallState& state) -> bool
  {
    // Then check if the node is still in the scene node table, and retrieve
    // its implementation object.
    try {
      state.scene = GetScene();
      const auto* impl_ref
        = &state.scene->GetNodeImplRefUnsafe(node_->GetHandle());
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
  std::optional<std::string> result_ {};
  SceneNode* node_;
};

//! A validator that checks if a SceneNode is valid and belongs to the \p scene.
class SceneNode::NodeIsValidValidator : public BaseNodeValidator {

public:
  explicit NodeIsValidValidator(SceneNode& node) noexcept
    : BaseNodeValidator(node)
  {
  }

  auto operator()(SafeCallState& state) -> std::optional<std::string>
  {
    state.node = const_cast<SceneNode*>(&GetNode());
    if (CheckSceneNotExpired(state) && CheckNodeIsValid()) [[likely]] {
      return std::nullopt;
    }
    return GetResult();
  }
};

//! A validator that checks if a SceneNode is valid and belongs to the \p scene.
class SceneNode::NodeIsValidAndInSceneValidator : public BaseNodeValidator {
public:
  explicit NodeIsValidAndInSceneValidator(SceneNode& node) noexcept
    : BaseNodeValidator(node)
  {
  }

  auto operator()(SafeCallState& state) -> std::optional<std::string>
  {
    state.node = const_cast<SceneNode*>(&GetNode());
    if (CheckSceneNotExpired(state) && CheckNodeIsValid()
      && PopulateStateWithNodeImpl(state)) [[likely]] {
      return std::nullopt;
    }
    return GetResult();
  }
};

auto SceneNode::NodeIsValid() -> NodeIsValidValidator
{
  return NodeIsValidValidator { *this };
}

auto SceneNode::NodeIsValidAndInScene() -> NodeIsValidAndInSceneValidator
{
  return NodeIsValidAndInSceneValidator { *this };
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
auto SceneNode::GetObject() noexcept -> OptionalRefToImpl
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> OptionalRefToImpl {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      const auto& impl_ref = state.scene->GetNodeImplRefUnsafe(GetHandle());
      return { std::reference_wrapper(const_cast<SceneNodeImpl&>(
        impl_ref)) }; // NOLINT(*-pro-type-const-cast)
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

//! Attaches a camera component to this SceneNode. If a camera already exists,
//! this will fail.
auto SceneNode::AttachCamera(std::unique_ptr<Component> camera) -> bool
{
  if (!camera) {
    LOG_F(ERROR, "Cannot attach a null camera. SceneNode: {}",
      nostd::to_string(*this));
    return false;
  }

  // TODO: update Composition to add using an already constructed object in
  // a std::unique_ptr

  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      const auto type_id = camera->GetTypeId();
      bool already_exists;
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
        // Static cast unique_ptr<Component> to unique_ptr<PerspectiveCamera>
        // and move the object
        const auto cam_ptr = std::unique_ptr<PerspectiveCamera>(
          static_cast<PerspectiveCamera*>(camera.release()));
        state.node_impl->AddComponent<PerspectiveCamera>(std::move(*cam_ptr));
      } else if (type_id == OrthographicCamera::ClassTypeId()) {
        const auto cam_ptr = std::unique_ptr<OrthographicCamera>(
          static_cast<OrthographicCamera*>(camera.release()));
        state.node_impl->AddComponent<OrthographicCamera>(std::move(*cam_ptr));
      }
      return true;
    });
}

//! Detaches the camera component from this SceneNode, if present.
auto SceneNode::DetachCamera() -> bool
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
      return removed;
    });
}

//! Replaces the current camera component with a new one. If no camera exists,
//! this acts as attach.
auto SceneNode::ReplaceCamera(std::unique_ptr<Component> camera) -> bool
{
  if (!camera) {
    LOG_F(ERROR, "Cannot attach a null camera. SceneNode: {}",
      nostd::to_string(*this));
    return false;
  }

  // TODO: use ReplaceComponent()
  // TODO: update Composition to replace using an already constructed object in
  // a std::unique_ptr
  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      // Remove any existing camera
      if (state.node_impl->HasComponent<PerspectiveCamera>()) {
        state.node_impl->RemoveComponent<PerspectiveCamera>();
      }
      if (state.node_impl->HasComponent<OrthographicCamera>()) {
        state.node_impl->RemoveComponent<OrthographicCamera>();
      }
      const auto type_id = camera->GetTypeId();
      if (type_id == PerspectiveCamera::ClassTypeId()) {
        state.node_impl->AddComponent<PerspectiveCamera>(
          std::move(*static_cast<PerspectiveCamera*>(camera.get())));
      } else if (type_id == OrthographicCamera::ClassTypeId()) {
        state.node_impl->AddComponent<OrthographicCamera>(
          std::move(*static_cast<OrthographicCamera*>(camera.get())));
      } else {
        return false;
      }
      return true;
    });
}

//! Gets the attached camera component if present.
auto SceneNode::GetCamera() noexcept
  -> std::optional<std::reference_wrapper<Component>>
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](SafeCallState& state) noexcept
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
