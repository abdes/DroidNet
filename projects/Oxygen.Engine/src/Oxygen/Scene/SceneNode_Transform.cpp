//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeImpl;

// =============================================================================
// SceneNode::Transform Validator Implementations
// =============================================================================

auto SceneNode::Transform::LogSafeCallError(const char* reason) const noexcept
  -> void
{
  try {
    DLOG_F(ERROR, "Operation on SceneNode::Transform {} failed: {}",
      nostd::to_string(node_->GetHandle()), reason);
  } catch (...) {
    // If logging fails, we can do nothing about it
    (void)0;
  }
}

//! Base validator for Transform operations, following Scene's pattern
class SceneNode::Transform::BaseTransformValidator {
public:
  explicit BaseTransformValidator(const Transform& transform) noexcept
    : transform_(&transform)
  {
    DCHECK_NOTNULL_F(
      transform.node_, "expecting Transform to have a non-null SceneNode");
  }

protected:
  [[nodiscard]] auto GetTransform() const noexcept -> const Transform&
  {
    return *transform_;
  }

  [[nodiscard]] auto GetNode() const noexcept -> const SceneNode&
  {
    return *transform_->node_;
  }

  [[nodiscard]] auto GetResult() noexcept -> std::optional<std::string>
  {
    return std::move(result_);
  }

  auto CheckNodeIsValid() -> bool
  {
    // In debug mode, we can also explicitly check if the node is valid.
    // This is not strictly needed, as nodes_ table will check if the handle
    // is within bounds (i.e. valid), but it can help troubleshoot exactly
    // the reason why validation failed.
    if (!GetNode().IsValid()) [[unlikely]] {
      result_ = fmt::format("node({}) is invalid", nostd::to_string(GetNode()));
      return false;
    }
    result_.reset();
    return true;
  }

  //! Validate that transform is not dirty (required for certain operations)
  auto CheckTransformIsClean(const SafeCallState& state) -> bool
  {
    DCHECK_NOTNULL_F(state.node_impl,
      "expected state to be populated with a valid transform component");

    if (state.node_impl->IsTransformDirty()) {
      result_ = fmt::format(
        "node({}) has unexpected dirty transform", nostd::to_string(GetNode()));
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
      const auto impl_ref = transform_->node_->GetImpl();
      if (!impl_ref.has_value()) [[unlikely]] {
        result_ = fmt::format(
          "node({}) is no longer in scene", nostd::to_string(GetNode()));
        return false;
      }
      state.node_impl = &impl_ref->get();
      state.transform_component
        = &state.node_impl->GetComponent<detail::TransformComponent>();
      result_.reset();
      return true;
    } catch (const std::exception&) {
      result_ = fmt::format(
        "node({}) is no longer in scene", nostd::to_string(GetNode()));
      return false;
    }
  }

private:
  std::optional<std::string> result_ {};
  const Transform* transform_;
};

//! Basic validator - standard validation for most Transform operations
class SceneNode::Transform::BasicTransformValidator
  : public BaseTransformValidator {
public:
  explicit BasicTransformValidator(const Transform& transform) noexcept
    : BaseTransformValidator(transform)
  {
  }

  auto operator()(SafeCallState& state) -> std::optional<std::string>
  {
    state.node = const_cast<SceneNode*>(&GetNode());
    if (CheckNodeIsValid() && PopulateStateWithNodeImpl(state)) [[likely]] {
      return std::nullopt; // Success
    }
    return GetResult();
  }
};

//! Clean validator - requires transform to not be dirty
class SceneNode::Transform::CleanTransformValidator
  : public BaseTransformValidator {
public:
  explicit CleanTransformValidator(const Transform& transform) noexcept
    : BaseTransformValidator(transform)
  {
  }

  auto operator()(SafeCallState& state) -> std::optional<std::string>
  {
    if (CheckNodeIsValid() && PopulateStateWithNodeImpl(state)
      && CheckTransformIsClean(state)) [[likely]] {
      return std::nullopt; // Success
    }
    return GetResult();
  }
};

auto SceneNode::Transform::BasicValidator() const -> BasicTransformValidator
{
  return BasicTransformValidator(*this);
}

auto SceneNode::Transform::CleanValidator() const -> CleanTransformValidator
{
  return CleanTransformValidator(*this);
}

// =============================================================================
// SceneNode::Transform Implementations
// =============================================================================

/*!
 @param position New local position vector.
 @param rotation New local rotation quaternion (should be normalized).
 @param scale New local scale vector (positive values recommended).
 @return True if the operation succeeded, false if the node is no longer valid.
*/
auto SceneNode::Transform::SetLocalTransform(const Vec3& position,
  const Quat& rotation, const Vec3& scale) noexcept -> bool
{
  return SafeCall(BasicValidator(), [&](const SafeCallState& state) -> bool {
    DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
    DCHECK_NOTNULL_F(state.node_impl);
    DCHECK_NOTNULL_F(state.transform_component);

    state.transform_component->SetLocalTransform(position, rotation, scale);
    state.node_impl->MarkTransformDirty();
    return true;
  });
}

/*!
 @param position New local position vector in local coordinate space.
 @return True if the operation succeeded, false if the node is no longer valid.
*/
auto SceneNode::Transform::SetLocalPosition(const Vec3& position) noexcept
  -> bool
{
  return SafeCall(BasicValidator(), [&](const SafeCallState& state) -> bool {
    DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
    DCHECK_NOTNULL_F(state.node_impl);
    DCHECK_NOTNULL_F(state.transform_component);

    state.transform_component->SetLocalPosition(position);
    state.node_impl->MarkTransformDirty();
    return true;
  });
}

/*!
 @param rotation New local rotation quaternion (should be normalized).
 @return True if the operation succeeded, false if the node is no longer valid.
*/
auto SceneNode::Transform::SetLocalRotation(const Quat& rotation) noexcept
  -> bool
{
  return SafeCall(BasicValidator(), [&](const SafeCallState& state) -> bool {
    DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
    DCHECK_NOTNULL_F(state.node_impl);
    DCHECK_NOTNULL_F(state.transform_component);

    state.transform_component->SetLocalRotation(rotation);
    state.node_impl->MarkTransformDirty();
    return true;
  });
}

/*!
 @param scale New local scale vector (positive values recommended).
 @return True if the operation succeeded, false if the node is no longer valid.
*/
auto SceneNode::Transform::SetLocalScale(const Vec3& scale) noexcept -> bool
{
  return SafeCall(BasicValidator(), [&](const SafeCallState& state) -> bool {
    DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
    DCHECK_NOTNULL_F(state.node_impl);
    DCHECK_NOTNULL_F(state.transform_component);

    state.transform_component->SetLocalScale(scale);
    state.node_impl->MarkTransformDirty();
    return true;
  });
}

/*!
 @return Optional local position vector, or std::nullopt if the node is no
 longer valid.
*/
auto SceneNode::Transform::GetLocalPosition() const noexcept
  -> std::optional<Vec3>
{
  return SafeCall(BasicValidator(),
    [&](const SafeCallState& state) noexcept -> std::optional<Vec3> {
      DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
      DCHECK_NOTNULL_F(state.node_impl);
      DCHECK_NOTNULL_F(state.transform_component);

      return state.transform_component->GetLocalPosition();
    });
}

/*!
 @return Optional local rotation quaternion, or std::nullopt if the node is no
 longer valid.
*/
auto SceneNode::Transform::GetLocalRotation() const noexcept
  -> std::optional<Quat>
{
  return SafeCall(BasicValidator(),
    [&](const SafeCallState& state) noexcept -> std::optional<Quat> {
      DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
      DCHECK_NOTNULL_F(state.node_impl);
      DCHECK_NOTNULL_F(state.transform_component);

      return state.transform_component->GetLocalRotation();
    });
}

/*!
 @return Optional local scale vector, or std::nullopt if the node is no longer
 valid.
*/
auto SceneNode::Transform::GetLocalScale() const noexcept -> std::optional<Vec3>
{
  return SafeCall(BasicValidator(),
    [&](const SafeCallState& state) noexcept -> std::optional<Vec3> {
      DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
      DCHECK_NOTNULL_F(state.node_impl);
      DCHECK_NOTNULL_F(state.transform_component);

      return state.transform_component->GetLocalScale();
    });
}

/*!
 @param offset Distance to move along each axis.
 @param local If true, offset is rotated by current orientation; if false,
 offset is applied directly in world space.
 @return True if the operation succeeded, false if the node is no longer valid.
*/
auto SceneNode::Transform::Translate(
  const Vec3& offset, const bool local) noexcept -> bool
{
  return SafeCall(
    BasicValidator(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
      DCHECK_NOTNULL_F(state.node_impl);
      DCHECK_NOTNULL_F(state.transform_component);

      state.transform_component->Translate(offset, local);
      state.node_impl->MarkTransformDirty();
      return true;
    });
}

/*!
 @param rotation Quaternion rotation to apply (should be normalized).
 @param local If true, applies rotation after current rotation (local space); if
 false, applies rotation before current rotation (world space).
 @return True if the operation succeeded, false if the node is no longer valid.
*/
auto SceneNode::Transform::Rotate(
  const Quat& rotation, const bool local) noexcept -> bool
{
  return SafeCall(
    BasicValidator(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
      DCHECK_NOTNULL_F(state.node_impl);
      DCHECK_NOTNULL_F(state.transform_component);

      state.transform_component->Rotate(rotation, local);
      state.node_impl->MarkTransformDirty();
      return true;
    });
}

/*!
 @param scale_factor Multiplicative scale factor for each axis.
 @return True if the operation succeeded, false if the node is no longer valid.
*/
auto SceneNode::Transform::Scale(const Vec3& scale_factor) noexcept -> bool
{
  return SafeCall(
    BasicValidator(), [&](const SafeCallState& state) noexcept -> bool {
      DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
      DCHECK_NOTNULL_F(state.node_impl);
      DCHECK_NOTNULL_F(state.transform_component);

      state.transform_component->Scale(scale_factor);
      state.node_impl->MarkTransformDirty();
      return true;
    });
}

/*!
 Returns the cached world-space transformation matrix without forcing
 computation. The matrix is computed lazily during scene update passes and
 cached until marked dirty.

 @return Optional world transformation matrix, or std::nullopt if the node is no
 longer valid.
*/
auto SceneNode::Transform::GetWorldMatrix() const noexcept
  -> std::optional<Mat4>
{
  return SafeCall(BasicValidator(),
    [&](const SafeCallState& state) noexcept -> std::optional<Mat4> {
      DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
      DCHECK_NOTNULL_F(state.node_impl);
      DCHECK_NOTNULL_F(state.transform_component);

      return state.transform_component->GetWorldMatrix();
    });
}

/*!
 @return Optional world-space position vector, or std::nullopt if the node is no
 longer valid.
*/
auto SceneNode::Transform::GetWorldPosition() const noexcept
  -> std::optional<Vec3>
{
  return SafeCall(BasicValidator(),
    [&](const SafeCallState& state) noexcept -> std::optional<Vec3> {
      DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
      DCHECK_NOTNULL_F(state.node_impl);
      DCHECK_NOTNULL_F(state.transform_component);

      return state.transform_component->GetWorldPosition();
    });
}

/*!
 @return Optional world-space rotation quaternion, or std::nullopt if the node
 is no longer valid.
*/
auto SceneNode::Transform::GetWorldRotation() const noexcept
  -> std::optional<Quat>
{
  return SafeCall(BasicValidator(),
    [&](const SafeCallState& state) noexcept -> std::optional<Quat> {
      DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
      DCHECK_NOTNULL_F(state.node_impl);
      DCHECK_NOTNULL_F(state.transform_component);

      return state.transform_component->GetWorldRotation();
    });
}

/*!
 @return Optional world-space scale vector, or std::nullopt if the node is no
 longer valid.
*/
auto SceneNode::Transform::GetWorldScale() const noexcept -> std::optional<Vec3>
{
  return SafeCall(BasicValidator(),
    [&](const SafeCallState& state) noexcept -> std::optional<Vec3> {
      DCHECK_EQ_F(state.node, node_, "mismatched state - different nodes");
      DCHECK_NOTNULL_F(state.node_impl);
      DCHECK_NOTNULL_F(state.transform_component);

      return state.transform_component->GetWorldScale();
    });
}

/*!
 Rotates the node so that its forward direction (-Z axis in local space) points
 toward the target position. This sets the local rotation directly without
 attempting to compute inverse parent transforms, as world transform computation
 is deferred and handled by the Scene.

 @param target_position World-space position to look at.
 @param up_direction World-space up direction (default: Y-up).
 @return True if the operation succeeded, false if the node is no longer valid.
 @note This computes rotation based on current cached world position. For
 accurate results, ensure scene transforms are up to date.
*/
auto SceneNode::Transform::LookAt(
  const Vec3& target_position, const Vec3& up_direction) noexcept -> bool
{
  return SafeCall(BasicValidator(), [&](const SafeCallState& state) -> bool {
    // Get current world position from cached data
    const auto world_pos = GetWorldPosition();
    if (!world_pos) {
      return false;
    }

    // Compute look-at rotation in world space
    const auto forward = glm::normalize(target_position - *world_pos);
    const auto right = glm::normalize(glm::cross(forward, up_direction));
    const auto up = glm::cross(right, forward);

    // Create rotation matrix (note: -forward because we use -Z as forward)
    Mat4 look_matrix(1.0F);
    look_matrix[0] = glm::vec4(right, 0);
    look_matrix[1] = glm::vec4(up, 0);
    look_matrix[2] = glm::vec4(-forward, 0);

    // Convert to quaternion and set as local rotation
    const auto look_rotation = glm::quat_cast(look_matrix);

    state.transform_component->SetLocalRotation(look_rotation);
    state.node_impl->MarkTransformDirty();
    return true;
  });
}
