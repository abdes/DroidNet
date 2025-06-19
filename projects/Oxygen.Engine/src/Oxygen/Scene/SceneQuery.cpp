//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// #include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneQuery.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Detail/PathMatcher.h>
#include <Oxygen/Scene/Detail/PathParser.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <vector>

using oxygen::scene::ConstVisitedNode;
using oxygen::scene::QueryResult;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneQuery;
using oxygen::scene::SceneTraversal;

namespace {

} // namespace

namespace oxygen::scene {

//! ADL-compatible GetNodeName for ConstVisitedNode integration with PathMatcher
/*!
 Enables ConstVisitedNode to work directly with concept-based PathMatcher
 without requiring adapter types or data structure conversion.

 @param visited Visited node containing handle and implementation reference
 @return Node name as string view, empty if node_impl is null
*/
std::string_view GetNodeName(const ConstVisitedNode& visited) noexcept
{
  // Access node name through SceneNodeImpl
  if (visited.node_impl) {
    return visited.node_impl->GetName();
  }
  return {};
}

//! ADL-compatible GetDepth for ConstVisitedNode integration with PathMatcher
/*!
 Enables ConstVisitedNode to work directly with concept-based PathMatcher
 by providing access to the hierarchical depth tracked during traversal.

 @param visited Visited node containing depth information from traversal
 @return Hierarchical depth of the node (0 = root level)
*/
std::size_t GetDepth(const ConstVisitedNode& visited) noexcept
{
  return visited.depth;
}

} // namespace oxygen::scene

namespace {
std::shared_ptr<const Scene> CheckScene(std::shared_ptr<const Scene> scene)
{
  CHECK_F(scene != nullptr, "scene cannot be null");
  return scene;
}
} // namespace

//=== Construction / Destruction =============================================//

/*!
 @param scene Shared pointer to the scene to be queried (const for
 read-only operations)

 @note The scene must remain valid for the lifetime of this SceneQuery object.
 Query operations will fail gracefully if the scene is destroyed.
*/
SceneQuery::SceneQuery(std::shared_ptr<const Scene> scene)
  : scene_weak_(CheckScene(scene))
  , traversal_(std::move(scene))
{
}

//=== Traversal Scope Configuration Implementation ===========================//

/*!
 Resets the query scope to traverse the entire scene graph starting from all
 root nodes. This clears any previously configured scope restrictions.

 @return Reference to this SceneQuery for method chaining
*/
SceneQuery& SceneQuery::ResetTraversalScope() noexcept
{
  traversal_scope_.clear();
  return *this;
}

/*!
 Adds the hierarchy starting from the specified node to the query traversal
 scope. If this is the first call to AddToTraversalScope, it will change from
 full-scene traversal to scoped traversal.

 @param starting_node Root node of the hierarchy to add to scope
 @return Reference to this SceneQuery for method chaining

 ### Usage Examples

 ```cpp
 auto query = scene->Query();

 // Scope to player hierarchy only
 query.ResetTraversalScope().AddToTraversalScope(player_node);
 auto player_weapons = query.Collect(weapons, weapon_predicate);
 ```

 @note The node must be part of the scene associated with this SceneQuery
*/
SceneQuery& SceneQuery::AddToTraversalScope(
  const SceneNode& starting_node) noexcept
{
  traversal_scope_.push_back(starting_node);
  return *this;
}

/*!
 Adds the hierarchies starting from the specified nodes to the query traversal
 scope. If this is the first call to AddToTraversalScope, it will change from
 full-scene traversal to scoped traversal.

 @param starting_nodes Root nodes of the hierarchies to add to scope
 @return Reference to this SceneQuery for method chaining

 ### Usage Examples

 ```cpp
 auto query = scene->Query();
 std::array important_nodes = {player_node, enemy_root, ui_root};

 // Scope to multiple hierarchies
 query.ResetTraversalScope().AddToTraversalScope(important_nodes);
 auto all_objects = query.Collect(objects, any_predicate);
 ```

 @note All nodes must be part of the scene associated with this SceneQuery
*/
SceneQuery& SceneQuery::AddToTraversalScope(
  std::span<const SceneNode> starting_nodes) noexcept
{
  // Reserve space to avoid multiple reallocations
  traversal_scope_.reserve(traversal_scope_.size() + starting_nodes.size());

  // Add all nodes to scope
  traversal_scope_.insert(
    traversal_scope_.end(), starting_nodes.begin(), starting_nodes.end());

  return *this;
}

//=== Query Methods Implementations ==========================================//

/*!
 Executes immediate FindFirst operation using dedicated scene traversal with
 early termination optimization for single-result queries.

 @param predicate Node filtering function to test each visited node
 @return Optional SceneNode containing first match, nullopt if no matches

 ### Execution Strategy

 - Creates accept/reject filter based on predicate evaluation
 - Uses visitor that captures first matching node and stops traversal
 - Leverages VisitResult::kStop for optimal early termination
 - Constructs SceneNode from scene reference and node handle

 ### Performance Characteristics

 - Time Complexity: O(k) where k is position of first match
 - Memory: Single SceneNode allocation for result
 - Optimization: Early termination prevents unnecessary traversal

 @note This function is used when not in batch mode for immediate results
 @see ExecuteBatchFindFirst for batch mode equivalent
*/
std::optional<SceneNode> SceneQuery::ExecuteImmediateFindFirst(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
{
  std::optional<SceneNode> result;
  auto query_filter = [&predicate](const ConstVisitedNode& visited,
                        FilterResult) -> FilterResult {
    if (predicate(visited)) {
      return FilterResult::kAccept;
    }
    return FilterResult::kReject;
  };
  auto traversal_result = ExecuteTraversal(
    [&](const ConstVisitedNode& visited, bool) -> VisitResult {
      result = SceneNode { scene_weak_.lock(), visited.handle };
      return VisitResult::kStop;
    },
    query_filter);
  (void)traversal_result; // Suppress unused variable warning
  return result;
}

/*!
 Executes immediate Count operation using dedicated scene traversal with
 comprehensive node examination and match counting.

 @param predicate Node filtering function to test each visited node
 @return QueryResult with nodes_examined, nodes_matched, and completion status

 ### Execution Strategy

 - Creates counting filter that tracks examined and accepted nodes
 - Uses visitor that increments match counter and continues traversal
 - Maintains separate counters for examination and matching statistics
 - Leverages full traversal for complete count accuracy

 ### Performance Characteristics

 - Time Complexity: O(n) full scene traversal required
 - Memory: Zero allocations beyond QueryResult structure
 - Metrics: Comprehensive performance tracking for optimization

 @note This function is used when not in batch mode for immediate results
 @see ExecuteBatchCount for batch mode equivalent
*/
QueryResult SceneQuery::ExecuteImmediateCount(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
{
  QueryResult result;
  auto count_filter = [&result, &predicate](const ConstVisitedNode& visited,
                        FilterResult) -> FilterResult {
    ++result.nodes_examined;
    if (predicate(visited)) {
      return FilterResult::kAccept;
    }
    return FilterResult::kReject;
  };
  auto traversal_result = ExecuteTraversal(
    [&](const ConstVisitedNode& visited, bool dry_run) -> VisitResult {
      // Simple counting, no use of visited node, increment if not dry run
      if (!dry_run) {
        // Check if this node matches the predicate
        if (predicate(visited)) {
          ++result.nodes_matched;
        }
      }
      return VisitResult::kContinue;
    },
    count_filter);
  result.completed = traversal_result.completed;
  return result;
}

/*!
 Executes immediate Any operation using dedicated scene traversal with
 early termination optimization for existence checking.

 @param predicate Node filtering function to test each visited node
 @return Optional bool: true if any match found, false if no matches

 ### Execution Strategy

 - Creates accept/reject filter based on predicate evaluation
 - Uses visitor that sets found flag and stops on first match
 - Leverages VisitResult::kStop for optimal early termination
 - Returns boolean result for existence testing

 ### Performance Characteristics

 - Time Complexity: O(k) where k is position of first match
 - Memory: Single boolean allocation for result tracking
 - Optimization: Early termination prevents unnecessary traversal

 @note This function is used when not in batch mode for immediate results
 @see ExecuteBatchAny for batch mode equivalent
*/
std::optional<bool> SceneQuery::ExecuteImmediateAny(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
{
  bool found = false;
  auto any_filter = [&predicate](const ConstVisitedNode& visited,
                      FilterResult) -> FilterResult {
    if (predicate(visited)) {
      return FilterResult::kAccept;
    }
    return FilterResult::kReject;
  };
  auto traversal_result = ExecuteTraversal(
    [&](const ConstVisitedNode& /*visited*/, bool /*dry_run*/) -> VisitResult {
      // Stop immediately as our filter has found a match. It does not matter if
      // it's a dry_run or not
      found = true;
      return VisitResult::kStop;
    },
    any_filter);
  (void)traversal_result; // Suppress unused variable warning
  return found;
}

/*!
 Navigates the scene hierarchy using an absolute path specification,
 starting from scene root nodes. Optimized for simple paths without wildcards.

 @param path Absolute path string (e.g., "World/Player/Equipment/Weapon")
 @return Optional SceneNode if path exists, nullopt if path not found

 ### Path Navigation

 - Uses direct parent-child navigation for simple paths (O(depth) complexity)
 - Falls back to traversal-based search for wildcard patterns (O(n) complexity)
 - Supports forward slash separator for hierarchical navigation

 ### Performance Characteristics

 - Simple paths: O(depth) via direct navigation
 - Wildcard patterns: O(n) via filtered traversal
 - Memory: Zero allocations during navigation

 ### Usage Examples

 ```cpp
 // Direct path navigation
 auto weapon = query.FindFirstByPath("World/Player/Equipment/Weapon");
 auto ui_panel = query.FindFirstByPath("UI/HUD/HealthBar");

 // Invalid paths return nullopt
 auto missing = query.FindFirstByPath("NonExistent/Path");
 ```

 @note Path queries are not supported in batch mode (ExecuteBatch)
 @see FindFirstByPath(const SceneNode&, std::string_view) for relative paths
*/
std::optional<SceneNode> SceneQuery::FindFirstByPath(
  std::string_view path) const noexcept
{
  if (scene_weak_.expired()) [[unlikely]] {
    return std::nullopt;
  }

  auto scene = scene_weak_.lock();

  // Use new PathParser for parsing
  auto parsed_path = detail::query::ParsePath(path);
  if (!parsed_path.IsValid()) {
    return std::nullopt;
  }
  // Use streaming PathMatcher with scene traversal
  detail::query::PathMatcher matcher(parsed_path);
  detail::query::PatternMatchState match_state;

  std::optional<SceneNode> found_node;
  // Traverse scene and use streaming matcher
  SceneTraversal<const Scene> traversal(scene_weak_.lock());
  auto traversal_result = ExecuteTraversal(
    [&](const ConstVisitedNode& visited, bool) -> VisitResult {
      auto result = matcher.Match(visited, match_state);

      switch (result) {
      case detail::query::MatchResult::kCompleteMatch:
        // Found our target!
        found_node = SceneNode { scene_weak_.lock(), visited.handle };
        return VisitResult::kStop; // Stop traversal

      case detail::query::MatchResult::kPartialMatch:
        // Continue deeper in this subtree
        return VisitResult::kContinue;

      case detail::query::MatchResult::kNoMatch:
        // This path doesn't match, skip this subtree
        return VisitResult::kSkipSubtree;
      }
      return VisitResult::kContinue; // Should never reach here
    },
    AcceptAllFilter {});
  (void)traversal_result; // Suppress unused variable warning

  return found_node;
}

/*!
 Internal implementation for collecting nodes matching a path pattern using
 type-erased container operations. Enables template CollectByPath methods.

 @param add_to_container Function to add matched SceneNode to container
 @param path_pattern Path pattern string with potential wildcards
 @return QueryResult with performance metrics and completion status

 ### Implementation Strategy

 - Parses path pattern and validates structure
 - Creates path-based predicate for traversal filtering
 - Uses type-erased container insertion via function parameter
 - Tracks performance metrics during traversal

 ### Performance Characteristics

 - Time Complexity: O(n) for full scene traversal with wildcard patterns
 - Memory: Allocates nodes in user-provided container
 - Metrics: Tracks nodes examined and matched for performance analysis

 @note This function enables container-agnostic path-based collection
 @see CollectByPath template methods for type-safe public interface
*/
QueryResult SceneQuery::ExecuteImmediateCollectByPathImpl(
  std::function<void(const SceneNode&)> add_to_container,
  std::string_view path_pattern) const noexcept
{
  if (scene_weak_.expired()) [[unlikely]] {
    return QueryResult { .completed = false };
  }

  // Use new PathParser for parsing
  auto parsed_path = detail::query::ParsePath(path_pattern);
  if (!parsed_path.IsValid()) {
    return QueryResult { .completed = false };
  }
  // Use streaming PathMatcher with scene traversal
  detail::query::PathMatcher matcher(parsed_path);
  detail::query::PatternMatchState match_state;
  QueryResult result;

  // Use ExecuteTraversal to respect traversal scope
  auto traversal_result = ExecuteTraversal(
    [&](const ConstVisitedNode& visited, bool) -> VisitResult {
      ++result.nodes_examined;
      auto match_result = matcher.Match(visited, match_state);

      switch (match_result) {
      case detail::query::MatchResult::kCompleteMatch:
        // Found a match! Add to collection
        add_to_container(SceneNode { scene_weak_.lock(), visited.handle });
        ++result.nodes_matched;
        // Continue traversal to find more matches
        return VisitResult::kContinue;

      case detail::query::MatchResult::kPartialMatch:
        // Continue deeper in this subtree
        return VisitResult::kContinue;

      case detail::query::MatchResult::kNoMatch:
        // This path doesn't match, skip this subtree
        return VisitResult::kSkipSubtree;
      }

      return VisitResult::kContinue; // Should never reach here
    },
    AcceptAllFilter {});

  result.completed = traversal_result.completed;
  return result;
}

//=== Type-Erased Batch Implementation Helpers ===---------------------------//

/*!
 Internal implementation for batch query execution using type-erased operations.
 Orchestrates the four-phase batch execution process for optimal performance.

 @param batch_func Function that populates batch operations via query interface
 @return BatchResult with aggregated metrics from all batch operations

 ### Four-Phase Execution Process

 1. **Initialize**: Clear previous operations and set batch state
 2. **Collect**: Execute batch_func to populate operation queue
 3. **Execute**: Single traversal with composite filter and result handling
 4. **Consolidate**: Aggregate results and cleanup batch state

 ### Performance Characteristics

 - Time Complexity: O(n) single traversal for all operations
 - Memory: Accumulates operations in batch_operations_ vector
 - Optimization: Composite filtering eliminates redundant traversals

 ### Error Handling

 - Returns failed BatchResult if scene is expired
 - Handles empty operation sets gracefully
 - Maintains operation state consistency across phases
 @note This function enables type-erased batch execution for template interface
 @see ExecuteBatch template method for type-safe public interface
*/
// Implementation moved to SceneQueryBatch.cpp for coroutine-based batch
// processing

/*!
 Creates a composite filter function that evaluates all batch operation
 predicates and manages per-operation match flags for result processing.

 @param batch_func Function that populates batch operations via query interface
 @return Composite filter function for use with SceneTraversal

 ### Composite Filter Strategy

 - Executes batch_func to populate batch_operations_ vector
 - Tests all active operation predicates against each visited node
 - Sets matched_current_node flags for subsequent result handling
 - Returns Accept if any operation shows interest in the node

 ### Performance Characteristics

 - Time Complexity: O(operations) per node evaluation
 - Memory: Zero allocations during filter evaluation
 - State Management: Updates operation match flags for processing

 ### Filter Logic

 - Skips terminated operations (FindFirst, Any after first match)
 - Tests all remaining predicates against current node
 - Sets operation-specific match flags for ProcessBatchedNode
 - Early acceptance on first predicate match

 @note This function enables efficient multi-predicate evaluation
 @see ProcessBatchedNode for result handling of matched operations
*/
// Implementation moved to SceneQueryBatch.cpp for coroutine-based batch
// processing

/*!
 Internal implementation for batch collection operations using type-erased
 container insertion. Enables template batch collection methods.

 @param add_to_container Function to add matched SceneNode to container
 @param predicate Node filtering predicate for matching logic
 @return QueryResult with performance metrics (updated during ExecuteBatch)

 ### Batch Operation Registration

 - Creates BatchOperation with Collect type and provided predicate
 - Configures result handler to use type-erased container insertion
 - Tracks match count for QueryResult aggregation
 - Returns placeholder result updated during batch execution

 ### Performance Characteristics

 - Time Complexity: O(1) operation registration
 - Memory: Single BatchOperation allocation in operations vector
 - Execution: Deferred until ExecuteBatch processes all operations

 @note This function registers the operation but defers execution
 @see ExecuteBatch for actual traversal and result population
*/
// Implementation moved to SceneQueryBatch.cpp for coroutine-based batch
// processing
