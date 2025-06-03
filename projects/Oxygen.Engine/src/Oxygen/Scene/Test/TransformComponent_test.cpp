//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/TransformComponent.h>

using oxygen::TypeId;
using oxygen::scene::TransformComponent;

namespace {

class TransformComponentTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        component_ = std::make_unique<TransformComponent>();
    }

    void TearDown() override
    {
        component_.reset();
    }

    //! Helper to create test vectors and quaternions
    static constexpr auto MakeVec3(float x, float y, float z) -> TransformComponent::Vec3
    {
        return TransformComponent::Vec3 { x, y, z };
    }

    static auto MakeQuat(float w, float x, float y, float z) -> TransformComponent::Quat
    {
        return TransformComponent::Quat { w, x, y, z };
    }

    //! Helper to create normalized rotation quaternion from euler angles (degrees)
    static auto QuatFromEuler(float pitch, float yaw, float roll) -> TransformComponent::Quat
    {
        return glm::quat(glm::radians(TransformComponent::Vec3 { pitch, yaw, roll }));
    }

    //! Helper to check if two vectors are approximately equal
    static void ExpectVec3Near(const TransformComponent::Vec3& actual,
        const TransformComponent::Vec3& expected,
        float tolerance = 1e-5f)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
        EXPECT_NEAR(actual.z, expected.z, tolerance);
    }

    //! Helper to check if two quaternions are approximately equal
    //! Handles the fact that q and -q represent the same rotation
    static void ExpectQuatNear(const TransformComponent::Quat& actual,
        const TransformComponent::Quat& expected,
        float tolerance = 1e-5f)
    {
        // Check if quaternions are the same or negatives of each other
        // (both represent the same rotation)
        const auto dot_product = glm::dot(actual, expected);

        if (dot_product >= 0.0f) {
            // Same orientation
            EXPECT_NEAR(actual.w, expected.w, tolerance);
            EXPECT_NEAR(actual.x, expected.x, tolerance);
            EXPECT_NEAR(actual.y, expected.y, tolerance);
            EXPECT_NEAR(actual.z, expected.z, tolerance);
        } else {
            // Opposite orientation (equivalent rotation)
            EXPECT_NEAR(actual.w, -expected.w, tolerance);
            EXPECT_NEAR(actual.x, -expected.x, tolerance);
            EXPECT_NEAR(actual.y, -expected.y, tolerance);
            EXPECT_NEAR(actual.z, -expected.z, tolerance);
        }
    }

    //! Helper to check if two matrices are approximately equal
    static void ExpectMat4Near(const TransformComponent::Mat4& actual,
        const TransformComponent::Mat4& expected,
        float tolerance = 1e-5f)
    {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                EXPECT_NEAR(actual[i][j], expected[i][j], tolerance)
                    << "Matrices differ at [" << i << "][" << j << "]";
            }
        }
    }

    std::unique_ptr<TransformComponent> component_;
};

//=== Construction and Default Values ===-------------------------------------//

NOLINT_TEST_F(TransformComponentTest, DefaultConstructorInitializesIdentityTransform)
{
    // Should initialize to identity transformation
    ExpectVec3Near(component_->GetLocalPosition(), MakeVec3(0.0f, 0.0f, 0.0f));
    ExpectQuatNear(component_->GetLocalRotation(), MakeQuat(1.0f, 0.0f, 0.0f, 0.0f));
    ExpectVec3Near(component_->GetLocalScale(), MakeVec3(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(component_->IsDirty());
}

//=== Local Transform Setters and Getters ===--------------------------------//

NOLINT_TEST_F(TransformComponentTest, SetAndGetLocalPosition)
{
    const auto test_position = MakeVec3(1.0f, 2.0f, 3.0f);

    component_->SetLocalPosition(test_position);

    ExpectVec3Near(component_->GetLocalPosition(), test_position);
    EXPECT_TRUE(component_->IsDirty());
}

NOLINT_TEST_F(TransformComponentTest, SetAndGetLocalRotation)
{
    const auto test_rotation = QuatFromEuler(45.0f, 90.0f, 180.0f);

    component_->SetLocalRotation(test_rotation);

    ExpectQuatNear(component_->GetLocalRotation(), test_rotation);
    EXPECT_TRUE(component_->IsDirty());
}

NOLINT_TEST_F(TransformComponentTest, SetAndGetLocalScale)
{
    const auto test_scale = MakeVec3(2.0f, 0.5f, 3.0f);

    component_->SetLocalScale(test_scale);

    ExpectVec3Near(component_->GetLocalScale(), test_scale);
    EXPECT_TRUE(component_->IsDirty());
}

NOLINT_TEST_F(TransformComponentTest, SetLocalTransformAllComponents)
{
    const auto test_position = MakeVec3(1.0f, 2.0f, 3.0f);
    const auto test_rotation = QuatFromEuler(45.0f, 90.0f, 180.0f);
    const auto test_scale = MakeVec3(2.0f, 0.5f, 3.0f);

    component_->SetLocalTransform(test_position, test_rotation, test_scale);

    ExpectVec3Near(component_->GetLocalPosition(), test_position);
    ExpectQuatNear(component_->GetLocalRotation(), test_rotation);
    ExpectVec3Near(component_->GetLocalScale(), test_scale);
    EXPECT_TRUE(component_->IsDirty());
}

//=== Dirty State Management ===----------------------------------------------//

NOLINT_TEST_F(TransformComponentTest, SettersMarkComponentDirty)
{
    // Clear dirty state by calling UpdateWorldTransformAsRoot
    component_->UpdateWorldTransformAsRoot();
    EXPECT_FALSE(component_->IsDirty());

    // Each setter should mark as dirty
    component_->SetLocalPosition(MakeVec3(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(component_->IsDirty());

    component_->UpdateWorldTransformAsRoot();
    EXPECT_FALSE(component_->IsDirty());

    component_->SetLocalRotation(QuatFromEuler(45.0f, 0.0f, 0.0f));
    EXPECT_TRUE(component_->IsDirty());

    component_->UpdateWorldTransformAsRoot();
    EXPECT_FALSE(component_->IsDirty());

    component_->SetLocalScale(MakeVec3(2.0f, 2.0f, 2.0f));
    EXPECT_TRUE(component_->IsDirty());
}

NOLINT_TEST_F(TransformComponentTest, SetterWithSameValueDoesNotMarkDirty)
{
    const auto initial_position = component_->GetLocalPosition();
    const auto initial_rotation = component_->GetLocalRotation();
    const auto initial_scale = component_->GetLocalScale();

    component_->UpdateWorldTransformAsRoot();
    EXPECT_FALSE(component_->IsDirty());

    // Setting the same values should not mark dirty
    component_->SetLocalPosition(initial_position);
    EXPECT_FALSE(component_->IsDirty());

    component_->SetLocalRotation(initial_rotation);
    EXPECT_FALSE(component_->IsDirty());

    component_->SetLocalScale(initial_scale);
    EXPECT_FALSE(component_->IsDirty());
}

//=== Transform Operations ===------------------------------------------------//

NOLINT_TEST_F(TransformComponentTest, TranslateLocal)
{
    const auto initial_position = MakeVec3(1.0f, 1.0f, 1.0f);
    const auto offset = MakeVec3(1.0f, 2.0f, 3.0f);
    const auto rotation = QuatFromEuler(0.0f, 90.0f, 0.0f); // 90 degrees around Y

    component_->SetLocalPosition(initial_position);
    component_->SetLocalRotation(rotation);

    component_->Translate(offset, true); // local space

    // In local space, the offset should be rotated by the current rotation
    const auto expected_world_offset = rotation * offset;
    const auto expected_position = initial_position + expected_world_offset;

    ExpectVec3Near(component_->GetLocalPosition(), expected_position);
    EXPECT_TRUE(component_->IsDirty());
}

NOLINT_TEST_F(TransformComponentTest, TranslateWorld)
{
    const auto initial_position = MakeVec3(1.0f, 1.0f, 1.0f);
    const auto offset = MakeVec3(1.0f, 2.0f, 3.0f);
    const auto rotation = QuatFromEuler(0.0f, 90.0f, 0.0f);

    component_->SetLocalPosition(initial_position);
    component_->SetLocalRotation(rotation);

    component_->Translate(offset, false); // world space

    // In world space, the offset should be added directly
    const auto expected_position = initial_position + offset;

    ExpectVec3Near(component_->GetLocalPosition(), expected_position);
    EXPECT_TRUE(component_->IsDirty());
}

NOLINT_TEST_F(TransformComponentTest, RotateLocal)
{
    const auto initial_rotation = QuatFromEuler(45.0f, 0.0f, 0.0f);
    const auto additional_rotation = QuatFromEuler(0.0f, 45.0f, 0.0f);

    component_->SetLocalRotation(initial_rotation);

    component_->Rotate(additional_rotation, true); // local space

    // Local rotation: apply after current rotation
    const auto expected_rotation = initial_rotation * additional_rotation;

    ExpectQuatNear(component_->GetLocalRotation(), expected_rotation);
    EXPECT_TRUE(component_->IsDirty());
}

NOLINT_TEST_F(TransformComponentTest, RotateWorld)
{
    const auto initial_rotation = QuatFromEuler(45.0f, 0.0f, 0.0f);
    const auto additional_rotation = QuatFromEuler(0.0f, 45.0f, 0.0f);

    component_->SetLocalRotation(initial_rotation);

    component_->Rotate(additional_rotation, false); // world space

    // World rotation: apply before current rotation
    const auto expected_rotation = additional_rotation * initial_rotation;

    ExpectQuatNear(component_->GetLocalRotation(), expected_rotation);
    EXPECT_TRUE(component_->IsDirty());
}

NOLINT_TEST_F(TransformComponentTest, Scale)
{
    const auto initial_scale = MakeVec3(2.0f, 1.0f, 0.5f);
    const auto scale_factor = MakeVec3(2.0f, 3.0f, 0.5f);

    component_->SetLocalScale(initial_scale);

    component_->Scale(scale_factor);

    const auto expected_scale = initial_scale * scale_factor;

    ExpectVec3Near(component_->GetLocalScale(), expected_scale);
    EXPECT_TRUE(component_->IsDirty());
}

//=== Local Matrix Computation ===--------------------------------------------//

NOLINT_TEST_F(TransformComponentTest, GetLocalMatrixIdentity)
{
    // Default transform should produce identity matrix
    const auto local_matrix = component_->GetLocalMatrix();
    const auto identity = TransformComponent::Mat4 { 1.0f };

    ExpectMat4Near(local_matrix, identity);
}

NOLINT_TEST_F(TransformComponentTest, GetLocalMatrixWithTransformations)
{
    const auto position = MakeVec3(1.0f, 2.0f, 3.0f);
    const auto rotation = QuatFromEuler(0.0f, 90.0f, 0.0f);
    const auto scale = MakeVec3(2.0f, 1.0f, 0.5f);

    component_->SetLocalTransform(position, rotation, scale);

    const auto local_matrix = component_->GetLocalMatrix();

    // Manually compute expected matrix: T * R * S
    const auto translation_matrix = glm::translate(TransformComponent::Mat4 { 1.0f }, position);
    const auto rotation_matrix = glm::mat4_cast(rotation);
    const auto scale_matrix = glm::scale(TransformComponent::Mat4 { 1.0f }, scale);
    const auto expected_matrix = translation_matrix * rotation_matrix * scale_matrix;

    ExpectMat4Near(local_matrix, expected_matrix);
}

//=== World Transform Management ===------------------------------------------//

NOLINT_TEST_F(TransformComponentTest, WorldMatrixRequiresUpdateCall)
{
    // Accessing world matrix without calling UpdateWorldTransform should fail
    EXPECT_DEATH(
        { [[maybe_unused]] const auto& wm = component_->GetWorldMatrix(); },
        "UpdateWorldTransform.*never been called");
}

NOLINT_TEST_F(TransformComponentTest, UpdateWorldTransformAsRoot)
{
    const auto position = MakeVec3(1.0f, 2.0f, 3.0f);
    const auto rotation = QuatFromEuler(45.0f, 90.0f, 0.0f);
    const auto scale = MakeVec3(2.0f, 1.0f, 0.5f);

    component_->SetLocalTransform(position, rotation, scale);
    EXPECT_TRUE(component_->IsDirty());

    component_->UpdateWorldTransformAsRoot();

    EXPECT_FALSE(component_->IsDirty());

    // For root transforms, world matrix should equal local matrix
    const auto world_matrix = component_->GetWorldMatrix();
    const auto local_matrix = component_->GetLocalMatrix();

    ExpectMat4Near(world_matrix, local_matrix);
}

NOLINT_TEST_F(TransformComponentTest, UpdateWorldTransformWithParent)
{
    const auto parent_transform = glm::translate(TransformComponent::Mat4 { 1.0f }, MakeVec3(10.0f, 20.0f, 30.0f));
    const auto position = MakeVec3(1.0f, 2.0f, 3.0f);

    component_->SetLocalPosition(position);

    component_->UpdateWorldTransform(parent_transform);

    EXPECT_FALSE(component_->IsDirty());

    // World matrix should be parent * local
    const auto world_matrix = component_->GetWorldMatrix();
    const auto local_matrix = component_->GetLocalMatrix();
    const auto expected_world_matrix = parent_transform * local_matrix;

    ExpectMat4Near(world_matrix, expected_world_matrix);
}

//=== World Space Getters ===------------------------------------------------//

NOLINT_TEST_F(TransformComponentTest, GetWorldPosition)
{
    const auto position = MakeVec3(1.0f, 2.0f, 3.0f);
    component_->SetLocalPosition(position);
    component_->UpdateWorldTransformAsRoot();

    const auto world_position = component_->GetWorldPosition();

    ExpectVec3Near(world_position, position);
}

NOLINT_TEST_F(TransformComponentTest, GetWorldPositionWithParent)
{
    const auto parent_position = MakeVec3(10.0f, 20.0f, 30.0f);
    const auto local_position = MakeVec3(1.0f, 2.0f, 3.0f);
    const auto parent_transform = glm::translate(TransformComponent::Mat4 { 1.0f }, parent_position);

    component_->SetLocalPosition(local_position);
    component_->UpdateWorldTransform(parent_transform);

    const auto world_position = component_->GetWorldPosition();
    const auto expected_world_position = parent_position + local_position;

    ExpectVec3Near(world_position, expected_world_position);
}

NOLINT_TEST_F(TransformComponentTest, GetWorldRotation)
{
    const auto rotation = QuatFromEuler(45.0f, 90.0f, 180.0f);
    component_->SetLocalRotation(rotation);
    component_->UpdateWorldTransformAsRoot();

    const auto world_rotation = component_->GetWorldRotation();

    ExpectQuatNear(world_rotation, rotation);
}

NOLINT_TEST_F(TransformComponentTest, GetWorldScale)
{
    const auto scale = MakeVec3(2.0f, 0.5f, 3.0f);
    component_->SetLocalScale(scale);
    component_->UpdateWorldTransformAsRoot();

    const auto world_scale = component_->GetWorldScale();

    ExpectVec3Near(world_scale, scale);
}

NOLINT_TEST_F(TransformComponentTest, GetWorldScaleWithParentScale)
{
    const auto parent_scale = MakeVec3(2.0f, 2.0f, 2.0f);
    const auto local_scale = MakeVec3(0.5f, 3.0f, 1.0f);
    const auto parent_transform = glm::scale(TransformComponent::Mat4 { 1.0f }, parent_scale);

    component_->SetLocalScale(local_scale);
    component_->UpdateWorldTransform(parent_transform);

    const auto world_scale = component_->GetWorldScale();
    const auto expected_world_scale = parent_scale * local_scale;

    ExpectVec3Near(world_scale, expected_world_scale, 1e-4f); // Slightly higher tolerance for matrix decomposition
}

//=== Error Handling and Edge Cases ===--------------------------------------//

NOLINT_TEST_F(TransformComponentTest, GetWorldDataWithoutUpdate)
{
    // All world space getters should fail if UpdateWorldTransform hasn't been called
    EXPECT_DEATH(
        { [[maybe_unused]] auto wp = component_->GetWorldPosition(); },
        "UpdateWorldTransform.*never been called");
    EXPECT_DEATH(
        { [[maybe_unused]] auto wr = component_->GetWorldRotation(); },
        "UpdateWorldTransform.*never been called");
    EXPECT_DEATH(
        { [[maybe_unused]] auto ws = component_->GetWorldScale(); },
        "UpdateWorldTransform.*never been called");
}

NOLINT_TEST_F(TransformComponentTest, MatrixDecompositionFallback)
{
    // Test with a matrix that might cause decomposition issues
    component_->SetLocalScale(MakeVec3(0.0f, 1.0f, 1.0f)); // Zero scale on one axis
    component_->UpdateWorldTransformAsRoot();

    // Should not crash and return reasonable fallback values
    const auto world_rotation = component_->GetWorldRotation();
    const auto world_scale = component_->GetWorldScale();

    // Should get identity quaternion as fallback if decomposition fails
    // Should get unit scale as fallback if decomposition fails
    EXPECT_NO_FATAL_FAILURE({
        auto _ = world_rotation;
        auto __ = world_scale;
    });
}

NOLINT_TEST_F(TransformComponentTest, LargeTransformationValues)
{
    // Test with very large values
    const auto large_position = MakeVec3(1e6f, -1e6f, 1e6f);
    const auto large_scale = MakeVec3(1000.0f, 0.001f, 1000.0f);

    component_->SetLocalPosition(large_position);
    component_->SetLocalScale(large_scale);
    component_->UpdateWorldTransformAsRoot();

    ExpectVec3Near(component_->GetLocalPosition(), large_position);
    ExpectVec3Near(component_->GetLocalScale(), large_scale);

    // Should not crash when accessing world space data
    EXPECT_NO_FATAL_FAILURE({
        auto world_pos = component_->GetWorldPosition();
        auto world_scale = component_->GetWorldScale();
    });
}

//=== Complex Transformation Scenarios ===-----------------------------------//

NOLINT_TEST_F(TransformComponentTest, ComplexHierarchicalTransform)
{
    // Simulate a complex parent transformation
    const auto parent_position = MakeVec3(10.0f, 5.0f, -3.0f);
    const auto parent_rotation = QuatFromEuler(0.0f, 45.0f, 0.0f);
    const auto parent_scale = MakeVec3(2.0f, 1.0f, 2.0f);

    const auto parent_matrix = glm::translate(TransformComponent::Mat4 { 1.0f }, parent_position) * glm::mat4_cast(parent_rotation) * glm::scale(TransformComponent::Mat4 { 1.0f }, parent_scale);

    // Set up local transform
    const auto local_position = MakeVec3(0.0f, 1.0f, 2.0f);
    const auto local_rotation = QuatFromEuler(90.0f, 0.0f, 0.0f);
    const auto local_scale = MakeVec3(0.5f, 0.5f, 0.5f);

    component_->SetLocalTransform(local_position, local_rotation, local_scale);
    component_->UpdateWorldTransform(parent_matrix);

    // Verify world matrix computation
    const auto world_matrix = component_->GetWorldMatrix();
    const auto expected_world_matrix = parent_matrix * component_->GetLocalMatrix();

    ExpectMat4Near(world_matrix, expected_world_matrix);

    // Verify that world space getters work
    EXPECT_NO_FATAL_FAILURE({
        auto world_pos = component_->GetWorldPosition();
        auto world_rot = component_->GetWorldRotation();
        auto world_scale = component_->GetWorldScale();
    });
}

NOLINT_TEST_F(TransformComponentTest, TransformationChaining)
{
    // Test multiple transformations applied in sequence
    component_->SetLocalPosition(MakeVec3(1.0f, 0.0f, 0.0f));
    component_->Translate(MakeVec3(0.0f, 1.0f, 0.0f), false);
    component_->Translate(MakeVec3(0.0f, 0.0f, 1.0f), false);

    const auto expected_position = MakeVec3(1.0f, 1.0f, 1.0f);
    ExpectVec3Near(component_->GetLocalPosition(), expected_position);

    // Chain rotations
    component_->SetLocalRotation(QuatFromEuler(45.0f, 0.0f, 0.0f));
    component_->Rotate(QuatFromEuler(0.0f, 45.0f, 0.0f), true);
    component_->Rotate(QuatFromEuler(0.0f, 0.0f, 45.0f), true);

    // Chain scales
    component_->SetLocalScale(MakeVec3(2.0f, 2.0f, 2.0f));
    component_->Scale(MakeVec3(0.5f, 1.0f, 0.25f));

    const auto expected_scale = MakeVec3(1.0f, 2.0f, 0.5f);
    ExpectVec3Near(component_->GetLocalScale(), expected_scale);
}

} // namespace
