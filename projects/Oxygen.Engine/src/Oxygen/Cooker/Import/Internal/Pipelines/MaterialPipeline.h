//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/OxCo/Channel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import {

//! Pipeline for CPU-bound material cooking.
/*!
 MaterialPipeline is a compute-only pipeline used by async imports. It
 assembles `MaterialAssetDesc` payloads and optional shader references, then
 computes content hashes using the provided `co::ThreadPool` when enabled.

 The pipeline does not perform I/O and does not assign resource indices.
 Use `AssetEmitter` to emit cooked payloads.

 ### Work Model

 - Producers submit `WorkItem` objects.
 - Worker coroutines run on the import thread and offload hashing (and optional
   build work) to the ThreadPool.
 - Completed `WorkResult` objects are collected on the import thread.

 ### Cancellation Semantics

 - Pipelines do not provide a direct cancel API.
 - Cancellation is expressed by cancelling the job nursery and by checking the
   `WorkItem` stop tokens during processing.

 ### TODO

 - TODO: Wire `header.streaming_priority` from import configuration.
 - TODO: Wire `header.variant_flags` from import configuration.
*/
class MaterialPipeline final : public Object {
  OXYGEN_TYPED(MaterialPipeline)
public:
  static constexpr PlanItemKind kItemKind = PlanItemKind::kMaterialAsset;
  //! Configuration for the pipeline.
  struct Config {
    //! Bounded capacity of the input and output queues.
    size_t queue_capacity = 64;

    //! Number of worker coroutines to start.
    uint32_t worker_count = 2;

    //! Enable ThreadPool offload for descriptor assembly.
    bool use_thread_pool = true;

    //! Enable or disable material content hashing.
    /*!
     When false, the pipeline MUST NOT compute `content_hash`.
    */
    bool with_content_hashing = true;
  };

  //! Cooked material payload returned by the pipeline.
  struct CookedMaterialPayload {
    data::AssetKey material_key;
    std::string virtual_path;
    std::string descriptor_relpath;
    std::vector<std::byte> descriptor_bytes;
    data::AssetReferences references;
  };

  //! Work submission item.
  struct WorkItem {
    std::string source_id;
    const void* source_key = nullptr;
    MaterialSource material;

    //! Callback fired when a worker starts processing this item.
    std::function<void()> on_started {};

    //! Callback fired when a worker finishes processing this item.
    std::function<void()> on_finished {};

    ImportRequest request;
    observer_ptr<NamingService> naming_service;
    std::stop_token stop_token;
  };

  //! Work completion result.
  struct WorkResult {
    std::string source_id;
    std::optional<CookedMaterialPayload> cooked;
    std::vector<ImportDiagnostic> diagnostics;
    ImportWorkItemTelemetry telemetry {};
    bool success = false;
  };

  //! Create a material pipeline using the given ThreadPool.
  OXGN_COOK_API explicit MaterialPipeline(
    co::ThreadPool& thread_pool, std::optional<Config> config = {});

  OXGN_COOK_API ~MaterialPipeline();

  OXYGEN_MAKE_NON_COPYABLE(MaterialPipeline)
  OXYGEN_MAKE_NON_MOVABLE(MaterialPipeline)

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

  //! Close the input queue.
  /*!
   Causes workers to eventually exit after draining queued work.

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

static_assert(ImportPipeline<MaterialPipeline>);

} // namespace oxygen::content::import
