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
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/Internal/ImportPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/OxCo/Channel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import {

//! Geometry descriptor finalization pipeline.
class GeometryPipeline final : public Object {
  OXYGEN_TYPED(GeometryPipeline)
public:
  static constexpr PlanItemKind kItemKind = PlanItemKind::kGeometryAsset;
  //! Configuration for descriptor finalization.
  struct Config {
    //! Bounded capacity of the input and output queues.
    size_t queue_capacity = 32;

    //! Number of worker coroutines to start.
    uint32_t worker_count = 1;

    bool with_content_hashing = true;
  };

  //! Material patch to apply at a descriptor offset.
  struct MaterialKeyPatch {
    data::pak::DataBlobSizeT material_key_offset = 0;
    data::AssetKey key {};
  };

  //! Work submission item.
  struct WorkItem {
    //! Correlation ID for diagnostics and lookup (e.g., mesh name).
    std::string source_id;

    //! Cooked geometry payload.
    MeshBuildPipeline::CookedGeometryPayload cooked;

    //! Buffer bindings to patch into the descriptor.
    std::vector<MeshBufferBindings> bindings;

    //! Material key patches for submesh slots.
    std::vector<MaterialKeyPatch> material_patches;

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

    //! Cooked geometry payload.
    std::optional<MeshBuildPipeline::CookedGeometryPayload> cooked;

    //! Finalized descriptor bytes (patched + hashed).
    std::vector<std::byte> finalized_descriptor_bytes;

    //! Any diagnostics produced during processing.
    std::vector<ImportDiagnostic> diagnostics;

    //! Per-item telemetry captured during pipeline execution.
    ImportWorkItemTelemetry telemetry;

    //! True if successful; false if canceled or failed.
    bool success = false;
  };

  OXGN_CNTT_API explicit GeometryPipeline(
    co::ThreadPool& thread_pool, std::optional<Config> config = {});

  OXGN_CNTT_API ~GeometryPipeline();

  OXYGEN_MAKE_NON_COPYABLE(GeometryPipeline)
  OXYGEN_MAKE_NON_MOVABLE(GeometryPipeline)

  //! Start worker coroutines in the given nursery.
  OXGN_CNTT_API auto Start(co::Nursery& nursery) -> void;

  //! Submit work (may suspend if the queue is full).
  OXGN_CNTT_NDAPI auto Submit(WorkItem item) -> co::Co<>;

  //! Try to submit work without blocking.
  OXGN_CNTT_NDAPI auto TrySubmit(WorkItem item) -> bool;

  //! Collect one completed result (suspends until ready or closed).
  OXGN_CNTT_NDAPI auto Collect() -> co::Co<WorkResult>;

  //! Close the input queue.
  OXGN_CNTT_API auto Close() -> void;

  //! Whether any submitted work is still pending completion.
  OXGN_CNTT_NDAPI auto HasPending() const noexcept -> bool;

  //! Number of submitted work items not yet collected.
  OXGN_CNTT_NDAPI auto PendingCount() const noexcept -> size_t;

  //! Get pipeline progress counters.
  OXGN_CNTT_NDAPI auto GetProgress() const noexcept -> PipelineProgress;

  //! Number of queued items waiting in the input queue.
  OXGN_CNTT_NDAPI auto InputQueueSize() const noexcept -> size_t
  {
    return input_channel_.Size();
  }

  //! Capacity of the input queue.
  OXGN_CNTT_NDAPI auto InputQueueCapacity() const noexcept -> size_t
  {
    return config_.queue_capacity;
  }

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

  //! Patch buffer indices and compute descriptor content hash.
  OXGN_CNTT_NDAPI auto FinalizeDescriptorBytes(
    std::span<const MeshBufferBindings> bindings,
    std::span<const std::byte> descriptor_bytes,
    std::span<const MaterialKeyPatch> material_patches,
    std::vector<ImportDiagnostic>& diagnostics)
    -> co::Co<std::optional<std::vector<std::byte>>>;

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

static_assert(ImportPipeline<GeometryPipeline>);

} // namespace oxygen::content::import
