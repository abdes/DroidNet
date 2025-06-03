//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/gtx/matrix_decompose.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/TransformComponent.h>

using oxygen::scene::TransformComponent;

void TransformComponent::SetLocalTransform(
    const Vec3& position, const Quat& rotation,
    const Vec3& scale) noexcept
{
    local_position_ = position;
    local_rotation_ = rotation;
    local_scale_ = scale;
    MarkDirty();
}

void TransformComponent::SetLocalPosition(const Vec3& position) noexcept
{
    if (local_position_ != position) {
        local_position_ = position;
        MarkDirty();
    }
}

void TransformComponent::SetLocalRotation(const Quat& rotation) noexcept
{
    if (local_rotation_ != rotation) {
        local_rotation_ = rotation;
        MarkDirty();
    }
}

void TransformComponent::SetLocalScale(const Vec3& scale) noexcept
{
    if (local_scale_ != scale) {
        local_scale_ = scale;
        MarkDirty();
    }
}

void TransformComponent::Translate(const Vec3& offset, const bool local) noexcept
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

void TransformComponent::Scale(const Vec3& scale_factor) noexcept
{
    local_scale_ *= scale_factor;
    MarkDirty();
}

auto TransformComponent::GetLocalMatrix() const -> Mat4
{
    // Compose TRS matrix: T * R * S
    const Mat4 translation_matrix = glm::translate(Mat4 { 1.0f }, local_position_);
    const Mat4 rotation_matrix = glm::mat4_cast(local_rotation_);
    const Mat4 scale_matrix = glm::scale(Mat4 { 1.0f }, local_scale_);

    return translation_matrix * rotation_matrix * scale_matrix;
}

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
        "UpdateWorldTransform() has never been called! This TransformComponent "
        "must be registered with the scene hierarchy and UpdateWorldTransform() "
        "must be called by the SceneManager before accessing world space data.");

    return world_matrix_;
}

// ReSharper disable once CppMemberFunctionMayBeConst
void TransformComponent::UpdateWorldTransform(const Mat4& parent_world_matrix)
{
    world_matrix_ = parent_world_matrix * GetLocalMatrix();
    is_dirty_ = false;
}

// ReSharper disable once CppMemberFunctionMayBeConst
void TransformComponent::UpdateWorldTransformAsRoot()
{
    // For root nodes, world matrix equals local matrix (identity parent)
    world_matrix_ = GetLocalMatrix();
    is_dirty_ = false;
}

auto TransformComponent::GetWorldPosition() const -> Vec3
{
    const Mat4& world = GetWorldMatrix();
    return Vec3 { world[3] };
}

auto TransformComponent::GetWorldRotation() const -> Quat
{
    const Mat4& world = GetWorldMatrix();

    // Extract rotation from world matrix
    Vec3 scale;
    Quat rotation;
    Vec3 translation;
    Vec3 skew;
    glm::vec4 perspective;

    if (glm::decompose(world, scale, rotation, translation, skew, perspective)) {
        return rotation;
    }

    // Fallback to identity if decomposition fails
    return Quat { 1.0f, 0.0f, 0.0f, 0.0f };
}

auto TransformComponent::GetWorldScale() const -> Vec3
{
    const Mat4& world = GetWorldMatrix();

    // Extract scale from world matrix
    Vec3 scale;
    Quat rotation;
    Vec3 translation;
    Vec3 skew;
    glm::vec4 perspective;

    if (glm::decompose(world, scale, rotation, translation, skew, perspective)) {
        return scale;
    }

    // Fallback to unit scale if decomposition fails
    return Vec3 { 1.0f, 1.0f, 1.0f };
}
