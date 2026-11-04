//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/OxCo/Channel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import {

//! Pipeline for CPU-bound buffer post-processing.
/*!
 BufferPipeline is a small compute-only helper intended for async imports.
 It offloads expensive CPU work (currently optional SHA-256 based content
 hashing) to a shared `co::ThreadPool`.

 The pipeline does not perform any I/O and does not assign resource indices.
 Use `BufferEmitter` to perform deduplication and to write `buffers.data` and
 `buffers.table`.

 ### Work Model

 - Producers submit `WorkItem` objects.
 - Worker coroutines receive work on the import thread, then offload CPU-bound
   tasks to the ThreadPool.
 - Completed `WorkResult` objects are collected on the import thread.

 ### Cancellation Semantics

 - Pipelines do not provide a direct cancel API.
 - Cancellation is expressed by cancelling the job nursery and by checking the
   `WorkItem` stop tokens during processing.

 @see CookedBufferPayload, BufferEmitter
*/
class BufferPipeline final : public Object {
  OXYGEN_TYPED(BufferPipeline)
public:
  static constexpr PlanItemKind kItemKind = PlanItemKind::kBufferResource;
  //! Configuration for the pipeline.
  struct Config {
    //! Bounded capacity of the input and output queues.
    size_t queue_capacity = 64;

    //! Number of worker coroutines to start.
    uint32_t worker_count = 2;
  };

  //! Work submission item.
  struct WorkItem {
    //! Correlation ID for diagnostics and lookup (e.g., mesh/buffer name).
    std::string source_id;

    //! The buffer bytes; the pipeline hashes them into `cooked.data`.
    std::vector<std::byte> bytes;

    //! Cooked buffer metadata; its `data` is set by the pipeline.
    CookedBufferPayload cooked;

    //! Callback fired when a worker starts processing this item.
    std::function<void()> on_started;

    //! Callback fired when a worker finishes processing this item.
    std::function<void()> on_finished;

    //! Cancellation token.
    std::stop_token stop_token;
  };

  //! Work completion result.
  struct WorkResult {
    //! Echoed from WorkItem for correlation.
    std::string source_id;

    //! Cooked payload, with its bytes hashed; empty when cancelled.
    CookedBufferPayload cooked;

    //! Any diagnostics produced during processing.
    std::vector<ImportDiagnostic> diagnostics;

    //! Per-item telemetry captured during pipeline execution.
    ImportWorkItemTelemetry telemetry;

    //! True if successful; false if canceled or failed.
    bool success = false;
  };

  //! Create a buffer pipeline using the default configuration.
  OXGN_COOK_API explicit BufferPipeline(co::ThreadPool& thread_pool);

  //! Create a buffer pipeline using the given ThreadPool and configuration.
  OXGN_COOK_API explicit BufferPipeline(
    co::ThreadPool& thread_pool, Config config);

  OXGN_COOK_API ~BufferPipeline() override;

  OXYGEN_MAKE_NON_COPYABLE(BufferPipeline)
  OXYGEN_MAKE_NON_MOVABLE(BufferPipeline)

  //! Start worker coroutines in the given nursery.
  /*!
   @param nursery Nursery that will own the workers.

   @note Must be called on the import thread.
  */
  OXGN_COOK_API auto Start(co::Nursery& nursery) -> void;

  //! Submit work (may suspend if the queue is full).
  OXGN_COOK_NDAPI auto Submit(WorkItem item) -> co::Co<>;

  //! Try to submit work without blocking.
  OXGN_COOK_NDAPI auto TrySubmit(WorkItem item) -> bool;

  //! Collect one completed result (suspends until ready or closed).
  OXGN_COOK_NDAPI auto Collect() -> co::Co<WorkResult>;

  //! The next completed result, or nothing once the pipeline is closed, its
  //! workers have finished and every result was collected.
  /*!
   A collector that runs while work is submitted loops on this, with no count
   of what will come, and keeps the bounded queues moving: submitting more than
   the queues hold before collecting never ends.
  */
  OXGN_COOK_NDAPI auto CollectNext() -> co::Co<std::optional<WorkResult>>;

  //! Close the input queue.
  /*!
   Causes workers to eventually exit after draining queued work; the last one
   to exit closes the output queue, whose results stay collectable.

   @note Does not cancel ThreadPool tasks already running.
  */
  OXGN_COOK_API auto Close() -> void;

  //! Whether any submitted work is still pending completion.
  OXGN_COOK_NDAPI auto HasPending() const noexcept -> bool;

  //! Number of submitted work items not yet collected.
  OXGN_COOK_NDAPI auto PendingCount() const noexcept -> size_t;

  //! Get pipeline progress counters.
  OXGN_COOK_NDAPI auto GetProgress() const noexcept -> PipelineProgress;

  //! Number of queued items waiting in the input queue.
  OXGN_COOK_NDAPI auto InputQueueSize() const noexcept -> size_t
  {
    return input_channel_.Size();
  }

  //! Capacity of the input queue.
  OXGN_COOK_NDAPI auto InputQueueCapacity() const noexcept -> size_t
  {
    return config_.queue_capacity;
  }

  //! Number of completed results waiting in the output queue.
  OXGN_COOK_NDAPI auto OutputQueueSize() const noexcept -> size_t
  {
    return output_channel_.Size();
  }

  //! Capacity of the output queue.
  OXGN_COOK_NDAPI auto OutputQueueCapacity() const noexcept -> size_t
  {
    return config_.queue_capacity;
  }

private:
  [[nodiscard]] auto Worker() -> co::Co<>;
  //! Hashes `item.bytes` into `item.cooked.data` on the thread pool.
  //! @return False when the item was cancelled first.
  [[nodiscard]] auto HashBytes(WorkItem& item) -> co::Co<bool>;
  auto ReportCancelled(WorkItem item) -> co::Co<>;

  co::ThreadPool& thread_pool_;
  Config config_;

  co::Channel<WorkItem> input_channel_;
  co::Channel<WorkResult> output_channel_;

  std::atomic<size_t> pending_ { 0 };
  std::atomic<size_t> submitted_ { 0 };
  std::atomic<size_t> completed_ { 0 };
  std::atomic<size_t> failed_ { 0 };
  bool started_ = false;
  uint32_t running_workers_ = 0;
};

static_assert(ImportPipeline<BufferPipeline>);

} // namespace oxygen::content::import
