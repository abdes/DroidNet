//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneQueryTestBase.h"

#include <Oxygen/Testing/GTest.h>

namespace oxygen::scene::testing {

namespace {

  //=== ADL Function Test Fixture ============================================//

  class SceneQueryADLTest : public SceneQueryTestBase {
  protected:
    void SetUp() override
    {
      // Create hierarchy suitable for ADL function testing
      CreateComplexHierarchyFromJson();
    } // Helper to create a ConstVisitedNode for testing
    [[nodiscard]] auto CreateConstVisitedNode(
      const SceneNode& node, std::size_t depth = 0) const -> ConstVisitedNode
    {
      ConstVisitedNode visited;
      visited.handle
        = node
            .GetHandle(); // Get the node implementation directly from the scene
      // Since we can't call GetObject() on const SceneNode (lazy invalidation),
      // we use the scene's method to get the implementation
      visited.node_impl = &(scene_->GetNodeImplRefUnsafe(node.GetHandle()));

      visited.depth = depth;
      return visited;
    }
  };

  //=== GetNodeName Tests ===================================================//

  NOLINT_TEST_F(SceneQueryADLTest, GetNodeName_WithValidNode_ReturnsName)
  {
    // Arrange: Find a known node
    auto world_node = query_->FindFirst(NodeNameEquals("World"));
    ASSERT_TRUE(world_node.has_value());

    auto visited = CreateConstVisitedNode(world_node.value());

    // Act: Use ADL GetNodeName function
    auto name = GetNodeName(visited);

    // Assert: Should return correct name
    EXPECT_EQ(name, "World");
    EXPECT_FALSE(name.empty());
  }

  NOLINT_TEST_F(SceneQueryADLTest, GetNodeName_WithNullNodeImpl_ReturnsEmpty)
  {
    // Arrange: Create ConstVisitedNode with null implementation
    ConstVisitedNode visited;
    visited.handle = NodeHandle {}; // Invalid handle
    visited.node_impl = nullptr;
    visited.depth = 0;

    // Act: Use ADL GetNodeName function with null impl
    auto name = GetNodeName(visited);

    // Assert: Should return empty string
    EXPECT_TRUE(name.empty());
    EXPECT_EQ(name.size(), 0);
  }

  NOLINT_TEST_F(
    SceneQueryADLTest, GetNodeName_WithVisitedNode_IntegratesWithPathMatcher)
  {
    // Arrange: Create multiple nodes for path matching simulation
    auto environment = query_->FindFirst(NodeNameEquals("Environment"));
    auto buildings = query_->FindFirst(NodeNameEquals("Buildings"));
    auto house1 = query_->FindFirst(NodeNameEquals("House1"));

    ASSERT_TRUE(environment.has_value());
    ASSERT_TRUE(buildings.has_value());
    ASSERT_TRUE(house1.has_value());

    // Act: Test GetNodeName with different visited nodes
    auto env_visited = CreateConstVisitedNode(environment.value(), 1);
    auto buildings_visited = CreateConstVisitedNode(buildings.value(), 2);
    auto house_visited = CreateConstVisitedNode(house1.value(), 3);

    auto env_name = GetNodeName(env_visited);
    auto buildings_name = GetNodeName(buildings_visited);
    auto house_name = GetNodeName(house_visited);

    // Assert: Should work correctly for path matcher integration
    EXPECT_EQ(env_name, "Environment");
    EXPECT_EQ(buildings_name, "Buildings");
    EXPECT_EQ(house_name, "House1");

    // Verify names can be used in path-like operations
    EXPECT_TRUE(env_name.starts_with("Env"));
    EXPECT_TRUE(house_name.starts_with("House"));
  }

  NOLINT_TEST_F(
    SceneQueryADLTest, GetNodeName_WithVariousNodeTypes_ReturnsCorrectNames)
  {
    // Arrange: Test with different types of nodes from the hierarchy
    std::vector<std::string> expected_names
      = { "World", "Environment", "Characters", "Player", "NPCs", "Effects" };

    std::vector<SceneNode> found_nodes;

    for (const auto& name : expected_names) {
      auto node = query_->FindFirst(NodeNameEquals(name));
      ASSERT_TRUE(node.has_value()) << "Failed to find node: " << name;
      found_nodes.push_back(node.value());
    }

    // Act: Get names using ADL function
    std::vector<std::string> actual_names;
    for (std::size_t i = 0; i < found_nodes.size(); ++i) {
      auto visited = CreateConstVisitedNode(found_nodes[i], i);
      auto name = GetNodeName(visited);
      actual_names.push_back(std::string(name));
    }

    // Assert: All names should match expected
    EXPECT_EQ(actual_names.size(), expected_names.size());
    for (std::size_t i = 0; i < expected_names.size(); ++i) {
      EXPECT_EQ(actual_names[i], expected_names[i])
        << "Mismatch at index " << i;
    }
  }

  //=== GetDepth Tests ======================================================//

  NOLINT_TEST_F(SceneQueryADLTest, GetDepth_WithRootNode_ReturnsZero)
  {
    // Arrange: Get root node
    auto world_node = query_->FindFirst(NodeNameEquals("World"));
    ASSERT_TRUE(world_node.has_value());

    auto visited = CreateConstVisitedNode(world_node.value(), 0);

    // Act: Use ADL GetDepth function
    auto depth = GetDepth(visited);

    // Assert: Root node should have depth 0
    EXPECT_EQ(depth, 0);
  }

  NOLINT_TEST_F(SceneQueryADLTest, GetDepth_WithNestedNode_ReturnsCorrectDepth)
  {
    // Arrange: Get nodes at different depths
    auto environment
      = query_->FindFirst(NodeNameEquals("Environment")); // Depth 1
    auto buildings = query_->FindFirst(NodeNameEquals("Buildings")); // Depth 2
    auto house1 = query_->FindFirst(NodeNameEquals("House1")); // Depth 3

    ASSERT_TRUE(environment.has_value());
    ASSERT_TRUE(buildings.has_value());
    ASSERT_TRUE(house1.has_value());

    // Create visited nodes with appropriate depths
    auto env_visited = CreateConstVisitedNode(environment.value(), 1);
    auto buildings_visited = CreateConstVisitedNode(buildings.value(), 2);
    auto house_visited = CreateConstVisitedNode(house1.value(), 3);

    // Act: Get depths using ADL function
    auto env_depth = GetDepth(env_visited);
    auto buildings_depth = GetDepth(buildings_visited);
    auto house_depth = GetDepth(house_visited);

    // Assert: Should return correct depths
    EXPECT_EQ(env_depth, 1);
    EXPECT_EQ(buildings_depth, 2);
    EXPECT_EQ(house_depth, 3);

    // Verify depth increases with nesting
    EXPECT_LT(env_depth, buildings_depth);
    EXPECT_LT(buildings_depth, house_depth);
  }

  NOLINT_TEST_F(
    SceneQueryADLTest, GetDepth_WithVisitedNode_IntegratesWithPathMatcher)
  {
    // Arrange: Simulate path matcher use case with depth tracking
    std::vector<std::pair<std::string, std::size_t>> node_depth_pairs
      = { { "World", 0 }, { "Environment", 1 }, { "Buildings", 2 },
          { "House1", 3 }, { "House2", 3 }, { "Office", 3 } };

    // Act: Test depth integration for path matching
    for (const auto& [name, expected_depth] : node_depth_pairs) {
      auto node = query_->FindFirst(NodeNameEquals(name));
      ASSERT_TRUE(node.has_value()) << "Failed to find node: " << name;

      auto visited = CreateConstVisitedNode(node.value(), expected_depth);
      auto actual_depth = GetDepth(visited);
      auto actual_name = GetNodeName(visited);

      // Assert: Depth and name should match for path matcher integration
      EXPECT_EQ(actual_depth, expected_depth) << "Depth mismatch for " << name;
      EXPECT_EQ(actual_name, name) << "Name mismatch for " << name;
    }
  }

  NOLINT_TEST_F(SceneQueryADLTest, GetDepth_WithZeroDepth_HandlesCorrectly)
  {
    // Arrange: Test multiple root nodes (if any) with zero depth
    auto world = query_->FindFirst(NodeNameEquals("World"));
    ASSERT_TRUE(world.has_value());

    auto visited_zero = CreateConstVisitedNode(world.value(), 0);

    // Act: Test zero depth handling
    auto depth = GetDepth(visited_zero);

    // Assert: Should handle zero depth correctly
    EXPECT_EQ(depth, 0);
    EXPECT_GE(depth, 0); // Depth should never be negative
  }
  //=== ADL Integration Tests ===============================================//
  NOLINT_TEST_F(
    SceneQueryADLTest, ADLFunctions_InTraversalContext_WorkCorrectly)
  {
    // Arrange: Simulate traversal context where ADL functions would be used
    std::vector<std::pair<std::string, std::size_t>> traversal_data;

    // Act: Use immediate mode to avoid batch collection issues
    //
    // PITFALL WARNING: Originally this test used ExecuteBatch with a local
    // vector:
    //   auto result = query_->ExecuteBatch([&](const auto& q) {
    //     std::vector<SceneNode> all_nodes;  // Local vector in lambda
    //     auto collect_result = q.Collect(all_nodes, ...);
    //   });
    //
    // This causes ASan errors because:
    // 1. ExecuteBatch stores a lambda that captures all_nodes by reference
    // 2. The batch execution is deferred until after lambda registration
    // 3. During traversal, each matching node triggers container.emplace_back()
    // 4. Vector reallocation during emplace_back invalidates internal pointers
    // 5. ASan detects corruption when vector metadata becomes inconsistent
    //
    // SOLUTION: Use immediate mode collection which executes synchronously
    // without deferred lambda execution and vector reference capture issues.
    std::vector<SceneNode> all_nodes;
    all_nodes.reserve(20); // Pre-allocate to minimize reallocation risk
    auto collect_result = query_->Collect(
      all_nodes, [](const ConstVisitedNode&) { return true; });
    EXPECT_TRUE(collect_result.completed);

    // Simulate what would happen during actual traversal with ADL functions
    for (std::size_t i = 0; i < all_nodes.size(); ++i) {
      auto visited = CreateConstVisitedNode(
        all_nodes[i], i % 4); // Simulate various depths

      auto name = GetNodeName(visited);
      auto depth = GetDepth(visited);

      traversal_data.emplace_back(std::string(name), depth);
    }

    // Assert: ADL functions should work correctly in traversal context
    EXPECT_GT(
      traversal_data.size(), 10); // Should have collected multiple nodes

    // Verify data integrity
    for (const auto& [name, depth] : traversal_data) {
      EXPECT_FALSE(name.empty()) << "Empty name found in traversal data";
      EXPECT_LT(depth, 10) << "Unexpectedly large depth: "
                           << depth; // Reasonable depth limit
    }
  }
  NOLINT_TEST_F(
    SceneQueryADLTest, ADLFunctions_WithPathMatching_SimulateIntegration)
  {
    // Arrange: Simulate path matching scenarios where ADL functions enable
    // ConstVisitedNode integration
    struct PathMatchResult {
      std::string full_path;
      std::size_t depth;
      bool matches_pattern;
    };

    std::vector<PathMatchResult> path_results;

    // Act: Simulate path matching using ADL functions
    //
    // BATCH MODE PITFALL: This test uses ExecuteBatch with local vector
    // collection. While it works here because:
    // 1. The nodes vector is small (filtered by depth <= 3)
    // 2. PathMatchResult construction doesn't trigger re-entrant calls
    // 3. No concurrent access to the same container
    //
    // GENERAL BATCH COLLECTION RISKS:
    // - Vector reallocation during emplace_back can invalidate captured
    // references
    // - Re-entrant calls from SceneNode construction/copy can corrupt container
    // - Multiple batch operations on same container create race conditions
    // - Deferred execution timing can cause use-after-scope issues
    //
    // SAFER ALTERNATIVES for batch mode:
    // 1. Pre-reserve container capacity: nodes.reserve(expected_size)
    // 2. Use immediate mode for large or unpredictable collections
    // 3. Collect only handles/data, not full SceneNode objects
    // 4. Use separate containers for each batch operation
    auto path_simulation = query_->ExecuteBatch([&](const auto& q) {
      std::vector<SceneNode> nodes;
      nodes.reserve(15); // Pre-allocate for safety - hierarchy has ~12 nodes
                         // with depth <= 3
      auto collect_result
        = q.Collect(nodes, [](const ConstVisitedNode& visited) {
            auto name = GetNodeName(visited);
            auto depth = GetDepth(visited);

            // Simulate path matcher logic using ADL functions
            return !name.empty() && depth <= 3;
          });

      EXPECT_TRUE(collect_result.completed);

      // Build path results using ADL functions
      for (const auto& node : nodes) {
        auto visited = CreateConstVisitedNode(node, 2); // Example depth

        auto name = GetNodeName(visited);
        auto depth = GetDepth(visited);

        PathMatchResult result;
        result.full_path = std::string(name);
        result.depth = depth;
        result.matches_pattern
          = name.starts_with("House") || name.starts_with("Character");

        path_results.push_back(result);
      }
    });

    // Assert: Path matching simulation should work with ADL functions
    EXPECT_TRUE(path_simulation.completed);
    EXPECT_GT(path_results.size(), 5);

    // Verify path matching results
    bool found_house_match = false;
    bool found_character_match = false;

    for (const auto& result : path_results) {
      EXPECT_FALSE(result.full_path.empty());
      EXPECT_LE(result.depth, 3); // Based on our filter

      if (result.full_path.starts_with("House")) {
        found_house_match = true;
        EXPECT_TRUE(result.matches_pattern);
      }
      if (result.full_path.starts_with("Character")) {
        found_character_match = true;
        EXPECT_TRUE(result.matches_pattern);
      }
    }

    EXPECT_TRUE(found_house_match);
    EXPECT_TRUE(found_character_match);
  }

  NOLINT_TEST_F(SceneQueryADLTest, ADLFunctions_ErrorHandling_WithInvalidData)
  {
    // Arrange: Test ADL functions with various invalid inputs

    // Test with completely invalid ConstVisitedNode
    ConstVisitedNode invalid_visited {};
    invalid_visited.node_impl = nullptr;
    invalid_visited.handle = NodeHandle {};
    invalid_visited.depth = 0;

    // Act & Assert: Functions should handle invalid data gracefully

    // GetNodeName with invalid data
    auto name = GetNodeName(invalid_visited);
    EXPECT_TRUE(name.empty());

    // GetDepth with invalid data (should still return the depth field)
    auto depth = GetDepth(invalid_visited);
    EXPECT_EQ(depth, 0);

    // Test with valid handle but null impl
    auto valid_node = query_->FindFirst(NodeNameEquals("World"));
    ASSERT_TRUE(valid_node.has_value());

    ConstVisitedNode partial_invalid {};
    partial_invalid.handle = valid_node->GetHandle();
    partial_invalid.node_impl = nullptr; // Null implementation
    partial_invalid.depth = 5;

    auto partial_name = GetNodeName(partial_invalid);
    auto partial_depth = GetDepth(partial_invalid);

    EXPECT_TRUE(partial_name.empty()); // Should be empty due to null impl
    EXPECT_EQ(partial_depth, 5); // Should return the depth field value
  }

} // namespace

} // namespace oxygen::scene::testing
