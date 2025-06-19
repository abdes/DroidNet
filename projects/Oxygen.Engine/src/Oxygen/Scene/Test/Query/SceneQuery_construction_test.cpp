//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneQueryTestBase.h"

#include <Oxygen/Scene/SceneQuery.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::scene::ConstVisitedNode;
using oxygen::scene::Scene;
using oxygen::scene::SceneQuery;
using oxygen::scene::testing::SceneQueryTestBase;

namespace {

//=== Construction Test Fixture ===-------------------------------------------//

class SceneQueryConstructionTest : public SceneQueryTestBase {
protected:
  void SetUp() override
  {
    // Don't call base SetUp() - we need manual control for construction tests
    GetFactory().Reset();
  }
};

//=== Construction Tests ===--------------------------------------------------//

NOLINT_TEST_F(SceneQueryConstructionTest, Construction_WithValidScene_Succeeds)
{
  // Arrange: Create a valid scene using TestSceneFactory
  auto scene = GetFactory().CreateSingleNodeScene("ValidScene");
  ASSERT_NE(scene, nullptr);

  // Act: Construct SceneQuery with valid scene
  const auto query = std::make_unique<SceneQuery>(scene);

  // Assert: Query should be successfully constructed
  EXPECT_NE(query, nullptr);
}

NOLINT_TEST_F(
  SceneQueryConstructionTest, Construction_WithExpiredScene_HandlesGracefully)
{
  // Arrange: Create a scene and let it expire
  std::shared_ptr<const Scene> scene_weak;
  {
    const auto scene = GetFactory().CreateParentChildScene("TempScene");
    scene_weak = scene;
    // scene goes out of scope here
  }

  // Verify scene is expired
  ASSERT_TRUE(scene_weak.use_count() > 0); // Still referenced by scene_weak

  // Act: Construct SceneQuery with scene that will expire
  const auto query = std::make_unique<SceneQuery>(scene_weak);

  // Clear the reference to make it truly expired
  scene_weak.reset();

  // Assert: Query should handle expired scene gracefully in operations
  EXPECT_NE(
    query, nullptr); // Test that operations fail gracefully on expired scene
  const auto result = query->FindFirst([](const ConstVisitedNode& visited) {
    return visited.node_impl && visited.node_impl->GetName() == "Root";
  });
  EXPECT_FALSE(result.has_value());
}

NOLINT_TEST_F(
  SceneQueryConstructionTest, Construction_WithNullScene_HandlesGracefully)
{
  // Arrange: Create null scene pointer
  std::shared_ptr<const Scene> null_scene = nullptr;

  // Act & Assert: Constructing SceneQuery with null scene should cause death
  EXPECT_DEATH(
    { auto query = std::make_unique<SceneQuery>(null_scene); },
    ".*"); // Match any death message
}

NOLINT_TEST_F(SceneQueryConstructionTest, Construction_WithEmptyScene_Succeeds)
{
  // Arrange: Create an empty scene using TestSceneFactory
  auto scene = GetFactory().CreateSingleNodeScene("EmptyScene");
  scene->Clear(); // Make it empty
  ASSERT_TRUE(scene->IsEmpty());

  // Act: Construct SceneQuery with empty scene
  const auto query = std::make_unique<SceneQuery>(scene);

  // Assert: Query should be successfully constructed
  EXPECT_NE(query, nullptr); // Operations should work but find nothing
  const auto result = query->FindFirst([](const ConstVisitedNode& visited) {
    return visited.node_impl && visited.node_impl->GetName() == "NonExistent";
  });
  EXPECT_FALSE(result.has_value());

  const auto count_result = query->Count([](const ConstVisitedNode& visited) {
    return visited.node_impl && visited.node_impl->GetName() == "NonExistent";
  });
  EXPECT_EQ(count_result.nodes_matched, 0);
  EXPECT_TRUE(count_result.completed);
}

} // namespace
