//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <functional>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Yield.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneQuery.h>
#include <Oxygen/Scene/SceneTraversal.h>

using oxygen::scene::AsyncSceneTraversal;
using oxygen::scene::BatchResult;
using oxygen::scene::ConstVisitedNode;
using oxygen::scene::FilterResult;
using oxygen::scene::QueryResult;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneQuery;
using oxygen::scene::SceneTraversal;
using oxygen::scene::TraversalOrder;
using oxygen::scene::VisitResult;

//=== BroadcastChannel-Based Batch Query Implementation ===-------------------//

namespace oxygen::scene::detail {

//! Minimal EventLoop implementation for scene query batch processing
class MinimalEventLoop {
public:
  void Run()
  {
    running_ = true;
    while (!should_stop_ || !tasks_.empty()) {
      if (!tasks_.empty()) {
        auto task = std::move(tasks_.front());
        tasks_.pop();
        task();
      }
    }
    running_ = false;
  }

  void Stop() { should_stop_ = true; }
  bool IsRunning() const noexcept { return running_; }
  void Schedule(std::function<void()> task) { tasks_.push(std::move(task)); }

private:
  std::atomic<bool> running_ { false };
  std::atomic<bool> should_stop_ { false };
  std::queue<std::function<void()>> tasks_;
};

} // namespace oxygen::scene::detail

using oxygen::scene::detail::MinimalEventLoop;

//! EventLoopTraits specialization for MinimalEventLoop
template <> struct oxygen::co::EventLoopTraits<MinimalEventLoop> {
  static EventLoopID EventLoopId(MinimalEventLoop& loop)
  {
    return EventLoopID(&loop);
  }

  static void Schedule(MinimalEventLoop& loop, std::function<void()> task)
  {
    loop.Schedule(std::move(task));
  }

  static void Run(MinimalEventLoop& loop) { loop.Run(); }
  static void Stop(MinimalEventLoop& loop) { loop.Stop(); }
  static bool IsRunning(const MinimalEventLoop& loop)
  {
    return loop.IsRunning();
  }
};

namespace {

//! Node data for BroadcastChannel coordination
struct NodeData {
  ConstVisitedNode visited_node;
  std::size_t index;
  bool is_last;

  NodeData(ConstVisitedNode visited, std::size_t idx, bool last = false)
    : visited_node(visited)
    , index(idx)
    , is_last(last)
  {
  }
};

//! Comprehensive result structure for batch operations
struct BatchOperationResult {
  enum class Type { FindFirst, Collect, Count, Any };
  enum class Status { Pending, Completed, Failed };

  Type type;
  Status status = Status::Pending;
  std::string error_message;

  // Statistics
  std::size_t nodes_examined = 0;
  std::size_t nodes_matched = 0;

  // Operation-specific results
  std::optional<SceneNode> found_node; // For FindFirst
  std::optional<bool> any_result; // For Any
  std::size_t count_result = 0; // For Count (also used by Collect)

  // Coroutine function
  std::function<oxygen::co::Co<>(oxygen::co::BroadcastChannel<NodeData>&)>
    operation;

  explicit BatchOperationResult(Type op_type)
    : type(op_type)
  {
  }
};

//! BroadcastChannel-based batch coordinator following the established pattern
class BatchQueryExecutor {
public:
  explicit BatchQueryExecutor(std::weak_ptr<const Scene> scene,
    std::vector<SceneNode> traversal_scope = {})
    : scene_weak_(std::move(scene))
    , traversal_scope_(std::move(traversal_scope))
  {
  }
  //! Register FindFirst operation with BroadcastChannel coroutine
  template <typename Predicate> std::size_t FindFirst(Predicate predicate)
  {
    auto& result
      = operation_results_.emplace_back(BatchOperationResult::Type::FindFirst);
    std::size_t operation_index = operation_results_.size() - 1;

    result.operation
      = [this, predicate = std::move(predicate), operation_index](
          oxygen::co::BroadcastChannel<NodeData>& channel) -> oxygen::co::Co<> {
      auto& op_result = operation_results_[operation_index];

      try {
        auto reader = channel.ForRead();
        op_result.found_node = std::nullopt;

        while (true) {
          auto node_data = co_await reader.Receive();
          if (!node_data) {
            // Channel closed - no match found
            break;
          }

          op_result.nodes_examined++;

          if (predicate(node_data->visited_node)) {
            op_result.nodes_matched++;
            if (node_data->visited_node.handle.IsValid()) {
              op_result.found_node = SceneNode { scene_weak_.lock(),
                node_data->visited_node.handle };
            }
            break; // Early termination
          }
        }

        op_result.status = BatchOperationResult::Status::Completed;
      } catch (const std::exception& e) {
        op_result.status = BatchOperationResult::Status::Failed;
        op_result.error_message = e.what();
      } catch (...) {
        op_result.status = BatchOperationResult::Status::Failed;
        op_result.error_message = "Unknown exception in FindFirst operation";
      }

      co_return;
    };

    return operation_index;
  }
  //! Register Collect operation with BroadcastChannel coroutine
  template <typename Predicate>
  std::size_t Collect(
    Predicate predicate, std::function<void(const SceneNode&)> add_to_container)
  {
    auto& result
      = operation_results_.emplace_back(BatchOperationResult::Type::Collect);
    std::size_t operation_index = operation_results_.size() - 1;

    result.operation
      = [this, predicate = std::move(predicate),
          add_to_container = std::move(add_to_container), operation_index](
          oxygen::co::BroadcastChannel<NodeData>& channel) -> oxygen::co::Co<> {
      auto& op_result = operation_results_[operation_index];

      try {
        auto reader = channel.ForRead();

        while (true) {
          auto node_data = co_await reader.Receive();
          if (!node_data) {
            // Channel closed
            break;
          }

          op_result.nodes_examined++;

          if (predicate(node_data->visited_node)) {
            op_result.nodes_matched++;
            if (node_data->visited_node.handle.IsValid()) {
              add_to_container(SceneNode {
                scene_weak_.lock(), node_data->visited_node.handle });
              op_result.count_result++;
            }
          }
        }

        op_result.status = BatchOperationResult::Status::Completed;
      } catch (const std::exception& e) {
        op_result.status = BatchOperationResult::Status::Failed;
        op_result.error_message = e.what();
      } catch (...) {
        op_result.status = BatchOperationResult::Status::Failed;
        op_result.error_message = "Unknown exception in Collect operation";
      }

      co_return;
    };

    return operation_index;
  }
  //! Register Count operation with BroadcastChannel coroutine
  template <typename Predicate> std::size_t Count(Predicate predicate)
  {
    auto& result
      = operation_results_.emplace_back(BatchOperationResult::Type::Count);
    std::size_t operation_index = operation_results_.size() - 1;

    result.operation
      = [this, predicate = std::move(predicate), operation_index](
          oxygen::co::BroadcastChannel<NodeData>& channel) -> oxygen::co::Co<> {
      auto& op_result = operation_results_[operation_index];

      try {
        auto reader = channel.ForRead();

        while (true) {
          auto node_data = co_await reader.Receive();
          if (!node_data) {
            // Channel closed
            break;
          }

          op_result.nodes_examined++;

          if (predicate(node_data->visited_node)) {
            op_result.nodes_matched++;
            op_result.count_result++;
          }
        }

        op_result.status = BatchOperationResult::Status::Completed;
      } catch (const std::exception& e) {
        op_result.status = BatchOperationResult::Status::Failed;
        op_result.error_message = e.what();
      } catch (...) {
        op_result.status = BatchOperationResult::Status::Failed;
        op_result.error_message = "Unknown exception in Count operation";
      }

      co_return;
    };

    return operation_index;
  }
  //! Register Any operation with BroadcastChannel coroutine
  template <typename Predicate> std::size_t Any(Predicate predicate)
  {
    auto& result
      = operation_results_.emplace_back(BatchOperationResult::Type::Any);
    std::size_t operation_index = operation_results_.size() - 1;

    result.operation
      = [this, predicate = std::move(predicate), operation_index](
          oxygen::co::BroadcastChannel<NodeData>& channel) -> oxygen::co::Co<> {
      auto& op_result = operation_results_[operation_index];

      try {
        auto reader = channel.ForRead();
        op_result.any_result = false;

        while (true) {
          auto node_data = co_await reader.Receive();
          if (!node_data) {
            // Channel closed
            break;
          }

          op_result.nodes_examined++;

          if (predicate(node_data->visited_node)) {
            op_result.nodes_matched++;
            op_result.any_result = true;
            break; // Early termination
          }
        }

        op_result.status = BatchOperationResult::Status::Completed;
      } catch (const std::exception& e) {
        op_result.status = BatchOperationResult::Status::Failed;
        op_result.error_message = e.what();
      } catch (...) {
        op_result.status = BatchOperationResult::Status::Failed;
        op_result.error_message = "Unknown exception in Any operation";
      }

      co_return;
    };

    return operation_index;
  }
  //! Execute all registered operations using BroadcastChannel pattern
  void ExecuteBatch(const AsyncSceneTraversal<const Scene>& async_traversal,
    const std::function<void(BatchQueryExecutor&)>& batch_operations)
  {
    operation_results_.clear();

    // Register operations via the lambda
    batch_operations(*this);

    if (operation_results_.empty()) {
      return;
    }

    // Create event loop following the BroadcastChannel example pattern
    MinimalEventLoop loop;
    oxygen::co::Run(loop, ExecuteBatchAsync(async_traversal));

    // Clear operations after execution
    operation_results_.clear();
  }

  //! Get result for a specific operation
  const BatchOperationResult& GetResult(std::size_t operation_index) const
  {
    return operation_results_[operation_index];
  }

private:
  //! Execute batch using coroutines and BroadcastChannel
  oxygen::co::Co<> ExecuteBatchAsync(
    const AsyncSceneTraversal<const Scene>& async_traversal)
  {
    // Create broadcast channel for node distribution
    oxygen::co::BroadcastChannel<NodeData> node_channel;
    auto& writer = node_channel.ForWrite();
    OXCO_WITH_NURSERY(nursery)
    {
      // Start a coroutine for each operation - each manages its own reader
      for (auto& op_result : operation_results_) {
        nursery.Start([&op_result, &node_channel]() -> oxygen::co::Co<> {
          co_await op_result.operation(node_channel);
          co_return;
        });
      }

      // Start the scene traversal coroutine
      nursery.Start([this, &writer, &async_traversal]() -> oxygen::co::Co<> {
        co_await TraverseSceneAsync(writer, async_traversal, traversal_scope_);
        co_return;
      });

      co_return oxygen::co::kJoin; // Wait for all nursery tasks to finish
    };

    co_return;
  }

  //! Traverse scene and broadcast nodes using scoped traversal when applicable
  static oxygen::co::Co<> TraverseSceneAsync(
    oxygen::co::detail::channel::BroadcastingWriter<NodeData>& writer,
    const AsyncSceneTraversal<const Scene>& async_traversal,
    const std::vector<SceneNode>& traversal_scope)
  {
    // Collect all nodes first to determine total count
    std::vector<ConstVisitedNode> nodes_to_broadcast;

    // Extract common visitor and filter to avoid code duplication (DRY)
    auto collect_visitor
      = [&nodes_to_broadcast](const ConstVisitedNode& visited,
          bool dry_run) -> oxygen::co::Co<VisitResult> {
      if (!dry_run) {
        nodes_to_broadcast.push_back(visited);
      }
      co_return VisitResult::kContinue;
    };

    auto accept_all_filter = [](const ConstVisitedNode& visited,
                               FilterResult previous_result) -> FilterResult {
      return FilterResult::kAccept; // Accept all nodes for broadcast
    };

    // Choose traversal method based on scope configuration
    if (traversal_scope.empty()) {
      // Use full scene traversal
      auto traversal_result = co_await async_traversal.TraverseAsync(
        collect_visitor, TraversalOrder::kPreOrder, accept_all_filter);
      (void)traversal_result; // Suppress unused variable warning
    } else {
      // Use scoped traversal
      auto traversal_result
        = co_await async_traversal.TraverseHierarchiesAsync(traversal_scope,
          collect_visitor, TraversalOrder::kPreOrder, accept_all_filter);
      (void)traversal_result; // Suppress unused variable warning
    }

    // Send each node through the broadcast channel
    for (size_t i = 0; i < nodes_to_broadcast.size(); ++i) {
      bool is_last = (i == nodes_to_broadcast.size() - 1);
      NodeData node_data(nodes_to_broadcast[i], i, is_last);

      // Send node to all operations via broadcast channel
      bool sent = co_await writer.Send(std::move(node_data));

      if (!sent) {
        break; // Channel closed
      }

      // Yield to allow operations to process this node interleaved
      co_await oxygen::co::Yield {};
    }

    // Close the channel to signal completion
    writer.Close();
    co_return;
  }
  std::weak_ptr<const Scene> scene_weak_;
  std::vector<SceneNode> traversal_scope_;
  std::vector<BatchOperationResult> operation_results_;
};

} // anonymous namespace

//=== SceneQuery BroadcastChannel Integration ===---------------------------//

BatchResult SceneQuery::ExecuteBatchImpl(
  std::function<void(const SceneQuery&)> batch_func) const noexcept
{
  if (scene_weak_.expired()) [[unlikely]] {
    return BatchResult { .completed = false };
  }

  // Initialize batch state
  batch_active_ = true;
  try {
    // Create BroadcastChannel coordinator with current traversal scope
    BatchQueryExecutor coordinator(scene_weak_, traversal_scope_);

    // Store coordinator reference for batch operations
    batch_coordinator_ = &coordinator;

    // Execute coordinated batch traversal with registered operations
    coordinator.ExecuteBatch(
      async_traversal_, [&batch_func, this](BatchQueryExecutor& coord) {
        // Call the user's batch function to register operations
        batch_func(*this);
      });

    // Clean up batch state
    batch_coordinator_ = nullptr;
    batch_active_ = false;

    // Return successful batch result
    return BatchResult {
      .nodes_examined = 0, .total_matches = 0, .completed = true
    };
  } catch (...) {
    // Ensure clean state on exception
    batch_coordinator_ = nullptr;
    batch_active_ = false;

    // Return failed batch result
    return BatchResult {
      .nodes_examined = 0, .total_matches = 0, .completed = false
    };
  }
}

std::optional<SceneNode> SceneQuery::ExecuteBatchFindFirst(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
{
  if (!batch_coordinator_) {
    // Fallback to immediate execution if no coordinator
    return ExecuteImmediateFindFirst(predicate);
  }

  // Register query with BroadcastChannel coordinator
  auto coordinator = static_cast<BatchQueryExecutor*>(batch_coordinator_);
  std::size_t operation_index = coordinator->FindFirst(predicate);

  // The operation will be executed during ExecuteBatch, retrieve result after
  // completion
  const auto& result = coordinator->GetResult(operation_index);

  return result.found_node;
}

QueryResult SceneQuery::ExecuteBatchCount(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
{
  if (!batch_coordinator_) {
    // Fallback to immediate execution if no coordinator
    return ExecuteImmediateCount(predicate);
  }

  // Register query with BroadcastChannel coordinator
  auto coordinator = static_cast<BatchQueryExecutor*>(batch_coordinator_);
  std::size_t operation_index = coordinator->Count(predicate);

  // The operation will be executed during ExecuteBatch, retrieve result after
  // completion
  const auto& result = coordinator->GetResult(operation_index);

  return QueryResult { .nodes_examined = result.nodes_examined,
    .nodes_matched = result.count_result,
    .completed = (result.status == BatchOperationResult::Status::Completed) };
}

std::optional<bool> SceneQuery::ExecuteBatchAny(
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
{
  if (!batch_coordinator_) {
    // Fallback to immediate execution if no coordinator
    return ExecuteImmediateAny(predicate);
  }

  // Register query with BroadcastChannel coordinator
  auto coordinator = static_cast<BatchQueryExecutor*>(batch_coordinator_);
  std::size_t operation_index = coordinator->Any(predicate);

  // The operation will be executed during ExecuteBatch, retrieve result after
  // completion
  const auto& result = coordinator->GetResult(operation_index);

  return result.any_result;
}

QueryResult SceneQuery::ExecuteBatchCollectImpl(
  std::function<void(const SceneNode&)> add_to_container,
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
{
  if (!batch_coordinator_) {
    // Fallback to immediate execution if no coordinator
    // Note: This would require implementing ExecuteImmediateCollectImpl
    return QueryResult { .completed = false };
  }

  // Register query with BroadcastChannel coordinator
  auto coordinator = static_cast<BatchQueryExecutor*>(batch_coordinator_);
  std::size_t operation_index
    = coordinator->Collect(predicate, std::move(add_to_container));

  // The operation will be executed during ExecuteBatch, retrieve result after
  // completion
  const auto& result = coordinator->GetResult(operation_index);

  return QueryResult { .nodes_examined = result.nodes_examined,
    .nodes_matched = result.count_result,
    .completed = (result.status == BatchOperationResult::Status::Completed) };
}
