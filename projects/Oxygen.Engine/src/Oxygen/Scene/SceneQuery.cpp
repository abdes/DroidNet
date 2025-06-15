//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// #include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneQuery.h>

#include <Oxygen/Scene/Detail/PathMatcher.h>
#include <Oxygen/Scene/Detail/PathParser.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <algorithm>
#include <string_view>
#include <vector>

using oxygen::scene::ConstVisitedNode;
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
auto GetNodeName(const ConstVisitedNode& visited) noexcept -> std::string_view
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
auto GetDepth(const ConstVisitedNode& visited) noexcept -> std::size_t
{
  return visited.depth;
}

} // namespace oxygen::scene

SceneQuery::SceneQuery(std::shared_ptr<const Scene> scene_weak)
  : scene_weak_(scene_weak)
  , traversal_(scene_weak)
{
}

/*!
 Prepares the query object for batch execution by clearing any previous
 batch operations and setting the batch_active flag. This marks the beginning
 of the batch collection phase.
*/
auto SceneQuery::BatchBegin() const noexcept -> void
{
  batch_operations_.clear();
  batch_active_ = true;
}

/*!
 Processes the traversal results and aggregates metrics from all batch
 operations into a single BatchResult. Clears the batch_active flag and
 calculates total matches across all operations.

 @param traversal_result Result from the batch traversal execution
 @return BatchResult with aggregated metrics and completion status
*/
auto SceneQuery::BatchEnd(
  const TraversalResult& traversal_result) const noexcept -> BatchResult
{
  batch_active_ = false;

  BatchResult result;
  result.nodes_examined
    = traversal_result.nodes_visited; // Visited = examined for batch
  result.completed = traversal_result.completed;
  // Count total matches across all operations
  for (const auto& op : batch_operations_) {
    result.total_matches += op.match_count;
  }

  return result;
}

//! Process node during batch execution with result handling
/*!
 Visitor function called for each accepted node during batch traversal.
 Executes result handlers for operations that matched the current node.
 Controls traversal continuation based on operation termination states.

 @param visited Current node being processed with handle and implementation
 @param scene Scene reference for SceneNode construction in result handlers
 @param dry_run Whether this is a dry run (unused for queries)
 @return VisitResult controlling traversal continuation
*/
auto SceneQuery::ProcessBatchedNode(const ConstVisitedNode& visited,
  const Scene& scene, bool dry_run) const noexcept -> VisitResult
{
  if (dry_run) {
    return VisitResult::kContinue; // No dry-run logic needed for queries
  }

  // Process all operations that matched this node
  bool any_operation_wants_to_continue = false;
  for (auto& op : batch_operations_) {
    if (!op.has_terminated && op.matched_current_node) {
      op.result_handler(visited); // Execute the result logic
      ++op.match_count; // Track the match for BatchResult calculation

      // Check if this operation wants to terminate (FindFirst, Any)
      if (op.type == BatchOperation::Type::kFindFirst
        || op.type == BatchOperation::Type::kAny) {
        op.has_terminated = true;
      }

      if (!op.has_terminated) {
        any_operation_wants_to_continue = true;
      }
    }
  }

  // Continue only if at least one operation is still active
  return any_operation_wants_to_continue ? VisitResult::kContinue
                                         : VisitResult::kStop;
}

/*!
 Registers a FindFirst operation for batch execution with early termination
 behavior and result capture configuration.

 @param predicate Node filtering function for matching logic
 @return Optional SceneNode placeholder (populated during ExecuteBatch)

 ### Batch Operation Registration

 - Creates BatchOperation with FindFirst type and termination behavior
 - Configures result handler to capture first matching SceneNode
 - Sets up automatic termination after first successful match
 - Returns placeholder result updated during batch execution

 ### Performance Characteristics

 - Time Complexity: O(1) operation registration
 - Memory: Single BatchOperation allocation in operations vector
 - Execution: Deferred until ExecuteBatch processes all operations

 @note This function registers the operation but defers execution
 @see ExecuteBatch for actual traversal and result population
*/
auto SceneQuery::ExecuteBatchFindFirst(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> std::optional<SceneNode>
{
  std::optional<SceneNode> result;
  batch_operations_.push_back(BatchOperation { .predicate = predicate,
    .type = BatchOperation::Type::kFindFirst,
    .result_destination = &result,
    .result_handler =
      [&result, scene = scene_weak_](const ConstVisitedNode& visited) {
        result = SceneNode { scene.lock(), visited.handle };
      },
    .match_count = 0 });
  return result; // Will be populated during ExecuteBatch
}

/*!
 Registers a Count operation for batch execution with comprehensive match
 counting and performance metric tracking.

 @param predicate Node filtering function for matching logic
 @return QueryResult placeholder (populated during ExecuteBatch)

 ### Batch Operation Registration

 - Creates BatchOperation with Count type and continuous execution
 - Configures result handler to increment nodes_matched counter
 - Sets up full traversal behavior (no early termination)
 - Returns placeholder result updated during batch execution

 ### Performance Characteristics

 - Time Complexity: O(1) operation registration
 - Memory: Single BatchOperation allocation in operations vector
 - Execution: Deferred until ExecuteBatch processes all operations

 @note This function registers the operation but defers execution
 @see ExecuteBatch for actual traversal and result population
*/
auto SceneQuery::ExecuteBatchCount(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> oxygen::scene::QueryResult
{
  QueryResult result;
  batch_operations_.push_back(BatchOperation { .predicate = predicate,
    .type = BatchOperation::Type::kCount,
    .result_destination = &result,
    .result_handler =
      [&result](const ConstVisitedNode& /*visited*/) {
        // Simple counting, no use of visited node
        ++result.nodes_matched;
      },
    .match_count = 0 });
  return result; // Will be updated during ExecuteBatch
}

/*!
 Registers an Any operation for batch execution with early termination
 behavior and existence checking configuration.

 @param predicate Node filtering function for matching logic
 @return Optional bool placeholder (populated during ExecuteBatch)

 ### Batch Operation Registration

 - Creates BatchOperation with Any type and termination behavior
 - Configures result handler to set existence flag to true
 - Sets up automatic termination after first successful match
 - Returns placeholder result updated during batch execution

 ### Performance Characteristics

 - Time Complexity: O(1) operation registration
 - Memory: Single BatchOperation allocation in operations vector
 - Execution: Deferred until ExecuteBatch processes all operations

 @note This function registers the operation but defers execution
 @see ExecuteBatch for actual traversal and result population
*/
auto SceneQuery::ExecuteBatchAny(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> std::optional<bool>
{
  std::optional<bool> result;
  batch_operations_.push_back(BatchOperation { .predicate = predicate,
    .type = BatchOperation::Type::kAny,
    .result_destination = &result,
    .result_handler =
      [&result](const ConstVisitedNode& /*visited*/) {
        // Execution is deferred, no use of visited node
        result = true;
      },
    .match_count = 0 });
  return result; // Will be updated during ExecuteBatch
}

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
auto SceneQuery::ExecuteImmediateFindFirst(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> std::optional<SceneNode>
{
  std::optional<SceneNode> result;
  auto query_filter = [&predicate](const ConstVisitedNode& visited,
                        FilterResult) -> FilterResult {
    if (predicate(visited)) {
      return FilterResult::kAccept;
    }
    return FilterResult::kReject;
  };
  traversal_.Traverse(
    [&](const ConstVisitedNode& visited, bool) -> VisitResult {
      result = SceneNode { scene_weak_.lock(), visited.handle };
      return VisitResult::kStop;
    },
    TraversalOrder::kPreOrder, query_filter);
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
auto SceneQuery::ExecuteImmediateCount(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> oxygen::scene::QueryResult
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
  auto traversal_result = traversal_.Traverse(
    [&](const ConstVisitedNode& /*visited*/, bool dry_run) -> VisitResult {
      // Simple counting, no use of visited node, increment if not dry run
      if (!dry_run) {
        ++result.nodes_examined;
      }
      return VisitResult::kContinue;
    },
    TraversalOrder::kPreOrder, count_filter);
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
auto SceneQuery::ExecuteImmediateAny(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> std::optional<bool>
{
  bool found = false;
  auto any_filter = [&predicate](const ConstVisitedNode& visited,
                      FilterResult) -> FilterResult {
    if (predicate(visited)) {
      return FilterResult::kAccept;
    }
    return FilterResult::kReject;
  };
  traversal_.Traverse(
    [&](const ConstVisitedNode& /*visited*/, bool /*dry_run*/) -> VisitResult {
      // Stop immediately as our filter has found a match. It does not matter if
      // it's a dry_run or not
      found = true;
      return VisitResult::kStop;
    },
    TraversalOrder::kPreOrder, any_filter);
  return found;
}

/*!
 Executes immediate Collect operation using dedicated scene traversal with
 comprehensive node collection into user-provided container.

 @param container Reference to user-provided container for collecting nodes
 @param predicate Node filtering function to test each visited node
 @return QueryResult with nodes_examined, nodes_matched, and completion status

 ### Execution Strategy

 - Creates counting filter that tracks examined and accepted nodes
 - Uses a visitor that adds scene nodes and continues traversal
 - Maintains separate counters for examination and collection statistics
 - Leverages full traversal for complete collection accuracy

 ### Performance Characteristics

 - Time Complexity: O(n) full scene traversal required
 - Memory: User-controlled allocation via provided container
 - Container Agnostic: Works with any container supporting emplace_back

 ### Container Requirements

 - Must support emplace_back(SceneNode) operation
 - Examples: std::vector<SceneNode>, std::deque<SceneNode>

 @note This function is used when not in batch mode for immediate results
 @see ExecuteBatchCollectImpl for batch mode equivalent
*/
auto SceneQuery::ExecuteImmediateCollect(auto& container,
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> QueryResult
{
  QueryResult result {};
  auto queryFilter = [&result, &predicate](const ConstVisitedNode& visited,
                       FilterResult) -> FilterResult {
    ++result.nodes_examined;
    return predicate(visited) ? FilterResult::kAccept : FilterResult::kReject;
  };
  auto visitHandler = [this, &container, &result](
                        const ConstVisitedNode& visited, bool) -> VisitResult {
    container.emplace_back(scene_weak_.lock(), visited.handle);
    ++result.nodes_matched;
    return VisitResult::kContinue;
  };
  auto traversalResult
    = traversal_.Traverse(visitHandler, TraversalOrder::kPreOrder, queryFilter);
  result.completed = traversalResult.completed;
  return result;
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
auto SceneQuery::FindFirstByPath(std::string_view path) const noexcept
  -> std::optional<SceneNode>
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
  traversal.Traverse(
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
    TraversalOrder::kPreOrder);

  return found_node;
}

/*!
 Navigates the scene hierarchy using a relative path specification,
 starting from the children of the specified context node.

 @param context Starting node for relative path navigation
 @param relative_path Relative path string (e.g., "Equipment/Weapon")
 @return Optional SceneNode if path exists, nullopt if path not found

 ### Relative Path Rules

 - Path navigation starts from context node's children
 - Relative paths should not begin with "/" (absolute marker)
 - Uses same optimization strategy as absolute paths

 ### Performance Characteristics

 - Simple paths: O(depth) via direct navigation from context
 - Wildcard patterns: O(subtree) via filtered traversal
 - Memory: Zero allocations during navigation

 ### Usage Examples

 ```cpp
 auto player = query.FindFirstByPath("World/Player");
 if (player) {
   // Find weapon relative to player
   auto weapon = query.FindFirstByPath(*player, "Equipment/Weapon");
   auto inventory = query.FindFirstByPath(*player, "UI/Inventory");
 }
 ```

 @note Path queries are not supported in batch mode (ExecuteBatch)
 @see FindFirstByPath(std::string_view) for absolute paths
*/
auto SceneQuery::FindFirstByPath(const SceneNode& context,
  std::string_view relative_path) const noexcept -> std::optional<SceneNode>
{
  if (scene_weak_.expired()) [[unlikely]] {
    return std::nullopt;
  }

  auto scene = scene_weak_.lock();

  // Use new PathParser for parsing
  auto parsed_path = detail::query::ParsePath(relative_path);
  if (!parsed_path.IsValid()) {
    return std::nullopt;
  }

  // TODO: Implement relative path support with new PathMatcher
  // For now, relative paths with wildcards are not supported
  return std::nullopt;
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
auto SceneQuery::ExecuteImmediateCollectByPathImpl(
  std::function<void(const SceneNode&)> add_to_container,
  std::string_view path_pattern) const noexcept -> QueryResult
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

  // Traverse scene and use streaming matcher
  SceneTraversal<const Scene> traversal(scene_weak_.lock());
  auto traversal_result = traversal.Traverse(
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
    TraversalOrder::kPreOrder);

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
auto SceneQuery::ExecuteBatchImpl(
  std::function<void(const SceneQuery&)> batch_func) const noexcept
  -> BatchResult
{
  if (scene_weak_.expired()) [[unlikely]] {
    return BatchResult { .completed = false };
  }

  // Phase 1: Initialize batch state, counters, clear previous operations
  BatchBegin();
  // Phase 2: Execute lambda to collect operations and create composite filter
  auto composite_filter = CreateCompositeFilterImpl(batch_func);
  if (batch_operations_.empty()) {
    auto empty_result
      = TraversalResult { .nodes_visited = 0, .completed = true };
    return BatchEnd(empty_result);
  } // Phase 3: Execute single traversal with composite filter
  auto traversal_result = traversal_.Traverse(
    [this](const ConstVisitedNode& visited, bool dry_run) -> VisitResult {
      // ProcessBatchedNode expects a Scene& parameter, but we can get it from
      // scene_weak_
      auto scene = scene_weak_.lock();
      return ProcessBatchedNode(visited, *scene, dry_run);
    },
    TraversalOrder::kPreOrder, composite_filter);

  // Phase 4: Consolidate results and cleanup
  auto result = BatchEnd(traversal_result);

  return result;
}

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
auto SceneQuery::CreateCompositeFilterImpl(
  std::function<void(const SceneQuery&)> batch_func) const noexcept
  -> std::function<FilterResult(const ConstVisitedNode&, FilterResult)>
{
  // Execute the lambda - this will populate batch_operations_
  batch_func(*this); // Pass query as batch interface

  // Create composite filter from collected operations
  return [this](const ConstVisitedNode& visited,
           FilterResult parent_result) -> FilterResult {
    // SceneTraversal guarantees node_impl is valid - no null checks needed
    bool any_operation_interested = false;

    // Test all predicates once and flag matches
    for (auto& op : batch_operations_) {
      op.matched_current_node = false; // Reset flag

      if (!op.has_terminated && op.predicate(visited)) {
        op.matched_current_node = true;
        any_operation_interested = true;
      }
    }

    return any_operation_interested ? FilterResult::kAccept
                                    : FilterResult::kReject;
  };
}

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
auto SceneQuery::ExecuteBatchCollectImpl(
  std::function<void(const SceneNode&)> add_to_container,
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> QueryResult
{
  QueryResult result;
  batch_operations_.push_back(BatchOperation { .predicate = predicate,
    .type = BatchOperation::Type::kCollect,
    .result_destination = &result,
    .result_handler =
      [add_to_container, &result, scene = scene_weak_](
        const ConstVisitedNode& visited) {
        add_to_container(SceneNode { scene.lock(), visited.handle });
        ++result.nodes_matched;
      },
    .match_count = 0 });
  return result; // Will be updated during ExecuteBatch
}
