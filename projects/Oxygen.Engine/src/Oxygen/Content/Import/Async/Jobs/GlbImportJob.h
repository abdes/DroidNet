//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Import/Async/Detail/ImportJob.h>

namespace oxygen::content::import {
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::detail {

//! GLB/glTF import job orchestrating async pipelines and emitters.
/*!
 Coordinates the glTF/GLB import flow within a job-scoped nursery. The job
 drives parse, cook, and emit stages with progress reporting.

### Architecture Notes

 - Parsing and validation should run on the ThreadPool.
 - Cooked resources are emitted through async emitters owned by ImportSession.
 - Pipeline integration is introduced in Phase 5.
*/
class GlbImportJob final : public ImportJob {
  OXYGEN_TYPED(GlbImportJob)
public:
  using ImportJob::ImportJob;

private:
  //! Placeholder for parsed GLB asset state.
  struct ParsedGlbAsset {
    bool success = true;
  };

  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;

  [[nodiscard]] auto ParseAsset(ImportSession& session)
    -> co::Co<ParsedGlbAsset>;

  [[nodiscard]] auto CookTextures(
    const ParsedGlbAsset& asset, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto CookBuffers(
    const ParsedGlbAsset& asset, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto EmitMaterials(
    const ParsedGlbAsset& asset, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto EmitScene(
    const ParsedGlbAsset& asset, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto FinalizeSession(ImportSession& session)
    -> co::Co<ImportReport>;
};

} // namespace oxygen::content::import::detail
