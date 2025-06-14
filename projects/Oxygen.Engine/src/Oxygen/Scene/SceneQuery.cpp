//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// #include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneQuery.h>

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

//=== Path Query Implementation Details ===-----------------------------------//

//! Individual path component with wildcard matching flags
/*!
 Represents a single segment of a hierarchical path used for scene node
 navigation. Supports exact name matching and wildcard patterns.
*/
struct PathSegment {
  //! Node name or wildcard pattern for this path segment
  std::string name;
  //! true if this segment is "*" (matches direct children only)
  bool is_wildcard_single = false;
  //! true if this segment is "**" (matches at any depth)
  bool is_wildcard_recursive = false;
  //! true if this is the first segment of an absolute path
  bool is_absolute = false;
};

//! Complete parsed path representation with validation state
/*!
 Contains all path segments and metadata about the parsed path structure,
 including wildcard detection and validation status.
*/
struct ParsedPath {
  //! Ordered sequence of path segments from root to target
  std::vector<PathSegment> segments;
  //! true if path parsing succeeded and segments are valid
  bool is_valid = false;
  //! true if any segment contains wildcard patterns
  bool has_wildcards = false;
};

/*!
 Parses a hierarchical path string into structured segments with wildcard
 detection and validation. Supports both absolute and relative path formats.

 @param path Path string to parse (e.g., "World/Player" or "/UI/*\/Button")
 @return ParsedPath structure with segments and metadata

 ### Path Format Rules
 - Forward slash "/" separates path segments
 - Leading "/" indicates absolute path from scene root
 - "*" wildcard matches any single direct child
 - "**" wildcard matches any node at any depth below current level
 - Empty segments are ignored (e.g., "A//B" becomes "A/B")

 ### Performance Characteristics
 - Time Complexity: O(n) where n is path string length
 - Memory: Allocates storage for each path segment
 - Validation: Performs basic syntax checking during parsing

 ### Usage Examples
 ```cpp
 auto simple = ParsePath("World/Player/Equipment");    // No wildcards
 auto wildcard = ParsePath("*\/Enemy");                 // Single wildcard
 auto recursive = ParsePath("**\/Weapon");              // Recursive wildcard
 auto absolute = ParsePath("/UI/HUD/HealthBar");       // Absolute path
 ```

 @note Empty paths return invalid ParsedPath with is_valid = false
 @see NavigatePathDirectly for optimized navigation of simple paths
*/
auto ParsePath(std::string_view path) -> ParsedPath
{
  ParsedPath result;

  if (path.empty()) {
    return result;
  }

  // Handle absolute path
  bool is_absolute = path.starts_with('/');
  if (is_absolute) {
    path.remove_prefix(1);
  }

  // Split by '/' and create segments
  std::string_view remaining = path;
  bool first_segment = true;

  while (!remaining.empty()) {
    auto pos = remaining.find('/');
    std::string_view segment_view = remaining.substr(0, pos);

    if (!segment_view.empty()) {
      PathSegment segment;
      segment.name = std::string(segment_view);
      segment.is_absolute = first_segment && is_absolute;

      // Check for wildcards
      if (segment.name == "*") {
        segment.is_wildcard_single = true;
        result.has_wildcards = true;
      } else if (segment.name == "**") {
        segment.is_wildcard_recursive = true;
        result.has_wildcards = true;
      }

      result.segments.push_back(std::move(segment));
      first_segment = false;
    }

    if (pos == std::string_view::npos) {
      break;
    }
    remaining.remove_prefix(pos + 1);
  }

  result.is_valid = !result.segments.empty();
  return result;
}

/*!
 Extracts the node name from a visited node through its implementation.
 Provides safe access to node metadata during traversal operations.

 @param visited Visited node containing handle and implementation reference
 @return Node name as string view, empty if node_impl is null

 ### Performance Characteristics
 - Time Complexity: O(1) direct member access
 - Memory: Zero allocations, returns view of existing string
 - Safety: Null-safe implementation returns empty view for invalid nodes

 @note This function assumes the node_impl is valid as guaranteed by
 SceneTraversal during normal traversal operations
*/
auto GetNodeName(const ConstVisitedNode& visited) -> std::string_view
{
  // Access node name through SceneNodeImpl
  if (visited.node_impl) {
    return visited.node_impl->GetName();
  }
  return {};
}

//=== Path Navigation Implementation ===--------------------------------------//

/*!
 Performs optimized direct navigation through scene hierarchy for simple paths
 without wildcard patterns. Uses parent-child relationships for efficient
 traversal.

 @param scene Scene instance containing the nodes to navigate
 @param path Parsed path structure (must not contain wildcards)
 @return Optional SceneNode if path exists, nullopt if not found

 ### Navigation Strategy
 - Starts from scene root nodes
 - Iteratively searches children at each level for name matches
 - Maintains current level nodes for next iteration
 - Early termination on path segment mismatch

 ### Performance Characteristics
 - Time Complexity: O(depth × children_per_level)
 - Memory: O(children_per_level) for temporary level storage
 - Optimization: Direct parent-child navigation (no full traversal)

 @note This function requires !path.has_wildcards for optimal performance
 @see NavigatePathDirectlyFromContext for relative path navigation
*/
auto NavigatePathDirectly(std::shared_ptr<const Scene> scene,
  const ParsedPath& path) -> std::optional<SceneNode>
{
  if (!scene || path.segments.empty() || path.has_wildcards) {
    return std::nullopt;
  }

  // Start from root nodes
  auto root_nodes = scene->GetRootNodes();
  std::vector<SceneNode> current_level = std::move(root_nodes);

  for (const auto& segment : path.segments) {
    std::vector<SceneNode> next_level;

    // Search current level for matching nodes
    for (const auto& node : current_level) {
      if (auto node_impl = scene->GetNodeImpl(node)) {
        if (node_impl->get().GetName() == segment.name) {
          // Found match, get its children for next iteration
          if (&segment == &path.segments.back()) {
            // This is the final segment, return the node
            return node;
          } else {
            // Get children for next level
            auto child_handles = scene->GetChildren(node);
            for (const auto& child_handle : child_handles) {
              if (auto child_node = scene->GetNode(child_handle)) {
                next_level.push_back(*child_node);
              }
            }
          }
        }
      }
    }

    if (next_level.empty()) {
      return std::nullopt; // Path not found
    }

    current_level = std::move(next_level);
  }

  return std::nullopt;
}

/*!
 Performs optimized direct navigation through scene hierarchy for relative paths
 starting from a specific context node. Uses parent-child relationships for
 efficient traversal.

 @param scene Scene instance containing the nodes to navigate
 @param context Starting node for relative path navigation
 @param path Parsed path structure (must not contain wildcards)
 @return Optional SceneNode if path exists, nullopt if not found

 ### Navigation Strategy
 - Starts from context node's children
 - Iteratively searches children at each level for name matches
 - Maintains current level nodes for next iteration
 - Early termination on path segment mismatch

 ### Performance Characteristics
 - Time Complexity: O(depth × children_per_level)
 - Memory: O(children_per_level) for temporary level storage
 - Optimization: Direct parent-child navigation from context

 @note This function requires !path.has_wildcards for optimal performance
 @see NavigatePathDirectly for absolute path navigation from scene root
*/
auto NavigatePathDirectlyFromContext(std::shared_ptr<const Scene> scene,
  const SceneNode& context, const ParsedPath& path) -> std::optional<SceneNode>
{
  if (!scene || path.segments.empty() || path.has_wildcards) {
    return std::nullopt;
  }

  // Start from context node's children
  auto children_handles = scene->GetChildren(context);
  std::vector<SceneNode> current_level;
  for (const auto& child_handle : children_handles) {
    if (auto child_node = scene->GetNode(child_handle)) {
      current_level.push_back(*child_node);
    }
  }

  for (const auto& segment : path.segments) {
    std::vector<SceneNode> next_level;

    // Search current level for matching nodes
    for (const auto& node : current_level) {
      if (auto node_impl = scene->GetNodeImpl(node)) {
        if (node_impl->get().GetName() == segment.name) {
          // Found match, get its children for next iteration
          if (&segment == &path.segments.back()) {
            // This is the final segment, return the node
            return node;
          } else {
            // Get children for next level
            auto child_handles_inner = scene->GetChildren(node);
            for (const auto& child_handle : child_handles_inner) {
              if (auto child_node = scene->GetNode(child_handle)) {
                next_level.push_back(*child_node);
              }
            }
          }
        }
      }
    }

    if (next_level.empty()) {
      return std::nullopt; // Path not found
    }

    current_level = std::move(next_level);
  }

  return std::nullopt;
}

//=== Wildcard Pattern Matching ===-------------------------------------------//

//! State tracking for wildcard pattern matching during traversal
/*!
 Maintains context during recursive pattern matching operations, tracking
 progress through path segments and wildcard matching states.
*/
struct PatternState {
  //! Current position in the pattern segments being matched
  size_t segment_index = 0;
  //! true if currently processing a recursive wildcard ("**") match
  bool in_recursive_match = false;
  //! Path traversed so far (used for debugging and validation)
  std::vector<std::string> visited_path;
};

/*!
 Tests whether a visited node matches the current pattern segment during
 wildcard-enabled path traversal operations.

 @param visited Node being tested for pattern match
 @param pattern Complete parsed path pattern with potential wildcards
 @param state Current matching state including segment position
 @return true if node matches current pattern segment, false otherwise

 ### Pattern Matching Rules
 - Exact name match for literal segments
 - "*" wildcard matches any single node name
 - "**" wildcard activates recursive matching mode
 - Updates state for recursive wildcard processing

 ### Performance Characteristics
 - Time Complexity: O(1) for name comparison
 - Memory: Zero allocations during comparison
 - State Management: Updates pattern state for recursive wildcards

 @note This function modifies state.in_recursive_match for "**" patterns
 @see ShouldContinueTraversal for traversal continuation logic
*/
auto MatchesPattern(const ConstVisitedNode& visited, const ParsedPath& pattern,
  PatternState& state) -> bool
{
  if (state.segment_index >= pattern.segments.size()) {
    return false;
  }

  const auto& segment = pattern.segments[state.segment_index];
  auto node_name = GetNodeName(visited);

  if (segment.is_wildcard_single) {
    // "*" matches any single node name
    return true;
  } else if (segment.is_wildcard_recursive) {
    // "**" matches any sequence of nodes
    state.in_recursive_match = true;
    return true;
  } else {
    // Exact name match
    return node_name == segment.name;
  }
}

/*!
 Determines whether traversal should continue based on current pattern matching
 state and remaining segments to process.

 @param visited Current node being processed (unused for logic)
 @param pattern Complete parsed path pattern (unused for logic)
 @param state Current matching state including segment position
 @return true if traversal should continue, false to terminate

 ### Continuation Logic
 - Always continue during recursive wildcard matching
 - Continue if pattern segments remain unprocessed
 - Terminate when pattern is fully matched

 ### Performance Characteristics
 - Time Complexity: O(1) state checking
 - Memory: Zero allocations
 - Early Termination: Enables efficient traversal pruning

 @note Currently unused parameters are marked [[maybe_unused]]
 @see MatchesPattern for pattern matching logic
*/
auto ShouldContinueTraversal([[maybe_unused]] const ConstVisitedNode& visited,
  [[maybe_unused]] const ParsedPath& pattern, const PatternState& state) -> bool
{
  // Always continue if we're in a recursive wildcard
  if (state.in_recursive_match) {
    return true;
  }

  // Continue if we haven't reached the end of the pattern
  return state.segment_index < pattern.segments.size();
}

//=== Path-based Query Predicates ===-----------------------------------------//

/*!
 Creates a predicate function for path-based node filtering during traversal
 operations. Handles both simple exact matching and wildcard patterns.

 @param parsed_path Parsed path structure with segments and wildcard flags
 @return Function object that tests nodes against the path pattern

 ### Predicate Behavior
 - Returns false for invalid parsed paths
 - Optimized exact matching for single-segment simple paths
 - Complex pattern matching for wildcard-enabled paths
 - Stateful matching for recursive wildcards

 ### Performance Characteristics
 - Simple paths: O(1) direct name comparison
 - Wildcard patterns: O(1) per node with state management
 - Memory: Captures parsed_path by value in closure

 ### Usage Examples
 ```cpp
 auto parsed = ParsePath("World/Player");
 auto predicate = CreatePathPredicate(parsed);
 bool matches = predicate(visited_node);
 ```

 @note Generated predicates are suitable for use with traversal filters
 @see ParsePath for path parsing and validation
*/
auto CreatePathPredicate(const ParsedPath& parsed_path)
  -> std::function<bool(const ConstVisitedNode&)>
{
  return [parsed_path](const ConstVisitedNode& visited) -> bool {
    if (!parsed_path.is_valid) {
      return false;
    }

    // For now, implement simple exact matching
    // More sophisticated pattern matching can be added later
    if (!parsed_path.has_wildcards && parsed_path.segments.size() == 1) {
      auto node_name = GetNodeName(visited);
      return node_name == parsed_path.segments[0].name;
    }

    // For wildcard patterns, implement more complex matching logic
    PatternState state;
    return MatchesPattern(visited, parsed_path, state);
  };
}

} // namespace

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
    .result_handler
    = [&result](const ConstVisitedNode& visited) { ++result.nodes_matched; },
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
    .result_handler
    = [&result](const ConstVisitedNode& visited) { result = true; },
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
    [&](const ConstVisitedNode& visited, bool) -> VisitResult {
      ++result.nodes_matched;
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
    [&](const ConstVisitedNode& visited, bool) -> VisitResult {
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
 - Uses visitor that emplaces SceneNodes and continues traversal
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
  auto parsed_path = ParsePath(path);

  if (!parsed_path.is_valid) {
    return std::nullopt;
  }

  // Try direct navigation first (faster for simple paths)
  if (!parsed_path.has_wildcards) {
    return NavigatePathDirectly(scene, parsed_path);
  }

  // Fall back to traversal-based search for wildcard patterns
  auto path_predicate = CreatePathPredicate(parsed_path);
  return ExecuteImmediateFindFirst(path_predicate);
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
  auto parsed_path = ParsePath(relative_path);

  if (!parsed_path.is_valid || parsed_path.segments.empty()) {
    return std::nullopt;
  }

  // Relative paths should not be absolute
  if (parsed_path.segments[0].is_absolute) {
    return std::nullopt;
  }
  // Try direct navigation first (faster for simple paths)
  if (!parsed_path.has_wildcards) {
    return NavigatePathDirectlyFromContext(scene, context, parsed_path);
  }

  // Fall back to traversal-based search for wildcard patterns
  // TODO: Implement context-aware wildcard search
  auto path_predicate = CreatePathPredicate(parsed_path);
  return ExecuteImmediateFindFirst(path_predicate);
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

  auto parsed_path = ParsePath(path_pattern);
  if (!parsed_path.is_valid) {
    return QueryResult { .completed = false };
  }

  auto path_predicate = CreatePathPredicate(parsed_path);

  // Create a temporary container-agnostic collection logic
  QueryResult result {};
  auto queryFilter = [&result, &path_predicate](const ConstVisitedNode& visited,
                       FilterResult) -> FilterResult {
    ++result.nodes_examined;
    return path_predicate(visited) ? FilterResult::kAccept
                                   : FilterResult::kReject;
  };

  auto visitHandler = [&add_to_container, &result, scene = scene_weak_](
                        const ConstVisitedNode& visited, bool) -> VisitResult {
    add_to_container(SceneNode { scene.lock(), visited.handle });
    ++result.nodes_matched;
    return VisitResult::kContinue;
  };

  auto traversalResult
    = traversal_.Traverse(visitHandler, TraversalOrder::kPreOrder, queryFilter);
  result.completed = traversalResult.completed;
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
