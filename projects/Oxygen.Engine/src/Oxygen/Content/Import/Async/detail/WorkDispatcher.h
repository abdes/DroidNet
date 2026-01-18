//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/Async/AsyncImportService.h>
#include <Oxygen/Content/Import/Async/Detail/WorkPayloadStore.h>
#include <Oxygen/Content/Import/Async/ImportPlanner.h>
#include <Oxygen/Content/Import/Async/ImportSession.h>
#include <Oxygen/Content/Import/Async/Pipelines/BufferPipeline.h>
#include <Oxygen/Content/Import/Async/Pipelines/GeometryPipeline.h>
#include <Oxygen/Content/Import/Async/Pipelines/MaterialPipeline.h>
#include <Oxygen/Content/Import/Async/Pipelines/ScenePipeline.h>
#include <Oxygen/Content/Import/Async/Pipelines/TexturePipeline.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

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
    oxygen::observer_ptr<co::ThreadPool> thread_pool,
    const ImportConcurrency& concurrency, std::stop_token stop_token);

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
    GeometryPipeline::WorkResult result) -> co::Co<bool>;

  [[nodiscard]] auto EmitTexturePayload(TexturePipeline::WorkResult& result)
    -> std::optional<uint32_t>;

  [[nodiscard]] auto EmitBufferPayload(BufferPipeline::WorkResult result)
    -> bool;

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
  auto EnsureGeometryPipeline(co::Nursery& nursery) -> GeometryPipeline&;
  auto EnsureScenePipeline(co::Nursery& nursery) -> ScenePipeline&;

  auto ClosePipelines() noexcept -> void;

  ImportSession& session_;
  oxygen::observer_ptr<co::ThreadPool> thread_pool_ {};
  const ImportConcurrency& concurrency_;
  std::stop_token stop_token_;

  std::unique_ptr<TexturePipeline> texture_pipeline_;
  std::unique_ptr<BufferPipeline> buffer_pipeline_;
  std::unique_ptr<MaterialPipeline> material_pipeline_;
  std::unique_ptr<GeometryPipeline> geometry_pipeline_;
  std::unique_ptr<ScenePipeline> scene_pipeline_;
};

} // namespace oxygen::content::import::detail
