//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::TypeId;
using oxygen::scene::detail::TransformComponent;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

class TransformComponentTest : public testing::Test {
protected:
  void SetUp() override
  {
    // Arrange: Create fresh component for each test
    component_ = std::make_unique<TransformComponent>();
  }

  void TearDown() override
  {
    // Clean up: Reset component pointer
    component_.reset();
  }

  // Helper: Create test vectors and quaternions
  static constexpr auto MakeVec3(const float x, const float y, const float z)
    -> TransformComponent::Vec3
  {
    return TransformComponent::Vec3 { x, y, z };
  }

  static auto MakeQuat(const float w, const float x, const float y,
    const float z) -> TransformComponent::Quat
  {
    return TransformComponent::Quat { w, x, y, z };
  }

  // Helper: Create normalized rotation quaternion from euler angles (degrees)
  static auto QuatFromEuler(const float pitch, const float yaw,
    const float roll) -> TransformComponent::Quat
  {
    return { glm::radians(TransformComponent::Vec3 { pitch, yaw, roll }) };
  }

  // Helper: Check if two vectors are approximately equal
  static void ExpectVec3Near(const TransformComponent::Vec3& actual,
    const TransformComponent::Vec3& expected, const float tolerance = 1e-5F)
  {
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
  }

  // Helper: Check if two quaternions are approximately equal
  // Handles the fact that q and -q represent the same rotation
  static void ExpectQuatNear(const TransformComponent::Quat& actual,
    const TransformComponent::Quat& expected, const float tolerance = 1e-5F)
  {
    // Check if quaternions are the same or negatives of each other
    // (both represent the same rotation)

    if (const auto dot_product = glm::dot(actual, expected);
      dot_product >= 0.0F) {
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

  // Helper: Check if two matrices are approximately equal
  static void ExpectMat4Near(const TransformComponent::Mat4& actual,
    const TransformComponent::Mat4& expected, const float tolerance = 1e-5F)
  {
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        EXPECT_NEAR(actual[i][j], expected[i][j], tolerance)
          << "Matrices differ at [" << i << "][" << j << "]";
      }
    }
  }

  // Helper: Verify component is in dirty state
  void ExpectComponentDirty() const { EXPECT_TRUE(component_->IsDirty()); }

  // Helper: Clear dirty state by updating world transform
  void ClearDirtyState() const
  {
    component_->UpdateWorldTransformAsRoot();
    EXPECT_FALSE(component_->IsDirty());
  }

  std::unique_ptr<TransformComponent> component_;
};

//------------------------------------------------------------------------------
// Construction and Default Values Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(
  TransformComponentTest, DefaultConstruction_InitializesIdentityTransform)
{
  // Arrange: Default constructed component (done in SetUp)

  // Act: Get default values
  const auto position = component_->GetLocalPosition();
  const auto rotation = component_->GetLocalRotation();
  const auto scale = component_->GetLocalScale();

  // Assert: Should initialize to identity transformation
  ExpectVec3Near(position, MakeVec3(0.0F, 0.0F, 0.0F));
  ExpectQuatNear(rotation, MakeQuat(1.0F, 0.0F, 0.0F, 0.0F));
  ExpectVec3Near(scale, MakeVec3(1.0F, 1.0F, 1.0F));
  ExpectComponentDirty();
}

//------------------------------------------------------------------------------
// Local Transform Setters and Getters Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(
  TransformComponentTest, SetLocalPosition_UpdatesPositionAndMarksDirty)
{
  // Arrange: Clear initial dirty state
  ClearDirtyState();
  constexpr auto test_position = MakeVec3(1.0F, 2.0F, 3.0F);

  // Act: Set local position
  component_->SetLocalPosition(test_position);

  // Assert: Position should be updated and component marked dirty
  ExpectVec3Near(component_->GetLocalPosition(), test_position);
  ExpectComponentDirty();
}

NOLINT_TEST_F(
  TransformComponentTest, SetLocalRotation_UpdatesRotationAndMarksDirty)
{
  // Arrange: Clear initial dirty state and create test rotation
  ClearDirtyState();
  const auto test_rotation = QuatFromEuler(45.0F, 90.0F, 180.0F);

  // Act: Set local rotation
  component_->SetLocalRotation(test_rotation);

  // Assert: Rotation should be updated and component marked dirty
  ExpectQuatNear(component_->GetLocalRotation(), test_rotation);
  ExpectComponentDirty();
}

NOLINT_TEST_F(TransformComponentTest, SetLocalScale_UpdatesScaleAndMarksDirty)
{
  // Arrange: Clear initial dirty state and create test scale
  ClearDirtyState();
  constexpr auto test_scale = MakeVec3(2.0F, 0.5F, 3.0F);

  // Act: Set local scale
  component_->SetLocalScale(test_scale);

  // Assert: Scale should be updated and component marked dirty
  ExpectVec3Near(component_->GetLocalScale(), test_scale);
  ExpectComponentDirty();
}

NOLINT_TEST_F(
  TransformComponentTest, SetLocalTransform_UpdatesAllComponentsAndMarksDirty)
{
  // Arrange: Clear initial dirty state and create test values
  ClearDirtyState();
  constexpr auto test_position = MakeVec3(1.0F, 2.0F, 3.0F);
  const auto test_rotation = QuatFromEuler(45.0F, 90.0F, 180.0F);
  constexpr auto test_scale = MakeVec3(2.0F, 0.5F, 3.0F);

  // Act: Set all transform components at once
  component_->SetLocalTransform(test_position, test_rotation, test_scale);

  // Assert: All components should be updated and marked dirty
  ExpectVec3Near(component_->GetLocalPosition(), test_position);
  ExpectQuatNear(component_->GetLocalRotation(), test_rotation);
  ExpectVec3Near(component_->GetLocalScale(), test_scale);
  ExpectComponentDirty();
}

//------------------------------------------------------------------------------
// Dirty State Management Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(TransformComponentTest, Setters_AllMarkComponentDirty)
{
  // Arrange: Clear dirty state for each setter test

  // Act & Assert: Position setter marks dirty
  ClearDirtyState();
  component_->SetLocalPosition(MakeVec3(1.0F, 0.0F, 0.0F));
  ExpectComponentDirty();

  // Act & Assert: Rotation setter marks dirty
  ClearDirtyState();
  component_->SetLocalRotation(QuatFromEuler(45.0F, 0.0F, 0.0F));
  ExpectComponentDirty();

  // Act & Assert: Scale setter marks dirty
  ClearDirtyState();
  component_->SetLocalScale(MakeVec3(2.0F, 2.0F, 2.0F));
  ExpectComponentDirty();
}

NOLINT_TEST_F(TransformComponentTest, SetterWithSameValue_DoesNotMarkDirty)
{
  // Arrange: Get initial values and clear dirty state
  const auto initial_position = component_->GetLocalPosition();
  const auto initial_rotation = component_->GetLocalRotation();
  const auto initial_scale = component_->GetLocalScale();
  ClearDirtyState();

  // Act: Set the same values
  component_->SetLocalPosition(initial_position);
  component_->SetLocalRotation(initial_rotation);
  component_->SetLocalScale(initial_scale);

  // Assert: Component should remain clean (not dirty)
  EXPECT_FALSE(component_->IsDirty());
}

//------------------------------------------------------------------------------
// Transform Operations Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(TransformComponentTest, TranslateLocal_AppliesRotatedOffset)
{
  // Arrange: Set initial position and rotation for local space calculation
  constexpr auto initial_position = MakeVec3(1.0F, 1.0F, 1.0F);
  constexpr auto offset = MakeVec3(1.0F, 2.0F, 3.0F);
  const auto rotation = QuatFromEuler(0.0F, 90.0F, 0.0F); // 90 degrees around Y

  component_->SetLocalPosition(initial_position);
  component_->SetLocalRotation(rotation);

  // Act: Translate in local space
  component_->Translate(offset, true);

  // Assert: Offset should be rotated by current rotation and added to
  // position
  const auto expected_world_offset = rotation * offset;
  const auto expected_position = initial_position + expected_world_offset;
  ExpectVec3Near(component_->GetLocalPosition(), expected_position);
  ExpectComponentDirty();
}

NOLINT_TEST_F(TransformComponentTest, TranslateWorld_AppliesDirectOffset)
{
  // Arrange: Set initial position and rotation
  constexpr auto initial_position = MakeVec3(1.0F, 1.0F, 1.0F);
  constexpr auto offset = MakeVec3(1.0F, 2.0F, 3.0F);
  const auto rotation = QuatFromEuler(0.0F, 90.0F, 0.0F);

  component_->SetLocalPosition(initial_position);
  component_->SetLocalRotation(rotation);

  // Act: Translate in world space
  component_->Translate(offset, false);

  // Assert: Offset should be added directly without rotation
  constexpr auto expected_position = initial_position + offset;
  ExpectVec3Near(component_->GetLocalPosition(), expected_position);
  ExpectComponentDirty();
}

NOLINT_TEST_F(TransformComponentTest, RotateLocal_AppliesAfterCurrentRotation)
{
  // Arrange: Set initial rotation and additional rotation
  const auto initial_rotation = QuatFromEuler(45.0F, 0.0F, 0.0F);
  const auto additional_rotation = QuatFromEuler(0.0F, 45.0F, 0.0F);
  component_->SetLocalRotation(initial_rotation);

  // Act: Rotate in local space
  component_->Rotate(additional_rotation, true);

  // Assert: Local rotation applies after current rotation
  const auto expected_rotation = initial_rotation * additional_rotation;
  ExpectQuatNear(component_->GetLocalRotation(), expected_rotation);
  ExpectComponentDirty();
}

NOLINT_TEST_F(TransformComponentTest, RotateWorld_AppliesBeforeCurrentRotation)
{
  // Arrange: Set initial rotation and additional rotation
  const auto initial_rotation = QuatFromEuler(45.0F, 0.0F, 0.0F);
  const auto additional_rotation = QuatFromEuler(0.0F, 45.0F, 0.0F);
  component_->SetLocalRotation(initial_rotation);

  // Act: Rotate in world space
  component_->Rotate(additional_rotation, false);

  // Assert: World rotation applies before current rotation
  const auto expected_rotation = additional_rotation * initial_rotation;
  ExpectQuatNear(component_->GetLocalRotation(), expected_rotation);
  ExpectComponentDirty();
}

NOLINT_TEST_F(TransformComponentTest, Scale_MultipliesCurrentScale)
{
  // Arrange: Set initial scale and scale factor
  constexpr auto initial_scale = MakeVec3(2.0F, 1.0F, 0.5F);
  constexpr auto scale_factor = MakeVec3(2.0F, 3.0F, 0.5F);
  component_->SetLocalScale(initial_scale);

  // Act: Apply scale factor
  component_->Scale(scale_factor);

  // Assert: Scale should be multiplied component-wise
  constexpr auto expected_scale = initial_scale * scale_factor;
  ExpectVec3Near(component_->GetLocalScale(), expected_scale);
  ExpectComponentDirty();
}

//------------------------------------------------------------------------------
// Local Matrix Computation Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(
  TransformComponentTest, GetLocalMatrix_IdentityTransformProducesIdentity)
{
  // Arrange: Default identity transform (done in SetUp)

  // Act: Get local matrix
  const auto local_matrix = component_->GetLocalMatrix();

  // Assert: Should produce identity matrix
  constexpr auto identity = TransformComponent::Mat4 { 1.0F };
  ExpectMat4Near(local_matrix, identity);
}

NOLINT_TEST_F(
  TransformComponentTest, GetLocalMatrix_ComputesCorrectTransformation)
{
  // Arrange: Set specific transform values
  constexpr auto position = MakeVec3(1.0F, 2.0F, 3.0F);
  const auto rotation = QuatFromEuler(0.0F, 90.0F, 0.0F);
  constexpr auto scale = MakeVec3(2.0F, 1.0F, 0.5F);
  component_->SetLocalTransform(position, rotation, scale);

  // Act: Get local matrix
  const auto local_matrix = component_->GetLocalMatrix();

  // Assert: Matrix should match manually computed T * R * S
  constexpr auto translation_matrix
    = glm::translate(TransformComponent::Mat4 { 1.0F }, position);
  const auto rotation_matrix = glm::mat4_cast(rotation);
  const auto scale_matrix
    = glm::scale(TransformComponent::Mat4 { 1.0F }, scale);
  const auto expected_matrix
    = translation_matrix * rotation_matrix * scale_matrix;

  ExpectMat4Near(local_matrix, expected_matrix);
}

//------------------------------------------------------------------------------
// World Transform Management Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(TransformComponentTest, GetWorldMatrix_RequiresUpdateCall)
{
  // Arrange: Fresh component that hasn't had UpdateWorldTransform called

  // Act & Assert: Accessing world matrix without update should abort
  EXPECT_DEATH(
    { [[maybe_unused]] auto wm = component_->GetWorldMatrix(); },
    "UpdateWorldTransform.*never been called");
}

NOLINT_TEST_F(TransformComponentTest,
  UpdateWorldTransformAsRoot_ClearsDirtyAndEnablesWorldAccess)
{
  // Arrange: Set specific transform values
  constexpr auto position = MakeVec3(1.0F, 2.0F, 3.0F);
  const auto rotation = QuatFromEuler(45.0F, 90.0F, 0.0F);
  constexpr auto scale = MakeVec3(2.0F, 1.0F, 0.5F);
  component_->SetLocalTransform(position, rotation, scale);
  ExpectComponentDirty();

  // Act: Update world transform as root
  component_->UpdateWorldTransformAsRoot();

  // Assert: Component should be clean and world matrix accessible
  EXPECT_FALSE(component_->IsDirty());

  // Assert: For root transforms, world matrix should equal local matrix
  const auto world_matrix = component_->GetWorldMatrix();
  const auto local_matrix = component_->GetLocalMatrix();
  ExpectMat4Near(world_matrix, local_matrix);
}

NOLINT_TEST_F(TransformComponentTest,
  UpdateWorldTransformWithParent_ComputesCorrectWorldMatrix)
{
  // Arrange: Set up parent transform and local transform
  constexpr auto parent_transform = glm::translate(
    TransformComponent::Mat4 { 1.0F }, MakeVec3(10.0F, 20.0F, 30.0F));
  constexpr auto position = MakeVec3(1.0F, 2.0F, 3.0F);
  component_->SetLocalPosition(position);

  // Act: Update world transform with parent
  component_->UpdateWorldTransform(parent_transform);

  // Assert: Component should be clean
  EXPECT_FALSE(component_->IsDirty());

  // Assert: World matrix should be parent * local
  const auto world_matrix = component_->GetWorldMatrix();
  const auto local_matrix = component_->GetLocalMatrix();
  const auto expected_world_matrix = parent_transform * local_matrix;
  ExpectMat4Near(world_matrix, expected_world_matrix);
}

//------------------------------------------------------------------------------
// World Space Getters Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(
  TransformComponentTest, GetWorldPosition_ReturnsCorrectPositionForRoot)
{
  // Arrange: Set local position and update as root
  constexpr auto position = MakeVec3(1.0F, 2.0F, 3.0F);
  component_->SetLocalPosition(position);
  component_->UpdateWorldTransformAsRoot();

  // Act: Get world position
  const auto world_position = component_->GetWorldPosition();

  // Assert: World position should match local position for root
  ExpectVec3Near(world_position, position);
}

NOLINT_TEST_F(
  TransformComponentTest, GetWorldPosition_IncorporatesParentTransform)
{
  // Arrange: Set up parent and local positions
  constexpr auto parent_position = MakeVec3(10.0F, 20.0F, 30.0F);
  constexpr auto local_position = MakeVec3(1.0F, 2.0F, 3.0F);
  constexpr auto parent_transform
    = glm::translate(TransformComponent::Mat4 { 1.0F }, parent_position);

  component_->SetLocalPosition(local_position);
  component_->UpdateWorldTransform(parent_transform);

  // Act: Get world position
  const auto world_position = component_->GetWorldPosition();

  // Assert: World position should combine parent and local positions
  constexpr auto expected_world_position = parent_position + local_position;
  ExpectVec3Near(world_position, expected_world_position);
}

NOLINT_TEST_F(
  TransformComponentTest, GetWorldRotation_ReturnsCorrectRotationForRoot)
{
  // Arrange: Set local rotation and update as root
  const auto rotation = QuatFromEuler(45.0F, 90.0F, 180.0F);
  component_->SetLocalRotation(rotation);
  component_->UpdateWorldTransformAsRoot();

  // Act: Get world rotation
  const auto world_rotation = component_->GetWorldRotation();

  // Assert: World rotation should match local rotation for root
  ExpectQuatNear(world_rotation, rotation);
}

NOLINT_TEST_F(TransformComponentTest, GetWorldScale_ReturnsCorrectScaleForRoot)
{
  // Arrange: Set local scale and update as root
  constexpr auto scale = MakeVec3(2.0F, 0.5F, 3.0F);
  component_->SetLocalScale(scale);
  component_->UpdateWorldTransformAsRoot();

  // Act: Get world scale
  const auto world_scale = component_->GetWorldScale();

  // Assert: World scale should match local scale for root
  ExpectVec3Near(world_scale, scale);
}

NOLINT_TEST_F(TransformComponentTest, GetWorldScale_IncorporatesParentScale)
{
  // Arrange: Set up parent and local scales
  constexpr auto parent_scale = MakeVec3(2.0F, 2.0F, 2.0F);
  constexpr auto local_scale = MakeVec3(0.5F, 3.0F, 1.0F);
  const auto parent_transform
    = glm::scale(TransformComponent::Mat4 { 1.0F }, parent_scale);

  component_->SetLocalScale(local_scale);
  component_->UpdateWorldTransform(parent_transform);

  // Act: Get world scale
  const auto world_scale = component_->GetWorldScale();

  // Assert: World scale should combine parent and local scales
  constexpr auto expected_world_scale = parent_scale * local_scale;
  ExpectVec3Near(world_scale, expected_world_scale,
    1e-4F); // Slightly higher tolerance for matrix decomposition
}

//------------------------------------------------------------------------------
// Error Handling and Edge Cases Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(TransformComponentTest, GetWorldData_RequiresUpdateCall)
{
  // Arrange: Fresh component without UpdateWorldTransform called

  // Act & Assert: All world space getters should abort if update not called
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

NOLINT_TEST_F(TransformComponentTest,
  MatrixDecomposition_HandlesDecompositionIssuesGracefully)
{
  // Arrange: Set up problematic matrix (zero scale on one axis)
  component_->SetLocalScale(MakeVec3(0.0F, 1.0F, 1.0F));
  component_->UpdateWorldTransformAsRoot();

  // Act & Assert: Should not crash and return reasonable fallback values
  EXPECT_NO_FATAL_FAILURE({
    [[maybe_unused]] auto world_rotation = component_->GetWorldRotation();
    [[maybe_unused]] auto world_scale = component_->GetWorldScale();
  });
}

NOLINT_TEST_F(
  TransformComponentTest, LargeTransformationValues_HandledCorrectly)
{
  // Arrange: Set very large transformation values
  constexpr auto large_position = MakeVec3(1e6F, -1e6F, 1e6F);
  constexpr auto large_scale = MakeVec3(1000.0F, 0.001F, 1000.0F);

  component_->SetLocalPosition(large_position);
  component_->SetLocalScale(large_scale);
  component_->UpdateWorldTransformAsRoot();

  // Act & Assert: Values should be preserved correctly
  ExpectVec3Near(component_->GetLocalPosition(), large_position);
  ExpectVec3Near(component_->GetLocalScale(), large_scale);

  // Act & Assert: World space access should not crash
  EXPECT_NO_FATAL_FAILURE({
    [[maybe_unused]] auto world_pos = component_->GetWorldPosition();
    [[maybe_unused]] auto world_scale = component_->GetWorldScale();
  });
}

//------------------------------------------------------------------------------
// Complex Transformation Scenarios Tests
//------------------------------------------------------------------------------

NOLINT_TEST_F(TransformComponentTest,
  ComplexHierarchicalTransform_ComputesCorrectWorldMatrix)
{
  // Arrange: Set up complex parent transformation
  auto parent_position = MakeVec3(10.0F, 5.0F, -3.0F);
  auto parent_rotation = QuatFromEuler(0.0F, 45.0F, 0.0F);
  auto parent_scale = MakeVec3(2.0F, 1.0F, 2.0F);

  auto parent_matrix
    = glm::translate(TransformComponent::Mat4 { 1.0F }, parent_position)
    * glm::mat4_cast(parent_rotation)
    * glm::scale(TransformComponent::Mat4 { 1.0F }, parent_scale);

  // Arrange: Set up local transform
  auto local_position = MakeVec3(0.0F, 1.0F, 2.0F);
  auto local_rotation = QuatFromEuler(90.0F, 0.0F, 0.0F);
  auto local_scale = MakeVec3(0.5F, 0.5F, 0.5F);
  component_->SetLocalTransform(local_position, local_rotation, local_scale);

  // Act: Update with parent transform
  component_->UpdateWorldTransform(parent_matrix);

  // Assert: World matrix should be correctly computed
  auto world_matrix = component_->GetWorldMatrix();
  auto expected_world_matrix = parent_matrix * component_->GetLocalMatrix();
  ExpectMat4Near(world_matrix, expected_world_matrix);

  // Assert: World space getters should work without crashing
  EXPECT_NO_FATAL_FAILURE({
    [[maybe_unused]] auto world_pos = component_->GetWorldPosition();
    [[maybe_unused]] auto world_rot = component_->GetWorldRotation();
    [[maybe_unused]] auto world_scale = component_->GetWorldScale();
  });
}

NOLINT_TEST_F(
  TransformComponentTest, TransformationChaining_MultipleOperationsWork)
{
  // Arrange: Start with default transform

  // Act: Chain multiple position translations
  component_->SetLocalPosition(MakeVec3(1.0F, 0.0F, 0.0F));
  component_->Translate(MakeVec3(0.0F, 1.0F, 0.0F), false);
  component_->Translate(MakeVec3(0.0F, 0.0F, 1.0F), false);

  // Assert: Final position should be cumulative
  constexpr auto expected_position = MakeVec3(1.0F, 1.0F, 1.0F);
  ExpectVec3Near(component_->GetLocalPosition(), expected_position);

  // Act: Chain rotations
  component_->SetLocalRotation(QuatFromEuler(45.0F, 0.0F, 0.0F));
  component_->Rotate(QuatFromEuler(0.0F, 45.0F, 0.0F), true);
  component_->Rotate(QuatFromEuler(0.0F, 0.0F, 45.0F), true);

  // Act: Chain scales
  component_->SetLocalScale(MakeVec3(2.0F, 2.0F, 2.0F));
  component_->Scale(MakeVec3(0.5F, 1.0F, 0.25F));

  // Assert: Final scale should be cumulative
  constexpr auto expected_scale = MakeVec3(1.0F, 2.0F, 0.5F);
  ExpectVec3Near(component_->GetLocalScale(), expected_scale);
}

NOLINT_TEST_F(TransformComponentTest, IdentityOperations_DoNotChangeTransform)
{
  // Arrange: Set up non-identity transform
  constexpr auto initial_position = MakeVec3(1.0F, 2.0F, 3.0F);
  const auto initial_rotation = QuatFromEuler(45.0F, 90.0F, 0.0F);
  constexpr auto initial_scale = MakeVec3(2.0F, 0.5F, 1.5F);
  component_->SetLocalTransform(
    initial_position, initial_rotation, initial_scale);
  ClearDirtyState();

  // Act: Apply identity operations
  component_->Translate(MakeVec3(0.0F, 0.0F, 0.0F), false); // Zero translation
  component_->Rotate(
    MakeQuat(1.0F, 0.0F, 0.0F, 0.0F), true); // Identity rotation
  component_->Scale(MakeVec3(1.0F, 1.0F, 1.0F)); // Unity scale

  // Assert: Transform should remain unchanged
  ExpectVec3Near(component_->GetLocalPosition(), initial_position);
  ExpectQuatNear(component_->GetLocalRotation(), initial_rotation);
  ExpectVec3Near(component_->GetLocalScale(), initial_scale);
}

} // namespace
