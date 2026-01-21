//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/ImportConcurrency.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportProgress.h>
#include <Oxygen/Content/Import/Internal/ImportPlanner.h>
#include <Oxygen/Content/Import/Internal/ImportSession.h>
#include <Oxygen/Content/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Content/Import/Internal/WorkPayloadStore.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::co {
class ThreadPool;
} // namespace oxygen::co

namespace oxygen::content::import::detail {

//! Generic scheduler for import plan execution.
/*!
 Executes a planner-driven import plan using pipeline backpressure and
 readiness tracking. The dispatcher owns pipeline instances for the duration
 of the run and emits cooked results through the supplied import session.
*/
class WorkDispatcher final {
public:
  //! Progress reporter used to emit granular updates.
  struct ProgressReporter final {
    ImportJobId job_id = kInvalidJobId;
    ImportProgressCallback on_progress;
    float overall_start = 0.0f;
    float overall_end = 1.0f;

    auto Report(ImportProgressEvent event, ImportPhase phase,
      float phase_progress, uint32_t items_completed, uint32_t items_total,
      float overall_progress, std::string message, std::string item_kind = {},
      std::string item_name = {}) const -> void;
  };

  //! Context required to execute a plan.
  struct PlanContext final {
    ImportPlanner& planner;
    WorkPayloadStore& payloads;
    std::vector<PlanStep>& steps;
    std::span<const PlanItemId> material_slots;
    std::span<const PlanItemId> geometry_items;
  };

  //! Create a dispatcher bound to a single import session.
  WorkDispatcher(ImportSession& session,
    observer_ptr<co::ThreadPool> thread_pool,
    const ImportConcurrency& concurrency, std::stop_token stop_token,
    std::optional<ProgressReporter> progress = std::nullopt);

  OXYGEN_MAKE_NON_COPYABLE(WorkDispatcher)
  OXYGEN_MAKE_NON_MOVABLE(WorkDispatcher)

  //! Run the dispatcher inside the job nursery.
  [[nodiscard]] auto Run(PlanContext context, co::Nursery& nursery)
    -> co::Co<bool>;

private:
  [[nodiscard]] static auto DefaultMaterialKey() -> data::AssetKey;

  [[nodiscard]] static auto MakeErrorDiagnostic(std::string code,
    std::string message, std::string_view source_id,
    std::string_view object_path) -> ImportDiagnostic;

  [[nodiscard]] static auto MakeWarningDiagnostic(std::string code,
    std::string message, std::string_view source_id,
    std::string_view object_path) -> ImportDiagnostic;

  static auto AddDiagnostics(
    ImportSession& session, std::vector<ImportDiagnostic> diagnostics) -> void;

  [[nodiscard]] auto EmitGeometryPayload(GeometryPipeline& pipeline,
    const MeshBuildPipeline::CookedGeometryPayload& cooked,
    const std::vector<MeshBufferBindings>& bindings, std::string_view source_id)
    -> co::Co<bool>;

  [[nodiscard]] auto EmitTexturePayload(TexturePipeline::WorkResult& result)
    -> std::optional<uint32_t>;

  [[nodiscard]] auto EmitBufferPayload(BufferPipeline::WorkResult result)
    -> std::optional<uint32_t>;

  [[nodiscard]] auto EmitMaterialPayload(MaterialPipeline::WorkResult result)
    -> bool;

  [[nodiscard]] auto EmitScenePayload(ScenePipeline::WorkResult result) -> bool;

  static auto UpdateMaterialBindings(
    const std::unordered_map<std::string, uint32_t>& texture_indices,
    MaterialPipeline::WorkItem& item,
    std::vector<ImportDiagnostic>& diagnostics) -> void;

  auto EnsureTexturePipeline(co::Nursery& nursery) -> TexturePipeline&;
  auto EnsureBufferPipeline(co::Nursery& nursery) -> BufferPipeline&;
  auto EnsureMaterialPipeline(co::Nursery& nursery) -> MaterialPipeline&;
  auto EnsureMeshBuildPipeline(co::Nursery& nursery) -> MeshBuildPipeline&;
  auto EnsureGeometryPipeline() -> GeometryPipeline&;
  auto EnsureScenePipeline(co::Nursery& nursery) -> ScenePipeline&;

  auto ClosePipelines() noexcept -> void;

  ImportSession& session_;
  observer_ptr<co::ThreadPool> thread_pool_ {};
  const ImportConcurrency& concurrency_;
  std::stop_token stop_token_;
  std::optional<ProgressReporter> progress_ {};

  std::unique_ptr<TexturePipeline> texture_pipeline_;
  std::unique_ptr<BufferPipeline> buffer_pipeline_;
  std::unique_ptr<MaterialPipeline> material_pipeline_;
  std::unique_ptr<MeshBuildPipeline> mesh_build_pipeline_;
  std::unique_ptr<GeometryPipeline> geometry_pipeline_;
  std::unique_ptr<ScenePipeline> scene_pipeline_;
};

} // namespace oxygen::content::import::detail
