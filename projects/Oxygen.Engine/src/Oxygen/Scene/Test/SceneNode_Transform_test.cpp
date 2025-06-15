//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::ObjectMetaData;
using oxygen::scene::NodeHandle;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeImpl;

//------------------------------------------------------------------------------
// Anonymous namespace for test isolation
//------------------------------------------------------------------------------
namespace {

class SceneNodeTransformTest : public testing::Test {
protected:
  void SetUp() override { scene_ = std::make_shared<Scene>("TestScene", 1024); }
  void TearDown() override { scene_.reset(); }

  // Helper: Set transform values for testing
  static void SetTransformValues(
    const SceneNode& node, const glm::vec3& position, const glm::vec3& scale)
  {
    auto transform = node.GetTransform();
    EXPECT_TRUE(transform.SetLocalPosition(position));
    EXPECT_TRUE(transform.SetLocalScale(scale));
  }

  // Helper: Verify transform values match expected
  static void ExpectTransformValues(const SceneNode& node,
    const glm::vec3& expected_pos, const glm::vec3& expected_scale)
  {
    const auto transform = node.GetTransform();
    const auto position = transform.GetLocalPosition();
    const auto scale = transform.GetLocalScale();

    ASSERT_TRUE(position.has_value());
    ASSERT_TRUE(scale.has_value());
    EXPECT_EQ(*position, expected_pos);
    EXPECT_EQ(*scale, expected_scale);
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
    = transform.SetLocalPosition({ 1.0F, 2.0F, 3.0F });

  // Assert: Position should be set successfully
  EXPECT_TRUE(set_position_result);

  // Act: Get local position
  const auto position = transform.GetLocalPosition();

  // Assert: Position should match what was set
  ASSERT_TRUE(position.has_value());
  EXPECT_FLOAT_EQ(position->x, 1.0F);
  EXPECT_FLOAT_EQ(position->y, 2.0F);
  EXPECT_FLOAT_EQ(position->z, 3.0F);

  // Act: Set local scale
  const auto set_scale_result = transform.SetLocalScale({ 2.0F, 2.0F, 2.0F });

  // Assert: Scale should be set successfully
  EXPECT_TRUE(set_scale_result);

  // Act: Get local scale
  const auto scale = transform.GetLocalScale();

  // Assert: Scale should match what was set
  ASSERT_TRUE(scale.has_value());
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
  EXPECT_FALSE(transform.SetLocalPosition({ 1.0F, 2.0F, 3.0F }));
  EXPECT_FALSE(transform.GetLocalPosition().has_value());
  EXPECT_FALSE(transform.SetLocalScale({ 2.0F, 2.0F, 2.0F }));
  EXPECT_FALSE(transform.GetLocalScale().has_value());
}

NOLINT_TEST_F(
  SceneNodeTransformTest, TransformIntegration_ModificationsPreserved)
{
  // Arrange: Create node and set initial transform
  const auto node = scene_->CreateNode("TestNode");
  constexpr auto initial_pos = glm::vec3 { 1.0F, 2.0F, 3.0F };
  constexpr auto initial_scale = glm::vec3 { 2.0F, 2.0F, 2.0F };

  SetTransformValues(node, initial_pos, initial_scale);

  // Act: Verify initial values are set
  TRACE_GCHECK_F(
    ExpectTransformValues(node, initial_pos, initial_scale), "initial");

  // Act: Modify transform values
  constexpr auto new_pos = glm::vec3 { 10.0F, 20.0F, 30.0F };
  constexpr auto new_scale = glm::vec3 { 3.0F, 3.0F, 3.0F };
  SetTransformValues(node, new_pos, new_scale);

  // Assert: New values should be preserved
  TRACE_GCHECK_F(ExpectTransformValues(node, new_pos, new_scale), "new");
}

} // namespace
