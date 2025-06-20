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

#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Algorithms.h>
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
  auto Run() -> void
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

  auto Stop() -> void { should_stop_ = true; }
  auto IsRunning() const noexcept -> bool { return running_; }
  auto Schedule(std::function<void()> task) -> void
  {
    tasks_.push(std::move(task));
  }

private:
  std::atomic<bool> running_ { false };
  std::atomic<bool> should_stop_ { false };
  std::queue<std::function<void()>> tasks_;
};

} // namespace oxygen::scene::detail

using oxygen::scene::detail::MinimalEventLoop;

//! EventLoopTraits specialization for MinimalEventLoop
template <> struct oxygen::co::EventLoopTraits<MinimalEventLoop> {
  static auto EventLoopId(MinimalEventLoop& loop) -> EventLoopID
  {
    return EventLoopID(&loop);
  }

  static auto Schedule(MinimalEventLoop& loop, std::function<void()> task)
    -> void
  {
    loop.Schedule(std::move(task));
  }

  static auto Run(MinimalEventLoop& loop) -> void { loop.Run(); }
  static auto Stop(MinimalEventLoop& loop) -> void { loop.Stop(); }
  static auto IsRunning(const MinimalEventLoop& loop) -> bool
  {
    return loop.IsRunning();
  }
};

namespace {

//! Internal execution context for batch operations (separate from public data)
struct BatchOperation {
  enum class Status { Pending, Completed, Failed };

  Status status = Status::Pending;
  // Public result data (metadata only - results go to user references)
  QueryResult result;

  // Internal result storage for coordination and early termination
  std::optional<SceneNode> internal_found_node; // For FindFirst coordination
  std::optional<bool> internal_any_result; // For Any coordination
  std::size_t internal_count_result = 0; // For Count/Collect coordination
  // Internal execution context only
  std::function<oxygen::co::Co<>(
    oxygen::co::BroadcastChannel<ConstVisitedNode>&)>
    operation;
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

  //! Register FindFirst operation with reference output
  template <typename Predicate>
  auto FindFirst(std::optional<SceneNode>& output, Predicate predicate) -> void
  {
    LOG_F(INFO,
      "BatchQuery: Registering FindFirst operation during registration phase");

    auto& operation = operations_.emplace_back();
    std::size_t operation_index = operations_.size() - 1;
    operation.operation
      = [this, &output, predicate = std::move(predicate), operation_index](
          oxygen::co::BroadcastChannel<ConstVisitedNode>& channel)
      -> oxygen::co::Co<> {
      LOG_F(INFO,
        "BatchQuery: FindFirst operation starting execution during traversal "
        "(op_index=%zu)",
        operation_index);

      auto& op = operations_[operation_index];
      auto& result = op.result;
      try {
        auto reader = channel.ForRead();
        op.internal_found_node = std::nullopt;
        output = std::nullopt; // Initialize output

        while (true) {
          auto node_data = co_await reader.Receive();
          if (!node_data) {
            // Channel closed - no match found
            LOG_F(INFO,
              "BatchQuery: FindFirst operation completed during traversal "
              "(op_index=%zu, examined=%zu, matched=%zu)",
              operation_index, result.nodes_examined, result.nodes_matched);
            break;
          }
          result.nodes_examined++;
          if (predicate(*node_data)) {
            result.nodes_matched++;
            LOG_F(INFO, "BatchQuery: FindFirst found match during traversal");

            if (node_data->handle.IsValid()) {
              auto found_node
                = SceneNode { scene_weak_.lock(), node_data->handle };
              op.internal_found_node = found_node; // Store internally
              output = std::move(found_node); // Populate caller's variable
            }

            // Update status immediately when work is complete
            op.status = BatchOperation::Status::Completed;
            LOG_F(INFO,
              "BatchQuery: FindFirst operation completed immediately after "
              "finding match (op_index=%zu)",
              operation_index);
            break; // Early termination
          }
        }

        // Only update status if not already completed
        if (op.status != BatchOperation::Status::Completed) {
          op.status = BatchOperation::Status::Completed;
        }
      } catch (const std::exception& e) {
        LOG_F(ERROR,
          "BatchQuery: FindFirst operation failed during traversal "
          "(op_index=%zu): %s",
          operation_index, e.what());
        op.status = BatchOperation::Status::Failed;
        result.error_message = e.what();
      } catch (...) {
        LOG_F(ERROR,
          "BatchQuery: FindFirst operation failed with unknown exception "
          "during traversal (op_index=%zu)",
          operation_index);
        op.status = BatchOperation::Status::Failed;
        result.error_message = "Unknown exception in FindFirst operation";
      }
      co_return;
    };
  }

  //! Register Collect operation with inserter function
  template <typename Predicate>
  auto Collect(
    std::function<void(const SceneNode&)> inserter, Predicate predicate) -> void
  {
    LOG_F(INFO,
      "BatchQuery: Registering Collect operation during registration phase");

    auto& operation = operations_.emplace_back();
    std::size_t operation_index = operations_.size() - 1;

    operation.operation
      = [this, inserter, predicate = std::move(predicate), operation_index](
          oxygen::co::BroadcastChannel<ConstVisitedNode>& channel)
      -> oxygen::co::Co<> {
      LOG_F(INFO,
        "BatchQuery: Collect operation starting execution during traversal "
        "(op_index=%zu)",
        operation_index);

      auto& op = operations_[operation_index];
      auto& result = op.result;

      try {
        auto reader = channel.ForRead();
        op.internal_count_result = 0; // Reset internal counter

        while (true) {
          auto node_data = co_await reader.Receive();
          if (!node_data) {
            // Channel closed
            LOG_F(INFO,
              "BatchQuery: Collect operation completed during traversal "
              "(op_index=%zu, examined=%zu, collected=%zu)",
              operation_index, result.nodes_examined, op.internal_count_result);
            break;
          }
          result.nodes_examined++;
          if (predicate(*node_data)) {
            result.nodes_matched++;
            if (node_data->handle.IsValid()) {
              // Use inserter function instead of direct container access
              inserter(SceneNode { scene_weak_.lock(), node_data->handle });
              op.internal_count_result++; // Track internally
            }
          }
        }

        op.status = BatchOperation::Status::Completed;
      } catch (const std::exception& e) {
        LOG_F(ERROR,
          "BatchQuery: Collect operation failed during traversal "
          "(op_index=%zu): %s",
          operation_index, e.what());
        op.status = BatchOperation::Status::Failed;
        result.error_message = e.what();
      } catch (...) {
        LOG_F(ERROR,
          "BatchQuery: Collect operation failed with unknown exception during "
          "traversal (op_index=%zu)",
          operation_index);
        op.status = BatchOperation::Status::Failed;
        result.error_message = "Unknown exception in Collect operation";
      }

      co_return;
    };
  }

  //! Register Count operation with QueryResult return (no ref needed)
  template <typename Predicate>
  auto Count(std::optional<size_t>& output, Predicate predicate) -> void
  {
    LOG_F(INFO,
      "BatchQuery: Registering Count operation during registration phase");

    auto& operation = operations_.emplace_back();
    std::size_t operation_index = operations_.size() - 1;

    operation.operation
      = [this, &output, predicate = std::move(predicate), operation_index](
          oxygen::co::BroadcastChannel<ConstVisitedNode>& channel)
      -> oxygen::co::Co<> {
      LOG_F(INFO,
        "BatchQuery: Count operation starting execution during traversal "
        "(op_index=%zu)",
        operation_index);

      auto& op = operations_[operation_index];
      auto& result = op.result;

      try {
        auto reader = channel.ForRead();

        while (true) {
          auto node_data = co_await reader.Receive();
          if (!node_data) {
            // Channel closed
            LOG_F(INFO,
              "BatchQuery: Count operation completed during traversal "
              "(op_index=%zu, examined=%zu, counted=%zu)",
              operation_index, result.nodes_examined, op.internal_count_result);
            break;
          }
          result.nodes_examined++;
          if (predicate(*node_data)) {
            result.nodes_matched++;
            op.internal_count_result++; // Track internally
          }
        }
        output.emplace(result.nodes_matched);
        op.status = BatchOperation::Status::Completed;
      } catch (const std::exception& e) {
        LOG_F(ERROR,
          "BatchQuery: Count operation failed during traversal (op_index=%zu): "
          "%s",
          operation_index, e.what());
        op.status = BatchOperation::Status::Failed;
        result.error_message = e.what();
      } catch (...) {
        LOG_F(ERROR,
          "BatchQuery: Count operation failed with unknown exception during "
          "traversal (op_index=%zu)",
          operation_index);
        op.status = BatchOperation::Status::Failed;
        result.error_message = "Unknown exception in Count operation";
      }

      co_return;
    };
  }

  //! Register Any operation with reference output
  template <typename Predicate>
  auto Any(std::optional<bool>& output, Predicate predicate) -> void
  {
    LOG_F(
      INFO, "BatchQuery: Registering Any operation during registration phase");

    auto& operation = operations_.emplace_back();
    std::size_t operation_index = operations_.size() - 1;

    operation.operation
      = [this, &output, predicate = std::move(predicate), operation_index](
          oxygen::co::BroadcastChannel<ConstVisitedNode>& channel)
      -> oxygen::co::Co<> {
      LOG_F(INFO,
        "BatchQuery: Any operation starting execution during traversal "
        "(op_index=%zu)",
        operation_index);

      auto& op = operations_[operation_index];
      auto& result = op.result;
      try {
        auto reader = channel.ForRead();
        op.internal_any_result = false;
        output = false; // Initialize output

        while (true) {
          auto node_data = co_await reader.Receive();
          if (!node_data) {
            // Channel closed
            LOG_F(INFO,
              "BatchQuery: Any operation completed during traversal "
              "(op_index=%zu, examined=%zu, found=%s)",
              operation_index, result.nodes_examined,
              op.internal_any_result.value_or(false) ? "true" : "false");
            break;
          }
          result.nodes_examined++;
          if (predicate(*node_data)) {
            result.nodes_matched++;
            op.internal_any_result = true; // Store internally
            output = true; // Set caller's variable
            LOG_F(INFO,
              "BatchQuery: Any operation found match during traversal "
              "(op_index=%zu, examined=%zu)",
              operation_index, result.nodes_examined);

            // Update status immediately when work is complete
            op.status = BatchOperation::Status::Completed;
            LOG_F(INFO,
              "BatchQuery: Any operation completed immediately after finding "
              "match (op_index=%zu)",
              operation_index);
            break; // Early termination
          }
        }

        op.status = BatchOperation::Status::Completed;
      } catch (const std::exception& e) {
        LOG_F(ERROR,
          "BatchQuery: Any operation failed during traversal (op_index=%zu): "
          "%s",
          operation_index, e.what());
        op.status = BatchOperation::Status::Failed;
        result.error_message = e.what();
      } catch (...) {
        LOG_F(ERROR,
          "BatchQuery: Any operation failed with unknown exception during "
          "traversal (op_index=%zu)",
          operation_index);
        op.status = BatchOperation::Status::Failed;
        result.error_message = "Unknown exception in Any operation";
      }

      co_return;
    };
  }

  //! Execute all registered operations using BroadcastChannel pattern
  auto ExecuteBatch(const AsyncSceneTraversal<const Scene>& async_traversal,
    const std::function<void(BatchQueryExecutor&)>& batch_operations)
    -> BatchResult
  {
    operations_.clear();

    // Register all operations by calling the lambda
    batch_operations(*this);

    if (operations_.empty()) {
      return {};
    }

    // Execute the batch - all reference variables will be populated
    MinimalEventLoop loop;
    oxygen::co::Run(loop, ExecuteBatchAsync(async_traversal));

    // Create final result with metrics
    return CreateFinalResult();
  }

  //! Execute batch async (made public for ExecuteBatchImpl)
  auto ExecuteBatchAsync(
    const AsyncSceneTraversal<const Scene>& async_traversal) -> oxygen::co::Co<>
  { // Create broadcast channel for node distribution
    oxygen::co::BroadcastChannel<ConstVisitedNode> node_channel;
    auto& writer = node_channel.ForWrite();

    // Create a vector of operation coroutines
    std::vector<oxygen::co::Co<>> operation_coroutines;
    operation_coroutines.reserve(operations_.size());

    for (auto& operation : operations_) {
      operation_coroutines.emplace_back(operation.operation(node_channel));
    } // Create the traversal coroutine
    auto traversal_coroutine = this->StreamTraverseSceneAsync(
      writer, async_traversal, traversal_scope_);

    // Use AnyOf to race the traversal against ALL operations completing
    // The batch completes when EITHER:
    // 1. The traversal finishes (all nodes processed), OR
    // 2. ALL operations complete (early termination when no more work needed)
    co_await oxygen::co::AnyOf(std::move(traversal_coroutine),
      oxygen::co::AllOf(std::move(operation_coroutines)));

    co_return;
  }

  //! Create final result (made public for ExecuteBatchImpl)
  auto CreateFinalResult() -> BatchResult
  {
    BatchResult result;
    result.success = true;

    // Reserve space to avoid reallocations
    result.operation_results.reserve(
      operations_.size()); // Move public data from each operation
    for (auto& op : operations_) {
      // Aggregate metrics
      result.nodes_examined
        = std::max(result.nodes_examined, op.result.nodes_examined);
      result.total_matches += op.result.nodes_matched;

      result.success &= static_cast<bool>(op.result);

      // Move operation result (avoid copying)
      result.operation_results.emplace_back(std::move(op.result));
    }

    return result;
  }

  //! Stream nodes directly during traversal (single-pass, no intermediate
  //! collection)
  auto StreamTraverseSceneAsync(
    oxygen::co::detail::channel::BroadcastingWriter<ConstVisitedNode>& writer,
    const AsyncSceneTraversal<const Scene>& async_traversal,
    const std::vector<SceneNode>& traversal_scope) -> oxygen::co::Co<>
  {
    auto streaming_visitor = [this, &writer](const ConstVisitedNode& visited,
                               bool dry_run) -> oxygen::co::Co<VisitResult> {
      if (!dry_run) {
        // Stream node directly to broadcast channel
        bool sent = co_await writer.Send(visited);

        if (!sent) {
          // Channel closed early - stop traversal
          co_return VisitResult::kStop;
        }

        // Yield to allow operations to process this node
        co_await oxygen::co::Yield {};

        // Check if all operations are complete after processing this node
        bool all_complete = true;
        for (const auto& op : operations_) {
          if (op.status == BatchOperation::Status::Pending) {
            all_complete = false;
            break;
          }
        }

        if (all_complete) {
          LOG_F(
            INFO, "BatchQuery: All operations complete, stopping traversal");
          co_return VisitResult::kStop;
        }
      }
      co_return VisitResult::kContinue;
    };

    auto accept_all_filter
      = [](const ConstVisitedNode& /*visited*/,
          FilterResult /*previous_result*/) -> FilterResult {
      return FilterResult::kAccept; // Accept all nodes for broadcast
    };

    try {
      // Choose traversal method based on scope configuration
      if (traversal_scope.empty()) {
        // Use full scene traversal
        auto traversal_result = co_await async_traversal.TraverseAsync(
          streaming_visitor, TraversalOrder::kPreOrder, accept_all_filter);
        (void)traversal_result; // Suppress unused variable warning
      } else {
        // Use scoped traversal
        auto traversal_result
          = co_await async_traversal.TraverseHierarchiesAsync(traversal_scope,
            streaming_visitor, TraversalOrder::kPreOrder, accept_all_filter);
        (void)traversal_result; // Suppress unused variable warning
      }
    } catch (...) {
      // Ensure channel is closed even on exception
    }

    // Close the channel to signal completion
    writer.Close();
    co_return;
  }

  std::weak_ptr<const Scene> scene_weak_;
  std::vector<SceneNode> traversal_scope_;
  std::vector<BatchOperation> operations_;
};

} // anonymous namespace

//=== SceneQuery BroadcastChannel Integration ===---------------------------//

auto SceneQuery::ExecuteBatchImpl(
  std::function<void(const SceneQuery&)> batch_func) const noexcept
  -> BatchResult
{
  // Initialize batch state
  batch_active_ = true;
  try {
    // Create BroadcastChannel coordinator with current traversal scope
    BatchQueryExecutor coordinator(scene_weak_, traversal_scope_);

    // Store coordinator reference for batch operations
    batch_coordinator_ = &coordinator;

    // Execute coordinated batch traversal with registered operations
    auto batch_result = coordinator.ExecuteBatch(
      async_traversal_, [&batch_func, this](BatchQueryExecutor& /*coord*/) {
        // Call the user's batch function - they will use the new
        // reference-based methods
        batch_func(*this);
      });

    // Clean up batch state
    batch_coordinator_ = nullptr;
    batch_active_ = false;

    // Return batch result with proper aggregated metrics
    return batch_result;
  } catch (...) {
    // Ensure clean state on exception
    batch_coordinator_ = nullptr;
    batch_active_ = false;

    // Return failed batch result
    return BatchResult {
      .nodes_examined = 0, .total_matches = 0, .success = false
    };
  }
}

//=== Batch Implementation Methods ===---------------------------------------//

auto SceneQuery::BatchFindFirstImpl(std::optional<SceneNode>& result,
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> void
{
  // Register with batch coordinator using reference output
  auto coordinator = static_cast<BatchQueryExecutor*>(batch_coordinator_);
  coordinator->FindFirst(result, predicate);
}

auto SceneQuery::BatchCollectImpl(
  std::function<void(const SceneNode&)> inserter,
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> void
{
  // Register with batch coordinator using inserter function
  auto coordinator = static_cast<BatchQueryExecutor*>(batch_coordinator_);
  coordinator->Collect(inserter, predicate);
}

auto SceneQuery::BatchCountImpl(std::optional<size_t>& result,
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> void
{
  // Register with batch coordinator - results go into BatchResult
  auto coordinator = static_cast<BatchQueryExecutor*>(batch_coordinator_);
  coordinator->Count(result, predicate);
}

auto SceneQuery::BatchAnyImpl(std::optional<bool>& result,
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> void
{
  // Register with batch coordinator using reference output
  auto coordinator = static_cast<BatchQueryExecutor*>(batch_coordinator_);
  coordinator->Any(result, predicate);
}
