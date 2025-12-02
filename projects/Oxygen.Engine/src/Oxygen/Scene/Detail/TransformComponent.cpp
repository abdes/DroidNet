//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>

using oxygen::scene::detail::TransformComponent;

/*!
 Updates position, rotation, and scale simultaneously. This is more efficient
 than calling individual setters when changing multiple components, as it marks
 the transform dirty only once.

 @param position New local position vector.
 @param rotation New local rotation quaternion (must be normalized).
 @param scale New local scale vector (positive for normal scaling).
 */
void TransformComponent::SetLocalTransform(
  const Vec3& position, const Quat& rotation, const Vec3& scale) noexcept
{
  local_position_ = position;
  local_rotation_ = rotation;
  local_scale_ = scale;
  MarkDirty();
}

/*!
 Updates the object's position in local space. Only marks dirty and updates if
 the new position differs from the current one.

 @param position New local position vector in local coordinate space.
 */
void TransformComponent::SetLocalPosition(const Vec3& position) noexcept
{
  if (local_position_ != position) {
    local_position_ = position;
    MarkDirty();
  }
}

/*!
 Updates the object's orientation in local space using a quaternion. Only marks
 dirty and updates if the new rotation differs from current.

 @param rotation New local rotation quaternion (should be normalized).
 @warning Non-normalized quaternions may cause unexpected behavior.
 */
void TransformComponent::SetLocalRotation(const Quat& rotation) noexcept
{
  if (local_rotation_ != rotation) {
    local_rotation_ = rotation;
    MarkDirty();
  }
}

/*!
 Updates the object's scale in local space. Only marks dirty and updates if the
 new scale differs from the current one.

 @param scale New local scale vector (typically positive values).
 @warning Negative scale values will cause mesh inversion.
 @warning Zero scale values will cause degenerate transformations.
 */
void TransformComponent::SetLocalScale(const Vec3& scale) noexcept
{
  if (local_scale_ != scale) {
    local_scale_ = scale;
    MarkDirty();
  }
}

/*!
 Moves the object by the specified offset vector. The movement can be applied in
 local space (relative to object's orientation) or world space.

 @param offset Distance to move along each axis.
 @param local If true, offset is rotated by current orientation before applying;
 if false, offset is applied directly in world space.
 */
void TransformComponent::Translate(
  const Vec3& offset, const bool local) noexcept
{
  if (local) {
    // Transform offset by current rotation for local-space translation
    const Vec3 world_offset = local_rotation_ * offset;
    local_position_ += world_offset;
  } else {
    // World-space translation
    local_position_ += offset;
  }
  MarkDirty();
}

/*!
 Rotates the object by the specified quaternion rotation. Can be applied as
 local rotation (after current rotation) or world rotation (before current).

 @param rotation Quaternion rotation to apply (should be normalized).
 @param local If true, applies rotation after current rotation (local space); if
 false, applies rotation before current rotation (world space).
 */
void TransformComponent::Rotate(const Quat& rotation, const bool local) noexcept
{
  if (local) {
    // Local rotation: apply after current rotation
    local_rotation_ = local_rotation_ * rotation;
  } else {
    // World rotation: apply before current rotation
    local_rotation_ = rotation * local_rotation_;
  }
  MarkDirty();
}

/*!
 Multiplies the current scale by the specified factor. This is cumulative
 scaling, not absolute scaling.

 @param scale_factor Multiplicative scale factor for each axis
 @warning Values of 0 will cause degenerate transformations.
 @warning Negative values will cause mesh inversion.
 */
void TransformComponent::Scale(const Vec3& scale_factor) noexcept
{
  local_scale_ *= scale_factor;
  MarkDirty();
}

/*!
 Composes a 4x4 transformation matrix from the local position, rotation, and
 scale using the standard TRS (Translation x Rotation x Scale) order. This
 matrix represents the object's transformation in local coordinate space.

 @return 4x4 local transformation matrix (Translation x Rotation x Scale).
 @note Matrix composition order: T x R x S (translation applied last).
 */
auto TransformComponent::GetLocalMatrix() const -> Mat4
{
  // Compose TRS matrix: T * R * S
  const Mat4 translation_matrix
    = glm::translate(Mat4 { 1.0f }, local_position_);
  const Mat4 rotation_matrix = glm::mat4_cast(local_rotation_);
  const Mat4 scale_matrix = glm::scale(Mat4 { 1.0f }, local_scale_);

  return translation_matrix * rotation_matrix * scale_matrix;
}

/*!
 Returns the world-space transformation matrix, computing it lazily if dirty.
 For root objects, this equals the local matrix. For child objects, this
 represents the concatenation of all parent transformations with the local
 matrix.

 @return Const reference to the 4x4 world transformation matrix.
 @note Matrix is computed lazily and cached until marked dirty.
 */
auto TransformComponent::GetWorldMatrix() const -> const Mat4&
{
  // IMPORTANT: This method does NOT compute the world matrix itself. The
  // world matrix MUST be computed externally by the scene management system
  // through proper hierarchy traversal calling UpdateWorldTransform().
  //
  // Why this design?
  // 1. Correct hierarchical transforms require parent-to-child traversal
  // 2. Computing world = local here would be WRONG for child objects
  // 3. A TransformComponent cannot know its position in the scene hierarchy
  // 4. Only the SceneManager/SceneGraph has the full hierarchy context
  //
  // Proper usage:
  // - SceneManager traverses the hierarchy (parent-first, depth-first)
  // - For each node, calls UpdateWorldTransform(parent_world_matrix)
  // - This method then returns the correctly computed cached result

  // Requires UpdateWorldTransform() to have been called, and the transform
  // component is not dirty (these two conditions are linked).
  CHECK_F(!is_dirty_,
    "expecting transforms to be up-to-date for node `{}`. "
    "UpdateWorldTransform() has never been called! This TransformComponent "
    "must be registered with the scene hierarchy and "
    "UpdateWorldTransform() "
    "must be called by the SceneManager before accessing world space data.",
    meta_data_ ? meta_data_->GetName() : "unknown");

  return world_matrix_;
}

/*!
 Computes and caches the world matrix by concatenating the parent's world matrix
 with this object's local matrix. Called by scene management systems during
 hierarchical transform updates.

 @param parent_world_matrix The parent object's world transformation matrix.
 @note Clears the dirty flag after computation.
 */
// ReSharper disable once CppMemberFunctionMayBeConst
void TransformComponent::UpdateWorldTransform(const Mat4& parent_world_matrix)
{
  world_matrix_ = parent_world_matrix * GetLocalMatrix();
  is_dirty_ = false;
}

/*!
 For root nodes that have no parent, the world matrix equals the local matrix.
 This is a convenience method that calls UpdateWorldTransform() with identity
 matrix.

 @note This should only be called for root nodes in the scene hierarchy.
 */
// ReSharper disable once CppMemberFunctionMayBeConst
void TransformComponent::UpdateWorldTransformAsRoot()
{
  // For root nodes, world matrix equals local matrix (identity parent)
  world_matrix_ = GetLocalMatrix();
  is_dirty_ = false;
}

/*!
 @return World-space position vector (translation component of world matrix).
 */
auto TransformComponent::GetWorldPosition() const -> Vec3
{
  const Mat4& world = GetWorldMatrix();
  return Vec3 { world[3] };
}

/*!
 @return World-space rotation quaternion (rotation component of world matrix).
 @note Returns identity quaternion (1,0,0,0) if matrix decomposition fails.
 */
auto TransformComponent::GetWorldRotation() const -> Quat
{
  const Mat4& world = GetWorldMatrix();

  // Extract rotation from world matrix
  // ReSharper disable CppTooWideScopeInitStatement
  Vec3 scale;
  Quat rotation;
  Vec3 translation;
  Vec3 skew;
  glm::vec4 perspective;
  // ReSharper restore CppTooWideScopeInitStatement

  if (glm::decompose(world, scale, rotation, translation, skew, perspective)) {
    return rotation;
  }

  // Fallback to identity if decomposition fails
  return Quat { 1.0f, 0.0f, 0.0f, 0.0f };
}

/*!
 @return World-space scale vector (scale component of world matrix).
 @note Returns unit scale (1,1,1) if matrix decomposition fails.
 */
auto TransformComponent::GetWorldScale() const -> Vec3
{
  const Mat4& world = GetWorldMatrix();

  // Extract scale from world matrix
  // ReSharper disable CppTooWideScopeInitStatement
  Vec3 scale;
  Quat rotation;
  Vec3 translation;
  Vec3 skew;
  glm::vec4 perspective;
  // ReSharper restore CppTooWideScopeInitStatement

  if (glm::decompose(world, scale, rotation, translation, skew, perspective)) {
    return scale;
  }

  // Fallback to unit scale if decomposition fails
  return Vec3 { 1.0f, 1.0f, 1.0f };
}
