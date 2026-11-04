//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <stop_token>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/ImportJob.h>
#include <Oxygen/Cooker/Import/Internal/ModelSettings.h>
#include <Oxygen/Serio/ByteBuffer.h>

namespace oxygen::content::import {
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::adapters {
class GltfAdapter;
class FbxAdapter;
} // namespace oxygen::content::import::adapters

namespace oxygen::content::import::detail {

//! The plan of one model import; it does not depend on the format.
struct PlannedModelImport;

//! A parsed model's adapter, one of the formats a model job reads.
using ModelAdapter = std::variant<std::shared_ptr<adapters::GltfAdapter>,
  std::shared_ptr<adapters::FbxAdapter>>;

enum class ModelFormatKind : uint8_t {
  kGltf,
  kFbx,
};

//! What differs between model formats: the adapter and the text naming it.
struct ModelFormat final {
  ModelFormatKind kind = ModelFormatKind::kGltf;
  std::string_view display_name;
  std::string_view read_failed_code;
  std::string_view read_failed_message;
};

//! Model import job orchestrating async pipelines and emitters.
/*!
 Coordinates one model import (glTF/GLB or FBX, named by its `ModelFormat`)
 within a job-scoped nursery. The job reads the source through the job's file
 reader, then drives parse, plan, cook, and emit stages with progress reporting.

### Architecture Notes

 - Parsing and plan building run on the ThreadPool.
 - Cooked resources are emitted through async emitters owned by ImportSession.
*/
class ModelImportJob : public ImportJob {
protected:
  ModelImportJob(ImportJobParams params, const ModelFormat& format);

private:
  //! Parsed model and its retained input bytes.
  struct ParsedModel {
    ModelAdapter adapter;
    std::shared_ptr<serio::ByteBuffer> source_bytes;
    std::vector<ImportDiagnostic> diagnostics;
    bool success = true;
    bool canceled = false;
  };

  struct PlanBuildOutcome {
    std::unique_ptr<PlannedModelImport> plan;
    std::vector<ImportDiagnostic> diagnostics;
    bool canceled = false;
  };

  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;
  [[nodiscard]] auto ExecuteSessionAsync(ImportSession& session)
    -> co::Co<ImportReport>;

  [[nodiscard]] auto ParseSource(ImportSession& session) -> co::Co<ParsedModel>;

  [[nodiscard]] auto BuildPlan(ParsedModel& model, const ImportRequest& request,
    const internal::MaterialRemapKeys& remap, const std::stop_token& stop_token)
    -> PlanBuildOutcome;

  [[nodiscard]] auto ExecutePlan(
    PlannedModelImport& plan, ImportSession& session) -> co::Co<bool>;

  [[nodiscard]] auto FinalizeSession(ImportSession& session)
    -> co::Co<ImportReport>;

  ModelFormat format_;
};

//! Import job for glTF and GLB sources.
class GlbImportJob final : public ModelImportJob {
  OXYGEN_TYPED(GlbImportJob)
public:
  explicit GlbImportJob(ImportJobParams params)
    : ModelImportJob(std::move(params),
        ModelFormat {
          .kind = ModelFormatKind::kGltf,
          .display_name = "glTF",
          .read_failed_code = import_codes::kGltfReadFailed,
          .read_failed_message = "Failed to read glTF source bytes",
        })
  {
  }
};

//! Import job for FBX sources.
class FbxImportJob final : public ModelImportJob {
  OXYGEN_TYPED(FbxImportJob)
public:
  explicit FbxImportJob(ImportJobParams params)
    : ModelImportJob(std::move(params),
        ModelFormat {
          .kind = ModelFormatKind::kFbx,
          .display_name = "FBX",
          .read_failed_code = import_codes::kFbxReadFailed,
          .read_failed_message = "Failed to read FBX source bytes",
        })
  {
  }
};

} // namespace oxygen::content::import::detail
