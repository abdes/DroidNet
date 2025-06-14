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
  OXGN_SCN_API explicit SceneQuery(std::shared_ptr<const Scene> scene_weak);

  //=== Core Query API ===----------------------------------------------------//

  // Core search operations for current development needs
  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto FindFirst(Predicate&& predicate) const noexcept
    -> std::optional<SceneNode>;

  template <typename Container,
    std::predicate<const ConstVisitedNode&> Predicate>
  auto Collect(Container& container, Predicate&& predicate) const noexcept
    -> QueryResult;

  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto Count(Predicate&& predicate) const noexcept -> QueryResult;

  template <std::predicate<const ConstVisitedNode&> Predicate>
  auto Any(Predicate&& predicate) const noexcept -> std::optional<bool>;

  auto FindFirstByPath(std::string_view path) const noexcept
    -> std::optional<SceneNode>;

  auto FindFirstByPath(const SceneNode& context,
    std::string_view relative_path) const noexcept -> std::optional<SceneNode>;

  template <typename Container>
  auto CollectByPath(Container& container,
    std::string_view path_pattern) const noexcept -> QueryResult;

  // Batch execution - execute multiple queries in single traversal pass
  template <typename BatchFunc>
  auto ExecuteBatch(BatchFunc&& batch_func) const noexcept -> BatchResult;

private: // Batch operation storage for building composite filter
  struct BatchOperation {
    std::function<bool(const ConstVisitedNode&)> predicate;
    enum class Type : uint8_t {
      kFindFirst,
      kCollect,
      kCount,
      kAny,
    } type;
    void* result_destination; // Type-erased destination for results
    std::function<void(const ConstVisitedNode&)> result_handler;
    bool has_terminated = false; // Track if this operation should stop
    mutable bool matched_current_node = false;
    mutable std::size_t match_count
      = 0; // Track number of matches for this operation
  };

  std::weak_ptr<const Scene> scene_weak_;

  // FIXME: until SceneTraversal offers const-correct methods
  SceneTraversal<const Scene> traversal_;

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
  auto ProcessBatchedNode(const ConstVisitedNode& visited, const Scene& scene,
    bool dry_run) const noexcept -> VisitResult;

  template <typename Container>
  auto ExecuteBatchCollect(Container& container,
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> QueryResult;

  template <typename Container>
  auto ExecuteBatchCollectByPath(Container& container,
    std::string_view path_pattern) const noexcept -> QueryResult;

  OXGN_SCN_NDAPI auto ExecuteBatchFindFirst(
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> std::optional<SceneNode>;

  auto ExecuteBatchCount(
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> QueryResult;

  auto ExecuteBatchAny(
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> std::optional<bool>;

  auto ExecuteImmediateCollect(auto& container,
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> QueryResult;

  template <typename Container>
  auto ExecuteImmediateCollectByPath(Container& container,
    std::string_view path_pattern) const noexcept -> QueryResult;
  // Private implementation helper
  auto ExecuteImmediateCollectByPathImpl(
    std::function<void(const SceneNode&)> add_to_container,
    std::string_view path_pattern) const noexcept -> QueryResult;

  // Type-erased implementation helpers for batch operations
  auto ExecuteBatchImpl(
    std::function<void(const SceneQuery&)> batch_func) const noexcept
    -> BatchResult;

  auto CreateCompositeFilterImpl(
    std::function<void(const SceneQuery&)> batch_func) const noexcept
    -> std::function<FilterResult(const ConstVisitedNode&, FilterResult)>;

  auto ExecuteBatchCollectImpl(
    std::function<void(const SceneNode&)> add_to_container,
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> QueryResult;

  OXGN_SCN_NDAPI auto ExecuteImmediateFindFirst(
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> std::optional<SceneNode>;

  auto ExecuteImmediateCount(
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> QueryResult;
  auto ExecuteImmediateAny(
    const std::function<bool(const ConstVisitedNode&)>& predicate)
    const noexcept -> std::optional<bool>;
};

//=== Template Implementations ===--------------------------------------------//

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

template <typename BatchFunc>
auto SceneQuery::ExecuteBatch(BatchFunc&& batch_func) const noexcept
  -> BatchResult
{
  // Type-erased wrapper - all complex logic moved to .cpp
  return ExecuteBatchImpl(std::function<void(const SceneQuery&)>(
    std::forward<BatchFunc>(batch_func)));
}

template <typename BatchFunc>
auto SceneQuery::CreateCompositeFilter(BatchFunc&& batch_func) const noexcept
{
  // Type-erased wrapper - complex filter creation logic moved to .cpp
  return CreateCompositeFilterImpl(std::function<void(const SceneQuery&)>(
    std::forward<BatchFunc>(batch_func)));
}

template <typename CompositeFilter>
auto SceneQuery::Execute(CompositeFilter&& composite_filter) const noexcept
  -> TraversalResult
{
  return traversal_.Traverse(
    [this](const ConstVisitedNode& visited, const Scene& scene, bool dry_run)
      -> VisitResult { return ProcessBatchedNode(visited, scene, dry_run); },
    TraversalOrder::kPreOrder, std::forward<CompositeFilter>(composite_filter));
}

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

template <typename Container>
auto SceneQuery::ExecuteImmediateCollectByPath(Container& container,
  std::string_view path_pattern) const noexcept -> QueryResult
{
  // Call the implementation helper that handles path parsing
  return ExecuteImmediateCollectByPathImpl(
    [&container](const SceneNode& node) { container.emplace_back(node); },
    path_pattern);
}

template <typename Container>
auto SceneQuery::ExecuteBatchCollectByPath(Container& container,
  std::string_view path_pattern) const noexcept -> QueryResult
{
  // Path-based queries are not supported in batch mode; trigger assertion or
  // return incomplete result (You may want to add an assert or error log here)
  return QueryResult { .completed = false };
}

} // namespace oxygen::scene
