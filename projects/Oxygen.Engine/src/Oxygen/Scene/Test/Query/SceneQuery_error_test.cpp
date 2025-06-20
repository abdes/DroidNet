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

  class SceneQueryErrorTest : public SceneQueryTestBase { };

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
      std::optional<SceneNode> find_result;
      [[maybe_unused]] auto _
        = query_->FindFirst(find_result, throwing_predicate);
    });

    // Count with throwing predicate
    EXPECT_NO_THROW({
      std::optional<size_t> count;
      auto result = query_->Count(count, throwing_predicate);
      // Result behavior with exceptions is implementation-defined
    });

    // Any with throwing predicate
    EXPECT_NO_THROW({
      std::optional<bool> any;
      auto result = query_->Any(any, throwing_predicate);
      // Result behavior with exceptions is implementation-defined
    });
  }

  //=== Container Error Tests ================================================//
  // Arrange: Create a container that might throw on emplace_back
  struct ThrowingContainer {
    std::vector<SceneNode> data;
    int call_count = 0;

    template <typename... Args> auto emplace_back(Args&&... args) -> void
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
    CreateMultiPlayerHierarchy();
    CreateQuery();
    std::vector<SceneNode> valid_container;

    // Test with valid container first
    auto result1
      = query_->Collect(valid_container, NodeNameStartsWith("Player"));
    EXPECT_TRUE(result1);
    EXPECT_GT(valid_container.size(), 0);

    // Test collecting into already-filled container
    auto result2 = query_->Collect(valid_container, NodeNameStartsWith("NPC"));
    EXPECT_TRUE(result2);

    // Should append to existing container
    EXPECT_GT(valid_container.size(), result1.nodes_matched);
  }

  //=== Path Error Tests ====================================================//

  NOLINT_TEST_F(
    SceneQueryErrorTest, PathQueries_WithMalformedPaths_HandleGracefully)
  {
    // Arrange: Various malformed path patterns

    // Act & Assert: Test various malformed paths
    std::optional<SceneNode> result1;
    auto _ = query_->FindFirstByPath(result1, "/");
    EXPECT_FALSE(result1.has_value());

    std::optional<SceneNode> result2;
    _ = query_->FindFirstByPath(result2, "//");
    EXPECT_FALSE(result2.has_value());

    std::optional<SceneNode> result3;
    _ = query_->FindFirstByPath(result3, "Node//Child");
    EXPECT_FALSE(result3.has_value());

    std::vector<SceneNode> nodes;
    _ = query_->CollectByPath(nodes, "**/**");
    // Should handle double recursive wildcards gracefully

    nodes.clear();
    _ = query_->CollectByPath(nodes, "*/*/");
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
    std::optional<SceneNode> result;
    auto _ = query_->FindFirstByPath(result, long_path);

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
    EXPECT_TRUE(result);
    EXPECT_GT(large_collection.size(), 10000);
    EXPECT_EQ(result.nodes_matched, large_collection.size());
  }
} // namespace

} // namespace oxygen::scene::testing
