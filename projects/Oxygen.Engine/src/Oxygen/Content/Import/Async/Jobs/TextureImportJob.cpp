//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/ImportSession.h>
#include <Oxygen/Content/Import/Async/Jobs/TextureImportJob.h>

namespace oxygen::content::import::detail {

/*!
 Execute a standalone texture import workflow.

 The current implementation wires the job lifecycle and progress reporting.
 Phase 5 will populate the load/cook/emit stages with real pipeline work.
*/
auto TextureImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "TextureImportJob starting: job_id={} path={}", JobId(),
    Request().source_path.string());

  EnsureCookedRoot();

  ImportSession session(Request(), FileReader(), FileWriter(), ThreadPool());

  ReportProgress(ImportPhase::kParsing, 0.0f, "Loading texture source...");
  const auto source = co_await LoadSource(session);
  if (!source.success) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Texture load failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kTextures, 0.4f, "Cooking texture...");
  if (!co_await CookTexture(source, session)) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Texture cook failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kWriting, 0.7f, "Emitting texture...");
  if (!co_await EmitTexture(source, session)) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Texture emit failed");
    co_return co_await FinalizeSession(session);
  }

  ReportProgress(ImportPhase::kWriting, 0.9f, "Finalizing import...");
  auto report = co_await FinalizeSession(session);

  ReportProgress(report.success ? ImportPhase::kComplete : ImportPhase::kFailed,
    1.0f, report.success ? "Import complete" : "Import failed");

  co_return report;
}

//! Load the texture bytes from disk or embedded data.
auto TextureImportJob::LoadSource([[maybe_unused]] ImportSession& session)
  -> co::Co<TextureSource>
{
  // TODO(Phase 5): Read texture bytes via IAsyncFileReader.
  // TODO(Phase 5): Honor StopToken() to support cancellation.
  co_return TextureSource {
    .success = true,
  };
}

//! Cook the texture via the async TexturePipeline.
auto TextureImportJob::CookTexture([[maybe_unused]] const TextureSource& source,
  [[maybe_unused]] ImportSession& session) -> co::Co<bool>
{
  // TODO(Phase 5): Submit work item to TexturePipeline.
  // TODO(Phase 5): Collect cooked payload and store for emission.
  co_return true;
}

//! Emit the cooked texture via TextureEmitter.
auto TextureImportJob::EmitTexture([[maybe_unused]] const TextureSource& source,
  [[maybe_unused]] ImportSession& session) -> co::Co<bool>
{
  // TODO(Phase 5): Emit cooked payload via session.TextureEmitter().
  co_return true;
}

//! Finalize the session and return the import report.
auto TextureImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
