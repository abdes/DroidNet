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
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

namespace detail {
  class ImportJob;
}

class SurfaceProvenance;
struct SceneImportSettings;

//! The content of a model import record. The model's identity is not part of
//! it: it is where the record lives (`ModelImportRecord::IdentityOf`).
struct ModelImportRecordContent final {
  //! The primary source file, absolute.
  std::filesystem::path source;
  //! The primary source's mount path, such as
  //! `/Content/SourceMedia/DCC/Ship.glb`. The record's `source` is the
  //! reference that names it from the record.
  std::string source_mount_path;
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
  //! False when the record is new: `record_path` is the record of the query's
  //! identity (`ModelImportRecord::RecordPathOf`).
  bool exists = false;
  //! What the lookup decided and the hints it found, such as
  //! `import.source_rebound` or `import.source_copy`, and each file it skipped
  //! as not a valid record (`import.model_record_unreadable`).
  std::vector<ImportDiagnostic> diagnostics;
};

//! The model LocateRecord looks for: imported from `source` as `identity`.
struct ModelImportRecordQuery final {
  std::filesystem::path source;
  //! The model's identity, such as `/Content/Models/Ship.glb`.
  std::string identity;
  //! The mount table the records are found and their sources located
  //! through; never null.
  observer_ptr<const MountTable> mounts;
};

//! The folder a caller owns where record-only imports publish generations:
//! `<root>/<source_identity>/`, the selected one named by its `head.json`.
struct ModelImportGenerations final {
  std::filesystem::path root;
};

//! What `Save` writes into a model import record.
struct ModelImportRecordSave final {
  //! The record, at its model's identity (`ModelImportRecord::RecordPathOf`).
  std::filesystem::path record_path;
  //! The primary source file; empty keeps the record's.
  std::filesystem::path source;
  //! Absent keeps the record's settings.
  std::optional<std::string> settings_json;
  //! The mount table the record and its source are located through; never
  //! null.
  observer_ptr<const MountTable> mounts;
};

//! A model's import state, in one file: its model import record.
/*!
 The record is the model's authored file: it lives in the model's mount at the
 model's identity with the suffix `.import.json`, so the model
 `/Content/Models/Ship.glb` has the record `Models/Ship.glb.import.json` in the
 `Content` mount's folder (`oxygen.model-import.schema.json`). The identity is
 where the record lives; the record never repeats it. It holds what to import
 (the source, the settings) and what earlier imports established (the source
 identity, the surface provenance, the node continuity, the published nodes,
 the source fingerprint). Its `source` is a file reference from the record:
 relative within the model's mount (`Ship.glb` beside it), the mount path of a
 file in another mount otherwise. The source identity, a UUIDv7 the record
 holds, never changes; moving a record is a relocation, never a save.

 This class is the record's one reader and writer, and the publisher of
 record-only imports: one isolated attempt that publishes an immutable
 generation and commits the import's new record content with it. Generations
 live in a generations folder the caller owns (`ModelImportGenerations`). A
 project whose outputs another publisher owns, such as an editor project,
 keeps its generations out of this store.
*/
class ModelImportRecord final {
public:
  //! The `schema_version` of a record.
  static constexpr uint32_t kSchemaVersion = 1;
  //! The suffix of a record's file name.
  static constexpr std::string_view kRecordSuffix = ".import.json";
  //! The conventional name of a generations folder in a host's own output
  //! folder, such as `<project>/.cooked/ModelGenerations`.
  static constexpr std::string_view kModelGenerationsFolder
    = "ModelGenerations";

  //! The record of the model `identity`: `<identity>.import.json` in the
  //! identity's mount folder.
  /*!
   @param findings Receives the `vpath.*` code of an identity the engine
     refuses, and what `MountTable::LocateSource` reports for an identity in
     no bound `Source` mount.
   @return The record's path, which need not exist, or nothing.
  */
  OXGN_COOK_NDAPI static auto RecordPathOf(const MountTable& mounts,
    std::string_view identity, std::vector<ImportDiagnostic>& findings)
    -> std::optional<std::filesystem::path>;

  //! The identity of the model whose record is `record_path`: the record's
  //! mount path without `.import.json`.
  /*!
   @param findings Receives `import.model_record_misnamed` for a file not
     named `*.import.json`, `import.source_outside_mount` for one in no
     `Source` mount, and the `vpath.*` code of an identity the engine refuses.
   @return The identity, or nothing.
  */
  OXGN_COOK_NDAPI static auto IdentityOf(const MountTable& mounts,
    const std::filesystem::path& record_path,
    std::vector<ImportDiagnostic>& findings) -> std::optional<std::string>;

  //! Reads and validates a record: its location, schema, source, settings,
  //! provenance and continuity.
  /*!
   `source` is a file reference from the record, located through
   `MountTable::LocateReference`, whose codes a source it refuses fails
   with.
  */
  OXGN_COOK_NDAPI static auto Read(const std::filesystem::path& record_path,
    const MountTable& mounts) -> ModelImportRecordContent;

  //! The JSON text of the record of the model `identity`; `source` is spelled
  //! from the record (`content::RelativeReference`). A source the reference
  //! grammar cannot name throws std::invalid_argument.
  OXGN_COOK_NDAPI static auto Serialize(const ModelImportRecordContent& content,
    std::string_view identity) -> std::string;

  //! What a successful model import from its record adds to the record, in
  //! the record's own fields: `surface_provenance`, `published_nodes` and,
  //! once its reads were verified, `source_fingerprint`. Empty for any other
  //! import. Each publisher commits it with its own publication.
  OXGN_COOK_NDAPI static auto SerializeImportUpdate(
    const ImportRequest& request, const ImportReport& report) -> std::string;

  //! The import manifest job of the record of the model `identity`: a gltf or
  //! fbx job, by the source's extension, with the record's settings and
  //! source.
  OXGN_COOK_NDAPI static auto MakeJob(const ModelImportRecordContent& content,
    std::string_view identity) -> std::string;

  //! The settings of `settings`, in the record's vocabulary.
  OXGN_COOK_NDAPI static auto MakeSettings(const SceneImportSettings& settings)
    -> std::string;

  //! Finds the record of a model imported from `source` as `identity`, among
  //! the records of the bound `Source` mounts.
  /*!
   The first step that matches decides: the record at the identity (its
   source rebound to this file when another, `import.source_rebound`); a
   record whose source is this file means a new model
   (`import.source_has_model`); a record whose source is missing and whose
   fingerprint equals this file's is that model, rebound to this file under
   its own identity (`import.source_relocated`, or
   `import.source_relocation_ambiguous` when several match); otherwise a new
   record, with the hints `import.source_copy` and
   `import.source_relocation_candidate`. Reads only; `Save` writes.
  */
  OXGN_COOK_NDAPI static auto LocateRecord(const ModelImportRecordQuery& query)
    -> ModelImportRecordLocation;

  //! Creates or updates a record at its model's identity, keeping everything
  //! earlier imports established. A new record gets a new source identity.
  //! Refuses a second record with the same source identity.
  OXGN_COOK_API static auto Save(const ModelImportRecordSave& save) -> void;

  //! Saves the settings and source of a single-job gltf or fbx manifest. Its
  //! source is a mount path or a path relative to the project root, located
  //! through `save.mounts`, as any manifest's, and its virtual path is the
  //! model's identity; validation precedes any write. Without a record path,
  //! LocateRecord finds the record of the job's source and identity; with one,
  //! the job names that record's identity. Returns the record written.
  OXGN_COOK_API static auto SaveRecipeFile(const ModelImportRecordSave& save,
    const std::filesystem::path& recipe_path, ImportFormat expected_format)
    -> ModelImportRecordLocation;

  //! Reads a baseline and reserves a private generation in `generations`.
  //! No source bytes or prior cooked output are copied. Concurrent attempts
  //! have distinct roots. A missing source is `import.source_missing`; an
  //! empty generations root throws std::invalid_argument. `notes` are the
  //! lookup's diagnostics, added to the attempt's report.
  OXGN_COOK_NDAPI static auto Prepare(const std::filesystem::path& record_path,
    const ModelImportGenerations& generations, const MountTable& mounts,
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

  //! Removes the record's unselected generations in `generations` whose
  //! writer/reader leases have expired. Call on a worker; selected
  //! generations and active attempts are retained. Refuses a record whose
  //! source identity another record of `mounts` has.
  OXGN_COOK_NDAPI static auto ReclaimUnusedGenerations(
    const std::filesystem::path& record_path,
    const ModelImportGenerations& generations, const MountTable& mounts)
    -> size_t;

  //! Resolves the record's selected immutable root in `generations`
  //! without creating another attempt.
  OXGN_COOK_NDAPI static auto SelectedGeneration(
    const std::filesystem::path& record_path,
    const ModelImportGenerations& generations)
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
