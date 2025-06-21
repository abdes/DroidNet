//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <string_view>
#include <vector>

#include <Oxygen/Scene/SceneQuery.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Detail/PathMatcher.h>
#include <Oxygen/Scene/Detail/PathParser.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

using oxygen::scene::ConstVisitedNode;
using oxygen::scene::QueryResult;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneQuery;
using oxygen::scene::SceneTraversal;

namespace oxygen::scene {

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

namespace {
auto CheckScene(std::shared_ptr<const Scene> scene)
  -> std::shared_ptr<const Scene>
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
SceneQuery::SceneQuery(const std::shared_ptr<const Scene>& scene)
  : scene_weak_(CheckScene(scene))
  , traversal_(scene)
  , async_traversal_(scene)
{
}

//=== Traversal Scope Configuration Implementation ===========================//

/*!
 Resets the query scope to traverse the entire scene graph starting from all
 root nodes. This clears any previously configured scope restrictions.

 @return Reference to this SceneQuery for method chaining
*/
auto SceneQuery::ResetTraversalScope() noexcept -> SceneQuery&
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
auto SceneQuery::AddToTraversalScope(const SceneNode& starting_node) noexcept
  -> SceneQuery&
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
auto SceneQuery::AddToTraversalScope(
  std::span<const SceneNode> starting_nodes) noexcept -> SceneQuery&
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

 @param output Optional SceneNode reference to receive the first match
 @param predicate Node filtering function to test each visited node
 @return QueryResult with performance metrics and completion status

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
 @see BatchFindFirstImpl for batch mode equivalent
*/
auto SceneQuery::FindFirstImpl(std::optional<SceneNode>& output,
  const QueryPredicate& predicate) const noexcept -> QueryResult
{
  QueryResult result;

  auto filter = [&](const ConstVisitedNode& visited,
                  FilterResult /*parent_result*/) -> FilterResult {
    ++result.nodes_examined;
    if (predicate(visited)) {
      ++result.nodes_matched;
      return FilterResult::kAccept;
    }
    return FilterResult::kReject;
  };

  auto visitor
    = [&](const ConstVisitedNode& visited, bool /*dry_run*/) -> VisitResult {
    // As soon as the predicate passes we stop, even if the visit was a dry
    // run
    output.emplace(scene_weak_.lock(), visited.handle);
    return VisitResult::kStop;
  };

  try {
    const auto _ = ExecuteTraversal(visitor, filter);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "traversal failed: {}", ex.what());
    result.error_message.emplace(ex.what());
  } catch (...) {
    LOG_F(ERROR, "traversal failed: unknown exception");
    result.error_message.emplace("Unknown exception in FindFirst operation");
  }

  return result;
}

/*!
 Executes immediate Count operation using dedicated scene traversal with
 comprehensive node examination and match counting.

 @param output Optional size_t reference to receive the count
 @param predicate Node filtering function to test each visited node
 @return QueryResult with performance metrics and completion status

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
 @see BatchCountImpl for batch mode equivalent
*/
auto SceneQuery::CountImpl(std::optional<size_t>& output,
  const QueryPredicate& predicate) const noexcept -> QueryResult
{
  QueryResult result;

  auto filter = [&](const ConstVisitedNode& visited,
                  FilterResult /*parent_result*/) -> FilterResult {
    ++result.nodes_examined;
    if (predicate(visited)) {
      ++result.nodes_matched;
      return FilterResult::kAccept;
    }
    return FilterResult::kReject;
  };

  // Simple counting, no use of visited node, increment if not dry run
  auto visitor = [&](const ConstVisitedNode& /*visited*/,
                   const bool /*dry_run*/) -> VisitResult {
    // Simply continue, the filter is doing the selection job, and the traversal
    // will count visited nodes (i.e. matched nodes
    return VisitResult::kContinue;
  };

  try {
    const auto traversal_result = ExecuteTraversal(visitor, filter);
    if (traversal_result.completed) {
      output.emplace(traversal_result.nodes_visited);
    } else {
      LOG_F(ERROR, "traversal did not complete");
      result.error_message.emplace("traversal did not complete");
    }
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "traversal failed: {}", ex.what());
    result.error_message.emplace(ex.what());
  } catch (...) {
    LOG_F(ERROR, "traversal failed: unknown exception");
    result.error_message.emplace("Unknown exception in Count operation");
  }

  return result;
}

/*!
 Executes immediate Any operation using dedicated scene traversal with
 early termination optimization for existence checking.

 @param output Optional bool reference to receive the existence result
 @param predicate Node filtering function to test each visited node
 @return QueryResult with performance metrics and completion status

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
 @see BatchAnyImpl for batch mode equivalent
*/
auto SceneQuery::AnyImpl(std::optional<bool>& output,
  const QueryPredicate& predicate) const noexcept -> QueryResult
{
  QueryResult result;

  auto filter = [&](const ConstVisitedNode& visited,
                  FilterResult /*parent_result*/) -> FilterResult {
    ++result.nodes_examined;
    if (predicate(visited)) {
      ++result.nodes_matched;
      return FilterResult::kAccept;
    }
    return FilterResult::kReject;
  };

  auto visitor = [&](const ConstVisitedNode& /*visited*/,
                   bool /*dry_run*/) -> VisitResult {
    // Stop immediately as our filter has found a match. It does not matter if
    // it's a dry_run or not
    output.emplace(true);
    return VisitResult::kStop;
  };

  try {
    const auto traversal_result = ExecuteTraversal(visitor, filter);
    if (!output.has_value()) {
      if (traversal_result.completed) {
        // Not found
        output.emplace(false);
      } else {
        LOG_F(ERROR, "traversal did not complete");
        result.error_message.emplace("traversal did not complete");
      }
    }
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "traversal failed: {}", ex.what());
    result.error_message.emplace(ex.what());
  } catch (...) {
    LOG_F(ERROR, "traversal failed: unknown exception");
    result.error_message.emplace("Unknown exception in Any operation");
  }

  return result;
}

/*!
 Parses path pattern and drives ExecuteTraversal using PathMatcher to evaluate
 nodes, converting match results into traversal control decisions.

 @param result QueryResult reference to populate with traversal metrics
 @param path_pattern Path pattern string to parse and match against
 @param match_handler Callback function to process complete path matches

 ### Path Processing Pipeline

 - Parses path_pattern using ParsePath and validates syntax
 - Creates PathMatcher from parsed path and maintains PatternMatchState
 - Evaluates each visited node using PathMatcher.Match() method
 - Converts MatchResult enum into VisitResult traversal control decisions

 ### Traversal Control Strategy

 - kCompleteMatch: Invokes match_handler and returns its result
 - kPartialMatch: Continues traversal deeper into hierarchy
 - kNoMatch: Skips entire subtree to avoid unnecessary evaluation

 ### Performance Characteristics

 - Time Complexity: O(1) parsing + O(n) traversal, optimized with subtree
 pruning
 - Memory: Pattern state maintained during traversal
 - Error Handling: Invalid patterns populate result.error_message

 @note Parsing errors are captured in result.error_message on failure
 @see detail::query::ParsePath for supported path pattern syntax
*/
auto SceneQuery::ExecutePathTraversal(QueryResult& result,
  std::string_view path_pattern,
  const std::function<VisitResult(const ConstVisitedNode&)>& match_handler)
  const -> void
{
  auto parsed_path = detail::query::ParsePath(path_pattern);
  if (!parsed_path.IsValid()) {
    result.error_message.emplace(parsed_path.error_info->error_message);
    return;
  }

  detail::query::PathMatcher matcher(parsed_path);
  detail::query::PatternMatchState match_state;

  auto visitor = [&](const ConstVisitedNode& visited, bool) -> VisitResult {
    switch (matcher.Match(visited, match_state)) {
    case detail::query::MatchResult::kCompleteMatch:
      ++result.nodes_matched;
      return match_handler(visited);

    case detail::query::MatchResult::kPartialMatch:
      return VisitResult::kContinue;

    case detail::query::MatchResult::kNoMatch:
      return VisitResult::kSkipSubtree;
    }
    OXYGEN_UNREACHABLE_RETURN(VisitResult::kContinue);
  };

  auto filter = [&](const ConstVisitedNode& /*visited*/,
                  FilterResult /*parent_result*/) -> FilterResult {
    ++result.nodes_examined;
    return FilterResult::kAccept;
  };

  const auto _ = ExecuteTraversal(visitor, filter);
}

/*!
 Navigates the scene hierarchy using an absolute path specification,
 starting from scene root nodes. Optimized for simple paths without wildcards.

 @param output Optional SceneNode reference to receive the found node
 @param path Absolute path string (e.g., "World/Player/Equipment/Weapon")
 @return QueryResult with performance metrics and completion status

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
 std::optional<SceneNode> weapon;
 auto result = query.FindFirstByPath(weapon, "World/Player/Equipment/Weapon");
 std::optional<SceneNode> ui_panel;
 query.FindFirstByPath(ui_panel, "UI/HUD/HealthBar");

 // Invalid paths leave output empty
 std::optional<SceneNode> missing;
 query.FindFirstByPath(missing, "NonExistent/Path");
 ```

 @note Path queries are not supported in batch mode (ExecuteBatch)
 @see FindFirstByPath(const SceneNode&, std::string_view) for relative paths
*/
auto SceneQuery::FindFirstByPath(std::optional<SceneNode>& output,
  std::string_view path) const noexcept -> QueryResult
{
  LOG_SCOPE_FUNCTION(2);

  EnsureCanExecute(true);

  output.reset();

  QueryResult result;
  try {
    ExecutePathTraversal(result, path, [&](const ConstVisitedNode& visited) {
      output.emplace(scene_weak_.lock(), visited.handle);
      return VisitResult::kStop;
    });
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "traversal failed: {}", ex.what());
    result.error_message.emplace(ex.what());
  } catch (...) {
    LOG_F(ERROR, "traversal failed: unknown exception");
    result.error_message.emplace(
      "Unknown exception in FindFirstByPath operation");
  }

  return result;
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
auto SceneQuery::CollectByPathImpl(
  std::function<void(const SceneNode&)> add_to_container,
  const std::string_view path_pattern) const noexcept -> QueryResult
{
  LOG_SCOPE_FUNCTION(2);

  EnsureCanExecute(true);

  // We do not clear the output container, we will only append to it.

  QueryResult result;
  try {
    ExecutePathTraversal(result, path_pattern,
      [add_to_container = std::move(add_to_container), this](
        const ConstVisitedNode& visited) {
        add_to_container({ scene_weak_.lock(), visited.handle });
        return VisitResult::kContinue;
      });
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "traversal failed: {}", ex.what());
    result.error_message.emplace(ex.what());
  } catch (...) {
    LOG_F(ERROR, "traversal failed: unknown exception");
    result.error_message.emplace(
      "Unknown exception in CollectByPath operation");
  }

  return result;
}
