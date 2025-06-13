//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// #include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneQuery.h>

using oxygen::scene::SceneQuery;
using oxygen::scene::SceneTraversal;

/*
auto SceneQuery::FindFirstByPath(std::string_view path) const noexcept
    -> std::optional<SceneNode>
{

  // Parse path into segments
  auto path_segments = ParsePath(path);
  if (path_segments.empty()) {
    return std::nullopt;
  }

  // For simple absolute paths without wildcards, use direct navigation
  if (path_segments[0].is_absolute && !HasWildcards(path_segments)) {
    return NavigatePathDirectly(scene_ptr, path_segments);
  }

  // For complex paths with wildcards, use filtered traversal
  std::optional<SceneNode> result;
  auto path_filter = CreatePathFilter(path_segments);

  traversal_.Traverse(
      [&](const ConstVisitedNode& visited, const Scene&, bool) -> VisitResult {
        result = SceneNode { std::shared_ptr<const Scene>(scene_),
          visited.handle };
        return VisitResult::kStop; // Early termination
      },
      TraversalOrder::kPreOrder, path_filter);

  return result;
}

auto SceneQuery::FindFirstByPath(const SceneNode& context,
    std::string_view relative_path) const noexcept -> std::optional<SceneNode>
{
}
*/
SceneQuery::SceneQuery(std::shared_ptr<const Scene> scene_weak)
  : scene_weak_(scene_weak)
  , traversal_(scene_weak)
{
}

auto SceneQuery::BatchBegin() const noexcept -> void
{
  batch_operations_.clear();
  batch_active_ = true;
}

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
    if (op.type == BatchOperation::Type::kCollect
      || op.type == BatchOperation::Type::kCount) {
      // For these operations, the result_handler already counted matches
      // We'd need to track this during execution - simplified for now
      ++result.total_matches;
    } else if (op.type == BatchOperation::Type::kFindFirst
      || op.type == BatchOperation::Type::kAny) {
      if (op.has_terminated) { // Found a match
        ++result.total_matches;
      }
    }
  }

  return result;
}

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

auto SceneQuery::ExecuteBatchFindFirst(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> std::optional<SceneNode>
{
  std::optional<SceneNode> result;
  batch_operations_.push_back(BatchOperation { .predicate = predicate,
    .type = BatchOperation::Type::kFindFirst,
    .result_destination = &result,
    .result_handler
    = [&result, scene = scene_weak_](const ConstVisitedNode& visited) {
        result = SceneNode { scene.lock(), visited.handle };
      } });
  return result; // Will be populated during ExecuteBatch
}

auto SceneQuery::ExecuteBatchCount(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> oxygen::scene::QueryResult
{
  QueryResult result;
  batch_operations_.push_back(BatchOperation {
    .predicate = predicate,
    .type = BatchOperation::Type::kCount,
    .result_destination = &result,
    .result_handler
    = [&result](const ConstVisitedNode& visited) { ++result.nodes_matched; },
  });
  return result; // Will be updated during ExecuteBatch
}

auto SceneQuery::ExecuteBatchAny(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> std::optional<bool>
{
  std::optional<bool> result;
  batch_operations_.push_back(BatchOperation {
    .predicate = predicate,
    .type = BatchOperation::Type::kAny,
    .result_destination = &result,
    .result_handler
    = [&result](const ConstVisitedNode& visited) { result = true; },
  });
  return result; // Will be updated during ExecuteBatch
}

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
