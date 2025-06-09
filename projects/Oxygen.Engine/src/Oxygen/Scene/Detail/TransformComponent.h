//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene::detail {

//! Component managing 3D spatial transformations with hierarchical support and
//! performance optimization.
/*!
 The TransformComponent stores transformation data using the standard TRS
 (Translation-Rotation-Scale) decomposition and provides efficient
 local-to-world space conversion through dirty tracking and lazy matrix
 evaluation. Supports parent-child hierarchical relationships for scene graph
 operations.

 Key features:
 - Stores position (translation), rotation (quaternion), and scale as separate
   components.
 - Lazy world matrix computation with dirty tracking for performance.
 - Hierarchical transform inheritance via UpdateWorldTransform().
 - SIMD-optimized GLM operations with 16-byte aligned data members.
 - Immutable getters and efficient setters with change detection.

 \note All GLM data types are 16-byte aligned for optimal SIMD performance.
*/
class TransformComponent final : public Component {
    OXYGEN_COMPONENT(TransformComponent)
    OXYGEN_COMPONENT_REQUIRES(ObjectMetaData)

public:
    using Vec3 = glm::vec3;
    using Quat = glm::quat;
    using Mat4 = glm::mat4;

    //! Constructs a TransformComponent with identity transformation values.
    /*!
     Creates a new TransformComponent initialized to represent no
     transformation:
     - Position: (0, 0, 0) - positioned at world origin.
     - Rotation: (w=1, x=0, y=0, z=0) - identity quaternion (no rotation).
     - Scale: (1, 1, 1) - uniform scale factor of 1 (no size change).
     - World matrix: 4×4 identity matrix.
     - Dirty flag: true (computes world matrix on first access).

     The identity transformation means objects will appear at the origin with
     their original orientation and size until explicitly transformed.
    */
    OXGN_SCN_API TransformComponent() = default;

    OXGN_SCN_API ~TransformComponent() override = default;

    OXYGEN_DEFAULT_COPYABLE(TransformComponent)
    OXYGEN_DEFAULT_MOVABLE(TransformComponent)

    //=== Local Transform Operations ===--------------------------------------//

    //! Sets all local transformation components atomically.
    /*!
     Updates position, rotation, and scale simultaneously. This is more
     efficient than calling individual setters when changing multiple
     components, as it marks the transform dirty only once.

     \param position New local position vector.
     \param rotation New local rotation quaternion (must be normalized).
     \param scale New local scale vector (positive for normal scaling).
     */
    OXGN_SCN_API void SetLocalTransform(
        const Vec3& position, const Quat& rotation, const Vec3& scale) noexcept;

    //! Sets the local position (translation component).
    /*!
     Updates the object's position in local space. Only marks dirty and updates
     if the new position differs from the current one.

     \param position New local position vector in local coordinate space.
     */
    OXGN_SCN_API void SetLocalPosition(const Vec3& position) noexcept;

    //! Sets the local rotation (rotation component).
    /*!
     Updates the object's orientation in local space using a quaternion.
     Only marks dirty and updates if the new rotation differs from current.

     \param rotation New local rotation quaternion (should be normalized).
     \warning Non-normalized quaternions may cause unexpected behavior.
     */
    OXGN_SCN_API void SetLocalRotation(const Quat& rotation) noexcept;

    //! Sets the local scale (scale component).
    /*!
     Updates the object's scale in local space. Only marks dirty and updates if
     the new scale differs from the current one.

     \param scale New local scale vector (typically positive values).
     \warning Negative scale values will cause mesh inversion.
     \warning Zero scale values will cause degenerate transformations.
     */
    OXGN_SCN_API void SetLocalScale(const Vec3& scale) noexcept;

    //=== Local Transform Getters ===----------------------------------------//

    //! Gets the local position (translation component).
    [[nodiscard]] auto GetLocalPosition() const noexcept -> const Vec3&
    {
        return local_position_;
    }

    //! Gets the local rotation (rotation component).
    [[nodiscard]] auto GetLocalRotation() const noexcept -> const Quat&
    {
        return local_rotation_;
    }

    //! Gets the local scale (scale component).
    [[nodiscard]] auto GetLocalScale() const noexcept -> const Vec3&
    {
        return local_scale_;
    }

    //=== Transform Operations ===--------------------------------------------//

    //! Applies a translation (movement) to the current position.
    /*!
     Moves the object by the specified offset vector. The movement can be
     applied in local space (relative to object's orientation) or world space.

     \param offset Distance to move along each axis.
     \param local If true, offset is rotated by current orientation before
                  applying; if false, offset is applied directly in world space.
     */
    OXGN_SCN_API void Translate(const Vec3& offset, bool local = true) noexcept;

    //! Applies a rotation to the current orientation.
    /*!
     Rotates the object by the specified quaternion rotation. Can be applied as
     local rotation (after current rotation) or world rotation (before current).

     \param rotation Quaternion rotation to apply (should be normalized).
     \param local If true, applies rotation after current rotation (local
                  space); if false, applies rotation before current rotation
                  (world space).
     */
    OXGN_SCN_API void Rotate(const Quat& rotation, bool local = true) noexcept;

    //! Applies a scaling factor to the current scale.
    /*!
     Multiplies the current scale by the specified factor. This is cumulative
     scaling, not absolute scaling.

     \param scale_factor Multiplicative scale factor for each axis
     \warning Values of 0 will cause degenerate transformations.
     \warning Negative values will cause mesh inversion.
     */
    OXGN_SCN_API void Scale(const Vec3& scale_factor) noexcept;

    //=== World Transform Access ===------------------------------------------//

    //! Gets the cached world transformation matrix.
    /*!
     Returns the world-space transformation matrix, computing it lazily if
     dirty. For root objects, this equals the local matrix. For child objects,
     this represents the concatenation of all parent transformations with the
     local matrix.

     \return Const reference to the 4×4 world transformation matrix.
     \note Matrix is computed lazily and cached until marked dirty.
     */
    OXGN_SCN_NDAPI auto GetWorldMatrix() const -> const Mat4&;

    //! Updates world transform from parent's world matrix (used by scene
    //! graph).
    /*!
     Computes and caches the world matrix by concatenating the parent's world
     matrix with this object's local matrix. Called by scene management systems
     during hierarchical transform updates.

     \param parent_world_matrix The parent object's world transformation matrix.
     \note Clears the dirty flag after computation.
     */
    OXGN_SCN_API auto UpdateWorldTransform(const Mat4& parent_world_matrix)
        -> void;

    //! Updates world transform for root nodes (no parent).
    /*!
     For root nodes that have no parent, the world matrix equals the local
     matrix. This is a convenience method that calls UpdateWorldTransform() with
     identity matrix.

     \note This should only be called for root nodes in the scene hierarchy.
     */
    OXGN_SCN_API auto UpdateWorldTransformAsRoot() -> void;

    //! Extracts the world-space position from the world transformation matrix.
    /*!
     \return World-space position vector (translation component of world
             matrix).
     */
    OXGN_SCN_NDAPI auto GetWorldPosition() const -> Vec3;

    //! Extracts the world-space rotation from the world transformation matrix.
    /*!
     \return World-space rotation quaternion (rotation component of world
             matrix).
     \note Returns identity quaternion (1,0,0,0) if matrix decomposition fails.
     */
    OXGN_SCN_NDAPI auto GetWorldRotation() const -> Quat;

    //! Extracts the world-space scale from the world transformation matrix.
    /*!
     \return World-space scale vector (scale component of world matrix).
     \note Returns unit scale (1,1,1) if matrix decomposition fails.
     */
    OXGN_SCN_NDAPI auto GetWorldScale() const -> Vec3;

    //! Computes the local transformation matrix from TRS components.
    /*!
     Composes a 4×4 transformation matrix from the local position, rotation, and
     scale using the standard TRS (Translation × Rotation × Scale) order. This
     matrix represents the object's transformation in local coordinate space.

     \return 4×4 local transformation matrix (Translation × Rotation × Scale).
     \note Matrix composition order: T × R × S (translation applied last).
     */
    OXGN_SCN_NDAPI auto GetLocalMatrix() const -> Mat4;

    //=== Dirty State Management ===------------------------------------------//

    //! Sets the dirty flag to indicate that the world matrix cache is invalid
    //! and needs to be recomputed. Called automatically by setter methods.
    // ReSharper disable once CppMemberFunctionMayBeConst
    auto MarkDirty() noexcept -> void
    {
        is_dirty_ = true;
    } //! Checks if the transform is dirty and needs matrix re-computation.
    [[nodiscard]] auto IsDirty() const noexcept -> bool { return is_dirty_; }

    //! Clears the dirty flag without recomputing the world matrix.
    void ForceClearDirty() noexcept { is_dirty_ = false; }

    [[nodiscard]] auto IsCloneable() const noexcept -> bool override
    {
        return true;
    }
    [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
    {
        auto clone = std::make_unique<TransformComponent>();
        clone->local_position_ = this->local_position_;
        clone->local_rotation_ = this->local_rotation_;
        clone->local_scale_ = this->local_scale_;
        // Clone starts dirty to ensure world matrix is computed fresh
        clone->is_dirty_ = true;
        return clone;
    }

protected:
    //! Updates the dependencies of this component.
    /*!
     This method is called when the component is cloned, to ensure that the
     dependencies are properly set up in the cloned component.
     */
    void UpdateDependencies(const Composition& composition) override
    {
        meta_data_ = &composition.GetComponent<ObjectMetaData>();
    }

private:
    //! Local position (translation) component in local coordinate space.
    alignas(16) Vec3 local_position_ { 0.0f, 0.0f, 0.0f };

    //! Local rotation component as normalized quaternion (w, x, y, z).
    alignas(16) Quat local_rotation_ { 1.0f, 0.0f, 0.0f, 0.0f };

    //! Local scale component (multiplicative scale factors per axis).
    alignas(16) Vec3 local_scale_ { 1.0f, 1.0f, 1.0f };

    //! Cached world transformation matrix (lazy-computed, mutable for const
    //! access).
    mutable alignas(16) Mat4 world_matrix_ { 1.0f };

    //! Dirty flag indicating world matrix cache needs re-computation.
    bool is_dirty_ = true;

    ObjectMetaData* meta_data_ { nullptr };
};

} // namespace oxygen::scene::detail
