//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

class Scene;

//=== Query Result Types ===--------------------------------------------------//

//! Result type for batch query operations containing aggregated metrics
/*!
 Provides performance metrics and completion status for batch query operations
 that execute multiple queries in a single scene traversal pass.

 @see SceneQuery::ExecuteBatch
*/
struct BatchResult {
  //! Total number of nodes examined during batch traversal
  std::size_t nodes_examined = 0;
  //! Aggregated count of all matches across all batch operations
  std::size_t total_matches = 0;
  //! true if batch completed successfully, false if interrupted
  bool completed = true;

  //! Conversion operator for testing batch operation success
  /*!
   @return true if the batch operation completed successfully
  */
  explicit operator bool() const noexcept { return completed; }
};

//! Result type for individual query operations containing performance metrics
/*!
 Provides detailed performance insights and completion status for single query
 operations, enabling optimization decisions and debugging.

 @see SceneQuery::FindFirst, SceneQuery::Collect, SceneQuery::Count
*/
struct QueryResult {
  //! Number of nodes examined by the filter
  std::size_t nodes_examined = 0;
  //! Number of nodes that matched the query criteria
  std::size_t nodes_matched = 0;
  //! true if query completed successfully, false if interrupted
  bool completed = true;

  //! Conversion operator for testing query operation success
  /*!
   @return true if the query operation completed successfully
  */
  explicit operator bool() const noexcept { return completed; }
};

//=== High-Performance Scene Graph Query ===----------------------------------//

//! High-performance scene graph query interface for targeted node searches
/*!
 Provides optimized query operations for scene graph traversal with both
 immediate and batch execution modes. Designed for game engine performance
 requirements with zero-copy operations and user-controlled memory allocation.

 ### Key Features

 - **Dual Execution Modes**: Automatic routing between immediate execution and
   batch mode for optimal performance
 - **Const-Correct Operations**: All queries are read-only and thread-safe
 - **Early Termination**: FindFirst and Any operations stop at first match
 - **User-Controlled Allocation**: Collect operations use user-provided
   containers
 - **Path-Based Queries**: Support for hierarchical path navigation with
   wildcards
 - **Performance Metrics**: Detailed node examination and match statistics

 ### Execution Modes

 **Immediate Mode** (default):
 - Single query executed immediately with dedicated traversal
 - Optimal for one-off queries or conditional logic between queries
 - Direct result availability with minimal overhead

 **Batch Mode** (via ExecuteBatch):
 - Multiple queries executed in single traversal pass
 - Significant performance improvement for multiple related queries
 - Composite filtering with early termination when all operations complete

 ### Path Query Syntax

 - Absolute paths: `"World/Player/Equipment/Weapon"`
 - Relative paths: `"Equipment/Weapon"` (from context node)
 - Single-level wildcards: `"*\/Enemy"` (direct children only)
 - Recursive wildcards: `"**\/Weapon"` (any depth)

 @warning Path-based queries are not supported in batch mode due to
 architectural incompatibility between direct navigation and traversal-based
 batching.

 @warning The underlying Scene must remain valid for the lifetime of the
 SceneQuery object. Query operations will fail gracefully if the Scene is
 destroyed.

 ### Usage Examples

 ```cpp
 auto scene = std::make_shared<Scene>("GameWorld");
 auto query = scene->Query();

 // Single query operations
 auto player = query.FindFirst([](const auto& visited) {
   return visited.node_impl->GetName() == "Player";
 });

 std::vector<SceneNode> enemies;
 auto result = query.Collect(enemies, [](const auto& visited) {
   return visited.node_impl->GetName().starts_with("Enemy");
 });

 // Batch operations for performance
 auto batch_result = query.ExecuteBatch([&](auto& q) {
   player = q.FindFirst(player_predicate);
   auto enemy_count = q.Count(enemy_predicate);
   auto has_powerups = q.Any(powerup_predicate);
 });

 // Path-based queries
 auto weapon = query.FindFirstByPath("World/Player/Equipment/Weapon");
 std::vector<SceneNode> all_weapons;
 query.CollectByPath(all_weapons, "**\/Weapon");
 ```

 @see Scene::Query() for obtaining a SceneQuery instance
*/
class SceneQuery {
public:
  //! Construct a scene query interface for the given scene
  /*!
   @param scene_weak Shared pointer to the scene to be queried (const for
   read-only operations)

   @note The scene must remain valid for the lifetime of this SceneQuery object.
   Query operations will fail gracefully if the scene is destroyed.
  */
  OXGN_SCN_API explicit SceneQuery(std::shared_ptr<const Scene> scene_weak);

  //=== Core Query API ===----------------------------------------------------//

  //! Find the first node matching the given predicate with early termination
  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto FindFirst(Predicate&& predicate) const noexcept
    -> std::optional<SceneNode>;

  //! Collect all nodes matching the predicate into user-provided container
  template <typename Container,
    std::predicate<const ConstVisitedNode&> Predicate>
  auto Collect(Container& container, Predicate&& predicate) const noexcept
    -> QueryResult;

  //! Count nodes matching the predicate without allocation
  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto Count(Predicate&& predicate) const noexcept -> QueryResult;

  //! Check if any node matches the predicate with early termination
  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto Any(Predicate&& predicate) const noexcept -> std::optional<bool>;

  //! Find first node by absolute path from scene root
  auto FindFirstByPath(std::string_view path) const noexcept
    -> std::optional<SceneNode>;

  //! Find first node by relative path from context node
  auto FindFirstByPath(const SceneNode& context,
    std::string_view relative_path) const noexcept -> std::optional<SceneNode>;

  //! Collect all nodes matching path pattern with wildcard support
  template <typename Container>
  auto CollectByPath(Container& container,
    std::string_view path_pattern) const noexcept -> QueryResult;

  //! Execute multiple queries in single traversal pass for optimal performance
  template <typename BatchFunc>
  auto ExecuteBatch(BatchFunc&& batch_func) const noexcept -> BatchResult;

private:
  //! Internal batch operation storage for composite filter construction
  /*!
   Type-erased storage for batch operations, enabling multiple query types
   to be processed in a single traversal pass with unified result handling.
  */
  struct BatchOperation {
    //! Node matching predicate
    std::function<bool(const ConstVisitedNode&)> predicate;
    //! Operation type for result handling
    enum class Type : uint8_t {
      //! Find first matching node with early termination
      kFindFirst,
      //! Collect all matching nodes to container
      kCollect,
      //! Count matching nodes without allocation
      kCount,
      //! Check existence with early termination
      kAny,
    } type;
    //! Type-erased result storage
    void* result_destination;
    //! Type-erased result processor
    std::function<void(const ConstVisitedNode&)> result_handler;
    //! Early termination flag for FindFirst/Any
    bool has_terminated = false;
    //! Current node match flag for filtering
    mutable bool matched_current_node = false;
    //! Match counter for BatchResult aggregation
    mutable std::size_t match_count = 0;
  };
  //! Weak reference to scene for lifetime safety
  std::weak_ptr<const Scene> scene_weak_;

  //! Const-correct traversal interface
  SceneTraversal<const Scene> traversal_;

  // Batch execution state (mutable for const methods)

  //! Active batch operations storage
  mutable std::vector<BatchOperation> batch_operations_;
  //! Batch mode execution flag
  mutable bool batch_active_ = false;

  //=== Private Implementation Helpers ===------------------------------------//

  //! Initialize batch execution state and clear previous operations
  auto BatchBegin() const noexcept -> void;

  //! Finalize batch execution and aggregate results
  auto BatchEnd(const TraversalResult& traversal_result) const noexcept
    -> BatchResult;

  //! Create composite filter from batch operations (type-erased wrapper)
  template <typename BatchFunc>
  auto CreateCompositeFilter(BatchFunc&& batch_func) const noexcept -> auto;

  //! Execute traversal with composite filter
  template <typename CompositeFilter>
  auto Execute(CompositeFilter&& composite_filter) const noexcept
    -> TraversalResult;

  //! Process node during batch execution with result handling
  auto ProcessBatchedNode(const ConstVisitedNode& visited, const Scene& scene,
    bool dry_run) const noexcept -> VisitResult;

  //=== Immediate Execution Helpers ===---------------------------------------//

  //! Type-erased immediate find first implementation
  OXGN_SCN_NDAPI auto ExecuteImmediateFindFirst(
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> std::optional<SceneNode>;

  //! Type-erased immediate count implementation
  auto ExecuteImmediateCount(
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> QueryResult;

  //! Type-erased immediate any implementation
  auto ExecuteImmediateAny(
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> std::optional<bool>;

  //! Execute immediate collect operation with direct traversal
  auto ExecuteImmediateCollect(auto& container,
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> QueryResult;

  //! Execute immediate path-based collect operation (type-erased wrapper)
  template <typename Container>
  auto ExecuteImmediateCollectByPath(Container& container,
    std::string_view path_pattern) const noexcept -> QueryResult;

  //! Implementation helper for immediate path-based collect (type-erased)
  auto ExecuteImmediateCollectByPathImpl(
    std::function<void(const SceneNode&)> add_to_container,
    std::string_view path_pattern) const noexcept -> QueryResult;

  //=== Batch Operation Helpers ===-------------------------------------------//

  //! Execute batch collect operation setup (type-erased wrapper)
  template <typename Container>
  auto ExecuteBatchCollect(Container& container,
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> QueryResult;

  //! Execute batch path collect operation (disabled - returns incomplete
  //! result)
  template <typename Container>
  auto ExecuteBatchCollectByPath(Container& container,
    std::string_view path_pattern) const noexcept -> QueryResult;

  //! Execute batch find first operation setup
  OXGN_SCN_NDAPI auto ExecuteBatchFindFirst(
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> std::optional<SceneNode>;

  //! Execute batch count operation setup
  auto ExecuteBatchCount(
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> QueryResult;

  //! Execute batch any operation setup
  auto ExecuteBatchAny(
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> std::optional<bool>;

  //=== Type-Erased Implementation Helpers ===--------------------------------//

  //! Type-erased batch execution implementation
  auto ExecuteBatchImpl(
    std::function<void(const SceneQuery&)> batch_func) const noexcept
    -> BatchResult;

  //! Type-erased composite filter creation implementation
  auto CreateCompositeFilterImpl(
    std::function<void(const SceneQuery&)> batch_func) const noexcept
    -> std::function<FilterResult(const ConstVisitedNode&, FilterResult)>;

  //! Type-erased batch collect implementation
  auto ExecuteBatchCollectImpl(
    std::function<void(const SceneNode&)> add_to_container,
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> QueryResult;
};

//=== Template Implementations ===--------------------------------------------//

/*!
 Searches the scene graph for the first node that satisfies the predicate,
 stopping immediately upon finding a match for optimal performance.

 @tparam Predicate A callable that accepts `const ConstVisitedNode&` and
 returns bool
 @param predicate Function to test each node during traversal
 @return Optional SceneNode containing the first match, or nullopt if no match
 found

 ### Performance Characteristics
 - Time Complexity: O(1) to O(n) with aggressive early termination
 - Memory: Zero allocations during search
 - Traversal: Stops at first match using VisitResult::kStop

 ### Usage Examples
 ```cpp
 // Find player node by name
 auto player = query.FindFirst([](const auto& visited) {
   return visited.node_impl->GetName() == "Player";
 });

 // Find first visible enemy
 auto enemy = query.FindFirst([](const auto& visited) {
   return visited.node_impl->HasTag("enemy") &&
          visited.node_impl->GetFlags().GetEffectiveValue(SceneNodeFlags::kVisible);
 });
 ```

 @note This method automatically routes to batch execution when called within
 ExecuteBatch()
 @see ExecuteBatch for batch processing multiple FindFirst operations
*/
template <std::predicate<const ConstVisitedNode&> Predicate>
auto SceneQuery::FindFirst(Predicate&& predicate) const noexcept
  -> std::optional<SceneNode>
{

  if (batch_active_) {
    // Use private helper for batch operation setup
    return ExecuteBatchFindFirst(std::function<bool(const ConstVisitedNode&)>(
      std::forward<Predicate>(predicate)));
  }

  // Use private helper for immediate execution
  return ExecuteImmediateFindFirst(std::function<bool(const ConstVisitedNode&)>(
    std::forward<Predicate>(predicate)));
}

/*!
 Traverses the entire scene graph and adds all matching nodes to the provided
 container, giving users complete control over memory allocation.

 @tparam Container Any container type supporting emplace_back(SceneNode)
 @tparam Predicate A callable that accepts `const ConstVisitedNode&` and
 returns bool
 @param container User-provided container to store matching nodes
 @param predicate Function to test each node during traversal
 @return QueryResult with performance metrics and completion status

 ### Performance Characteristics
 - Time Complexity: O(n) for full scene traversal
 - Memory: User-controlled via container parameter
 - Allocation: Zero framework allocations, only user container growth

 ### Usage Examples
 ```cpp
 // Collect all enemies with custom allocator
 std::vector<SceneNode> enemies;
 enemies.reserve(50); // User controls memory strategy
 auto result = query.Collect(enemies, [](const auto& visited) {
   return visited.node_impl->HasTag("enemy");
 });

 // Collect into different container types
 std::deque<SceneNode> visible_objects;
 query.Collect(visible_objects, [](const auto& visited) {
   return
 visited.node_impl->GetFlags().GetEffectiveValue(SceneNodeFlags::kVisible);
 });
 ```

 @note This method automatically routes to batch execution when called within
 ExecuteBatch()
 @see ExecuteBatch for batch processing multiple Collect operations
*/
template <typename Container, std::predicate<const ConstVisitedNode&> Predicate>
auto SceneQuery::Collect(
  Container& container, Predicate&& predicate) const noexcept -> QueryResult
{
  if (scene_weak_.expired()) [[unlikely]] {
    return QueryResult { .completed = false };
  }

  if (batch_active_) {
    // Use private helper for batch operation setup
    return ExecuteBatchCollect(container,
      std::function<bool(const ConstVisitedNode&)>(
        std::forward<Predicate>(predicate)));
  }

  // Use private helper for immediate execution
  return ExecuteImmediateCollect(container,
    std::function<bool(const ConstVisitedNode&)>(
      std::forward<Predicate>(predicate)));
}

/*!
 Traverses the scene graph and counts all nodes matching the predicate
 without creating SceneNode objects or allocating memory.

 @tparam Predicate A callable that accepts `const ConstVisitedNode&` and
 returns bool
 @param predicate Function to test each node during traversal
 @return QueryResult with match count in nodes_matched field

 ### Performance Characteristics
 - Time Complexity: O(n) for full scene traversal
 - Memory: Zero allocations
 - Result: Count available in QueryResult::nodes_matched

 ### Usage Examples
 ```cpp
 // Count visible objects for performance monitoring
 auto visible_count = query.Count([](const auto& visited) {
   return
 visited.node_impl->GetFlags().GetEffectiveValue(SceneNodeFlags::kVisible);
 });

 std::cout << "Visible objects: " << visible_count.nodes_matched
           << " (examined " << visible_count.nodes_examined << ")" <<
 std::endl;
 ```

 @note This method automatically routes to batch execution when called within
 ExecuteBatch()
 @see ExecuteBatch for batch processing multiple Count operations
*/
template <std::predicate<const ConstVisitedNode&> Predicate>
auto SceneQuery::Count(Predicate&& predicate) const noexcept -> QueryResult
{
  if (scene_weak_.expired()) [[unlikely]] {
    return QueryResult { .completed = false };
  }

  if (batch_active_) {
    // Use private helper for batch operation setup
    return ExecuteBatchCount(std::function<bool(const ConstVisitedNode&)>(
      std::forward<Predicate>(predicate)));
  }

  // Use private helper for immediate execution
  return ExecuteImmediateCount(std::function<bool(const ConstVisitedNode&)>(
    std::forward<Predicate>(predicate)));
}

/*!
 Searches the scene graph for any node matching the predicate, stopping
 immediately upon finding the first match for optimal performance.

 @tparam Predicate A callable that accepts `const ConstVisitedNode&` and
 returns bool
 @param predicate Function to test each node during traversal
 @return Optional bool: true if match found, false if no match, nullopt on
 error

 ### Performance Characteristics
 - Time Complexity: O(1) to O(n) with early termination
 - Memory: Zero allocations
 - Traversal: Stops at first match using VisitResult::kStop

 ### Usage Examples
 ```cpp
 // Check if scene has any explosions
 auto has_explosions = query.Any([](const auto& visited) {
   return visited.node_impl->HasTag("explosion");
 });

 if (has_explosions.value_or(false)) {
   // Handle explosion logic
 }
 ```

 @note This method automatically routes to batch execution when called within
 ExecuteBatch()
 @see ExecuteBatch for batch processing multiple Any operations
*/
template <std::predicate<const ConstVisitedNode&> Predicate>
auto SceneQuery::Any(Predicate&& predicate) const noexcept
  -> std::optional<bool>
{
  if (scene_weak_.expired()) [[unlikely]] {
    return std::nullopt;
  }

  if (batch_active_) {
    // Use private helper for batch operation setup
    return ExecuteBatchAny(std::function<bool(const ConstVisitedNode&)>(
      std::forward<Predicate>(predicate)));
  }

  // Use private helper for immediate execution
  return ExecuteImmediateAny(std::function<bool(const ConstVisitedNode&)>(
    std::forward<Predicate>(predicate)));
}

/*!
 Searches the scene graph for all nodes matching the specified path pattern,
 supporting both single-level (*) and recursive (**) wildcards.

 @tparam Container Any container type supporting emplace_back(SceneNode)
 @param container User-provided container to store matching nodes
 @param path_pattern Path pattern with optional wildcards
 @return QueryResult with performance metrics and completion status

 ### Wildcard Patterns
 - `*` matches any direct child name at that level
 - `**` matches any sequence of nodes at any depth
 - Exact names match only nodes with that specific name

 ### Performance Characteristics
 - Simple patterns: May use direct navigation optimization
 - Wildcard patterns: Full traversal with pattern matching
 - Memory: User-controlled via container parameter

 ### Usage Examples
 ```cpp
 std::vector<SceneNode> all_enemies;

 // Collect all direct children named "Enemy"
 query.CollectByPath(all_enemies, "*\/Enemy");

 // Collect all "Weapon" nodes at any depth
 query.CollectByPath(all_enemies, "**\/Weapon");

 // Collect all enemies under any direct child of "Level"
 query.CollectByPath(all_enemies, "Level/*\/Enemy");
 ```

 @note Path queries are not supported in batch mode (ExecuteBatch)
 @see FindFirstByPath for single node path navigation
*/
template <typename Container>
auto SceneQuery::CollectByPath(Container& container,
  std::string_view path_pattern) const noexcept -> QueryResult
{
  if (scene_weak_.expired()) [[unlikely]] {
    return QueryResult { .completed = false };
  }

  if (batch_active_) {
    // Use private helper for batch operation setup
    return ExecuteBatchCollectByPath(container, path_pattern);
  }

  // Use private helper for immediate execution
  return ExecuteImmediateCollectByPath(container, path_pattern);
}

/*!
 Enables batch processing of multiple query operations in a single scene
 traversal, providing significant performance improvements for multi-query
 scenarios common in game engines.

 @tparam BatchFunc Callable accepting `const SceneQuery&` for batch operations
 @param batch_func Lambda/function containing multiple query calls
 @return BatchResult with aggregated metrics from all batch operations

 ### Batch Architecture
 1. **Preparation Phase**: Collect all query operations via lambda execution
 2. **Composite Filtering**: Create unified filter testing all predicates
 3. **Single Traversal**: Execute all operations in one scene pass
 4. **Result Aggregation**: Combine metrics and results

 ### Performance Benefits
 - **N queries → 1 traversal**: Eliminates redundant scene traversal overhead
 - **Cache Efficiency**: Single pass maximizes CPU cache locality
 - **Early Termination**: Stops when all FindFirst/Any operations complete
 - **Memory Efficiency**: User controls all result container allocation

 ### Supported Operations in Batch
 - FindFirst: Stops at first match, flags operation as terminated
 - Collect: Accumulates all matches throughout traversal
 - Count: Increments counter for each match
 - Any: Stops at first match, flags operation as terminated

 ### Usage Examples
 ```cpp
 SceneNode player;
 std::vector<SceneNode> enemies, powerups;
 size_t destructible_count = 0;
 bool has_explosions = false;

 auto batch_result = query.ExecuteBatch([&](auto& q) {
   // All operations execute in single traversal
   player = q.FindFirst(player_predicate).value_or(SceneNode{});
   q.Collect(enemies, enemy_predicate);
   q.Collect(powerups, powerup_predicate);
   auto count_result = q.Count(destructible_predicate);
   destructible_count = count_result.nodes_matched;
   has_explosions = q.Any(explosion_predicate).value_or(false);
 });

 if (batch_result) {
   std::cout << "Batch examined " << batch_result.nodes_examined
             << " nodes, found " << batch_result.total_matches
             << " total matches" << std::endl;
 }
 ```

 @warning Path-based queries (FindFirstByPath, CollectByPath) are not
 supported in batch mode due to architectural incompatibility between direct
 navigation and traversal-based batching. Use them individually for optimal
 performance.

 @note Batch operations maintain full const-correctness and thread-safety
 @see QueryResult, BatchResult for performance metrics
*/
template <typename BatchFunc>
auto SceneQuery::ExecuteBatch(BatchFunc&& batch_func) const noexcept
  -> BatchResult
{
  // Type-erased wrapper - all complex logic moved to .cpp
  return ExecuteBatchImpl(std::function<void(const SceneQuery&)>(
    std::forward<BatchFunc>(batch_func)));
}

/*!
 Executes the batch function to collect operations, then creates a unified
 filter that tests all predicates efficiently in a single traversal pass.

 @tparam BatchFunc Callable accepting `const SceneQuery&` for operation
 collection
 @param batch_func Lambda containing query operations to batch
 @return Composite filter function for scene traversal
*/
template <typename BatchFunc>
auto SceneQuery::CreateCompositeFilter(BatchFunc&& batch_func) const noexcept
{
  // Type-erased wrapper - complex filter creation logic moved to .cpp
  return CreateCompositeFilterImpl(std::function<void(const SceneQuery&)>(
    std::forward<BatchFunc>(batch_func)));
}

/*!
 Performs the actual scene traversal using the provided composite filter
 and the batch visitor for result processing.

 @tparam CompositeFilter Filter function type from CreateCompositeFilter
 @param composite_filter Unified filter testing all batch predicates
 @return TraversalResult with traversal completion metrics
*/
template <typename CompositeFilter>
auto SceneQuery::Execute(CompositeFilter&& composite_filter) const noexcept
  -> TraversalResult
{
  return traversal_.Traverse(
    [this](const ConstVisitedNode& visited, const Scene& scene, bool dry_run)
      -> VisitResult { return ProcessBatchedNode(visited, scene, dry_run); },
    TraversalOrder::kPreOrder, std::forward<CompositeFilter>(composite_filter));
}

/*!
 @tparam Container Container type supporting emplace_back(SceneNode)
 @param container User container for storing collected nodes
 @param predicate Node matching predicate function
 @return QueryResult for this specific collect operation
*/
template <typename Container>
auto SceneQuery::ExecuteBatchCollect(Container& container,
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> QueryResult
{
  // Type-erased wrapper - batch operation setup logic moved to .cpp
  return ExecuteBatchCollectImpl(
    [&container](const SceneNode& node) { container.emplace_back(node); },
    predicate);
}

/*!
 @tparam Container Container type supporting emplace_back(SceneNode)
 @param container User container for storing collected nodes
 @param path_pattern Path pattern with optional wildcards
 @return QueryResult with immediate results and metrics
*/
template <typename Container>
auto SceneQuery::ExecuteImmediateCollectByPath(Container& container,
  std::string_view path_pattern) const noexcept -> QueryResult
{
  // Call the implementation helper that handles path parsing
  return ExecuteImmediateCollectByPathImpl(
    [&container](const SceneNode& node) { container.emplace_back(node); },
    path_pattern);
}

/*!
 Path-based queries are not supported in batch mode due to architectural
 incompatibility between direct navigation and traversal-based batching.

 @tparam Container Container type (unused)
 @param container User container (unused)
 @param path_pattern Path pattern (unused)
 @return QueryResult with completed=false
*/
template <typename Container>
auto SceneQuery::ExecuteBatchCollectByPath(Container& container,
  std::string_view path_pattern) const noexcept -> QueryResult
{
  // Path-based queries are not supported in batch mode; trigger assertion or
  // return incomplete result (You may want to add an assert or error log here)
  return QueryResult { .completed = false };
}

} // namespace oxygen::scene
