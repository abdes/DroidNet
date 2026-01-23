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
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <variant>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/Internal/ImportPipeline.h>
#include <Oxygen/Content/Import/ScratchImage.h>
#include <Oxygen/Content/Import/TextureImportDesc.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/OxCo/Channel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import {

//! Pipeline for CPU-bound texture cooking.
/*!
 TexturePipeline is a compute-only pipeline used by async imports. It accepts
 pre-acquired source bytes (or pre-decoded images), cooks them on a
 `co::ThreadPool`, and returns `CookedTexturePayload` results.

 The pipeline does not perform I/O and does not assign resource indices.
 Use `TextureEmitter` to emit cooked payloads.

 ### Work Model

 - Producers submit `WorkItem` objects.
 - Worker coroutines run on the import thread and offload CPU work to the
   ThreadPool.
 - Completed `WorkResult` objects are collected on the import thread.

 ### Cancellation Semantics

 - Pipelines do not provide a direct cancel API.
 - Cancellation is expressed by cancelling the job nursery and by checking the
   `WorkItem` stop tokens during processing.

 @see CookedTexturePayload, TextureEmitter
*/
class TexturePipeline final : public Object {
  OXYGEN_TYPED(TexturePipeline)
public:
  static constexpr PlanItemKind kItemKind = PlanItemKind::kTextureResource;
  //! Configuration for the pipeline.
  struct Config {
    //! Bounded capacity of the input and output queues.
    size_t queue_capacity = 64;

    //! Number of worker coroutines to start.
    uint32_t worker_count = 4;

    //! Enable or disable payload content hashing.
    /*!
     When false, the pipeline MUST NOT compute `content_hash` for textures.
    */
    bool with_content_hashing = true;
  };

  //! Policy for handling failures while cooking.
  enum class FailurePolicy : uint8_t {
    kStrict,
    kPlaceholder,
  };

  //! Source bytes for a single texture payload.
  struct SourceBytes {
    std::span<const std::byte> bytes;
    std::shared_ptr<const void> owner;
  };

  //! Source content variants supported by the pipeline.
  using SourceContent
    = std::variant<SourceBytes, TextureSourceSet, ScratchImage>;

  //! Work submission item.
  struct WorkItem {
    //! Diagnostic ID and decode extension hint.
    std::string source_id;

    //! External source path used to load bytes on the import thread.
    //! Leave empty when `source` already contains content.
    std::filesystem::path source_path;

    //! Canonical dedupe key (normalized path or embedded hash).
    std::string texture_id;

    //! Opaque correlation key (e.g., `ufbx_texture*`).
    const void* source_key = nullptr;

    //! Import descriptor to use for cooking.
    TextureImportDesc desc;

    //! Packing policy identifier (e.g., "d3d12", "tight").
    std::string packing_policy_id;

    //! True when output format is explicitly overridden.
    bool output_format_is_override = false;

    //! Failure policy for this work item.
    FailurePolicy failure_policy = FailurePolicy::kPlaceholder;

    //! Convert equirectangular input to cubemap inside the pipeline.
    bool equirect_to_cubemap = false;

    //! Cubemap face size for equirect conversion (required when enabled).
    uint32_t cubemap_face_size = 0;

    //! Cubemap layout hint for layout extraction (kUnknown disables).
    CubeMapImageLayout cubemap_layout = CubeMapImageLayout::kUnknown;

    //! Source content (bytes, multi-source set, or decoded image).
    SourceContent source;

    //! Callback fired when a worker starts processing this item.
    std::function<void()> on_started;

    //! Cancellation token.
    std::stop_token stop_token;
  };

  //! Work completion result.
  struct WorkResult {
    //! Echoed from WorkItem for correlation.
    std::string source_id;

    //! Echoed from WorkItem for dedupe mapping.
    std::string texture_id;

    //! Echoed from WorkItem for correlation.
    const void* source_key = nullptr;

    //! Cooked payload, if successful.
    std::optional<CookedTexturePayload> cooked;

    //! True if the caller should use the fallback texture index (0).
    bool used_placeholder = false;

    //! Diagnostics produced while cooking.
    std::vector<ImportDiagnostic> diagnostics;

    //! Time spent decoding source bytes, if applicable.
    std::optional<std::chrono::microseconds> decode_duration;

    //! True if successful; false if canceled or failed.
    bool success = false;
  };

  //! Create a texture pipeline using the given ThreadPool.
  OXGN_CNTT_API explicit TexturePipeline(
    co::ThreadPool& thread_pool, Config config = {});

  OXGN_CNTT_API ~TexturePipeline();

  OXYGEN_MAKE_NON_COPYABLE(TexturePipeline)
  OXYGEN_MAKE_NON_MOVABLE(TexturePipeline)

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

static_assert(ImportPipeline<TexturePipeline>);

} // namespace oxygen::content::import
