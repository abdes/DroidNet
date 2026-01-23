//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Import/BufferImportTypes.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/Internal/ImportPipeline.h>
#include <Oxygen/Content/api_export.h>
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

    //! Whether to compute the SHA-256 based content hash.
    /*!
     When enabled, the pipeline computes the SHA-256 digest of the buffer bytes
     and stores the first 8 bytes in `CookedBufferPayload::content_hash`.

     When disabled, the pipeline does not touch `content_hash`.
    */
    bool with_content_hashing = true;
  };

  //! Work submission item.
  struct WorkItem {
    //! Correlation ID for diagnostics and lookup (e.g., mesh/buffer name).
    std::string source_id;

    //! Cooked buffer payload.
    /*!
     When `Config::with_content_hashing` is enabled and `content_hash` is zero,
     the pipeline computes and populates it.
    */
    CookedBufferPayload cooked;

    //! Callback fired when a worker starts processing this item.
    std::function<void()> on_started;

    //! Cancellation token.
    std::stop_token stop_token;
  };

  //! Work completion result.
  struct WorkResult {
    //! Echoed from WorkItem for correlation.
    std::string source_id;

    //! Cooked payload.
    /*!
     If hashing is enabled, `content_hash` may be computed and filled.
    */
    CookedBufferPayload cooked;

    //! Any diagnostics produced during processing.
    std::vector<ImportDiagnostic> diagnostics;

    //! True if successful; false if canceled or failed.
    bool success = false;
  };

  //! Create a buffer pipeline using the given ThreadPool.
  OXGN_CNTT_API explicit BufferPipeline(
    co::ThreadPool& thread_pool, Config config = {});

  OXGN_CNTT_API ~BufferPipeline();

  OXYGEN_MAKE_NON_COPYABLE(BufferPipeline)
  OXYGEN_MAKE_NON_MOVABLE(BufferPipeline)

  //! Start worker coroutines in the given nursery.
  /*!
   @param nursery Nursery that will own the workers.

   @note Must be called on the import thread.
  */
  OXGN_CNTT_API auto Start(co::Nursery& nursery) -> void;

  //! Submit work (may suspend if the queue is full).
  OXGN_CNTT_NDAPI auto Submit(WorkItem item) -> co::Co<>;

  //! Try to submit work without blocking.
  OXGN_CNTT_NDAPI auto TrySubmit(WorkItem item) -> bool;

  //! Collect one completed result (suspends until ready or closed).
  OXGN_CNTT_NDAPI auto Collect() -> co::Co<WorkResult>;

  //! Close the input queue.
  /*!
   Causes workers to eventually exit after draining queued work.

   @note Does not cancel ThreadPool tasks already running.
  */
  OXGN_CNTT_API auto Close() -> void;

  //! Whether any submitted work is still pending completion.
  OXGN_CNTT_NDAPI auto HasPending() const noexcept -> bool;

  //! Number of submitted work items not yet collected.
  OXGN_CNTT_NDAPI auto PendingCount() const noexcept -> size_t;

  //! Get pipeline progress counters.
  OXGN_CNTT_NDAPI auto GetProgress() const noexcept -> PipelineProgress;

  //! Number of completed results waiting in the output queue.
  OXGN_CNTT_NDAPI auto OutputQueueSize() const noexcept -> size_t
  {
    return output_channel_.Size();
  }

  //! Capacity of the output queue.
  OXGN_CNTT_NDAPI auto OutputQueueCapacity() const noexcept -> size_t
  {
    return config_.queue_capacity;
  }

private:
  [[nodiscard]] auto Worker() -> co::Co<>;
  [[nodiscard]] auto ComputeContentHash(WorkItem& item)
    -> co::Co<std::optional<ImportDiagnostic>>;
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
};

static_assert(ImportPipeline<BufferPipeline>);

} // namespace oxygen::content::import
