//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Scene/Detail/GraphData.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Types/Flags.h>

using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;
using oxygen::scene::detail::GraphData;

// =============================================================================
// SceneNode Validator Implementations
// =============================================================================

void SceneNode::LogSafeCallError(const char* reason) const noexcept
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
  explicit BaseNodeValidator(const SceneNode& node) noexcept
    : node_(&node)
  {
  }

protected:
  [[nodiscard]] auto GetNode() const noexcept -> const SceneNode&
  {
    return *node_;
  }

  [[nodiscard]] auto GetScene() const noexcept -> Scene*
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
      auto& impl_ref = state.scene->GetNodeImplRefUnsafe(node_->GetHandle());
      state.node_impl = &impl_ref;
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
  const SceneNode* node_;
};

//! A validator that checks if a SceneNode is valid and belongs to the \p scene.
class SceneNode::NodeIsValidValidator : public BaseNodeValidator {
public:
  explicit NodeIsValidValidator(const SceneNode& node) noexcept
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
  explicit NodeIsValidAndInSceneValidator(const SceneNode& node) noexcept
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

auto SceneNode::NodeIsValid() const -> NodeIsValidValidator
{
  return NodeIsValidValidator { *this };
}

auto SceneNode::NodeIsValidAndInScene() const -> NodeIsValidAndInSceneValidator
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
SceneNode::SceneNode(std::weak_ptr<Scene> scene_weak)
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
SceneNode::SceneNode(std::weak_ptr<Scene> scene_weak, const NodeHandle& handle)
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
  const auto impl = node.GetObject();
  if (!impl) {
    return fmt::format("SN({}, stale)", to_string_compact(node.GetHandle()));
  }
  return fmt::format("SN({}, n='{}')", to_string_compact(node.GetHandle()),
    impl->get().GetName());
#else // defined(NDEBUG)
  return fmt::format("SN({})", to_string_compact(node.GetHandle()));
#endif // defined(NDEBUG)
}

/*!
 @return A reference (through unwrapping the std::reference_wrapper) to the
 underlying SceneNodeImpl object, or std::nullopt if the node is invalid or
 expired.
*/
auto SceneNode::GetObject() const noexcept -> OptionalConstRefToImpl
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> OptionalConstRefToImpl {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->GetNodeImplRefUnsafe(GetHandle());
    });
}

/*!
 @copydoc SceneNode::GetObject() const
*/
auto SceneNode::GetObject() noexcept -> OptionalRefToImpl
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> OptionalRefToImpl {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->GetNodeImplRefUnsafe(GetHandle());
    });
}

/*!
 @return A reference (through unwrapping the std::reference_wrapper) to the
 node's Flags, or std::nullopt if the node is invalid or expired.
*/
auto SceneNode::GetFlags() const noexcept -> OptionalConstRefToFlags
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> OptionalConstRefToFlags {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.node_impl->GetFlags();
    });
}

/*!
 @copydoc SceneNode::GetFlags() const
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

auto SceneNode::GetParent() const noexcept -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->GetParentUnsafe(*this, state.node_impl);
    });
}

auto SceneNode::GetFirstChild() const noexcept -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->GetFirstChildUnsafe(*this, state.node_impl);
    });
}

auto SceneNode::GetNextSibling() const noexcept -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->GetNextSiblingUnsafe(*this, state.node_impl);
    });
}

auto SceneNode::GetPrevSibling() const noexcept -> std::optional<SceneNode>
{
  return SafeCall(NodeIsValidAndInScene(),
    [&](const SafeCallState& state) noexcept -> std::optional<SceneNode> {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->GetPrevSiblingUnsafe(*this, state.node_impl);
    });
}

auto SceneNode::HasParent() const noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->HasParentUnsafe(*this, state.node_impl);
    });
}

auto SceneNode::HasChildren() const noexcept -> bool
{
  return SafeCall(
    NodeIsValidAndInScene(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, this);
      DCHECK_NOTNULL_F(state.scene);
      DCHECK_NOTNULL_F(state.node_impl);

      return state.scene->HasChildrenUnsafe(*this, state.node_impl);
    });
}

auto SceneNode::IsRoot() const noexcept -> bool { return !HasParent(); }

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
