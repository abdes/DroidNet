//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/Adapters/FbxGeometryAdapter.h>
#include <Oxygen/Content/Import/Async/Adapters/GeometryAdapterTypes.h>
#include <Oxygen/Content/Import/Async/Emitters/AssetEmitter.h>
#include <Oxygen/Content/Import/Async/Emitters/BufferEmitter.h>
#include <Oxygen/Content/Import/Async/ImportSession.h>
#include <Oxygen/Content/Import/Async/Jobs/FbxImportJob.h>
#include <Oxygen/Content/Import/Async/Pipelines/GeometryPipeline.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/OxCo/Nursery.h>

#include <string>
#include <utility>

namespace oxygen::content::import::detail {

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

  [[nodiscard]] auto MakeErrorDiagnostic(std::string code,
    std::string message, std::string_view source_id,
    std::string_view object_path) -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

  [[nodiscard]] auto EmitGeometryPayload(GeometryPipeline& pipeline,
    ImportSession& session, GeometryPipeline::WorkResult result) -> co::Co<bool>
  {
    if (!result.success || !result.cooked.has_value()) {
      AddDiagnostics(session, std::move(result.diagnostics));
      co_return false;
    }

    AddDiagnostics(session, std::move(result.diagnostics));

    auto& cooked = *result.cooked;
    auto& buffer_emitter = session.BufferEmitter();
    auto& asset_emitter = session.AssetEmitter();

    std::vector<GeometryPipeline::MeshBufferBindings> bindings;
    bindings.reserve(cooked.lods.size());

    bool ok = true;
    for (auto& lod : cooked.lods) {
      GeometryPipeline::MeshBufferBindings binding {};
      binding.vertex_buffer = buffer_emitter.Emit(std::move(lod.vertex_buffer));
      binding.index_buffer = buffer_emitter.Emit(std::move(lod.index_buffer));

      if (lod.auxiliary_buffers.size() == 4u) {
        binding.joint_index_buffer
          = buffer_emitter.Emit(std::move(lod.auxiliary_buffers[0]));
        binding.joint_weight_buffer
          = buffer_emitter.Emit(std::move(lod.auxiliary_buffers[1]));
        binding.inverse_bind_buffer
          = buffer_emitter.Emit(std::move(lod.auxiliary_buffers[2]));
        binding.joint_remap_buffer
          = buffer_emitter.Emit(std::move(lod.auxiliary_buffers[3]));
      } else if (!lod.auxiliary_buffers.empty()) {
        session.AddDiagnostic(MakeErrorDiagnostic("mesh.aux_buffer_count",
          "Unexpected auxiliary buffer count for mesh LOD",
          result.source_id, ""));
        ok = false;
      }

      bindings.push_back(binding);
    }

    std::vector<ImportDiagnostic> finalize_diagnostics;
    const auto finalized = co_await pipeline.FinalizeDescriptorBytes(
      bindings, cooked.descriptor_bytes, finalize_diagnostics);
    AddDiagnostics(session, std::move(finalize_diagnostics));

    if (!finalized.has_value()) {
      co_return false;
    }

    asset_emitter.Emit(cooked.geometry_key, data::AssetType::kGeometry,
      cooked.virtual_path, cooked.descriptor_relpath, *finalized);
    co_return ok;
  }

} // namespace

/*!
 Execute the FBX import workflow.

 The current implementation wires the job lifecycle and progress reporting.
 Phase 5 will populate the parse/cook/emit stages with real pipeline work.
*/
auto FbxImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "FbxImportJob starting: job_id={} path={}", JobId(),
    Request().source_path.string());

  EnsureCookedRoot();

  ImportSession session(
    Request(), FileReader(), FileWriter(), ThreadPool(), TableRegistry());

  ReportProgress(ImportPhase::kParsing, 0.0f, "Parsing FBX...");
  const auto scene = co_await ParseScene(session);
  if (!scene.success) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "FBX parse failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kTextures, 0.2f, "Submitting texture work...");

  // Phase 5 TODO: Build MaterialReadinessTracker to drive streaming emission
  // when textures become available.

  // Phase 5 TODO: Submit all texture work items using pipeline backpressure.
  // Use co_await pipeline.Submit(work) to respect bounded queues. Do NOT
  // enqueue hundreds of textures without awaiting; backpressure must be
  // honored to avoid unbounded memory growth.

  // Run concurrent streams (texture collect+emit+materials, geometry, anim).
  OXCO_WITH_NURSERY(job_streams)
  {
    job_streams.Start([&]() -> co::Co<> {
      ReportProgress(
        ImportPhase::kTextures, 0.3f, "Cooking textures (streaming)...");
      if (!co_await CookTextures(scene, session)) {
        ReportProgress(ImportPhase::kFailed, 1.0f, "Texture cooking failed");
      }
      co_return;
    });

    job_streams.Start([&]() -> co::Co<> {
      ReportProgress(
        ImportPhase::kGeometry, 0.5f, "Cooking geometry (streaming)...");
      if (!co_await CookGeometry(scene, session)) {
        ReportProgress(ImportPhase::kFailed, 1.0f, "Geometry cooking failed");
      }
      co_return;
    });

    job_streams.Start([&]() -> co::Co<> {
      // TODO(Phase 5): Stream animation baking on ThreadPool and emit buffers.
      co_return;
    });

    co_return co::kJoin;
  };

  ReportProgress(ImportPhase::kScene, 0.8f, "Emitting scene...");
  if (!co_await EmitScene(scene, session)) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Scene emission failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kWriting, 0.9f, "Finalizing import...");
  auto report = co_await FinalizeSession(session);

  ReportProgress(report.success ? ImportPhase::kComplete : ImportPhase::kFailed,
    1.0f, report.success ? "Import complete" : "Import failed");

  co_return report;
}

//! Parse the FBX source into an intermediate scene representation.
auto FbxImportJob::ParseScene([[maybe_unused]] ImportSession& session)
  -> co::Co<ParsedFbxScene>
{
  // TODO(Phase 5): Parse FBX via ufbx on the ThreadPool.
  // TODO(Phase 5): Honor StopToken() to support cancellation.
  // TODO(Phase 5): Populate scene metadata for downstream stages.
  co_return ParsedFbxScene {
    .success = true,
  };
}

//! Cook textures and emit them via TextureEmitter.
auto FbxImportJob::CookTextures([[maybe_unused]] const ParsedFbxScene& scene,
  [[maybe_unused]] ImportSession& session) -> co::Co<bool>
{
  // TODO(Phase 5): Start TexturePipeline in the job nursery.
  // TODO(Phase 5): Submit texture work items using backpressure-aware Submit.
  // TODO(Phase 5): Collect results and emit via session.TextureEmitter().
  // TODO(Phase 5): Stream material emission as textures become ready via
  // MaterialReadinessTracker.
  co_return true;
}

//! Cook geometry buffers and emit them via BufferEmitter.
auto FbxImportJob::CookGeometry([[maybe_unused]] const ParsedFbxScene& scene,
  [[maybe_unused]] ImportSession& session) -> co::Co<bool>
{
  if ((Request().options.import_content & ImportContentFlags::kGeometry)
    == ImportContentFlags::kNone) {
    co_return true;
  }

  adapters::GeometryAdapterInput input {
    .source_id_prefix = Request().source_path.string(),
    .object_path_prefix = {},
    .material_keys = {},
    .default_material_key = DefaultMaterialKey(),
    .request = Request(),
    .stop_token = StopToken(),
  };

  adapters::FbxGeometryAdapter adapter;
  auto output = adapter.BuildWorkItems(Request().source_path, input);
  AddDiagnostics(session, std::move(output.diagnostics));
  if (!output.success) {
    co_return false;
  }

  const size_t work_count = output.work_items.size();
  if (work_count == 0) {
    co_return true;
  }

  GeometryPipeline pipeline(*ThreadPool(), GeometryPipeline::Config {});
  bool ok = true;

  OXCO_WITH_NURSERY(n)
  {
    pipeline.Start(n);
    for (auto& item : output.work_items) {
      co_await pipeline.Submit(std::move(item));
    }
    pipeline.Close();

    for (size_t i = 0; i < work_count; ++i) {
      auto result = co_await pipeline.Collect();
      if (!co_await EmitGeometryPayload(pipeline, session, std::move(result))) {
        ok = false;
      }
    }

    co_return co::kJoin;
  };

  co_return ok;
}

//! Emit material descriptors via AssetEmitter.
auto FbxImportJob::EmitMaterials([[maybe_unused]] const ParsedFbxScene& scene,
  [[maybe_unused]] ImportSession& session) -> co::Co<bool>
{
  // TODO(Phase 5): Build material descriptors.
  // TODO(Phase 5): Emit .omat files via session.AssetEmitter().
  co_return true;
}

//! Emit scene descriptors via AssetEmitter.
auto FbxImportJob::EmitScene([[maybe_unused]] const ParsedFbxScene& scene,
  [[maybe_unused]] ImportSession& session) -> co::Co<bool>
{
  // TODO(Phase 5): Build scene descriptors.
  // TODO(Phase 5): Emit .oscene via session.AssetEmitter().
  co_return true;
}

//! Finalize the session and return the import report.
auto FbxImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
