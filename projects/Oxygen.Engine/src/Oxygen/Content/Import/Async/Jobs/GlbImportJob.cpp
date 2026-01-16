//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/ImportSession.h>
#include <Oxygen/Content/Import/Async/Jobs/GlbImportJob.h>

namespace oxygen::content::import::detail {

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

  ReportProgress(ImportPhase::kParsing, 0.0f, "Parsing GLB...");
  const auto asset = co_await ParseAsset(session);
  if (!asset.success) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "GLB parse failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kTextures, 0.2f, "Cooking textures...");
  if (!co_await CookTextures(asset, session)) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Texture cooking failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kGeometry, 0.4f, "Cooking buffers...");
  if (!co_await CookBuffers(asset, session)) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Buffer cooking failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kMaterials, 0.6f, "Emitting materials...");
  if (!co_await EmitMaterials(asset, session)) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Material emission failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kScene, 0.8f, "Emitting scene...");
  if (!co_await EmitScene(asset, session)) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Scene emission failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kWriting, 0.9f, "Finalizing import...");
  auto report = co_await FinalizeSession(session);

  ReportProgress(report.success ? ImportPhase::kComplete : ImportPhase::kFailed,
    1.0f, report.success ? "Import complete" : "Import failed");

  co_return report;
}

//! Parse the GLB source into an intermediate asset representation.
auto GlbImportJob::ParseAsset([[maybe_unused]] ImportSession& session)
  -> co::Co<ParsedGlbAsset>
{
  // TODO(Phase 5): Parse glTF/GLB data on the ThreadPool.
  // TODO(Phase 5): Honor StopToken() to support cancellation.
  // TODO(Phase 5): Populate asset metadata for downstream stages.
  co_return ParsedGlbAsset {
    .success = true,
  };
}

//! Cook textures and emit them via TextureEmitter.
auto GlbImportJob::CookTextures([[maybe_unused]] const ParsedGlbAsset& asset,
  [[maybe_unused]] ImportSession& session) -> co::Co<bool>
{
  // TODO(Phase 5): Start TexturePipeline in the job nursery.
  // TODO(Phase 5): Submit texture work items.
  // TODO(Phase 5): Collect results and emit via session.TextureEmitter().
  co_return true;
}

//! Cook buffers and emit them via BufferEmitter.
auto GlbImportJob::CookBuffers([[maybe_unused]] const ParsedGlbAsset& asset,
  [[maybe_unused]] ImportSession& session) -> co::Co<bool>
{
  // TODO(Phase 5): Submit mesh buffers to ThreadPool.
  // TODO(Phase 5): Emit buffers via session.BufferEmitter().
  co_return true;
}

//! Emit material descriptors via AssetEmitter.
auto GlbImportJob::EmitMaterials([[maybe_unused]] const ParsedGlbAsset& asset,
  [[maybe_unused]] ImportSession& session) -> co::Co<bool>
{
  // TODO(Phase 5): Build material descriptors.
  // TODO(Phase 5): Emit .omat files via session.AssetEmitter().
  co_return true;
}

//! Emit scene descriptors via AssetEmitter.
auto GlbImportJob::EmitScene([[maybe_unused]] const ParsedGlbAsset& asset,
  [[maybe_unused]] ImportSession& session) -> co::Co<bool>
{
  // TODO(Phase 5): Build scene descriptors.
  // TODO(Phase 5): Emit .oscene via session.AssetEmitter().
  co_return true;
}

//! Finalize the session and return the import report.
auto GlbImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
