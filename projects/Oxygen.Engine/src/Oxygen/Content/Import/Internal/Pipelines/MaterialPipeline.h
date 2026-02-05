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
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Internal/ImportPipeline.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/OxCo/Channel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import {

//! UV transform for a material texture slot.
struct MaterialUvTransform {
  float scale[2] = { 1.0F, 1.0F };
  float offset[2] = { 0.0F, 0.0F };
  float rotation_radians = 0.0F;
};

//! Material alpha mode from authoring.
enum class MaterialAlphaMode : uint8_t {
  kOpaque,
  kMasked,
  kBlended,
};

//! Shader request for material pipelines.
struct ShaderRequest {
  uint8_t shader_type = 0; // ShaderType enum value
  std::string source_path;
  std::string entry_point;
  std::string defines;
  uint64_t shader_hash = 0;
};

//! Texture binding for a single material slot.
struct MaterialTextureBinding {
  uint32_t index = 0;
  bool assigned = false;
  std::string source_id;
  uint8_t uv_set = 0;
  MaterialUvTransform uv_transform;
};

//! Texture bindings for all material slots.
struct MaterialTextureBindings {
  MaterialTextureBinding base_color;
  MaterialTextureBinding normal;
  MaterialTextureBinding metallic;
  MaterialTextureBinding roughness;
  MaterialTextureBinding ambient_occlusion;
  MaterialTextureBinding emissive;
  MaterialTextureBinding specular;
  MaterialTextureBinding sheen_color;
  MaterialTextureBinding clearcoat;
  MaterialTextureBinding clearcoat_normal;
  MaterialTextureBinding transmission;
  MaterialTextureBinding thickness;
};

//! Scalar material inputs.
struct MaterialInputs {
  float base_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
  float normal_scale = 1.0f;
  float metalness = 0.0f;
  float roughness = 1.0f;
  float ambient_occlusion = 1.0f;
  float emissive_factor[3] = { 0.0f, 0.0f, 0.0f };
  float alpha_cutoff = 0.5f;
  float ior = 1.5f;
  float specular_factor = 1.0f;
  float sheen_color_factor[3] = { 0.0f, 0.0f, 0.0f };
  float clearcoat_factor = 0.0f;
  float clearcoat_roughness = 0.0f;
  float transmission_factor = 0.0f;
  float thickness_factor = 0.0f;
  float attenuation_color[3] = { 1.0f, 1.0f, 1.0f };
  float attenuation_distance = 0.0f;
  bool double_sided = false;
  bool unlit = false;
  bool roughness_as_glossiness = false;
};

//! ORM packing policy for metallic/roughness/AO.
enum class OrmPolicy : uint8_t {
  kAuto,
  kForcePacked,
  kForceSeparate,
};

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
  };

  //! Work submission item.
  struct WorkItem {
    std::string source_id;
    std::string material_name;
    std::string storage_material_name;
    const void* source_key = nullptr;

    data::MaterialDomain material_domain = data::MaterialDomain::kOpaque;
    MaterialAlphaMode alpha_mode = MaterialAlphaMode::kOpaque;
    MaterialInputs inputs;
    MaterialTextureBindings textures;
    OrmPolicy orm_policy = OrmPolicy::kAuto;
    std::vector<ShaderRequest> shader_requests;

    //! Callback fired when a worker starts processing this item.
    std::function<void()> on_started;

    //! Callback fired when a worker finishes processing this item.
    std::function<void()> on_finished;

    ImportRequest request;
    observer_ptr<NamingService> naming_service;
    std::stop_token stop_token;
  };

  //! Work completion result.
  struct WorkResult {
    std::string source_id;
    std::optional<CookedMaterialPayload> cooked;
    std::vector<ImportDiagnostic> diagnostics;
    ImportWorkItemTelemetry telemetry;
    bool success = false;
  };

  //! Create a material pipeline using the given ThreadPool.
  OXGN_CNTT_API explicit MaterialPipeline(
    co::ThreadPool& thread_pool, std::optional<Config> config = {});

  OXGN_CNTT_API ~MaterialPipeline();

  OXYGEN_MAKE_NON_COPYABLE(MaterialPipeline)
  OXYGEN_MAKE_NON_MOVABLE(MaterialPipeline)

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
