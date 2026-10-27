//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

namespace detail {
  class ImportJob;
}

class SurfaceProvenance;
struct SceneImportSettings;

//! The content of a model import record.
struct ModelImportRecordContent final {
  //! The primary source file, absolute.
  std::filesystem::path source;
  //! The model's identity, under which its products are named.
  std::string virtual_path;
  //! The model import settings, a JSON object in the vocabulary of a gltf or
  //! fbx import manifest job.
  std::string settings_json = "{}";
  Uuid source_identity;
  //! Never null once read; carries the source identity.
  std::shared_ptr<const SurfaceProvenance> surface_provenance;
  std::vector<ModelNodeContinuity> node_continuity;
  std::vector<ModelPublishedNode> published_nodes;
  //! The published nodes the publication before the last did not emit.
  std::vector<ModelPublishedNode> new_nodes;
  //! The nodes earlier publications emitted whose ID the last did not.
  std::vector<ModelPublishedNode> vanished_nodes;
  //! Absent until the record's first publication.
  std::optional<ModelSourceFingerprint> source_fingerprint;
};

//! The record LocateRecord found or chose for a source.
struct ModelImportRecordLocation final {
  std::filesystem::path record_path;
  //! False when the record is new: `record_path` names it by a fresh source
  //! identity.
  bool exists = false;
  //! What the lookup decided and the hints it found, such as
  //! `import.source_rebound` or `import.source_copy`, and each file it skipped
  //! as not a valid record (`import.model_record_unreadable`).
  std::vector<ImportDiagnostic> diagnostics;
};

//! The model LocateRecord looks for: imported from `source` as
//! `virtual_path`.
struct ModelImportRecordQuery final {
  std::filesystem::path content_root;
  std::filesystem::path source;
  std::string virtual_path;
};

//! What `Save` writes into a model import record.
struct ModelImportRecordSave final {
  std::filesystem::path record_path;
  std::filesystem::path content_root;
  //! The primary source file; empty keeps the record's.
  std::filesystem::path source;
  //! Empty keeps the record's.
  std::string virtual_path;
  //! Absent keeps the record's settings.
  std::optional<std::string> settings_json;
};

//! A model's import state, in one file: its model import record.
/*!
 The record lives at `<content_root>/imports/<source_identity>.import.json`
 (`oxygen.model-import.schema.json`). It holds what to import (the source, the
 model's virtual path, the settings) and what earlier imports established (the
 source identity, the surface provenance, the node continuity, the published
 nodes, the source fingerprint). The source identity never changes, so a
 record is never renamed: relocating a model rewrites its `source`.

 This class is the record's one reader and writer, and the ImportTool's
 publisher: one isolated attempt that publishes an immutable generation and
 commits the import's new record content with it. The selected generation is
 kept in `<content_root>/.cooked/imports/<source_identity>/head.json`.
*/
class ModelImportRecord final {
public:
  //! The `schema_version` of a record.
  static constexpr uint32_t kSchemaVersion = 1;
  //! The folder of a content root that holds its records.
  static constexpr std::string_view kRecordsFolder = "imports";
  //! The suffix of a record's file name.
  static constexpr std::string_view kRecordSuffix = ".import.json";

  //! Reads and validates a record: its schema, settings, provenance and
  //! continuity, and that its file name is its source identity.
  OXGN_COOK_NDAPI static auto Read(const std::filesystem::path& record_path)
    -> ModelImportRecordContent;

  //! The record's JSON text; `source` is written relative to the record's
  //! folder when it is inside the content root.
  OXGN_COOK_NDAPI static auto Serialize(const ModelImportRecordContent& content,
    const std::filesystem::path& record_path) -> std::string;

  //! What a successful model import from its record adds to the record, in
  //! the record's own fields: `surface_provenance`, `published_nodes` and,
  //! once its reads were verified, `source_fingerprint`. Empty for any other
  //! import. Each publisher commits it with its own publication.
  OXGN_COOK_NDAPI static auto SerializeImportUpdate(
    const ImportRequest& request, const ImportReport& report) -> std::string;

  //! The import manifest job of the record: a gltf or fbx job, by the
  //! source's extension, with the record's settings, source and virtual path.
  OXGN_COOK_NDAPI static auto MakeJob(const ModelImportRecordContent& content)
    -> std::string;

  //! The settings of `settings`, in the record's vocabulary.
  OXGN_COOK_NDAPI static auto MakeSettings(const SceneImportSettings& settings)
    -> std::string;

  //! The path of the record of `source_identity`.
  OXGN_COOK_NDAPI static auto PathFor(const std::filesystem::path& content_root,
    const Uuid& source_identity) -> std::filesystem::path;

  //! Finds the record of a model imported from `source` as `virtual_path`.
  /*!
   The first step that matches decides: a record with the virtual path (its
   source rebound to this file when another, `import.source_rebound`); a
   record whose source is this file under another virtual path means a new
   model (`import.source_has_model`); a record whose source is missing and
   whose fingerprint equals this file's (`import.source_relocated`, or
   `import.source_relocation_ambiguous` when several match); otherwise a new
   record, with the hints `import.source_copy` and
   `import.source_relocation_candidate`. Reads only; `Save` writes.
  */
  OXGN_COOK_NDAPI static auto LocateRecord(const ModelImportRecordQuery& query)
    -> ModelImportRecordLocation;

  //! Creates or updates a record, keeping its identity and everything earlier
  //! imports established. A new record takes its identity from its file
  //! name. Refuses a second record with the same virtual path.
  OXGN_COOK_API static auto Save(const ModelImportRecordSave& save) -> void;

  //! Saves the settings, source and virtual path of a single-job gltf or fbx
  //! manifest. Relative sources resolve beside that manifest; validation
  //! precedes any write. Without a record path, LocateRecord finds the record
  //! of the job's source and virtual path. Returns the record written.
  OXGN_COOK_API static auto SaveRecipeFile(const ModelImportRecordSave& save,
    const std::filesystem::path& recipe_path, ImportFormat expected_format)
    -> ModelImportRecordLocation;

  //! Reads a baseline and reserves a private generation. No source bytes or
  //! prior cooked output are copied. Concurrent attempts have distinct roots.
  //! A missing source is `import.source_missing`. `notes` are the lookup's
  //! diagnostics, added to the attempt's report.
  OXGN_COOK_NDAPI static auto Prepare(const std::filesystem::path& record_path,
    std::vector<ImportDiagnostic> notes = {})
    -> std::shared_ptr<ModelImportRecord>;

  OXGN_COOK_API ~ModelImportRecord();
  OXYGEN_MAKE_NON_COPYABLE(ModelImportRecord)
  OXYGEN_MAKE_NON_MOVABLE(ModelImportRecord)

  OXGN_COOK_NDAPI auto Request() const -> const ImportRequest&;
  OXGN_COOK_NDAPI auto PreviousGeneration() const
    -> std::optional<std::filesystem::path>;
  OXGN_COOK_NDAPI auto Notes() const -> std::span<const ImportDiagnostic>;

  //! Performs native root and provenance validation before the short commit
  //! section. Call from the import worker pool, never a rendering callback.
  OXGN_COOK_API auto ValidateCandidate(const ImportReport& report) -> void;

  //! Atomically commits the import's surface provenance, published nodes and
  //! source fingerprint, and selects the validated candidate, if the record
  //! still matches the baseline. Throws without changing the record or the
  //! selected generation on validation, cancellation, lock or baseline
  //! failure.
  OXGN_COOK_API auto Publish(
    ImportReport& report, std::stop_token stop_token = {}) -> void;

  //! Removes unselected generations whose writer/reader leases have expired.
  //! Call on a worker; selected generations and active attempts are retained.
  OXGN_COOK_NDAPI static auto ReclaimUnusedGenerations(
    const std::filesystem::path& record_path) -> size_t;

  //! Resolves the selected immutable root without creating another attempt.
  OXGN_COOK_NDAPI static auto SelectedGeneration(
    const std::filesystem::path& record_path)
    -> std::optional<std::filesystem::path>;

private:
  friend class AsyncImportService;
  friend class detail::ImportJob;
  auto ClaimSubmission() -> void;
  auto ReclaimUnusedGenerations() const -> size_t;
  [[nodiscard]] auto AllowsWriting(const std::filesystem::path& root) const
    -> bool;
  struct State;
  explicit ModelImportRecord(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};

} // namespace oxygen::content::import
