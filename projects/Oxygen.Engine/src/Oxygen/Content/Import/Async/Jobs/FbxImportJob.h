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

//! FBX import job orchestrating async pipelines and emitters.
/*!
 Coordinates the FBX import flow within a job-scoped nursery. The job owns the
 per-import session and drives parse, cook, and emit stages with progress
 reporting.

### Architecture Notes

 - Parsing and CPU-heavy work are intended to run on the shared ThreadPool.
 - Cooked resources are emitted through async emitters owned by ImportSession.
 - Actual pipeline integration is introduced in Phase 5.
*/
class FbxImportJob final : public ImportJob {
  OXYGEN_TYPED(FbxImportJob)
public:
  using ImportJob::ImportJob;

private:
  //! Placeholder for parsed FBX scene state.
  struct ParsedFbxScene {
    bool success = true;
  };

  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;

  [[nodiscard]] auto ParseScene(ImportSession& session)
    -> co::Co<ParsedFbxScene>;

  [[nodiscard]] auto CookTextures(
    const ParsedFbxScene& scene, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto CookGeometry(
    const ParsedFbxScene& scene, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto EmitMaterials(
    const ParsedFbxScene& scene, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto EmitScene(
    const ParsedFbxScene& scene, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto FinalizeSession(ImportSession& session)
    -> co::Co<ImportReport>;
};

} // namespace oxygen::content::import::detail
