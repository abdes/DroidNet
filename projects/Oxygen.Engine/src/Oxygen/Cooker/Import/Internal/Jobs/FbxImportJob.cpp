//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/ImportPlanner.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/FbxImportJob.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/ModelSettings.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/Internal/WorkDispatcher.h>
#include <Oxygen/Cooker/Import/Internal/WorkPayloadStore.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::content::import::detail {

struct FbxImportJob::PlannedFbxImport final {
  ImportPlanner planner;
  WorkPayloadStore payloads;
  std::vector<PlanStep> plan;

  std::unordered_map<std::string, PlanItemId> texture_by_source_id;
  std::vector<PlanItemId> texture_items;
  std::vector<PlanItemId> material_items;
  std::vector<WorkDispatcher::PlanSurface> surfaces;
  std::vector<PlanItemId> geometry_items;

  // Textures and materials wait for the material remap, which decides what
  // the plan produces.
  std::vector<TexturePipeline::WorkItem> pending_textures;
  std::vector<MaterialPipeline::WorkItem> pending_materials;
  internal::ModelProductNames product_names;
  std::vector<PlanItemId> scene_items;
};

namespace {

  auto AddDiagnostics(
    ImportSession& session, std::vector<ImportDiagnostic> diagnostics) -> void
  {
    for (auto& diagnostic : diagnostics) {
      session.AddDiagnostic(std::move(diagnostic));
    }
  }

  [[nodiscard]] auto DefaultMaterialKey() -> data::AssetKey
  {
    return data::MaterialAsset::CreateDefault()->GetAssetKey();
  }

  [[nodiscard]] auto MakeErrorDiagnostic(std::string code, std::string message,
    std::string_view source_id, std::string_view object_path)
    -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

  [[nodiscard]] auto MakeWarningDiagnostic(std::string code,
    std::string message, std::string_view source_id,
    std::string_view object_path) -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kWarning,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

  [[nodiscard]] auto MakeDuration(
    const std::chrono::steady_clock::time_point start,
    const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds
  {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  }

} // namespace

/*!
 Execute the FBX import workflow.

 The current implementation wires the job lifecycle and progress reporting.
 Phase 5 will populate the parse/cook/emit stages with real pipeline work.
*/
auto FbxImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  EnsureCookedRoot();
  auto& session = Session();
  co_return co_await ExecuteSessionAsync(session);
}

auto FbxImportJob::ExecuteSessionAsync(ImportSession& session)
  -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting job: job_id={} path={}", JobId(),
    Request().source_path.string());

  const auto job_start = std::chrono::steady_clock::now();
  ImportTelemetry telemetry {
    .io_duration = std::chrono::microseconds { 0 },
    .source_load_duration = std::chrono::microseconds { 0 },
    .decode_duration = std::chrono::microseconds { 0 },
    .load_duration = std::chrono::microseconds { 0 },
    .cook_duration = std::chrono::microseconds { 0 },
    .emit_duration = std::chrono::microseconds { 0 },
    .finalize_duration = std::chrono::microseconds { 0 },
    .total_duration = std::chrono::microseconds { 0 },
  };
  const auto MakeDuration
    = [](const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  };
  const auto FinalizeWithTelemetry
    = [&](ImportSession& session) -> co::Co<ImportReport> {
    const auto finalize_start = std::chrono::steady_clock::now();
    auto report = co_await FinalizeSession(session);
    const auto finalize_end = std::chrono::steady_clock::now();
    telemetry.finalize_duration = MakeDuration(finalize_start, finalize_end);
    telemetry.total_duration = MakeDuration(job_start, finalize_end);
    telemetry.io_duration = session.IoDuration();
    telemetry.source_load_duration = session.SourceLoadDuration();
    telemetry.decode_duration = session.DecodeDuration();
    telemetry.load_duration
      = session.SourceLoadDuration() + session.LoadDuration();
    telemetry.cook_duration = session.CookDuration();
    telemetry.emit_duration = session.EmitDuration();
    report.telemetry = telemetry;
    co_return report;
  };

  if (auto placement = internal::PlaceModel(Request()); !placement) {
    session.AddDiagnostic(std::move(placement.error()));
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Model virtual path refused");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(ImportPhase::kLoading, 0.0F, "Parsing FBX...");
  const auto load_start = std::chrono::steady_clock::now();
  auto scene = co_await ParseScene(session);
  const auto load_end = std::chrono::steady_clock::now();
  session.AddSourceLoadDuration(MakeDuration(load_start, load_end));
  AddDiagnostics(session, std::move(scene.diagnostics));
  if (scene.canceled || !scene.success) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F, "FBX parse failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(ImportPhase::kPlanning, 0.1F, "Building import plan...");
  const auto request_copy = Request();
  const auto remap = internal::ResolveMaterialRemap(session, request_copy);
  const auto stop_token = StopToken();
  auto plan_outcome = co_await ThreadPool()->Run(
    [this, &scene, &request_copy, &remap, stop_token](
      co::ThreadPool::CancelToken canceled) -> PlanBuildOutcome {
      DLOG_F(1, "Build plan task begin");
      if (canceled || stop_token.stop_requested()) {
        PlanBuildOutcome canceled_outcome;
        canceled_outcome.canceled = true;
        return canceled_outcome;
      }
      return BuildPlan(scene, request_copy, remap, stop_token);
    });
  AddDiagnostics(session, std::move(plan_outcome.diagnostics));
  if (plan_outcome.canceled || !plan_outcome.plan) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F, "Plan build failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(ImportPhase::kWorking, 0.2F, "Executing plan...");
  const bool executed = co_await ExecutePlan(*plan_outcome.plan, session);
  if (!executed) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F, "Plan execution failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(ImportPhase::kFinalizing, 0.9F, "Finalizing import...");
  session.MarkSurfaceSourceProcessed();
  auto report = co_await FinalizeWithTelemetry(session);

  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");

  co_return report;
}

//! Parse the FBX source into an intermediate scene representation.
auto FbxImportJob::ParseScene(ImportSession& session) -> co::Co<ParsedFbxScene>
{
  const auto request_copy = Request();
  const auto stop_token = StopToken();
  const auto naming_service = observer_ptr { &GetNamingService() };
  auto reader = FileReader();
  std::shared_ptr<std::vector<std::byte>> source_bytes;

  if (reader != nullptr) {
    const auto read_start = std::chrono::steady_clock::now();
    auto read_result
      = co_await reader.get()->ReadFile(request_copy.source_path);
    const auto read_end = std::chrono::steady_clock::now();
    session.AddIoDuration(MakeDuration(read_start, read_end));
    if (read_result.has_value()) {
      source_bytes = std::make_shared<std::vector<std::byte>>(
        std::move(read_result.value()));
    } else {
      session.AddDiagnostic(MakeErrorDiagnostic("fbx.read_failed",
        "Failed to read FBX source bytes", request_copy.source_path.string(),
        ""));
      ParsedFbxScene failed;
      failed.success = false;
      co_return failed;
    }
  }

  auto parsed = co_await ThreadPool()->Run(
    [request_copy, stop_token, naming_service, source_bytes](
      co::ThreadPool::CancelToken canceled) -> ParsedFbxScene {
      DLOG_F(1, "Parse scene task begin");
      ParsedFbxScene out;
      if (canceled || stop_token.stop_requested()) {
        out.canceled = true;
        return out;
      }

      const auto source_id_prefix = request_copy.source_path.string();
      adapters::AdapterInput input {
        .source_id_prefix = source_id_prefix,
        .object_path_prefix = {},
        .material_keys = {},
        .default_material_key = DefaultMaterialKey(),
        .request = request_copy,
        .naming_service = naming_service,
        .stop_token = stop_token,
        .external_texture_bytes = {},
      };

      auto adapter = std::make_shared<adapters::FbxAdapter>();
      const auto parse_result = source_bytes
        ? adapter->Parse(std::span<const std::byte>(
                           source_bytes->data(), source_bytes->size()),
            input)
        : adapter->Parse(request_copy.source_path, input);
      out.adapter = std::move(adapter);
      out.source_bytes = source_bytes;
      out.diagnostics = parse_result.diagnostics;
      out.success = parse_result.success;
      return out;
    });

  co_return parsed;
}

//! Build the planner-driven execution plan for this import.
auto FbxImportJob::BuildPlan(ParsedFbxScene& scene,
  const ImportRequest& request, const internal::MaterialRemapKeys& remap,
  std::stop_token stop_token) -> PlanBuildOutcome
{
  DLOG_F(1, "Build plan begin");
  PlanBuildOutcome outcome;
  if (!scene.success || scene.adapter == nullptr) {
    return outcome;
  }

  auto plan = std::make_unique<PlannedFbxImport>();
  plan->planner.RegisterPipeline<TexturePipeline>(
    PlanItemKind::kTextureResource);
  plan->planner.RegisterPipeline<BufferPipeline>(PlanItemKind::kBufferResource);
  plan->planner.RegisterPipeline<MaterialPipeline>(
    PlanItemKind::kMaterialAsset);
  plan->planner.RegisterPipeline<MeshBuildPipeline>(PlanItemKind::kMeshBuild);
  plan->planner.RegisterPipeline<GeometryPipeline>(
    PlanItemKind::kGeometryAsset);
  plan->planner.RegisterPipeline<ScenePipeline>(PlanItemKind::kSceneAsset);

  const auto source_id_prefix = request.source_path.string();
  adapters::AdapterInput input {
    .source_id_prefix = source_id_prefix,
    .object_path_prefix = {},
    .material_keys = {},
    .default_material_key = DefaultMaterialKey(),
    .request = request,
    .naming_service = observer_ptr { &GetNamingService() },
    .stop_token = std::move(stop_token),
    .external_texture_bytes = {},
  };

  struct PlannerTextureSink final : adapters::TextureWorkItemSink {
    explicit PlannerTextureSink(PlannedFbxImport& plan)
      : plan_(&plan)
    {
    }

    auto Consume(TexturePipeline::WorkItem item) -> bool override
    {
      plan_->pending_textures.push_back(std::move(item));
      return true;
    }

    observer_ptr<PlannedFbxImport> plan_;
  };

  struct PlannerMaterialSink final : adapters::MaterialWorkItemSink {
    explicit PlannerMaterialSink(PlannedFbxImport& plan)
      : plan_(&plan)
    {
    }

    auto Consume(MaterialPipeline::WorkItem item) -> bool override
    {
      plan_->pending_materials.push_back(std::move(item));
      return true;
    }

    observer_ptr<PlannedFbxImport> plan_;
  };

  const auto add_texture = [&](TexturePipeline::WorkItem item) -> void {
    const auto handle = plan->payloads.Store(std::move(item));
    auto& payload = plan->payloads.Texture(handle);
    const auto id
      = plan->planner.AddTextureResource(payload.item.source_id, handle);
    plan->texture_by_source_id.emplace(payload.item.source_id, id);
    plan->texture_items.push_back(id);
  };

  const auto add_material = [&](MaterialPipeline::WorkItem item) -> PlanItemId {
    const auto handle = plan->payloads.Store(std::move(item));
    auto& payload = plan->payloads.Material(handle);
    const auto id
      = plan->planner.AddMaterialAsset(payload.item.material.name, handle);
    plan->material_items.push_back(id);

    auto add_dep = [&](const MaterialTextureBinding& binding) -> void {
      if (!binding.assigned || binding.source_id.empty()) {
        return;
      }

      const auto it = plan->texture_by_source_id.find(binding.source_id);
      if (it == plan->texture_by_source_id.end()) {
        outcome.diagnostics.push_back(
          MakeWarningDiagnostic("material.texture_missing",
            "Material texture is missing; it binds the error texture",
            payload.item.source_id, binding.source_id));
        return;
      }

      plan->planner.AddDependency(id, it->second);
    };

    for (const auto& slot : MaterialSource::TextureSlots()) {
      add_dep(payload.item.material.textures.*slot.binding);
    }
    return id;
  };

  struct PlannerGeometrySink final : adapters::GeometryWorkItemSink {
    explicit PlannerGeometrySink(PlannedFbxImport& plan)
      : plan_(&plan)
    {
    }

    auto Consume(MeshBuildPipeline::WorkItem item) -> bool override
    {
      const auto handle = plan_->payloads.Store(std::move(item));
      auto& payload = plan_->payloads.MeshBuild(handle);
      const auto mesh_build_id
        = plan_->planner.AddMeshBuild(payload.item.mesh_name, handle);

      const auto geometry_handle = plan_->payloads.Store(
        GeometryFinalizeWorkItem { .mesh_build_item = mesh_build_id });
      const auto geometry_id = plan_->planner.AddGeometryAsset(
        payload.item.mesh_name, geometry_handle);
      plan_->planner.AddDependency(geometry_id, mesh_build_id);
      plan_->product_names.geometries.insert(payload.item.storage_mesh_name);
      for (const auto surface : payload.item.surfaces_used) {
        if (surface >= plan_->surfaces.size()) {
          continue;
        }
        if (const auto* material
          = std::get_if<PlanItemId>(&plan_->surfaces.at(surface))) {
          plan_->planner.AddDependency(geometry_id, *material);
        }
      }
      plan_->geometry_items.push_back(geometry_id);
      return true;
    }

    observer_ptr<PlannedFbxImport> plan_;
  };

  struct PlannerSceneSink final : adapters::SceneWorkItemSink {
    explicit PlannerSceneSink(PlannedFbxImport& plan)
      : plan_(&plan)
    {
    }

    auto Consume(ScenePipeline::WorkItem item) -> bool override
    {
      const auto handle = plan_->payloads.Store(std::move(item));
      auto& payload = plan_->payloads.Scene(handle);
      const auto id
        = plan_->planner.AddSceneAsset(payload.item.source_id, handle);
      plan_->scene_items.push_back(id);
      return true;
    }

    observer_ptr<PlannedFbxImport> plan_;
  };

  PlannerTextureSink texture_sink(*plan);
  const auto texture_result = scene.adapter->BuildWorkItems(
    adapters::TextureWorkTag {}, texture_sink, input);
  for (const auto& diagnostic : texture_result.diagnostics) {
    outcome.diagnostics.push_back(diagnostic);
  }
  if (!texture_result.success) {
    return outcome;
  }

  PlannerMaterialSink material_sink(*plan);
  const auto material_result = scene.adapter->BuildWorkItems(
    adapters::MaterialWorkTag {}, material_sink, input);
  for (const auto& diagnostic : material_result.diagnostics) {
    outcome.diagnostics.push_back(diagnostic);
  }
  if (!material_result.success) {
    return outcome;
  }

  // A remapped source material binds the existing material and is not
  // produced, nor are the textures only it binds.
  auto produced = std::vector<const MaterialSource*> {};
  auto replaced = std::vector<const MaterialSource*> {};
  for (const auto& item : plan->pending_materials) {
    auto& group
      = remap.contains(item.material.storage_name) ? replaced : produced;
    group.push_back(&item.material);
  }
  const auto dropped
    = internal::TexturesOnlyReplacedMaterialsUse(produced, replaced);
  for (auto& item : plan->pending_textures) {
    plan->product_names.textures.insert(item.name);
    if (!dropped.contains(item.source_id)) {
      add_texture(std::move(item));
    }
  }
  for (auto& item : plan->pending_materials) {
    plan->product_names.materials.insert(item.material.storage_name);
    if (const auto it = remap.find(item.material.storage_name);
      it != remap.end()) {
      plan->surfaces.emplace_back(it->second);
    } else {
      plan->surfaces.emplace_back(add_material(std::move(item)));
    }
  }
  plan->pending_textures.clear();
  plan->pending_materials.clear();

  PlannerGeometrySink geometry_sink(*plan);
  const auto geometry_result = scene.adapter->BuildWorkItems(
    adapters::GeometryWorkTag {}, geometry_sink, input);
  for (const auto& diagnostic : geometry_result.diagnostics) {
    outcome.diagnostics.push_back(diagnostic);
  }
  if (!geometry_result.success) {
    return outcome;
  }
  for (auto& diagnostic : internal::ReportMissingSettingTargets(
         request.options, plan->product_names, source_id_prefix)) {
    outcome.diagnostics.push_back(std::move(diagnostic));
  }

  PlannerSceneSink scene_sink(*plan);
  const auto scene_result = scene.adapter->BuildWorkItems(
    adapters::SceneWorkTag {}, scene_sink, input);
  for (const auto& diagnostic : scene_result.diagnostics) {
    outcome.diagnostics.push_back(diagnostic);
  }
  if (!scene_result.success) {
    return outcome;
  }

  for (const auto scene_item : plan->scene_items) {
    for (const auto geometry_item : plan->geometry_items) {
      plan->planner.AddDependency(scene_item, geometry_item);
    }
  }

  plan->plan = plan->planner.MakePlan();
  outcome.plan = std::move(plan);
  return outcome;
}

//! Execute the planner-driven import plan.
auto FbxImportJob::ExecutePlan(PlannedFbxImport& plan, ImportSession& session)
  -> co::Co<bool>
{
  bool success = false;
  std::optional<WorkDispatcher::ProgressReporter> progress;
  if (ProgressCallback()) {
    progress = WorkDispatcher::ProgressReporter {
      .job_id = JobId(),
      .on_progress = ProgressCallback(),
      .overall_start = 0.2F,
      .overall_end = 0.9F,
    };
  }
  // Keep dispatcher alive for the whole nursery lifetime. Locals declared
  // inside OXCO_WITH_NURSERY can be destroyed before child tasks finish.
  WorkDispatcher dispatcher(
    session, ThreadPool(), Concurrency(), StopToken(), std::move(progress));

  OXCO_WITH_NURSERY(n)
  {
    WorkDispatcher::PlanContext context {
      .planner = plan.planner,
      .payloads = plan.payloads,
      .steps = plan.plan,
      .surfaces = plan.surfaces,
      .geometry_items = plan.geometry_items,
    };

    success = co_await dispatcher.Run(context, n);
    // Always join: WorkDispatcher owns pipeline shutdown/drain and returns once
    // it has reached a terminal state. Forcing nursery cancellation here can
    // interrupt in-flight pipeline tasks during teardown.
    co_return co::kJoin;
  };

  co_return success;
}

//! Finalize the session and return the import report.
auto FbxImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
