//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneQueryTestBase.h"

#include <Oxygen/Scene/SceneQuery.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::scene::testing {

namespace {

  //=== Construction Test Fixture
  //===------------------------------------------//

  class SceneQueryConstructionTest : public SceneQueryTestBase {
  protected:
    void SetUp() override
    {
      // Don't call base SetUp() - we need manual control for construction tests
      GetFactory().Reset();
    }
  };

  //=== Construction Tests ===------------------------------------------------//

  NOLINT_TEST_F(
    SceneQueryConstructionTest, Construction_WithValidScene_Succeeds)
  {
    // Arrange: Create a valid scene using TestSceneFactory
    auto scene = GetFactory().CreateSingleNodeScene("ValidScene");
    ASSERT_NE(scene, nullptr);

    // Act: Construct SceneQuery with valid scene
    auto query = std::make_unique<SceneQuery>(scene);

    // Assert: Query should be successfully constructed
    EXPECT_NE(query, nullptr);
  }

  NOLINT_TEST_F(
    SceneQueryConstructionTest, Construction_WithExpiredScene_HandlesGracefully)
  {
    // Arrange: Create a scene and let it expire
    std::shared_ptr<const Scene> scene_weak;
    {
      auto scene = GetFactory().CreateParentChildScene("TempScene");
      scene_weak = scene;
      // scene goes out of scope here
    }

    // Verify scene is expired
    ASSERT_TRUE(scene_weak.use_count() > 0); // Still referenced by scene_weak

    // Act: Construct SceneQuery with scene that will expire
    auto query = std::make_unique<SceneQuery>(scene_weak);

    // Clear the reference to make it truly expired
    scene_weak.reset();

    // Assert: Query should handle expired scene gracefully in operations
    EXPECT_NE(
      query, nullptr); // Test that operations fail gracefully on expired scene
    auto result = query->FindFirst([](const ConstVisitedNode& visited) {
      return visited.node_impl && visited.node_impl->GetName() == "Root";
    });
    EXPECT_FALSE(result.has_value());
  }

  NOLINT_TEST_F(
    SceneQueryConstructionTest, Construction_WithNullScene_HandlesGracefully)
  {
    // Arrange: Create null scene pointer
    std::shared_ptr<const Scene> null_scene = nullptr;

    // Act: Construct SceneQuery with null scene
    auto query = std::make_unique<SceneQuery>(null_scene);

    // Assert: Query should be constructed but operations should fail gracefully
    EXPECT_NE(
      query, nullptr); // Test that operations fail gracefully on null scene
    auto result = query->FindFirst([](const ConstVisitedNode& visited) {
      return visited.node_impl && visited.node_impl->GetName() == "Root";
    });
    EXPECT_FALSE(result.has_value());
  }

  NOLINT_TEST_F(
    SceneQueryConstructionTest, Construction_WithEmptyScene_Succeeds)
  {
    // Arrange: Create an empty scene using TestSceneFactory
    auto scene = GetFactory().CreateSingleNodeScene("EmptyScene");
    scene->Clear(); // Make it empty
    ASSERT_TRUE(scene->IsEmpty());

    // Act: Construct SceneQuery with empty scene
    auto query = std::make_unique<SceneQuery>(scene);

    // Assert: Query should be successfully constructed
    EXPECT_NE(query, nullptr); // Operations should work but find nothing
    auto result = query->FindFirst([](const ConstVisitedNode& visited) {
      return visited.node_impl && visited.node_impl->GetName() == "NonExistent";
    });
    EXPECT_FALSE(result.has_value());

    auto count_result = query->Count([](const ConstVisitedNode& visited) {
      return visited.node_impl && visited.node_impl->GetName() == "NonExistent";
    });
    EXPECT_EQ(count_result.nodes_matched, 0);
    EXPECT_TRUE(count_result.completed);
  }

  NOLINT_TEST_F(
    SceneQueryConstructionTest, Construction_WithComplexScene_Succeeds)
  {
    // Arrange: Create complex scene using JSON template from TestSceneFactory
    const auto json = R"({
    "scene": {
      "name": "ComplexConstructionTest",
      "nodes": [
        {
          "name": "Root1",
          "children": [
            {"name": "Child1A"},
            {"name": "Child1B"}
          ]
        },
        {
          "name": "Root2",
          "children": [
            {"name": "Child2A"},
            {
              "name": "Child2B",
              "children": [
                {"name": "Grandchild2B1"},
                {"name": "Grandchild2B2"}
              ]
            }
          ]
        }
      ]
    }
  })";

    auto scene = GetFactory().CreateFromJson(json, "ComplexConstructionTest");
    ASSERT_NE(scene, nullptr);
    ASSERT_FALSE(scene->IsEmpty());

    // Act: Construct SceneQuery with complex scene
    auto query = std::make_unique<SceneQuery>(scene);

    // Assert: Query should be successfully constructed and functional
    EXPECT_NE(query, nullptr); // Verify it can query the complex hierarchy
    auto root1 = query->FindFirst([](const ConstVisitedNode& visited) {
      return visited.node_impl && visited.node_impl->GetName() == "Root1";
    });
    EXPECT_TRUE(root1.has_value());
    EXPECT_EQ(root1->GetName(), "Root1");

    auto count_result
      = query->Count([](const ConstVisitedNode&) { return true; });
    EXPECT_GT(count_result.nodes_matched, 0);
    EXPECT_TRUE(count_result.completed);
  }

  NOLINT_TEST_F(
    SceneQueryConstructionTest, Construction_MultipleQueries_FromSameScene)
  {
    // Arrange: Create scene using TestSceneFactory
    auto scene = GetFactory().CreateBinaryTreeScene("SharedScene", 2);
    ASSERT_NE(scene, nullptr);

    // Act: Create multiple queries from same scene
    auto query1 = std::make_unique<SceneQuery>(scene);
    auto query2 = std::make_unique<SceneQuery>(scene);
    auto query3 = std::make_unique<SceneQuery>(scene);

    // Assert: All queries should be valid and independent
    EXPECT_NE(query1, nullptr);
    EXPECT_NE(query2, nullptr);
    EXPECT_NE(query3, nullptr); // Verify all queries work independently
    auto result1 = query1->FindFirst([](const ConstVisitedNode& visited) {
      return visited.node_impl && visited.node_impl->GetName() == "Root";
    });
    auto result2 = query2->FindFirst([](const ConstVisitedNode& visited) {
      return visited.node_impl && visited.node_impl->GetName() == "Root";
    });
    auto result3 = query3->FindFirst([](const ConstVisitedNode& visited) {
      return visited.node_impl && visited.node_impl->GetName() == "Root";
    });

    EXPECT_TRUE(result1.has_value());
    EXPECT_TRUE(result2.has_value());
    EXPECT_TRUE(result3.has_value());

    // All should find the same node but be independent operations
    EXPECT_EQ(result1->GetName(), "Root");
    EXPECT_EQ(result2->GetName(), "Root");
    EXPECT_EQ(result3->GetName(), "Root");
  }

} // namespace

} // namespace oxygen::scene::testing
