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

//=== Path Query Implementation Details
//===------------------------------------//

struct PathSegment {
  std::string name;
  bool is_wildcard_single = false; // "*" matches direct children only
  bool is_wildcard_recursive = false; // "**" matches at any depth
  bool is_absolute = false; // Leading "/" makes first segment absolute
};

struct ParsedPath {
  std::vector<PathSegment> segments;
  bool is_valid = false;
  bool has_wildcards = false;
};

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

auto GetNodeName(const ConstVisitedNode& visited) -> std::string_view
{
  // Access node name through SceneNodeImpl
  if (visited.node_impl) {
    return visited.node_impl->GetName();
  }
  return {};
}

//=== Path Navigation Implementation
//===---------------------------------------//

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

struct PatternState {
  size_t segment_index = 0;
  bool in_recursive_match = false;
  std::vector<std::string> visited_path; // For debugging/validation
};

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

//=== Path-based Query Predicates
//===------------------------------------------//

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
    result.total_matches += op.match_count;
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
