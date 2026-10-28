//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <memory>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::scene::Scene;
using oxygen::scene::SceneNode;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

class SceneNodeTransformTest : public testing::Test {
protected:
  void SetUp() override
  {
    static constexpr size_t kTestSceneCapacity = 1024;
    scene_ = std::make_shared<Scene>("TestScene", kTestSceneCapacity);
  }
  void TearDown() override { scene_.reset(); }

  //! A node's local position and scale.
  struct TransformValues {
    ::oxygen::Vec3 position;
    ::oxygen::Vec3 scale;
  };

  // Helper: Set transform values for testing
  static void SetTransformValues(
    const SceneNode& node, const TransformValues& values)
  {
    auto transform = node.GetTransform();
    EXPECT_TRUE(transform.SetLocalPosition(values.position));
    EXPECT_TRUE(transform.SetLocalScale(values.scale));
  }

  // Helper: Verify transform values match expected
  static void ExpectTransformValues(
    const SceneNode& node, const TransformValues& expected)
  {
    const auto transform = node.GetTransform();
    const auto position = transform.GetLocalPosition();
    const auto scale = transform.GetLocalScale();

    ASSERT_HAS_VALUE(position);
    ASSERT_HAS_VALUE(scale);
    EXPECT_EQ(*position, expected.position);
    EXPECT_EQ(*scale, expected.scale);
  }

  std::shared_ptr<Scene> scene_;
};

NOLINT_TEST_F(SceneNodeTransformTest, GetTransformWithValidNode_CreatesWrapper)
{
  // Arrange: Create a valid test node
  auto node = scene_->CreateNode("TestNode");

  // Act & Assert: Should be able to create Transform wrapper without error
  NOLINT_EXPECT_NO_THROW({
    [[maybe_unused]] auto transform = node.GetTransform();
    [[maybe_unused]] auto const_transform
      = static_cast<const SceneNode&>(node).GetTransform();
  });
}

NOLINT_TEST_F(
  SceneNodeTransformTest, GetTransformWithInvalidNode_HandlesGracefully)
{
  // Arrange: Create a node then destroy it
  auto node = scene_->CreateNode("TestNode");
  scene_->DestroyNode(node);

  // Act & Assert: Should still create wrapper (handles invalid nodes
  // gracefully)
  NOLINT_EXPECT_NO_THROW({
    [[maybe_unused]] auto transform = node.GetTransform();
    [[maybe_unused]] auto const_transform
      = static_cast<const SceneNode&>(node).GetTransform();
  });
}

NOLINT_TEST_F(SceneNodeTransformTest, TransformBasicOperations_WorkOnValidNode)
{
  // Arrange: Create a valid node and get transform wrapper
  auto node = scene_->CreateNode("TestNode");
  auto transform = node.GetTransform();

  // Act: Set local position
  const auto set_position_result
    = transform.SetLocalPosition(::oxygen::Vec3 { 1.0F, 2.0F, 3.0F });

  // Assert: Position should be set successfully
  EXPECT_TRUE(set_position_result);

  // Act: Get local position
  const auto position = transform.GetLocalPosition();

  // Assert: Position should match what was set
  ASSERT_HAS_VALUE(position);
  EXPECT_FLOAT_EQ(position->x, 1.0F);
  EXPECT_FLOAT_EQ(position->y, 2.0F);
  EXPECT_FLOAT_EQ(position->z, 3.0F);

  // Act: Set local scale
  const auto set_scale_result
    = transform.SetLocalScale(::oxygen::Vec3 { 2.0F, 2.0F, 2.0F });

  // Assert: Scale should be set successfully
  EXPECT_TRUE(set_scale_result);

  // Act: Get local scale
  const auto scale = transform.GetLocalScale();

  // Assert: Scale should match what was set
  ASSERT_HAS_VALUE(scale);
  EXPECT_FLOAT_EQ(scale->x, 2.0F);
  EXPECT_FLOAT_EQ(scale->y, 2.0F);
  EXPECT_FLOAT_EQ(scale->z, 2.0F);
}

NOLINT_TEST_F(
  SceneNodeTransformTest, TransformOperationsOnInvalidNode_FailGracefully)
{
  // Arrange: Create node, get transform, then destroy node
  auto node = scene_->CreateNode("TestNode");
  auto transform = node.GetTransform();
  scene_->DestroyNode(node);

  // Act & Assert: Operations should fail gracefully and return false/nullopt
  EXPECT_FALSE(transform.SetLocalPosition(::oxygen::Vec3 { 1.0F, 2.0F, 3.0F }));
  EXPECT_FALSE(transform.GetLocalPosition().has_value());
  EXPECT_FALSE(transform.SetLocalScale(::oxygen::Vec3 { 2.0F, 2.0F, 2.0F }));
  EXPECT_FALSE(transform.GetLocalScale().has_value());
}

NOLINT_TEST_F(
  SceneNodeTransformTest, TransformIntegration_ModificationsPreserved)
{
  // Arrange: Create node and set initial transform
  const auto node = scene_->CreateNode("TestNode");
  constexpr TransformValues initial {
    .position = { 1.0F, 2.0F, 3.0F },
    .scale = { 2.0F, 2.0F, 2.0F },
  };

  SetTransformValues(node, initial);

  // Act: Verify initial values are set
  TRACE_GCHECK_F(ExpectTransformValues(node, initial), "initial");

  // Act: Modify transform values
  constexpr TransformValues modified {
    .position = { 10.0F, 20.0F, 30.0F },
    .scale = { 3.0F, 3.0F, 3.0F },
  };
  SetTransformValues(node, modified);

  // Assert: New values should be preserved
  TRACE_GCHECK_F(ExpectTransformValues(node, modified), "new");
}

NOLINT_TEST_F(
  SceneNodeTransformTest, LookAt_AlignsOxygenForwardMinusYTowardTarget)
{
  auto node = scene_->CreateNode("LookAtNode");
  auto transform = node.GetTransform();
  const auto impl = node.GetImpl();
  ASSERT_HAS_VALUE(impl);

  ASSERT_TRUE(transform.SetLocalPosition(::oxygen::Vec3 { 0.0F, 0.0F, 0.0F }));
  impl->get().UpdateTransforms(*scene_);
  ASSERT_TRUE(transform.LookAt(::oxygen::Vec3 { 0.0F, -10.0F, 0.0F }));
  impl->get().UpdateTransforms(*scene_);

  const auto world_rotation = transform.GetWorldRotation();
  ASSERT_HAS_VALUE(world_rotation);

  const auto world_forward = (*world_rotation) * ::oxygen::space::move::Forward;
  EXPECT_NEAR(world_forward.x, 0.0F, 1.0e-5F);
  EXPECT_NEAR(world_forward.y, -1.0F, 1.0e-5F);
  EXPECT_NEAR(world_forward.z, 0.0F, 1.0e-5F);
}

//! LookAt yields a proper rotation: the node faces the target with its up
//! toward the requested up, never a mirrored basis.
NOLINT_TEST_F(SceneNodeTransformTest, LookAt_FacesTargetWithUpAsProperRotation)
{
  auto node = scene_->CreateNode("LookAtNode");
  auto transform = node.GetTransform();
  const auto impl = node.GetImpl();
  ASSERT_HAS_VALUE(impl);

  const ::oxygen::Vec3 position { 1.0F, -2.0F, 3.0F };
  const ::oxygen::Vec3 target { 6.0F, 4.0F, 0.0F };
  ASSERT_TRUE(transform.SetLocalPosition(position));
  impl->get().UpdateTransforms(*scene_);
  ASSERT_TRUE(transform.LookAt(target));
  impl->get().UpdateTransforms(*scene_);

  const auto rotation = transform.GetLocalRotation();
  ASSERT_HAS_VALUE(rotation);
  const auto forward = (*rotation) * ::oxygen::space::node::Forward;
  const auto expected = glm::normalize(target - position);
  EXPECT_NEAR(forward.x, expected.x, 1.0e-5F);
  EXPECT_NEAR(forward.y, expected.y, 1.0e-5F);
  EXPECT_NEAR(forward.z, expected.z, 1.0e-5F);
  const auto up = (*rotation) * ::oxygen::space::node::Up;
  EXPECT_NEAR(glm::dot(up, forward), 0.0F, 1.0e-5F);
  EXPECT_GT(up.z, 0.0F);
  EXPECT_NEAR(glm::determinant(glm::mat3_cast(*rotation)), 1.0F, 1.0e-5F);
}

} // namespace
