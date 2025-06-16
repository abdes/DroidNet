//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneQueryTestBase.h"

#include <vector>

#include <Oxygen/Testing/GTest.h>

namespace oxygen::scene::testing {

namespace {

  //=== Error Handling Test Fixture ==========================================//

  class SceneQueryErrorTest : public SceneQueryTestBase {
  protected:
    void SetUp() override
    {
      // Create complex scene for error testing
      CreateGameSceneHierarchy();
    }
  };

  //=== Scene Lifetime Tests =================================================//

  NOLINT_TEST_F(SceneQueryErrorTest, Query_WithExpiredScene_HandlesGracefully)
  {
    // Arrange: Create query with scene that will expire
    std::shared_ptr<Scene> temp_scene;
    std::unique_ptr<SceneQuery> temp_query;

    {
      temp_scene = GetFactory().CreateLinearChainScene("TempScene", 5);
      temp_query = std::make_unique<SceneQuery>(temp_scene);

      // Verify query works initially
      auto initial_result = temp_query->FindFirst(NodeNameEquals("Root"));
      EXPECT_TRUE(initial_result.has_value());
    }

    // Let scene expire
    temp_scene.reset();

    // Act: Try to use query after scene expiry
    auto find_result = temp_query->FindFirst(NodeNameEquals("Root"));
    auto any_result = temp_query->Any(NodeNameEquals("Root"));
    auto count_result = temp_query->Count(NodeNameEquals("Root"));

    std::vector<SceneNode> nodes;
    auto collect_result = temp_query->Collect(nodes, NodeNameEquals("Root"));

    auto batch_result = temp_query->ExecuteBatch([&](const auto& q) {
      auto batch_find = q.FindFirst(NodeNameEquals("Root"));
      EXPECT_FALSE(batch_find.has_value());
    });

    // Assert: All operations should fail gracefully
    EXPECT_FALSE(find_result.has_value());
    EXPECT_FALSE(any_result.has_value());
    EXPECT_FALSE(count_result.completed);
    EXPECT_FALSE(collect_result.completed);
    EXPECT_FALSE(batch_result.completed);
    EXPECT_TRUE(nodes.empty());
  }

  NOLINT_TEST_F(SceneQueryErrorTest, Query_WithModifiedScene_DocumentedBehavior)
  {
    // Arrange: Create scene and query
    auto test_scene = GetFactory().CreateParentChildScene("ModificationTest");
    auto test_query = std::make_unique<SceneQuery>(test_scene);

    // Get initial node count
    auto initial_count
      = test_query->Count([](const ConstVisitedNode&) { return true; });
    EXPECT_TRUE(initial_count.completed);

    // Act: Modify scene during query existence (documented as undefined
    // behavior) Note: This test documents the current behavior, not guarantees
    auto new_node = test_scene->CreateNode("NewNode");
    EXPECT_TRUE(new_node.IsValid());

    // Act: Query after modification
    auto after_count
      = test_query->Count([](const ConstVisitedNode&) { return true; });

    // Assert: Behavior is undefined but should not crash
    // The count may or may not reflect the new node
    EXPECT_TRUE(after_count.completed);
    EXPECT_GE(after_count.nodes_matched, initial_count.nodes_matched);

    // Document that concurrent modification during traversal is undefined
    // This test ensures we don't crash, but results are not guaranteed
  }

  NOLINT_TEST_F(
    SceneQueryErrorTest, Query_WithSceneClearDuringQuery_HandlesGracefully)
  {
    // Arrange: Create scene and start a batch operation
    auto test_scene = GetFactory().CreateBinaryTreeScene("ClearTest", 3);
    auto test_query = std::make_unique<SceneQuery>(test_scene);

    std::vector<SceneNode> collected_nodes;
    bool clear_called = false;

    // Act: Attempt to clear scene conceptually during query
    // Note: Actual concurrent modification is undefined, this tests the pattern
    auto result = test_query->ExecuteBatch([&](const auto& q) {
      // Simulate discovering we need to clear during query
      auto first_result = q.FindFirst(NodeNameEquals("Root"));
      EXPECT_TRUE(first_result.has_value());

      clear_called = true;
      // In real code, clearing during traversal would be undefined behavior
      // This test just documents the pattern
    });

    // After the query, clear the scene
    if (clear_called) {
      test_scene->Clear();
      EXPECT_TRUE(test_scene->IsEmpty());
    }

    // Assert: Query should have completed before clear
    EXPECT_TRUE(result.completed);
    EXPECT_TRUE(clear_called);
  }

  //=== Predicate Error Tests ================================================//

  NOLINT_TEST_F(
    SceneQueryErrorTest, Query_WithThrowingPredicate_HandlesException)
  {
    // Arrange: Create predicate that throws
    auto throwing_predicate
      = [call_count = 0](const ConstVisitedNode&) mutable -> bool {
      ++call_count;
      if (call_count == 3) {
        throw std::runtime_error("Test exception");
      }
      return false;
    };

    // Act & Assert: Operations should handle exceptions gracefully

    // FindFirst with throwing predicate
    EXPECT_NO_THROW({
      auto result = query_->FindFirst(throwing_predicate);
      // Result behavior with exceptions is implementation-defined
      // Test ensures no crash
    });

    // Count with throwing predicate
    EXPECT_NO_THROW({
      auto result = query_->Count(throwing_predicate);
      // Result behavior with exceptions is implementation-defined
    });

    // Any with throwing predicate
    EXPECT_NO_THROW({
      auto result = query_->Any(throwing_predicate);
      // Result behavior with exceptions is implementation-defined
    });
  }

  NOLINT_TEST_F(
    SceneQueryErrorTest, Query_WithInvalidNodeAccess_HandlesGracefully)
  {
    // Arrange: Create predicate that tries to access invalid data
    auto invalid_access_predicate
      = [](const ConstVisitedNode& visited) -> bool {
      // Try to access node_impl when it might be null
      if (!visited.node_impl) {
        return false; // Should handle null gracefully
      }

      // Access valid data
      auto name = visited.node_impl->GetName();
      return name == "NonExistent";
    };

    // Act: Use predicate that checks for null
    auto find_result = query_->FindFirst(invalid_access_predicate);
    auto count_result = query_->Count(invalid_access_predicate);
    auto any_result = query_->Any(invalid_access_predicate);

    // Assert: Should handle null checks gracefully
    EXPECT_FALSE(find_result.has_value()); // Won't find "NonExistent"
    EXPECT_TRUE(count_result.completed);
    EXPECT_EQ(count_result.nodes_matched, 0);
    ASSERT_TRUE(any_result.has_value());
    EXPECT_FALSE(any_result.value());
  }

  //=== Container Error Tests ================================================//
  // Arrange: Create a container that might throw on emplace_back
  struct ThrowingContainer {
    std::vector<SceneNode> data;
    int call_count = 0;

    template <typename... Args> void emplace_back(Args&&... args)
    {
      ++call_count;
      if (call_count == 3) {
        throw std::runtime_error("Container exception");
      }
      data.emplace_back(std::forward<Args>(args)...);
    }

    auto size() const { return data.size(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }
  };

  NOLINT_TEST_F(
    SceneQueryErrorTest, Collect_WithThrowingContainer_HandlesException)
  {

    ThrowingContainer container;

    // Act & Assert: Collect should handle container exceptions
    EXPECT_NO_THROW({
      auto result = query_->Collect(
        container, [](const ConstVisitedNode&) { return true; });
      // Exception handling behavior is implementation-defined
      // Test ensures no crash
    });
  }

  NOLINT_TEST_F(
    SceneQueryErrorTest, Collect_WithInvalidContainer_HandlesGracefully)
  {
    // Arrange: Test with different container states
    std::vector<SceneNode> valid_container;

    // Test with valid container first
    auto result1
      = query_->Collect(valid_container, NodeNameStartsWith("Enemy"));
    EXPECT_TRUE(result1.completed);
    EXPECT_GT(valid_container.size(), 0);

    // Test collecting into already-filled container
    auto result2
      = query_->Collect(valid_container, NodeNameStartsWith("Potion"));
    EXPECT_TRUE(result2.completed);

    // Should append to existing container
    EXPECT_GT(valid_container.size(), result1.nodes_matched);
  }

  //=== Path Error Tests ====================================================//

  NOLINT_TEST_F(
    SceneQueryErrorTest, PathQueries_WithMalformedPaths_HandleGracefully)
  {
    // Arrange: Various malformed path patterns

    // Act & Assert: Test various malformed paths
    auto result1 = query_->FindFirstByPath("/");
    EXPECT_FALSE(result1.has_value());

    auto result2 = query_->FindFirstByPath("//");
    EXPECT_FALSE(result2.has_value());

    auto result3 = query_->FindFirstByPath("Node//Child");
    EXPECT_FALSE(result3.has_value());

    std::vector<SceneNode> nodes;
    auto result4 = query_->CollectByPath(nodes, "**/**");
    // Should handle double recursive wildcards gracefully

    auto result5 = query_->CollectByPath(nodes, "*/*/");
    // Should handle trailing wildcards gracefully
  }

  NOLINT_TEST_F(
    SceneQueryErrorTest, PathQueries_WithVeryLongPaths_HandleGracefully)
  {
    // Arrange: Create very long path string
    std::string long_path = "Level1";
    for (int i = 0; i < 1000; ++i) {
      long_path += "/VeryLongNonExistentPathSegment" + std::to_string(i);
    }

    // Act: Try to find with very long path
    auto result = query_->FindFirstByPath(long_path);

    // Assert: Should handle long paths gracefully
    EXPECT_FALSE(result.has_value());
  }

  //=== Batch Error Tests ===================================================//

  NOLINT_TEST_F(SceneQueryErrorTest,
    ExecuteBatch_WithThrowingBatchFunction_HandlesException)
  {
    // Arrange: Batch function that throws
    auto throwing_batch = [](const auto& /*q*/) {
      throw std::runtime_error("Batch function exception");
    };

    // Act & Assert: Should handle batch function exceptions
    EXPECT_NO_THROW({
      auto result = query_->ExecuteBatch(throwing_batch);
      // Exception handling behavior is implementation-defined
    });
  }

  NOLINT_TEST_F(
    SceneQueryErrorTest, ExecuteBatch_WithMixedSuccessFailure_HandlesGracefully)
  {
    // Arrange: Batch with some operations that might fail
    std::optional<SceneNode> good_result;
    std::optional<SceneNode> bad_result;
    std::vector<SceneNode> collection;

    // Act: Execute batch with mixed operations
    auto batch_result = query_->ExecuteBatch([&](const auto& q) {
      // This should succeed
      good_result = q.FindFirst(NodeNameEquals("Player"));

      // This should fail (non-existent)
      bad_result = q.FindFirst(NodeNameEquals("NonExistent"));

      // This should partially succeed
      auto collect_result = q.Collect(collection, NodeNameStartsWith("Enemy"));
      EXPECT_TRUE(collect_result.completed);
    });

    // Assert: Should handle mixed success/failure gracefully
    EXPECT_TRUE(batch_result.completed);
    ASSERT_TRUE(good_result.has_value());
    EXPECT_EQ(good_result->GetName(), "Player");
    EXPECT_FALSE(bad_result.has_value());
    EXPECT_GT(collection.size(), 0);
  }

  //=== Memory and Resource Tests ============================================//

  NOLINT_TEST_F(
    SceneQueryErrorTest, Query_WithLargeResults_HandlesMemoryEfficiently)
  {
    // Arrange: Create very large hierarchy
    CreateForestScene(100, 100); // 100 roots * 100 children = 10,000+ nodes
    CreateQuery();

    std::vector<SceneNode> large_collection;
    large_collection.reserve(10000); // Pre-allocate to test memory patterns

    // Act: Collect all nodes
    auto result = query_->Collect(
      large_collection, [](const ConstVisitedNode&) { return true; });

    // Assert: Should handle large collections efficiently
    EXPECT_TRUE(result.completed);
    EXPECT_GT(large_collection.size(), 10000);
    EXPECT_EQ(result.nodes_matched, large_collection.size());
  }

  NOLINT_TEST_F(SceneQueryErrorTest,
    Query_MultipleOperationsOnExpiredScene_ConsistentBehavior)
  {
    // Arrange: Create query and let scene expire
    std::unique_ptr<SceneQuery> orphaned_query;
    {
      auto temp_scene = GetFactory().CreateSingleNodeScene("TempScene");
      orphaned_query = std::make_unique<SceneQuery>(temp_scene);
      // temp_scene expires here
    }

    // Act: Multiple operations on expired scene
    auto result1 = orphaned_query->FindFirst(NodeNameEquals("Root"));
    auto result2 = orphaned_query->FindFirst(NodeNameEquals("Root"));
    auto result3 = orphaned_query->Any(NodeNameEquals("Root"));

    // Assert: Should consistently fail gracefully
    EXPECT_FALSE(result1.has_value());
    EXPECT_FALSE(result2.has_value());
    EXPECT_FALSE(result3.has_value());
  }

} // namespace

} // namespace oxygen::scene::testing
