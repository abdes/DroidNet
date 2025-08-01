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

/*!
 Minimal EventLoop implementation for scene query batch processing that
 provides a simple cooperative task scheduler for BroadcastChannel operations.

 ### Key Features

 - **Simple Task Queue**: FIFO execution of scheduled tasks
 - **Cooperative Scheduling**: Tasks yield control voluntarily
 - **Atomic State Management**: Thread-safe start/stop operations
 - **Lightweight Design**: Minimal overhead for batch query coordination

 ### Usage Patterns

 Used internally by BatchQueryExecutor to coordinate coroutine execution
 during batch query operations. Not intended for general-purpose use.

 @note This is a minimal implementation focused on batch query requirements
 @see BatchQueryExecutor for the primary use case
*/
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

/*!
 EventLoopTraits specialization enabling MinimalEventLoop integration
 with the OxCo coroutine framework for batch query coordination.

 Provides standard EventLoop interface implementation by delegating
 to MinimalEventLoop methods for scheduling, lifecycle, and identification.

 @note Required specialization for OxCo framework compatibility
 @see MinimalEventLoop for the underlying event loop implementation
*/
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

namespace oxygen::scene::detail {

/*!
 Internal execution context for batch operations containing both public
 result metadata and private coordination state for early termination.

 ### Architecture

 - **Status Tracking**: Manages operation lifecycle (Pending/Completed/Failed)
 - **Result Storage**: Public QueryResult for metrics and error reporting
 - **Internal Coordination**: Private state for early termination decisions
 - **Coroutine Storage**: Function object for deferred execution

 ### Separation of Concerns

 Public result data (QueryResult) is separate from internal coordination
 state to maintain clean interfaces while enabling optimization features
 like early termination and progress tracking.

 @note This is an internal implementation detail of BatchQueryExecutor
*/
struct BatchOperation {
  enum class Status : uint8_t { kPending, kCompleted, kFailed };

  Status status = Status::kPending;
  QueryResult result;

  std::function<co::Co<>(co::BroadcastChannel<ConstVisitedNode>&)> operation;
};

/*!
 BroadcastChannel-based batch coordinator that executes multiple query
 operations in a single scene traversal using coroutine-based concurrency.

 ### Architecture

 - **Operation Registration**: Collects query operations during setup phase
 - **BroadcastChannel Distribution**: Streams nodes to all operations
 - **Coroutine Coordination**: Uses oxygen::co framework for concurrency
 - **Early Termination**: Stops when all FindFirst/Any operations complete
 - **Result Aggregation**: Combines metrics from all operations

 ### Execution Flow

 1. **Registration Phase**: User lambda registers operations via method calls
 2. **Channel Setup**: Creates BroadcastChannel for node distribution
 3. **Coroutine Launch**: Starts operation coroutines and traversal
 4. **Concurrent Execution**: Operations process nodes concurrently
 5. **Result Collection**: Aggregates metrics and populates user references

 ### Performance Benefits

 - **Single Traversal**: All operations share one scene traversal pass
 - **Memory Efficiency**: No intermediate collections, direct streaming
 - **Cache Locality**: Maximizes CPU cache utilization
 - **Early Termination**: Stops traversal when possible

 @note This class follows the established BroadcastChannel pattern
 @see BatchOperation for individual operation context
*/
class BatchQueryExecutor {
public:
  explicit BatchQueryExecutor(std::weak_ptr<const Scene> scene,
    std::vector<SceneNode> traversal_scope = {})
    : scene_weak_(std::move(scene))
    , traversal_scope_(std::move(traversal_scope))
  {
  }

  ~BatchQueryExecutor() = default;

  // This class manages a unique execution context and should not be copied or
  // moved.
  OXYGEN_MAKE_NON_COPYABLE(BatchQueryExecutor)
  OXYGEN_MAKE_NON_MOVABLE(BatchQueryExecutor)

  /*!
   Registers a FindFirst operation for batch execution, storing the result
   in the provided reference when a matching node is found.

   @tparam Predicate Callable predicate accepting const ConstVisitedNode&
   @param output Reference to optional SceneNode to receive the first match
   @param predicate Function to test each node during batch traversal

   ### Registration Strategy

   - **Coroutine Creation**: Builds async operation for later execution
   - **Reference Capture**: Stores user's output reference for result storage
   - **Early Termination**: Operation completes immediately upon first match
   - **Metrics Tracking**: Records nodes examined and matched for analytics

   ### Performance Characteristics

   - **Time Complexity**: O(k) where k is position of first match
   - **Memory**: Minimal, only reference storage and coordination state
   - **Concurrency**: Executes concurrently with other batch operations

   @note Output reference must remain valid until batch execution completes
   @see ExecuteBatch for batch execution orchestration
  */
  template <typename Predicate>
  auto FindFirst(std::optional<SceneNode>& output, Predicate predicate) -> void
  {
    LOG_F(2, "registering operation for batch execution");

    auto& operation = operations_.emplace_back();
    std::size_t operation_index = operations_.size() - 1;
    ++pending_operations_; // Increment for the new operation

    operation.operation
      = [this, &output, predicate = std::move(predicate), operation_index](
          co::BroadcastChannel<ConstVisitedNode>& channel) -> co::Co<> {
      auto& op = operations_[operation_index];
      auto& result = op.result;
      try {
        auto reader = channel.ForRead();
        output = std::nullopt; // Initialize output

        while (true) {
          auto node_data = co_await reader.Receive();
          if (!node_data) {
            // Channel closed - no match found
            LOG_F(2, "FindFirst({}): channel closed (examined={}, matched={})",
              operation_index, result.nodes_examined, result.nodes_matched);
            break;
          }
          result.nodes_examined++; // statistics update for THIS operation
          if (predicate(*node_data)) {
            result.nodes_matched++;
            if (node_data->handle.IsValid()) {
              SceneNode found_node { scene_weak_.lock(), node_data->handle };
              output = std::move(found_node); // Populate caller's variable
            }

            LOG_F(2, "FindFirst({}): found match", operation_index);
            break; // Early termination
          }
        }

        op.status = BatchOperation::Status::kCompleted;
      } catch (...) {
        HandleOperationException(op, operation_index, "FindFirst");
      }

      --pending_operations_;
      LOG_F(2, "FindFirst({}): completed (remaining operations={})",
        operation_index, pending_operations_.load());

      co_return;
    };
  }

  /*!
   Registers a Collect operation for batch execution using type-erased
   container insertion via the provided inserter function.

   @tparam Predicate Callable predicate accepting const ConstVisitedNode&
   @param inserter Function to add matching SceneNodes to user's container
   @param predicate Function to test each node during batch traversal

   ### Registration Strategy

   - **Type Erasure**: Uses inserter function to avoid template dependencies
   - **Full Traversal**: Processes all nodes, no early termination
   - **Container Agnostic**: Works with any container via inserter function
   - **Metrics Tracking**: Records nodes examined and collected

   ### Performance Characteristics

   - **Time Complexity**: O(n) for full scene traversal
   - **Memory**: User-controlled via container allocation strategy
   - **Concurrency**: Executes concurrently with other batch operations

   @note Container must remain valid until batch execution completes
   @see ExecuteBatch for batch execution orchestration
  */
  template <typename Predicate>
  auto Collect(
    std::function<void(const SceneNode&)> inserter, Predicate predicate) -> void
  {
    LOG_F(2, "registering operation for batch execution");

    auto& operation = operations_.emplace_back();
    std::size_t operation_index = operations_.size() - 1;
    ++pending_operations_; // Increment for the new operation

    operation.operation
      = [this, inserter = std::move(inserter), predicate = std::move(predicate),
          operation_index](
          co::BroadcastChannel<ConstVisitedNode>& channel) -> co::Co<> {
      auto& op = operations_[operation_index];
      auto& result = op.result;

      try {
        auto reader = channel.ForRead();
        while (true) {
          auto node_data = co_await reader.Receive();
          if (!node_data) {
            LOG_F(2, "Collect({}): channel closed (examined={}, collected={})",
              operation_index, result.nodes_examined, result.nodes_matched);
            break;
          }
          result.nodes_examined++;
          if (predicate(*node_data)) {
            result.nodes_matched++;
            if (node_data->handle.IsValid()) {
              // Use inserter function instead of direct container access
              inserter(SceneNode { scene_weak_.lock(), node_data->handle });
            }
          }
        }

        op.status = BatchOperation::Status::kCompleted;
      } catch (...) {
        HandleOperationException(op, operation_index, "Collect");
      }

      --pending_operations_;
      LOG_F(2, "Collect({}): completed (remaining operations={})",
        operation_index, pending_operations_.load());

      co_return;
    };
  }

  /*!
   Registers a Count operation for batch execution, storing the final count
   in the provided reference when traversal completes.

   @tparam Predicate Callable predicate accepting const ConstVisitedNode&
   @param output Reference to optional size_t to receive the final count
   @param predicate Function to test each node during batch traversal

   ### Registration Strategy

   - **Full Traversal**: Processes all nodes to ensure accurate count
   - **Reference Output**: Stores count in user's reference variable
   - **Zero Allocation**: No memory allocation during counting
   - **Metrics Tracking**: Records nodes examined and counted

   ### Performance Characteristics

   - **Time Complexity**: O(n) for full scene traversal
   - **Memory**: Zero allocations beyond internal coordination state
   - **Concurrency**: Executes concurrently with other batch operations

   @note Output reference must remain valid until batch execution completes
   @see ExecuteBatch for batch execution orchestration
  */
  template <typename Predicate>
  auto Count(std::optional<size_t>& output, Predicate predicate) -> void
  {
    LOG_F(2, "registering operation for batch execution");

    auto& operation = operations_.emplace_back();
    std::size_t operation_index = operations_.size() - 1;
    ++pending_operations_; // Increment for the new operation

    operation.operation
      = [this, &output, predicate = std::move(predicate), operation_index](
          co::BroadcastChannel<ConstVisitedNode>& channel) -> co::Co<> {
      auto& op = operations_[operation_index];
      auto& result = op.result;

      try {
        auto reader = channel.ForRead();

        while (true) {
          auto node_data = co_await reader.Receive();
          if (!node_data) {
            LOG_F(2, "Count({}): channel closed (examined={}, counted={})",
              operation_index, result.nodes_examined, result.nodes_matched);
            break;
          }
          result.nodes_examined++;
          if (predicate(*node_data)) {
            result.nodes_matched++;
          }
        }

        output.emplace(result.nodes_matched);
        op.status = BatchOperation::Status::kCompleted;
      } catch (...) {
        HandleOperationException(op, operation_index, "Count");
      }

      --pending_operations_;
      LOG_F(2, "Count({}): completed (remaining operations={})",
        operation_index, pending_operations_.load());

      co_return;
    };
  }

  /*!
   Registers an Any operation for batch execution, storing the boolean result
   in the provided reference when a match is found or traversal completes.

   @tparam Predicate Callable predicate accepting const ConstVisitedNode&
   @param output Reference to optional bool to receive the existence result
   @param predicate Function to test each node during batch traversal

   ### Registration Strategy

   - **Early Termination**: Operation completes immediately upon first match
   - **Reference Output**: Stores boolean result in user's reference variable
   - **Existence Testing**: Returns true if any match found, false otherwise
   - **Metrics Tracking**: Records nodes examined until match or completion

   ### Performance Characteristics

   - **Time Complexity**: O(k) where k is position of first match
   - **Memory**: Minimal, only boolean storage and coordination state
   - **Concurrency**: Executes concurrently with other batch operations

   @note Output reference must remain valid until batch execution completes
   @see ExecuteBatch for batch execution orchestration
  */
  template <typename Predicate>
  auto Any(std::optional<bool>& output, Predicate predicate) -> void
  {
    LOG_F(2, "registering operation for batch execution");

    auto& operation = operations_.emplace_back();
    std::size_t operation_index = operations_.size() - 1;
    ++pending_operations_; // Increment for the new operation

    operation.operation
      = [this, &output, predicate = std::move(predicate), operation_index](
          co::BroadcastChannel<ConstVisitedNode>& channel) -> co::Co<> {
      auto& op = operations_[operation_index];
      auto& result = op.result;
      try {
        auto reader = channel.ForRead();
        output = false; // Initialize output

        while (true) {
          auto node_data = co_await reader.Receive();
          if (!node_data) {
            LOG_F(2, "Any({}): channel closed (examined={}, found={})",
              operation_index, result.nodes_examined,
              result.nodes_matched != 0 ? "true" : "false");
            break;
          }
          result.nodes_examined++;
          if (predicate(*node_data)) {
            result.nodes_matched++;
            output = true;
            LOG_F(2, "Any({}): found match (examined={})", operation_index,
              result.nodes_examined);
            break;
          }
        }

        op.status = BatchOperation::Status::kCompleted;
      } catch (...) {
        HandleOperationException(op, operation_index, "Any");
      }

      --pending_operations_;
      LOG_F(2, "Any({}): completed (remaining operations={})", operation_index,
        pending_operations_.load());

      co_return;
    };
  }

  /*!
   Executes all registered operations using BroadcastChannel pattern with
   coroutine-based concurrency for optimal performance.

   @param async_traversal Scene traversal engine for async node streaming
   @param batch_operations Lambda function that registers operations
   @return BatchResult with aggregated metrics and success status

   ### Execution Strategy

   1. **Operation Registration**: Executes lambda to collect operations
   2. **Channel Creation**: Sets up BroadcastChannel for node distribution
   3. **Coroutine Launch**: Starts operation and traversal coroutines
   4. **Concurrent Execution**: All operations process nodes concurrently
   5. **Result Aggregation**: Combines metrics from all operations

   ### Performance Benefits

   - **Single Traversal**: All operations share one scene traversal pass
   - **Concurrent Processing**: Operations execute concurrently via coroutines
   - **Early Termination**: Stops when all FindFirst/Any operations complete
   - **Memory Efficiency**: No intermediate collections, direct streaming

   @note All user reference variables are populated during execution
   @see ExecuteBatchAsync for the coroutine implementation
  */
  auto ExecuteBatch(const AsyncSceneTraversal<const Scene>& async_traversal,
    const std::function<void(BatchQueryExecutor&)>& batch_operations)
    -> BatchResult
  {
    operations_.clear();
    pending_operations_ = 0; // Reset for new batch

    // Register all operations by calling the lambda
    batch_operations(*this);

    if (operations_.empty()) {
      return {};
    }

    // Execute the batch - all reference variables will be populated
    MinimalEventLoop loop;
    co::Run(loop, ExecuteBatchAsync(async_traversal));

    // Create final result with metrics
    return CreateFinalResult();
  }
  /*!
   Internal coroutine implementation for batch operation execution using
   BroadcastChannel pattern with early termination optimization.

   @param async_traversal Scene traversal engine for async node streaming
   @return Coroutine that completes when batch execution finishes

   ### Implementation Notes
   - Uses AnyOf to race traversal completion against operation completion
   - Enables early termination when all operations finish before full traversal
   - Coordinates BroadcastChannel distribution to operation coroutines

   @note This is an internal implementation called by ExecuteBatch
   @see ExecuteBatch for the public interface and execution strategy details
  */
  auto ExecuteBatchAsync(
    const AsyncSceneTraversal<const Scene>& async_traversal) const -> co::Co<>
  { // Create broadcast channel for node distribution
    co::BroadcastChannel<ConstVisitedNode> node_channel;
    auto& writer = node_channel.ForWrite();

    // Create a vector of operation coroutines
    std::vector<co::Co<>> operation_coroutines;
    operation_coroutines.reserve(operations_.size());

    for (auto& operation : operations_) {
      operation_coroutines.emplace_back(operation.operation(node_channel));
    }

    // The batch completes when EITHER:
    co_await oxygen::co::AnyOf(
      // 1. The traversal finishes (all nodes processed), OR
      this->StreamTraverseSceneAsync(writer, async_traversal, traversal_scope_),
      // 2. ALL operations complete (early termination when no more work needed)
      oxygen::co::AllOf(std::move(operation_coroutines)));

    co_return;
  }
  /*!
   Creates aggregated BatchResult from all completed operations with
   optimized move semantics to avoid copying operation results.

   @return BatchResult with combined metrics and per-operation details

   ### Key Aggregations
   - **Nodes Examined**: Maximum across operations (shared traversal)
   - **Total Matches**: Sum of matches from all operations
   - **Success Status**: Combined success from all operations

   @note Helper method called by ExecuteBatch after operations complete
   @see ExecuteBatch for the orchestration context that uses this result
  */
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

  /*!
   Streams scene nodes directly to BroadcastChannel during async traversal
   with early termination optimization for batch query coordination.

   @param writer BroadcastChannel writer for node distribution to operations
   @param async_traversal Scene traversal engine for async node streaming
   @param traversal_scope Node scope for traversal (empty = full scene)
   @return Coroutine that completes when traversal finishes or stops early

   ### Core Features
   - **Direct Streaming**: Zero-copy node distribution to operation coroutines
   - **Early Termination**: Stops when all operations complete
   - **Cooperative Yielding**: Allows operations to process nodes between sends
   - **Exception Safety**: Ensures proper channel closure on any error

   ### Performance Characteristics
   - **Time Complexity**: O(k) where k is nodes until early termination
   - **Memory**: Zero allocations, direct streaming architecture
   - **Concurrency**: Coordinates with operation coroutines via yielding

   @note Core streaming engine called by ExecuteBatchAsync
   @see ExecuteBatchAsync for the coordination context and AnyOf racing logic
  */
  auto StreamTraverseSceneAsync(
    co::detail::channel::BroadcastingWriter<ConstVisitedNode>& writer,
    const AsyncSceneTraversal<const Scene>& async_traversal,
    const std::vector<SceneNode>& traversal_scope) const -> co::Co<>
  {
    auto streaming_visitor = [this, &writer](const ConstVisitedNode& visited,
                               const bool dry_run) -> co::Co<VisitResult> {
      if (!dry_run) {
        // Stream node directly to broadcast channel
        const bool sent = co_await writer.Send(visited);

        if (!sent) {
          // Channel closed early - stop traversal
          co_return VisitResult::kStop;
        }

        // Yield to allow operations to process this node
        co_await co::Yield {};

        // Check if all operations are complete, and if so early terminate the
        // traversal.
        if (pending_operations_.load(std::memory_order_relaxed) == 0) {
          LOG_F(2, "all operations complete, stopping traversal");
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

    TraversalResult traversal_result;
    try {
      // Choose traversal method based on scope configuration
      if (traversal_scope.empty()) {
        // Use full scene traversal
        traversal_result = co_await async_traversal.TraverseAsync(
          streaming_visitor, TraversalOrder::kPreOrder, accept_all_filter);
      } else {
        // Use scoped traversal
        traversal_result
          = co_await async_traversal.TraverseHierarchiesAsync(traversal_scope,
            streaming_visitor, TraversalOrder::kPreOrder, accept_all_filter);
      }
    } catch (...) {
      // Ensure channel is closed even on exception
      (void)0;
    }

    // Close the channel to signal completion
    writer.Close();

    if (!traversal_result.completed) {
      LOG_F(ERROR, "Scene traversal did not complete: filtered={}, visited={}",
        traversal_result.nodes_filtered, traversal_result.nodes_visited);
    }

    co_return;
  }

  static auto HandleOperationException(BatchOperation& op,
    const std::size_t operation_index, const char* operation_name) -> void
  {
    using namespace std::string_literals;

    op.status = BatchOperation::Status::kFailed;
    try {
      throw; // Rethrow the current exception
    } catch (const std::exception& e) {
      LOG_F(ERROR, "{} operation failed during traversal (op_index={}): {}",
        operation_name, operation_index, e.what());
      op.result.error_message = e.what();
    } catch (...) {
      LOG_F(ERROR,
        "{} operation failed with unknown exception during "
        "traversal (op_index={})",
        operation_name, operation_index);
      op.result.error_message
        = "Unknown exception in "s + operation_name + " operation"s;
    }
  }

  std::weak_ptr<const Scene> scene_weak_;
  std::vector<SceneNode> traversal_scope_;
  std::vector<BatchOperation> operations_;
  std::atomic<std::size_t> pending_operations_ { 0 };
};

} // namespace oxygen::scene::detail

//=== Batch Implementation Methods ===----------------------------------------//

using oxygen::scene::detail::BatchQueryExecutor;

/*!
 Executes multiple query operations in a single scene traversal using
 BroadcastChannel-based coordination for optimal performance.

 @param batch_func Lambda function containing batch operation registrations
 @return BatchResult with aggregated metrics from all operations

 ### Implementation Strategy

 1. **Coordinator Creation**: Sets up BatchQueryExecutor with current scope
 2. **State Management**: Activates batch mode and stores coordinator reference
 3. **Operation Registration**: Executes user lambda to register operations
 4. **Batch Execution**: Delegates to coordinator for concurrent execution
 5. **State Cleanup**: Resets batch state and returns aggregated results

 ### Error Handling

 - **Exception Safety**: Guarantees batch state cleanup on any exception
 - **Graceful Degradation**: Returns failed BatchResult on execution errors
 - **State Consistency**: Ensures the coordinator is reset

 ### Performance Benefits

 - **Single Traversal**: All operations share one scene traversal pass
 - **Concurrent Processing**: Operations execute concurrently via coroutines
 - **Memory Efficiency**: User controls all container allocation
 - **Early Termination**: Stops when FindFirst/Any operations complete

 @note This is the core implementation behind ExecuteBatch template methods
 @see BatchQueryExecutor for the coordination implementation
*/
auto SceneQuery::ExecuteBatchImpl(
  std::function<void(const SceneQuery&)> batch_func) const noexcept
  -> BatchResult
{
  // Initialize batch state
  try {
    // Create BroadcastChannel coordinator with current traversal scope
    BatchQueryExecutor coordinator(scene_weak_, traversal_scope_);

    // Store coordinator reference for batch operations
    batch_coordinator_ = &coordinator;

    // Execute coordinated batch traversal with registered operations
    auto batch_result = coordinator.ExecuteBatch(async_traversal_,
      [&batch_func, this](BatchQueryExecutor& /*coordinator*/) {
        // Call the user's batch function - they will use the new
        // reference-based methods
        batch_func(*this);
      });

    // Clean up batch state
    batch_coordinator_ = nullptr;

    // Return batch result with proper aggregated metrics
    return batch_result;
  } catch (...) {
    // Ensure clean state on exception
    batch_coordinator_ = nullptr;

    // Return failed batch result
    return BatchResult {
      .nodes_examined = 0,
      .total_matches = 0,
      .success = false,
    };
  }
}

/*!
 Forwards FindFirst operation registration to the active batch coordinator.

 @param result Reference to receive the first matching node
 @param predicate Function to test each node during traversal
 @note Only valid during ExecuteBatch execution
 @see BatchQueryExecutor::FindFirst for implementation details
*/
auto SceneQuery::BatchFindFirstImpl(std::optional<SceneNode>& result,
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> void
{
  // Register with batch coordinator using reference output
  batch_coordinator_->FindFirst(result, predicate);
}

/*!
 Forwards Collect operation registration to the active batch coordinator.

 @param inserter Function to add matching nodes to user's container
 @param predicate Function to test each node during traversal
 @note Only valid during ExecuteBatch execution
 @see BatchQueryExecutor::Collect for implementation details
*/
auto SceneQuery::BatchCollectImpl(
  std::function<void(const SceneNode&)> inserter,
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> void
{
  // Register with batch coordinator using inserter function
  batch_coordinator_->Collect(std::move(inserter), predicate);
}

/*!
 Forwards Count operation registration to the active batch coordinator.

 @param result Reference to receive the final count
 @param predicate Function to test each node during traversal
 @note Only valid during ExecuteBatch execution
 @see BatchQueryExecutor::Count for implementation details
*/
auto SceneQuery::BatchCountImpl(std::optional<size_t>& result,
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> void
{
  // Register with batch coordinator - results go into BatchResult
  batch_coordinator_->Count(result, predicate);
}

/*!
 Forwards Any operation registration to the active batch coordinator.

 @param result Reference to receive the existence result
 @param predicate Function to test each node during traversal
 @note Only valid during ExecuteBatch execution
 @see BatchQueryExecutor::Any for implementation details
*/
auto SceneQuery::BatchAnyImpl(std::optional<bool>& result,
  const std::function<bool(const ConstVisitedNode&)>& predicate) const noexcept
  -> void
{
  // Register with batch coordinator using reference output
  batch_coordinator_->Any(result, predicate);
}
