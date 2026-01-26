//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/IAsyncFileReader.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/Internal/AdapterTypes.h>
#include <Oxygen/Content/Import/Internal/ImportPlanner.h>
#include <Oxygen/Content/Import/Internal/ImportSession.h>
#include <Oxygen/Content/Import/Internal/Jobs/FbxImportJob.h>
#include <Oxygen/Content/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Content/Import/Internal/WorkDispatcher.h>
#include <Oxygen/Content/Import/Internal/WorkPayloadStore.h>
#include <Oxygen/Content/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::content::import::detail {

struct FbxImportJob::PlannedFbxImport final {
  ImportPlanner planner;
  WorkPayloadStore payloads;
  std::vector<PlanStep> plan;

  std::unordered_map<std::string, PlanItemId> texture_by_source_id;
  std::vector<PlanItemId> texture_items;
  std::vector<PlanItemId> material_items;
  std::vector<PlanItemId> material_slots;
  std::vector<PlanItemId> geometry_items;
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

  EnsureCookedRoot();

  ImportSession session(Request(), FileReader(), FileWriter(), ThreadPool(),
    TableRegistry(), IndexRegistry());

  ReportPhaseProgress(ImportPhase::kLoading, 0.0f, "Parsing FBX...");
  const auto load_start = std::chrono::steady_clock::now();
  auto scene = co_await ParseScene(session);
  const auto load_end = std::chrono::steady_clock::now();
  session.AddSourceLoadDuration(MakeDuration(load_start, load_end));
  AddDiagnostics(session, std::move(scene.diagnostics));
  if (scene.canceled || !scene.success) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0f, "FBX parse failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(ImportPhase::kPlanning, 0.1f, "Building import plan...");
  const auto request_copy = Request();
  const auto stop_token = StopToken();
  const auto plan_start = std::chrono::steady_clock::now();
  auto plan_outcome = co_await ThreadPool()->Run(
    [this, &scene, request_copy, stop_token](
      co::ThreadPool::CancelToken canceled) -> PlanBuildOutcome {
      DLOG_F(1, "Build plan task begin");
      if (canceled || stop_token.stop_requested()) {
        PlanBuildOutcome canceled_outcome;
        canceled_outcome.canceled = true;
        return canceled_outcome;
      }
      return BuildPlan(scene, request_copy, stop_token);
    });
  AddDiagnostics(session, std::move(plan_outcome.diagnostics));
  if (plan_outcome.canceled || !plan_outcome.plan) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0f, "Plan build failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(ImportPhase::kWorking, 0.2f, "Executing plan...");
  const bool executed = co_await ExecutePlan(*plan_outcome.plan, session);
  if (!executed) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0f, "Plan execution failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(ImportPhase::kFinalizing, 0.9f, "Finalizing import...");
  auto report = co_await FinalizeWithTelemetry(session);

  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0f,
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
  bool should_read_source_bytes = false;

  if (reader != nullptr) {
    std::error_code status_error;
    const auto status
      = std::filesystem::status(request_copy.source_path, status_error);
    should_read_source_bytes
      = status_error || !std::filesystem::is_regular_file(status);
  }

  if (should_read_source_bytes) {
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
    }
  }

  auto parsed = co_await ThreadPool()->Run(
    [request_copy, stop_token, naming_service, source_bytes](
      co::ThreadPool::CancelToken canceled) {
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
  const ImportRequest& request, std::stop_token stop_token) -> PlanBuildOutcome
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
    .stop_token = stop_token,
    .external_texture_bytes = {},
  };

  struct PlannerTextureSink final : adapters::TextureWorkItemSink {
    explicit PlannerTextureSink(PlannedFbxImport& plan)
      : plan_(plan)
    {
    }

    auto Consume(TexturePipeline::WorkItem item) -> bool override
    {
      const auto handle = plan_.payloads.Store(std::move(item));
      auto& payload = plan_.payloads.Texture(handle);
      const auto id
        = plan_.planner.AddTextureResource(payload.item.source_id, handle);
      plan_.texture_by_source_id.emplace(payload.item.source_id, id);
      plan_.texture_items.push_back(id);
      return true;
    }

    PlannedFbxImport& plan_;
  };

  struct PlannerMaterialSink final : adapters::MaterialWorkItemSink {
    explicit PlannerMaterialSink(
      PlannedFbxImport& plan, std::vector<ImportDiagnostic>& diagnostics)
      : plan_(plan)
      , diagnostics_(diagnostics)
    {
    }

    auto Consume(MaterialPipeline::WorkItem item) -> bool override
    {
      const auto handle = plan_.payloads.Store(std::move(item));
      auto& payload = plan_.payloads.Material(handle);
      const auto id
        = plan_.planner.AddMaterialAsset(payload.item.material_name, handle);
      plan_.material_items.push_back(id);
      plan_.material_slots.push_back(id);

      auto add_dep = [&](const MaterialTextureBinding& binding) {
        if (!binding.assigned || binding.source_id.empty()) {
          return;
        }

        const auto it = plan_.texture_by_source_id.find(binding.source_id);
        if (it == plan_.texture_by_source_id.end()) {
          diagnostics_.push_back(MakeErrorDiagnostic("material.texture_missing",
            "Missing texture dependency", payload.item.source_id,
            binding.source_id));
          return;
        }

        plan_.planner.AddDependency(id, it->second);
      };

      add_dep(payload.item.textures.base_color);
      add_dep(payload.item.textures.normal);
      add_dep(payload.item.textures.metallic);
      add_dep(payload.item.textures.roughness);
      add_dep(payload.item.textures.ambient_occlusion);
      add_dep(payload.item.textures.emissive);
      add_dep(payload.item.textures.specular);
      add_dep(payload.item.textures.sheen_color);
      add_dep(payload.item.textures.clearcoat);
      add_dep(payload.item.textures.clearcoat_normal);
      add_dep(payload.item.textures.transmission);
      add_dep(payload.item.textures.thickness);

      return true;
    }

    PlannedFbxImport& plan_;
    std::vector<ImportDiagnostic>& diagnostics_;
  };

  struct PlannerGeometrySink final : adapters::GeometryWorkItemSink {
    explicit PlannerGeometrySink(PlannedFbxImport& plan)
      : plan_(plan)
    {
    }

    auto Consume(MeshBuildPipeline::WorkItem item) -> bool override
    {
      const auto handle = plan_.payloads.Store(std::move(item));
      auto& payload = plan_.payloads.MeshBuild(handle);
      const auto mesh_build_id
        = plan_.planner.AddMeshBuild(payload.item.mesh_name, handle);

      const auto geometry_handle = plan_.payloads.Store(
        GeometryFinalizeWorkItem { .mesh_build_item = mesh_build_id });
      const auto geometry_id = plan_.planner.AddGeometryAsset(
        payload.item.mesh_name, geometry_handle);
      plan_.planner.AddDependency(geometry_id, mesh_build_id);
      for (const auto slot : payload.item.material_slots_used) {
        if (slot < plan_.material_slots.size()) {
          plan_.planner.AddDependency(geometry_id, plan_.material_slots[slot]);
        }
      }
      plan_.geometry_items.push_back(geometry_id);
      return true;
    }

    PlannedFbxImport& plan_;
  };

  struct PlannerSceneSink final : adapters::SceneWorkItemSink {
    explicit PlannerSceneSink(PlannedFbxImport& plan)
      : plan_(plan)
    {
    }

    auto Consume(ScenePipeline::WorkItem item) -> bool override
    {
      const auto handle = plan_.payloads.Store(std::move(item));
      auto& payload = plan_.payloads.Scene(handle);
      const auto id
        = plan_.planner.AddSceneAsset(payload.item.source_id, handle);
      plan_.scene_items.push_back(id);
      return true;
    }

    PlannedFbxImport& plan_;
  };

  PlannerTextureSink texture_sink(*plan);
  const auto texture_result = scene.adapter->BuildWorkItems(
    adapters::TextureWorkTag {}, texture_sink, input);
  for (auto& diagnostic : texture_result.diagnostics) {
    outcome.diagnostics.push_back(std::move(diagnostic));
  }
  if (!texture_result.success) {
    return outcome;
  }

  PlannerMaterialSink material_sink(*plan, outcome.diagnostics);
  const auto material_result = scene.adapter->BuildWorkItems(
    adapters::MaterialWorkTag {}, material_sink, input);
  for (auto& diagnostic : material_result.diagnostics) {
    outcome.diagnostics.push_back(std::move(diagnostic));
  }
  if (!material_result.success) {
    return outcome;
  }

  PlannerGeometrySink geometry_sink(*plan);
  const auto geometry_result = scene.adapter->BuildWorkItems(
    adapters::GeometryWorkTag {}, geometry_sink, input);
  for (auto& diagnostic : geometry_result.diagnostics) {
    outcome.diagnostics.push_back(std::move(diagnostic));
  }
  if (!geometry_result.success) {
    return outcome;
  }

  PlannerSceneSink scene_sink(*plan);
  const auto scene_result = scene.adapter->BuildWorkItems(
    adapters::SceneWorkTag {}, scene_sink, input);
  for (auto& diagnostic : scene_result.diagnostics) {
    outcome.diagnostics.push_back(std::move(diagnostic));
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

  OXCO_WITH_NURSERY(n)
  {
    std::optional<WorkDispatcher::ProgressReporter> progress;
    if (ProgressCallback()) {
      progress = WorkDispatcher::ProgressReporter {
        .job_id = JobId(),
        .on_progress = ProgressCallback(),
        .overall_start = 0.2f,
        .overall_end = 0.9f,
      };
    }
    WorkDispatcher dispatcher(
      session, ThreadPool(), Concurrency(), StopToken(), std::move(progress));
    WorkDispatcher::PlanContext context {
      .planner = plan.planner,
      .payloads = plan.payloads,
      .steps = plan.plan,
      .material_slots = plan.material_slots,
      .geometry_items = plan.geometry_items,
    };

    success = co_await dispatcher.Run(context, n);
    if (success) {
      co_return co::kJoin;
    }
    co_return co::kCancel;
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
