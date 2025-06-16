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

  //=== Result Type Test Fixture =============================================//

  class SceneQueryResultTest : public SceneQueryTestBase {
  protected:
    void SetUp() override
    {
      // Create simple hierarchy for result type testing
      CreateLinearChainScene(5); // Simple chain of 5 nodes
    }
  };

  //=== QueryResult Tests ===================================================//

  NOLINT_TEST_F(
    SceneQueryResultTest, QueryResult_DefaultConstruction_InitializesCorrectly)
  {
    // Arrange: Default construct QueryResult

    // Act: Create default QueryResult
    QueryResult default_result;

    // Assert: Should have correct default values
    EXPECT_EQ(default_result.nodes_examined, 0);
    EXPECT_EQ(default_result.nodes_matched, 0);
    EXPECT_TRUE(default_result.completed);

    // Test bool conversion operator
    EXPECT_TRUE(default_result); // Should be true when completed
  }

  NOLINT_TEST_F(
    SceneQueryResultTest, QueryResult_BoolConversion_ReflectsCompletion)
  {
    // Arrange: Create QueryResults with different completion states

    // Act: Test bool conversion with completed result
    auto completed_result = query_->Count(NodeNameEquals("Root"));
    EXPECT_TRUE(completed_result.completed);
    EXPECT_TRUE(completed_result); // operator bool() should return true

    // Test bool conversion with successful operation
    auto successful_result
      = query_->Count([](const ConstVisitedNode&) { return true; });
    EXPECT_TRUE(successful_result.completed);
    EXPECT_TRUE(successful_result);

    // Create incomplete result by testing with expired scene
    std::unique_ptr<SceneQuery> expired_query;
    {
      auto temp_scene = GetFactory().CreateSingleNodeScene("TempScene");
      expired_query = std::make_unique<SceneQuery>(temp_scene);
      // temp_scene expires here
    }

    auto incomplete_result = expired_query->Count(NodeNameEquals("Root"));
    EXPECT_FALSE(incomplete_result.completed);
    EXPECT_FALSE(incomplete_result); // operator bool() should return false

    // Assert: Bool conversion should reflect completion status
    EXPECT_TRUE(completed_result);
    EXPECT_TRUE(successful_result);
    EXPECT_FALSE(incomplete_result);
  }

  NOLINT_TEST_F(
    SceneQueryResultTest, QueryResult_MetricsAccuracy_MatchesOperation)
  {
    // Arrange: Perform operations with known results

    // Act: Count all nodes (should match examined since all match)
    auto all_count
      = query_->Count([](const ConstVisitedNode&) { return true; });

    // Count specific nodes (subset)
    auto root_count = query_->Count(NodeNameEquals("Root"));

    // Count non-existent nodes
    auto none_count = query_->Count(NodeNameEquals("NonExistent"));

    // Assert: Metrics should be accurate
    EXPECT_TRUE(all_count.completed);
    EXPECT_EQ(
      all_count.nodes_matched, all_count.nodes_examined); // All nodes matched
    EXPECT_EQ(all_count.nodes_matched, 5); // 5 nodes in linear chain

    EXPECT_TRUE(root_count.completed);
    EXPECT_EQ(root_count.nodes_examined, 5); // Still examined all nodes
    EXPECT_EQ(root_count.nodes_matched, 1); // Only found one root

    EXPECT_TRUE(none_count.completed);
    EXPECT_EQ(none_count.nodes_examined, 5); // Still examined all nodes
    EXPECT_EQ(none_count.nodes_matched, 0); // Found none
  }

  NOLINT_TEST_F(SceneQueryResultTest, QueryResult_CollectOperation_MetricsMatch)
  {
    // Arrange: Use collect operation to verify metrics

    // Act: Collect all nodes
    std::vector<SceneNode> all_nodes;
    auto collect_all = query_->Collect(
      all_nodes, [](const ConstVisitedNode&) { return true; });

    // Collect subset
    std::vector<SceneNode> root_nodes;
    auto collect_root = query_->Collect(root_nodes, NodeNameEquals("Root"));

    // Assert: Collect metrics should match container contents
    EXPECT_TRUE(collect_all.completed);
    EXPECT_EQ(collect_all.nodes_matched, all_nodes.size());
    EXPECT_EQ(all_nodes.size(), 5);

    EXPECT_TRUE(collect_root.completed);
    EXPECT_EQ(collect_root.nodes_matched, root_nodes.size());
    EXPECT_EQ(root_nodes.size(), 1);
    EXPECT_EQ(root_nodes[0].GetName(), "Root");
  }

  NOLINT_TEST_F(
    SceneQueryResultTest, QueryResult_FieldAccess_DirectlyAccessible)
  {
    // Arrange: Perform operation with known result

    // Act: Get result and access fields directly
    auto result = query_->Count(NodeNameStartsWith("Child"));

    // Assert: Fields should be directly accessible
    EXPECT_TRUE(result.completed);
    EXPECT_GT(result.nodes_examined, 0);
    EXPECT_GT(result.nodes_matched, 0);
    EXPECT_LT(result.nodes_matched,
      result.nodes_examined); // Not all nodes are children

    // Verify specific values
    EXPECT_EQ(result.nodes_examined, 5); // Linear chain has 5 nodes
    EXPECT_EQ(
      result.nodes_matched, 4); // 4 children in the chain (excluding root)
  }

  //=== BatchResult Tests ===================================================//

  NOLINT_TEST_F(
    SceneQueryResultTest, BatchResult_DefaultConstruction_InitializesCorrectly)
  {
    // Arrange: Default construct BatchResult

    // Act: Create default BatchResult
    BatchResult default_result;

    // Assert: Should have correct default values
    EXPECT_EQ(default_result.nodes_examined, 0);
    EXPECT_EQ(default_result.total_matches, 0);
    EXPECT_TRUE(default_result.completed);

    // Test bool conversion operator
    EXPECT_TRUE(default_result); // Should be true when completed
  }

  NOLINT_TEST_F(
    SceneQueryResultTest, BatchResult_BoolConversion_ReflectsCompletion)
  {
    // Arrange: Execute batch operations

    // Act: Successful batch operation
    std::optional<SceneNode> found_root;
    auto successful_batch = query_->ExecuteBatch(
      [&](const auto& q) { found_root = q.FindFirst(NodeNameEquals("Root")); });

    // Failed batch operation (expired scene)
    std::unique_ptr<SceneQuery> expired_query;
    {
      auto temp_scene = GetFactory().CreateSingleNodeScene("TempScene");
      expired_query = std::make_unique<SceneQuery>(temp_scene);
      // temp_scene expires here
    }

    auto failed_batch = expired_query->ExecuteBatch([&](const auto& q) {
      auto temp_result = q.FindFirst(NodeNameEquals("Root"));
    });

    // Assert: Bool conversion should reflect completion
    EXPECT_TRUE(successful_batch.completed);
    EXPECT_TRUE(successful_batch); // operator bool() should return true
    EXPECT_TRUE(found_root.has_value());

    EXPECT_FALSE(failed_batch.completed);
    EXPECT_FALSE(failed_batch); // operator bool() should return false
  }

  NOLINT_TEST_F(
    SceneQueryResultTest, BatchResult_Aggregation_SumsIndividualResults)
  {
    // Arrange: Multiple operations in batch

    // Act: Execute batch with multiple operations
    std::vector<SceneNode> all_nodes;
    std::vector<SceneNode> root_nodes;
    QueryResult child_count;
    std::optional<SceneNode> first_node;

    auto batch_result = query_->ExecuteBatch([&](const auto& q) {
      q.Collect(
        all_nodes, [](const ConstVisitedNode&) { return true; }); // 5 matches
      q.Collect(root_nodes, NodeNameEquals("Root")); // 1 match
      child_count = q.Count(NodeNameStartsWith("Child")); // 4 matches
      first_node
        = q.FindFirst([](const ConstVisitedNode&) { return true; }); // 1 match
    });

    // Assert: Batch should aggregate all individual results
    EXPECT_TRUE(batch_result.completed);

    std::size_t expected_total
      = all_nodes.size() + root_nodes.size() + child_count.nodes_matched;
    if (first_node.has_value())
      expected_total += 1;

    EXPECT_EQ(batch_result.total_matches, expected_total);
    EXPECT_GT(batch_result.nodes_examined, 0);

    // Verify individual results
    EXPECT_EQ(all_nodes.size(), 5);
    EXPECT_EQ(root_nodes.size(), 1);
    EXPECT_EQ(child_count.nodes_matched, 4);
    EXPECT_TRUE(first_node.has_value());
  }

  NOLINT_TEST_F(
    SceneQueryResultTest, BatchResult_SingleTraversal_EfficientExamination)
  {
    // Arrange: Batch vs individual operations comparison

    // Individual operations
    auto individual_count1
      = query_->Count([](const ConstVisitedNode&) { return true; });
    auto individual_count2 = query_->Count(NodeNameEquals("Root"));
    auto individual_count3 = query_->Count(NodeNameStartsWith("Child"));

    auto individual_total_examined = individual_count1.nodes_examined
      + individual_count2.nodes_examined + individual_count3.nodes_examined;

    // Batch operations
    QueryResult batch_count1, batch_count2, batch_count3;
    auto batch_result = query_->ExecuteBatch([&](const auto& q) {
      batch_count1 = q.Count([](const ConstVisitedNode&) { return true; });
      batch_count2 = q.Count(NodeNameEquals("Root"));
      batch_count3 = q.Count(NodeNameStartsWith("Child"));
    });

    // Assert: Batch should be more efficient (single traversal)
    EXPECT_TRUE(batch_result.completed);
    EXPECT_LE(batch_result.nodes_examined, individual_total_examined);

    // Results should be equivalent
    EXPECT_EQ(batch_count1.nodes_matched, individual_count1.nodes_matched);
    EXPECT_EQ(batch_count2.nodes_matched, individual_count2.nodes_matched);
    EXPECT_EQ(batch_count3.nodes_matched, individual_count3.nodes_matched);

    // Total matches should equal sum of individual matches
    auto expected_matches = batch_count1.nodes_matched
      + batch_count2.nodes_matched + batch_count3.nodes_matched;
    EXPECT_EQ(batch_result.total_matches, expected_matches);
  }

  NOLINT_TEST_F(SceneQueryResultTest, BatchResult_EmptyBatch_HandlesCorrectly)
  {
    // Arrange: Empty batch operation

    // Act: Execute empty batch
    auto empty_batch = query_->ExecuteBatch([&](const auto& /*q*/) {
      // No operations
    });

    // Assert: Should complete successfully with zero results
    EXPECT_TRUE(empty_batch.completed);
    EXPECT_TRUE(empty_batch); // operator bool() should return true
    EXPECT_EQ(empty_batch.total_matches, 0);
    EXPECT_GE(
      empty_batch.nodes_examined, 0); // May examine nodes even if no operations
  }

  NOLINT_TEST_F(
    SceneQueryResultTest, BatchResult_FieldAccess_DirectlyAccessible)
  {
    // Arrange: Execute batch with known operations

    // Act: Batch operation and access fields
    std::vector<SceneNode> nodes;
    auto batch_result = query_->ExecuteBatch([&](const auto& q) {
      q.Collect(nodes, [](const ConstVisitedNode&) { return true; });
    });

    // Assert: Fields should be directly accessible
    EXPECT_TRUE(batch_result.completed);
    EXPECT_GT(batch_result.nodes_examined, 0);
    EXPECT_GT(batch_result.total_matches, 0);
    EXPECT_EQ(batch_result.total_matches, nodes.size());
    EXPECT_EQ(batch_result.total_matches, 5); // 5 nodes in linear chain
  }

  //=== Result Type Comparison Tests ========================================//

  NOLINT_TEST_F(SceneQueryResultTest, Results_ConsistentBehavior_BetweenTypes)
  {
    // Arrange: Compare QueryResult and BatchResult behavior

    // Single operation
    auto single_result
      = query_->Count([](const ConstVisitedNode&) { return true; });

    // Same operation in batch
    QueryResult batch_inner_result;
    auto batch_result = query_->ExecuteBatch([&](const auto& q) {
      batch_inner_result
        = q.Count([](const ConstVisitedNode&) { return true; });
    });

    // Assert: Results should be consistent
    EXPECT_TRUE(single_result.completed);
    EXPECT_TRUE(batch_result.completed);
    EXPECT_TRUE(batch_inner_result.completed);

    // Bool conversion should be consistent
    EXPECT_TRUE(single_result);
    EXPECT_TRUE(batch_result);
    EXPECT_TRUE(batch_inner_result);

    // Node counts should match
    EXPECT_EQ(single_result.nodes_matched, batch_inner_result.nodes_matched);
    EXPECT_EQ(batch_result.total_matches, batch_inner_result.nodes_matched);

    // All should indicate successful completion
    EXPECT_EQ(single_result.nodes_matched, 5);
    EXPECT_EQ(batch_inner_result.nodes_matched, 5);
    EXPECT_EQ(batch_result.total_matches, 5);
  }

  NOLINT_TEST_F(SceneQueryResultTest, Results_ErrorStates_HandleConsistently)
  {
    // Arrange: Create error conditions for both result types
    std::unique_ptr<SceneQuery> expired_query;
    {
      auto temp_scene = GetFactory().CreateSingleNodeScene("TempScene");
      expired_query = std::make_unique<SceneQuery>(temp_scene);
      // temp_scene expires here
    }

    // Act: Test error states
    auto failed_query_result = expired_query->Count(NodeNameEquals("Root"));
    auto failed_batch_result = expired_query->ExecuteBatch(
      [&](const auto& q) { auto temp = q.Count(NodeNameEquals("Root")); });

    // Assert: Both should handle errors consistently
    EXPECT_FALSE(failed_query_result.completed);
    EXPECT_FALSE(failed_batch_result.completed);

    EXPECT_FALSE(failed_query_result); // operator bool()
    EXPECT_FALSE(failed_batch_result); // operator bool()

    EXPECT_EQ(failed_query_result.nodes_matched, 0);
    EXPECT_EQ(failed_batch_result.total_matches, 0);
  }

} // namespace

} // namespace oxygen::scene::testing
