//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
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
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Channel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import {

//! One environment system record for the trailing scene block.
struct SceneEnvironmentSystem {
  uint32_t system_type = 0;
  std::vector<std::byte> record_bytes;
};

//! Intermediate scene build data produced by adapters.
struct SceneBuild final {
  std::vector<data::pak::NodeRecord> nodes;
  std::vector<std::byte> strings;

  std::vector<data::pak::RenderableRecord> renderables;
  std::vector<data::pak::PerspectiveCameraRecord> perspective_cameras;
  std::vector<data::pak::OrthographicCameraRecord> orthographic_cameras;
  std::vector<data::pak::DirectionalLightRecord> directional_lights;
  std::vector<data::pak::PointLightRecord> point_lights;
  std::vector<data::pak::SpotLightRecord> spot_lights;
};

//! Input provided to adapter scene stage processing.
struct SceneStageInput final {
  std::string_view source_id;
  std::span<const data::AssetKey> geometry_keys;
  const ImportRequest* request = nullptr;
  observer_ptr<NamingService> naming_service;
  std::stop_token stop_token;
};

//! Result of adapter scene stage processing.
struct SceneStageResult final {
  SceneBuild build;
  bool success = false;
};

//! Concept for adapters supporting the scene stage pipeline.
template <typename Adapter>
concept SceneStageAdapter = requires(const Adapter& adapter,
  const SceneStageInput& input, std::vector<ImportDiagnostic>& diagnostics) {
  {
    adapter.BuildSceneStage(input, diagnostics)
  } -> std::same_as<SceneStageResult>;
};

//! Pipeline for CPU-bound scene cooking.
class ScenePipeline final : public Object {
  OXYGEN_TYPED(ScenePipeline)
public:
  static constexpr PlanItemKind kItemKind = PlanItemKind::kSceneAsset;
  //! Configuration for the pipeline.
  struct Config {
    size_t queue_capacity = 8;
    uint32_t worker_count = 1;

    //! Enable or disable scene content hashing.
    /*!
     When false, the pipeline MUST NOT compute `content_hash`.
    */
    bool with_content_hashing = true;
  };

  //! Cooked scene payload returned by the pipeline.
  struct CookedScenePayload {
    data::AssetKey scene_key;
    std::string virtual_path;
    std::string descriptor_relpath;
    std::vector<std::byte> descriptor_bytes;
  };

  //! Work submission item.
  struct WorkItem {
    std::string source_id;
    std::shared_ptr<const void> adapter_owner;
    using BuildStageFn = SceneStageResult (*)(const void* adapter,
      const SceneStageInput& input, std::vector<ImportDiagnostic>& diagnostics);
    BuildStageFn build_stage = nullptr;
    std::vector<data::AssetKey> geometry_keys;
    std::vector<SceneEnvironmentSystem> environment_systems;

    //! Callback fired when a worker starts processing this item.
    std::function<void()> on_started;

    //! Callback fired when a worker finishes processing this item.
    std::function<void()> on_finished;

    ImportRequest request;
    observer_ptr<NamingService> naming_service;
    std::stop_token stop_token;

    template <SceneStageAdapter Adapter>
    [[nodiscard]] static auto MakeWorkItem(std::shared_ptr<Adapter> adapter,
      std::string source_id, std::vector<data::AssetKey> geometry_keys,
      std::vector<SceneEnvironmentSystem> environment_systems,
      ImportRequest request, observer_ptr<NamingService> naming_service,
      std::stop_token stop_token) -> WorkItem
    {
      WorkItem item;
      item.source_id = std::move(source_id);
      item.adapter_owner = std::move(adapter);
      item.build_stage
        = [](const void* adapter_ptr, const SceneStageInput& input,
            std::vector<ImportDiagnostic>& diagnostics) -> SceneStageResult {
        const auto* typed = static_cast<const Adapter*>(adapter_ptr);
        if (typed == nullptr) {
          return SceneStageResult {};
        }
        return typed->BuildSceneStage(input, diagnostics);
      };
      item.geometry_keys = std::move(geometry_keys);
      item.environment_systems = std::move(environment_systems);
      item.request = std::move(request);
      item.naming_service = naming_service;
      item.stop_token = stop_token;
      return item;
    }
  };

  //! Work completion result.
  struct WorkResult {
    std::string source_id;
    std::optional<CookedScenePayload> cooked;
    std::vector<ImportDiagnostic> diagnostics;
    ImportWorkItemTelemetry telemetry;
    bool success = false;
  };

  //! Create a scene pipeline using the given ThreadPool.
  OXGN_CNTT_API explicit ScenePipeline(
    co::ThreadPool& thread_pool, std::optional<Config> config = {});

  OXGN_CNTT_API ~ScenePipeline() override;

  OXYGEN_MAKE_NON_COPYABLE(ScenePipeline)
  OXYGEN_MAKE_NON_MOVABLE(ScenePipeline)

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

static_assert(ImportPipeline<ScenePipeline>);

} // namespace oxygen::content::import
