//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/IAsyncFileReader.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/Internal/AdapterTypes.h>
#include <Oxygen/Content/Import/Internal/ImportPlanner.h>
#include <Oxygen/Content/Import/Internal/ImportSession.h>
#include <Oxygen/Content/Import/Internal/Jobs/GlbImportJob.h>
#include <Oxygen/Content/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Content/Import/Internal/WorkDispatcher.h>
#include <Oxygen/Content/Import/Internal/WorkPayloadStore.h>
#include <Oxygen/Content/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::content::import::detail {

struct GlbImportJob::PlannedGlbImport final {
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

} // namespace

/*!
 Execute the GLB import workflow.

 The current implementation wires the job lifecycle and progress reporting.
 Phase 5 will populate the parse/cook/emit stages with real pipeline work.
*/
auto GlbImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "GlbImportJob starting: job_id={} path={}", JobId(),
    Request().source_path.string());

  EnsureCookedRoot();

  ImportSession session(
    Request(), FileReader(), FileWriter(), ThreadPool(), TableRegistry());

  ReportProgress(ImportProgressEvent::kPhaseStarted, ImportPhase::kLoading,
    0.0f, 0.0f, 0U, 0U, "Loading started");
  ReportProgress(ImportPhase::kLoading, 0.0f, 0.0f, 0U, 0U, "Parsing GLB...");
  auto asset = co_await ParseAsset(session);
  AddDiagnostics(session, std::move(asset.diagnostics));
  if (asset.canceled || !asset.success) {
    ReportProgress(
      ImportPhase::kFailed, 1.0f, 1.0f, 0U, 0U, "GLB parse failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(
    ImportPhase::kLoading, 0.05f, 0.0f, 0U, 0U, "Loading texture sources...");
  auto external_textures = co_await LoadExternalTextureBytes(asset, session);
  AddDiagnostics(session, std::move(external_textures.diagnostics));
  if (external_textures.canceled) {
    ReportProgress(
      ImportPhase::kFailed, 1.0f, 1.0f, 0U, 0U, "GLB load canceled");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportProgressEvent::kPhaseStarted, ImportPhase::kPlanning,
    0.1f, 0.0f, 0U, 0U, "Planning started");
  ReportProgress(
    ImportPhase::kPlanning, 0.1f, 0.0f, 0U, 0U, "Building import plan...");
  const auto request_copy = Request();
  const auto stop_token = StopToken();
  auto plan_outcome = co_await ThreadPool()->Run(
    [this, &asset, request_copy, stop_token, &external_textures](
      co::ThreadPool::CancelToken canceled) -> PlanBuildOutcome {
      DLOG_F(1, "GlbImportJob: BuildPlan task begin");
      if (canceled || stop_token.stop_requested()) {
        PlanBuildOutcome canceled_outcome;
        canceled_outcome.canceled = true;
        return canceled_outcome;
      }
      return BuildPlan(
        asset, request_copy, stop_token, external_textures.bytes);
    });
  AddDiagnostics(session, std::move(plan_outcome.diagnostics));
  if (plan_outcome.canceled || !plan_outcome.plan) {
    ReportProgress(
      ImportPhase::kFailed, 1.0f, 1.0f, 0U, 0U, "Plan build failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportProgressEvent::kPhaseFinished, ImportPhase::kLoading,
    1.0f, 1.0f, 0U, 0U, "Loading finished");
  ReportProgress(ImportProgressEvent::kPhaseFinished, ImportPhase::kPlanning,
    1.0f, 1.0f, 0U, 0U, "Planning finished");

  ReportProgress(ImportProgressEvent::kPhaseStarted, ImportPhase::kWorking,
    0.2f, 0.0f, 0U, 0U, "Working started");
  ReportProgress(
    ImportPhase::kWorking, 0.2f, 0.0f, 0U, 0U, "Executing plan...");
  if (!co_await ExecutePlan(*plan_outcome.plan, session)) {
    ReportProgress(
      ImportPhase::kFailed, 1.0f, 1.0f, 0U, 0U, "Plan execution failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportProgressEvent::kPhaseFinished, ImportPhase::kWorking,
    1.0f, 1.0f, 0U, 0U, "Working finished");

  ReportProgress(ImportProgressEvent::kPhaseStarted, ImportPhase::kFinalizing,
    0.9f, 0.0f, 0U, 0U, "Finalizing started");
  ReportProgress(
    ImportPhase::kFinalizing, 0.9f, 0.0f, 0U, 0U, "Finalizing import...");
  auto report = co_await FinalizeSession(session);

  ReportProgress(ImportProgressEvent::kPhaseFinished, ImportPhase::kFinalizing,
    1.0f, 1.0f, 0U, 0U, "Finalizing finished");

  ReportProgress(report.success ? ImportPhase::kComplete : ImportPhase::kFailed,
    1.0f, 1.0f, 0U, 0U, report.success ? "Import complete" : "Import failed");

  co_return report;
}

//! Parse the GLB source into an intermediate asset representation.
auto GlbImportJob::ParseAsset(ImportSession& session) -> co::Co<ParsedGlbAsset>
{
  const auto request_copy = Request();
  const auto stop_token = StopToken();
  const auto naming_service = observer_ptr { &GetNamingService() };

  if (ThreadPool() == nullptr) {
    ParsedGlbAsset parsed;
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

    auto adapter = std::make_shared<adapters::GltfAdapter>();
    const auto parse_result = adapter->Parse(request_copy.source_path, input);
    parsed.adapter = std::move(adapter);
    parsed.diagnostics = parse_result.diagnostics;
    parsed.success = parse_result.success;
    co_return parsed;
  }

  auto parsed = co_await ThreadPool()->Run(
    [request_copy, stop_token, naming_service](
      co::ThreadPool::CancelToken canceled) {
      DLOG_F(1, "GlbImportJob: ParseAsset task begin");
      ParsedGlbAsset out;
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

      auto adapter = std::make_shared<adapters::GltfAdapter>();
      const auto parse_result = adapter->Parse(request_copy.source_path, input);
      out.adapter = std::move(adapter);
      out.diagnostics = parse_result.diagnostics;
      out.success = parse_result.success;
      return out;
    });

  (void)session;
  co_return parsed;
}

//! Load external glTF texture bytes via the async file reader.
auto GlbImportJob::LoadExternalTextureBytes(ParsedGlbAsset& asset,
  ImportSession& session) -> co::Co<ExternalTextureLoadOutcome>
{
  ExternalTextureLoadOutcome outcome;
  if (!asset.success || asset.adapter == nullptr) {
    co_return outcome;
  }

  if (StopToken().stop_requested()) {
    outcome.canceled = true;
    co_return outcome;
  }

  auto reader = session.FileReader();
  if (reader == nullptr) {
    outcome.diagnostics.push_back(MakeErrorDiagnostic("import.file_reader",
      "Import session has no async file reader", Request().source_path.string(),
      ""));
    co_return outcome;
  }

  const auto request_copy = Request();
  const std::string source_id_prefix = request_copy.source_path.string();
  adapters::AdapterInput input {
    .source_id_prefix = source_id_prefix,
    .object_path_prefix = {},
    .material_keys = {},
    .default_material_key = DefaultMaterialKey(),
    .request = request_copy,
    .naming_service = observer_ptr { &GetNamingService() },
    .stop_token = StopToken(),
    .external_texture_bytes = {},
  };

  std::vector<ImportDiagnostic> source_diagnostics;
  const auto sources
    = asset.adapter->CollectExternalTextureSources(input, source_diagnostics);
  for (auto& diagnostic : source_diagnostics) {
    outcome.diagnostics.push_back(std::move(diagnostic));
  }

  for (const auto& source : sources) {
    if (StopToken().stop_requested()) {
      outcome.canceled = true;
      co_return outcome;
    }

    auto read_result = co_await reader.get()->ReadFile(source.resolved_path);
    if (!read_result.has_value()) {
      const auto message
        = "Failed to read glTF image file: " + read_result.error().ToString();
      outcome.diagnostics.push_back(
        MakeWarningDiagnostic("gltf.image.load_failed", message,
          source.texture_id, source.resolved_path.string()));
      outcome.bytes.push_back(adapters::AdapterInput::ExternalTextureBytes {
        .texture_id = source.texture_id,
        .bytes = std::make_shared<std::vector<std::byte>>(),
      });
      continue;
    }

    auto bytes = std::make_shared<std::vector<std::byte>>(
      std::move(read_result.value()));
    if (bytes->empty()) {
      outcome.diagnostics.push_back(
        MakeWarningDiagnostic("gltf.image.empty", "glTF image file is empty",
          source.texture_id, source.resolved_path.string()));
    }
    outcome.bytes.push_back(adapters::AdapterInput::ExternalTextureBytes {
      .texture_id = source.texture_id,
      .bytes = std::move(bytes),
    });
  }

  co_return outcome;
}

//! Build the planner-driven execution plan for this import.
auto GlbImportJob::BuildPlan(ParsedGlbAsset& asset,
  const ImportRequest& request, std::stop_token stop_token,
  std::span<const adapters::AdapterInput::ExternalTextureBytes>
    external_texture_bytes) -> PlanBuildOutcome
{
  PlanBuildOutcome outcome;
  if (!asset.success || asset.adapter == nullptr) {
    return outcome;
  }

  DLOG_F(1, "GlbImportJob: BuildPlan begin");

  auto plan = std::make_unique<PlannedGlbImport>();
  plan->planner.RegisterPipeline<TexturePipeline>(
    PlanItemKind::kTextureResource);
  plan->planner.RegisterPipeline<BufferPipeline>(PlanItemKind::kBufferResource);
  plan->planner.RegisterPipeline<MaterialPipeline>(
    PlanItemKind::kMaterialAsset);
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
    .external_texture_bytes = external_texture_bytes,
  };

  struct PlannerTextureSink final : adapters::TextureWorkItemSink {
    explicit PlannerTextureSink(PlannedGlbImport& plan)
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

    PlannedGlbImport& plan_;
  };

  struct PlannerMaterialSink final : adapters::MaterialWorkItemSink {
    explicit PlannerMaterialSink(
      PlannedGlbImport& plan, std::vector<ImportDiagnostic>& diagnostics)
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

    PlannedGlbImport& plan_;
    std::vector<ImportDiagnostic>& diagnostics_;
  };

  struct PlannerGeometrySink final : adapters::GeometryWorkItemSink {
    explicit PlannerGeometrySink(PlannedGlbImport& plan)
      : plan_(plan)
    {
    }

    auto Consume(GeometryPipeline::WorkItem item) -> bool override
    {
      const auto handle = plan_.payloads.Store(std::move(item));
      auto& payload = plan_.payloads.Geometry(handle);
      const auto id
        = plan_.planner.AddGeometryAsset(payload.item.mesh_name, handle);
      plan_.geometry_items.push_back(id);
      return true;
    }

    PlannedGlbImport& plan_;
  };

  struct PlannerSceneSink final : adapters::SceneWorkItemSink {
    explicit PlannerSceneSink(PlannedGlbImport& plan)
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

    PlannedGlbImport& plan_;
  };

  PlannerTextureSink texture_sink(*plan);
  const auto texture_result = asset.adapter->BuildWorkItems(
    adapters::TextureWorkTag {}, texture_sink, input);
  for (auto& diagnostic : texture_result.diagnostics) {
    outcome.diagnostics.push_back(std::move(diagnostic));
  }
  if (!texture_result.success) {
    return outcome;
  }

  PlannerMaterialSink material_sink(*plan, outcome.diagnostics);
  const auto material_result = asset.adapter->BuildWorkItems(
    adapters::MaterialWorkTag {}, material_sink, input);
  for (auto& diagnostic : material_result.diagnostics) {
    outcome.diagnostics.push_back(std::move(diagnostic));
  }
  if (!material_result.success) {
    return outcome;
  }

  PlannerGeometrySink geometry_sink(*plan);
  const auto geometry_result = asset.adapter->BuildWorkItems(
    adapters::GeometryWorkTag {}, geometry_sink, input);
  for (auto& diagnostic : geometry_result.diagnostics) {
    outcome.diagnostics.push_back(std::move(diagnostic));
  }
  if (!geometry_result.success) {
    return outcome;
  }

  PlannerSceneSink scene_sink(*plan);
  const auto scene_result = asset.adapter->BuildWorkItems(
    adapters::SceneWorkTag {}, scene_sink, input);
  for (auto& diagnostic : scene_result.diagnostics) {
    outcome.diagnostics.push_back(std::move(diagnostic));
  }
  if (!scene_result.success) {
    return outcome;
  }

  for (const auto geometry_item : plan->geometry_items) {
    for (const auto material_item : plan->material_items) {
      plan->planner.AddDependency(geometry_item, material_item);
    }
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
auto GlbImportJob::ExecutePlan(PlannedGlbImport& plan, ImportSession& session)
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
auto GlbImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
