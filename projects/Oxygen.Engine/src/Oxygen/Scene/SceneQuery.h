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

//=== Traversal Types ===-----------------------------------------------------//

// Batch result type
struct BatchResult {
  std::size_t nodes_examined = 0;
  std::size_t total_matches = 0;
  bool completed = true;

  explicit operator bool() const noexcept { return completed; }
};

// Minimal result type for current needs
struct QueryResult {
  std::size_t nodes_examined = 0;
  std::size_t nodes_matched = 0;
  bool completed = true;

  explicit operator bool() const noexcept { return completed; }
};

//=== High-Performance Scene Graph Query ===----------------------------------//

class SceneQuery {
public:
  OXGN_SCN_API explicit SceneQuery(const Scene& scene);

  //=== Core Query API ===----------------------------------------------------//

  // Core search operations for current development needs
  template <std::predicate<const VisitedNode&> Predicate>
  auto FindFirst(Predicate&& predicate) const noexcept
    -> std::optional<SceneNode>;

  template <typename Container, std::predicate<const VisitedNode&> Predicate>
  auto Collect(Container& container, Predicate&& predicate) const noexcept
    -> QueryResult;

  template <std::predicate<const VisitedNode&> Predicate>
  auto Count(Predicate&& predicate) const noexcept -> QueryResult;

  template <std::predicate<const VisitedNode&> Predicate>
  auto Any(Predicate&& predicate) const noexcept -> std::optional<bool>;

  /*
  auto FindFirstByPath(std::string_view path) const noexcept
      -> std::optional<SceneNode>;

  auto FindFirstByPath(
      const SceneNode& context, std::string_view relative_path) const noexcept
      -> std::optional<SceneNode>;

  template <typename Container>
  auto CollectByPath(Container& container,
      std::string_view path_pattern) const noexcept -> QueryResult;
*/
  // Batch execution - execute multiple queries in single traversal pass
  template <typename BatchFunc>
  auto ExecuteBatch(BatchFunc&& batch_func) const noexcept -> BatchResult;

private:
  // Batch operation storage for building composite filter
  struct BatchOperation {
    std::function<bool(const VisitedNode&)> predicate;
    enum class Type : uint8_t {
      kFindFirst,
      kCollect,
      kCount,
      kAny,
    } type;
    void* result_destination; // Type-erased destination for results
    std::function<void(const VisitedNode&)> result_handler;
    bool has_terminated = false; // Track if this operation should stop
    mutable bool matched_current_node = false;
  };

  const Scene* scene_;

  // FIXME: until SceneTraversal offers const-correct methods
  mutable SceneTraversal traversal_;

  // Batch execution state (mutable for const methods)
  mutable std::vector<BatchOperation> batch_operations_;
  mutable bool batch_active_ = false;

  //=== Private helpers ===---------------------------------------------------//

  auto BatchBegin() const noexcept -> void;

  template <typename BatchFunc>
  auto CreateCompositeFilter(BatchFunc&& batch_func) const noexcept -> auto;

  template <typename CompositeFilter>
  auto Execute(CompositeFilter&& composite_filter) const noexcept
    -> TraversalResult;

  auto BatchEnd(const TraversalResult& traversal_result) const noexcept
    -> BatchResult;

  // Private helper for batch execution visitor
  auto ProcessBatchedNode(const VisitedNode& visited, const Scene& scene,
    bool dry_run) const noexcept -> VisitResult;

  template <typename Container>
  auto ExecuteBatchCollect(Container& container,
    const std::function<bool(const VisitedNode&)>& predicate) const noexcept
    -> QueryResult;

  template <typename Container>
  auto ExecuteBatchCollectByPath(Container& container,
    std::string_view path_pattern) const noexcept -> QueryResult;

  auto ExecuteBatchFindFirst(
    const std::function<bool(const VisitedNode&)>& predicate) const noexcept
    -> std::optional<SceneNode>;

  auto ExecuteBatchCount(
    const std::function<bool(const VisitedNode&)>& predicate) const noexcept
    -> QueryResult;

  auto ExecuteBatchAny(
    const std::function<bool(const VisitedNode&)>& predicate) const noexcept
    -> std::optional<bool>;

  auto ExecuteImmediateCollect(auto& container,
    const std::function<bool(const VisitedNode&)>& predicate) const noexcept
    -> QueryResult;

  template <typename Container>
  auto ExecuteImmediateCollectByPath(Container& container,
    std::string_view path_pattern) const noexcept -> QueryResult;

  auto ExecuteImmediateFindFirst(
    const std::function<bool(const VisitedNode&)>& predicate) const noexcept
    -> std::optional<SceneNode>;

  auto ExecuteImmediateCount(
    const std::function<bool(const VisitedNode&)>& predicate) const noexcept
    -> QueryResult;

  auto ExecuteImmediateAny(
    const std::function<bool(const VisitedNode&)>& predicate) const noexcept
    -> std::optional<bool>;
};

//=== Template Implementations ===--------------------------------------------//

template <std::predicate<const VisitedNode&> Predicate>
auto SceneQuery::FindFirst(Predicate&& predicate) const noexcept
  -> std::optional<SceneNode>
{
  if (scene_.expired()) [[unlikely]] {
    return std::nullopt;
  }

  if (batch_active_) {
    // Use private helper for batch operation setup
    return ExecuteBatchFindFirst(std::function<bool(const VisitedNode&)>(
      std::forward<Predicate>(predicate)));
  }

  // Use private helper for immediate execution
  return ExecuteImmediateFindFirst(std::function<bool(const VisitedNode&)>(
    std::forward<Predicate>(predicate)));
}

template <typename Container, std::predicate<const VisitedNode&> Predicate>
auto SceneQuery::Collect(
  Container& container, Predicate&& predicate) const noexcept -> QueryResult
{
  if (scene_.expired()) [[unlikely]] {
    return QueryResult { .completed = false };
  }

  if (batch_active_) {
    // Use private helper for batch operation setup
    return ExecuteBatchCollect(container,
      std::function<bool(const VisitedNode&)>(
        std::forward<Predicate>(predicate)));
  }

  // Use private helper for immediate execution
  return ExecuteImmediateCollect(container,
    std::function<bool(const VisitedNode&)>(
      std::forward<Predicate>(predicate)));
}

template <std::predicate<const VisitedNode&> Predicate>
auto SceneQuery::Count(Predicate&& predicate) const noexcept -> QueryResult
{
  if (scene_.expired()) [[unlikely]] {
    return QueryResult { .completed = false };
  }

  if (batch_active_) {
    // Use private helper for batch operation setup
    return ExecuteBatchCount(std::function<bool(const VisitedNode&)>(
      std::forward<Predicate>(predicate)));
  }

  // Use private helper for immediate execution
  return ExecuteImmediateCount(std::function<bool(const VisitedNode&)>(
    std::forward<Predicate>(predicate)));
}

template <std::predicate<const VisitedNode&> Predicate>
auto SceneQuery::Any(Predicate&& predicate) const noexcept
  -> std::optional<bool>
{
  if (scene_.expired()) [[unlikely]] {
    return std::nullopt;
  }

  if (batch_active_) {
    // Use private helper for batch operation setup
    return ExecuteBatchAny(std::function<bool(const VisitedNode&)>(
      std::forward<Predicate>(predicate)));
  }

  // Use private helper for immediate execution
  return ExecuteImmediateAny(std::function<bool(const VisitedNode&)>(
    std::forward<Predicate>(predicate)));
}

/*
template <typename Container>
auto SceneQuery::CollectByPath(Container& container,
    std::string_view path_pattern) const noexcept -> QueryResult
{
  if (scene_.expired()) [[unlikely]] {
    return QueryResult { .completed = false };
  }

  if (batch_active_) {
    // Use private helper for batch operation setup (not supported, but for API
    // symmetry)
    return ExecuteBatchCollectByPath(container, path_pattern);
  }

  // Use private helper for immediate execution
  return ExecuteImmediateCollectByPath(container, path_pattern);
}
*/

template <typename BatchFunc>
auto SceneQuery::ExecuteBatch(BatchFunc&& batch_func) const noexcept
  -> BatchResult
{
  if (scene_.expired()) [[unlikely]] {
    return BatchResult { .completed = false };
  }

  // Phase 1: Initialize batch state, counters, clear previous operations
  BatchBegin();

  // Phase 2: Execute lambda to collect operations and create composite filter
  auto composite_filter
    = CreateCompositeFilter(std::forward<BatchFunc>(batch_func));

  if (batch_operations_.empty()) {
    BatchEnd();
    return BatchResult {};
  }

  // Phase 3: Execute single traversal with composite filter
  auto traversal_result = Execute(composite_filter);

  // Phase 4: Consolidate results and cleanup
  auto result = BatchEnd(traversal_result);

  return result;
}

template <typename BatchFunc>
auto SceneQuery::CreateCompositeFilter(BatchFunc&& batch_func) const noexcept
{
  // Execute the lambda - this will populate batch_operations_
  batch_func(*this); // Pass query as batch interface

  // Create composite filter from collected operations
  return [this](const VisitedNode& visited,
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

template <typename CompositeFilter>
auto SceneQuery::Execute(CompositeFilter&& composite_filter) const noexcept
  -> TraversalResult
{
  return traversal_.Traverse(
    [this](const VisitedNode& visited, const Scene& scene, bool dry_run)
      -> VisitResult { return ProcessBatchedNode(visited, scene, dry_run); },
    TraversalOrder::kPreOrder, std::forward<CompositeFilter>(composite_filter));
}

template <typename Container>
auto SceneQuery::ExecuteBatchCollect(Container& container,
  const std::function<bool(const VisitedNode&)>& predicate) const noexcept
  -> QueryResult
{
}

/*
template <typename Container>
auto SceneQuery::ExecuteBatchCollectByPath(Container& container,
    std::string_view path_pattern) const noexcept -> QueryResult
{
  // Path-based queries are not supported in batch mode; trigger assertion or
  // return incomplete result (You may want to add an assert or error log here)
  return QueryResult { .completed = false };
}

template <typename Container>
auto SceneQuery::ExecuteImmediateCollectByPath(Container& container,
    std::string_view path_pattern) const noexcept -> QueryResult
{
  QueryResult result;
  // Parse path pattern
  auto pattern = ParsePathPattern(path_pattern);
  if (!pattern.is_valid) {
    result.completed = false;
    return result;
  }
  // Create specialized filter for path matching with early subtree rejection
  auto path_filter = [&](const VisitedNode& visited,
                         FilterResult parent_result) -> FilterResult {
    ++result.nodes_examined;
    if (MatchesPathPattern(visited, pattern)) {
      return FilterResult::kAccept;
    }
    return ShouldTraverseForPattern(visited, pattern)
        ? FilterResult::kReject
        : FilterResult::kRejectSubTree;
  };
  auto traversal_result = GetTraversal().Traverse(
      [&](const VisitedNode& visited, const Scene&, bool) -> VisitResult {
        container.emplace_back(scene_.lock(), visited.handle);
        ++result.nodes_matched;
        return VisitResult::kContinue;
      },
      TraversalOrder::kPreOrder, path_filter);
  result.completed = traversal_result.completed;
  return result;
}
*/

} // namespace oxygen::scene
