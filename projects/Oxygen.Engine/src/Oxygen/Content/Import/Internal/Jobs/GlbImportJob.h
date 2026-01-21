//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/Internal/AdapterTypes.h>
#include <Oxygen/Content/Import/Internal/ImportJob.h>

namespace oxygen::content::import {
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::adapters {
class GltfAdapter;
} // namespace oxygen::content::import::adapters

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
    std::shared_ptr<adapters::GltfAdapter> adapter;
    std::vector<ImportDiagnostic> diagnostics;
    bool success = true;
    bool cancelled = false;
  };

  struct PlannedGlbImport;

  struct PlanBuildOutcome {
    std::unique_ptr<PlannedGlbImport> plan;
    std::vector<ImportDiagnostic> diagnostics;
    bool cancelled = false;
  };

  struct ExternalTextureLoadOutcome {
    std::vector<adapters::AdapterInput::ExternalTextureBytes> bytes;
    std::vector<ImportDiagnostic> diagnostics;
    bool cancelled = false;
  };

  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;

  [[nodiscard]] auto ParseAsset(ImportSession& session)
    -> co::Co<ParsedGlbAsset>;

  [[nodiscard]] auto BuildPlan(ParsedGlbAsset& asset,
    const ImportRequest& request, std::stop_token stop_token,
    std::span<const adapters::AdapterInput::ExternalTextureBytes>
      external_texture_bytes) -> PlanBuildOutcome;

  [[nodiscard]] auto LoadExternalTextureBytes(ParsedGlbAsset& asset,
    ImportSession& session) -> co::Co<ExternalTextureLoadOutcome>;

  [[nodiscard]] auto ExecutePlan(PlannedGlbImport& plan, ImportSession& session)
    -> co::Co<bool>;

  [[nodiscard]] auto FinalizeSession(ImportSession& session)
    -> co::Co<ImportReport>;
};

} // namespace oxygen::content::import::detail
