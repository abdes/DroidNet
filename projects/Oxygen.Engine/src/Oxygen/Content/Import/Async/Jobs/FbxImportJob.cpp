//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/ImportSession.h>
#include <Oxygen/Content/Import/Async/Jobs/FbxImportJob.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::content::import::detail {

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

  ImportSession session(Request(), FileReader(), FileWriter(), ThreadPool());

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
  // TODO(Phase 5): Submit mesh work to ThreadPool.
  // TODO(Phase 5): Emit buffers via session.BufferEmitter().
  co_return true;
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
