//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/BufferContainerImportSettings.h>
#include <Oxygen/Cooker/Import/CollisionShapeDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/GeometryDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/ImportConcurrency.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/InputImportSettings.h>
#include <Oxygen/Cooker/Import/MaterialDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/SceneDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/ScriptImportSettings.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::import {

struct ImportSourceAnalysis;

//! The diagnostic codes of loading a manifest.
/*!
 Each diagnostic's `source_path` is the manifest file (empty for text without
 one) and its `json_pointer` locates the value in that file.
*/
namespace manifest_codes {
  //! The manifest file cannot be read.
  inline constexpr std::string_view kReadFailed = "manifest.read_failed";
  //! The manifest text is not JSON.
  inline constexpr std::string_view kInvalidJson = "manifest.invalid_json";
  //! One issue the manifest schema finds, at the pointer of the offending
  //! value; an object that holds a key its job type does not take is the
  //! offending value, not the key.
  inline constexpr std::string_view kSchemaValidationFailed
    = "manifest.schema_validation_failed";
  //! The schema validator itself fails.
  inline constexpr std::string_view kSchemaValidatorFailure
    = "manifest.schema_validator_failure";
  //! A batch has no manifest.
  inline constexpr std::string_view kBatchEmpty = "manifest.batch_empty";
  //! Two jobs of a manifest or of a batch share an id.
  inline constexpr std::string_view kJobIdDuplicate
    = "manifest.job_id_duplicate";
  //! A `depends_on` entry names no job.
  inline constexpr std::string_view kDependencyMissing
    = "manifest.dependency_missing";
  //! A job depends on itself.
  inline constexpr std::string_view kDependencySelf
    = "manifest.dependency_self";
  //! Jobs depend on each other in a cycle; reported once per cycle.
  inline constexpr std::string_view kDependencyCycle
    = "manifest.dependency_cycle";
  //! A `depends_on` entry is only whitespace.
  inline constexpr std::string_view kDependencyEmpty
    = "manifest.dependency_empty";
  //! A `source_key` is not a canonical UUIDv7.
  inline constexpr std::string_view kSourceKeyInvalid
    = "manifest.source_key_invalid";
  //! A geometry default's LOD policy breaks a rule the schema cannot state.
  inline constexpr std::string_view kLodPolicyInvalid
    = "manifest.lod_policy_invalid";
  //! A geometry default's surface id is the nil id.
  inline constexpr std::string_view kSurfaceIdInvalid
    = "manifest.surface_id_invalid";
  //! A job type that has no sidecar takes `bindings`.
  inline constexpr std::string_view kBindingsNotAllowed
    = "manifest.bindings_not_allowed";
  //! `instance_bindings` without a script sidecar's inline bindings.
  inline constexpr std::string_view kInstanceBindingsNotAllowed
    = "manifest.instance_bindings_not_allowed";
  //! A job that cooks one asset names no `virtual_path`.
  inline constexpr std::string_view kVirtualPathMissing
    = "import.manifest.virtual_path_missing";
  //! A job that cooks several assets names a `virtual_path`.
  inline constexpr std::string_view kVirtualPathNotAllowed
    = "import.manifest.virtual_path_not_allowed";
  //! An input job's `id` is only whitespace.
  inline constexpr std::string_view kInputJobIdMissing
    = "input.manifest.job_id_missing";
} // namespace manifest_codes

//! Where a job of a loaded manifest or batch was declared.
struct ImportManifestJobOrigin final {
  //! The manifest file; empty for text without one.
  std::string source_path;
  //! The job's index in that file's `jobs`.
  std::size_t index = 0;
};

struct ImportManifestJob {
  std::string job_type;
  ImportManifestJobOrigin origin;
  std::optional<data::SourceKey> source_key;
  TextureImportSettings texture;
  SceneImportSettings fbx;
  SceneImportSettings gltf;
  ScriptAssetImportSettings script;
  ScriptingSidecarImportSettings scripting_sidecar;
  PhysicsSidecarImportSettings physics_sidecar;
  InputImportSettings input;
  BufferContainerImportSettings buffer_container;
  MaterialDescriptorImportSettings material_descriptor;
  PhysicsMaterialDescriptorImportSettings physics_material_descriptor;
  CollisionShapeDescriptorImportSettings collision_shape_descriptor;
  GeometryDescriptorImportSettings geometry_descriptor;
  SceneDescriptorImportSettings scene_descriptor;
  std::string id;
  std::vector<std::string> depends_on;

  //! The recorded source: the mount path of the job's source, located through
  //! the manifest's mount table; empty for a sidecar with inline bindings.
  std::string source_mount_path;

  //! The manifest's mount table, which the job's source and the files its
  //! descriptor references are located through.
  std::shared_ptr<const MountTable> mounts;

  //! Identity of the asset a single-asset job cooks; empty for a container.
  /*!
   Required for material, geometry, scene, script, texture, physics material and
   collision shape jobs: their descriptor is written at this virtual path.
  */
  std::string virtual_path;

  //! Declared primary input, available even when preparation fails or is
  //! unsupported.
  OXGN_COOK_NDAPI auto SourcePath() const -> std::filesystem::path;

  //! Builds the job's request; a refusal goes to `diagnostics`, located in the
  //! manifest when it is about the job's settings (see `request_codes`).
  OXGN_COOK_NDAPI auto BuildRequest(std::vector<ImportDiagnostic>& diagnostics,
    std::shared_ptr<const CapturedInputSet> captured_inputs = {}) const
    -> std::optional<ImportRequest>;

  //! Locates in the job's manifest each of `diagnostics` that is about the
  //! job's settings: one with no `source_path`.
  OXGN_COOK_API auto LocateSettingsDiagnostics(
    std::span<ImportDiagnostic> diagnostics) const -> void;

  //! Cooks the job into `cooked_root`, replacing the root it names.
  OXGN_COOK_API auto SetCookedRoot(const std::string& cooked_root) -> void;
};

struct ImportManifestDefaults {
  std::optional<data::SourceKey> source_key;
  TextureImportSettings texture;
  SceneImportSettings fbx;
  SceneImportSettings gltf;
  ScriptAssetImportSettings script;
  ScriptingSidecarImportSettings scripting_sidecar;
  PhysicsSidecarImportSettings physics_sidecar;
  InputImportSettings input;
  BufferContainerImportSettings buffer_container;
  MaterialDescriptorImportSettings material_descriptor;
  PhysicsMaterialDescriptorImportSettings physics_material_descriptor;
  CollisionShapeDescriptorImportSettings collision_shape_descriptor;
  GeometryDescriptorImportSettings geometry_descriptor;
  SceneDescriptorImportSettings scene_descriptor;
};

//! What a command line gives the loading of a manifest.
struct ImportManifestLoadOptions final {
  //! Replaces the manifest's `project_root` (`--root`).
  std::optional<std::filesystem::path> project_root;
  //! The mounts file applied above every `.mounts.json` (`--mounts`).
  std::optional<std::filesystem::path> mounts_file;
  //! Only the engine content build may declare the `Engine` mount.
  MountTableOwner owner = MountTableOwner::kProject;
  //! A resolved table that replaces the manifest's own `mounts`, as when a
  //! model import record validates its job.
  observer_ptr<const MountTable> mount_table;
};

struct ImportManifest {
  uint32_t version = 1;
  //! The canonical project root: relative mount folders and the project's
  //! `.mounts.json` belong to it, and relative job sources resolve against it.
  std::filesystem::path project_root;
  //! The mount table every source of the manifest is located through.
  std::shared_ptr<const MountTable> mounts;
  std::optional<uint32_t> thread_pool_size;
  std::optional<uint32_t> max_in_flight_jobs;
  std::optional<uint32_t> job_admission_capacity;
  //! The most bytes a job reads from one source; the service's default when
  //! unset.
  std::optional<serio::ByteCount> max_source_bytes;
  std::optional<ImportConcurrency> concurrency;
  ImportManifestDefaults defaults;
  std::vector<ImportManifestJob> jobs;

  //! Discover native dependencies and declared outputs without cooking.
  //! Captured inputs preserve logical paths while reading retained private
  //! bytes.
  OXGN_COOK_NDAPI auto AnalyzeSources(const std::stop_token& stop_token = {},
    std::shared_ptr<const CapturedInputSet> captured_inputs = {}) const
    -> ImportSourceAnalysis;

  //! Places cooked context sources beneath every job's own context roots.
  /*!
   Batch-wide sources, such as the engine content pak, resolve references for
   material, scene, geometry and scripting sidecar jobs with lower priority
   than the sources the manifest names.

   @param roots Loose cooked roots or pak files, lowest priority first;
     relative paths resolve against the current directory.
  */
  OXGN_COOK_API auto AddBaseCookedContextRoots(
    std::span<const std::filesystem::path> roots) -> void;

  //! Builds the request of every job the settings of which it accepts.
  OXGN_COOK_NDAPI auto BuildRequests(std::vector<ImportDiagnostic>& diagnostics,
    const std::shared_ptr<const CapturedInputSet>& captured_inputs = {}) const
    -> std::vector<ImportRequest>;

  //! Load a manifest from a JSON file.
  /*!
   Parses the manifest, validates it against the schema, builds its mount
   table and locates every job's source through it.

   The project root is `project_root`, relative to the manifest's folder (by
   default that folder), unless `options.project_root` replaces it. Job sources
   are mount paths (`/Content/...`) or paths relative to the project root that
   lie in a `Source` mount; output and cooked context roots are relative to the
   manifest's folder. Every path is relative: an absolute one fails with
   `path.absolute_committed`, a source in no mount with
   `import.source_outside_mount`.

   The jobs must have unique ids, name only jobs in `depends_on` and not
   depend on each other in a cycle.

   @param manifest_path Path to the JSON manifest file.
   @param options What the command line gives: the root, the mounts file.
   @param diagnostics Receives every diagnostic, each located in the file by
     `manifest_codes` and the codes of the mount contract.
   @return A populated ImportManifest on success, or std::nullopt when any
     diagnostic is an error.
  */
  OXGN_COOK_NDAPI static auto Load(const std::filesystem::path& manifest_path,
    const ImportManifestLoadOptions& options,
    std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<ImportManifest>;

  //! Loads several manifests as one batch with one mount table.
  /*!
   Every manifest is loaded with `options`. Their mount tables may declare the
   same mounts or different ones, never one name for two folders
   (`mount.name_conflict`, before any job runs). The jobs are merged in order;
   a job that cooks the same source to the same identity with the same type as
   an earlier one is cooked once, and the dependencies on it follow the kept
   job. Two different jobs with one id fail (`manifest.job_id_duplicate`). The
   job graph is checked once the jobs are merged; a diagnostic about a job
   names the manifest that declared it and the job's index in it.

   The first manifest gives the batch's scheduling settings.
  */
  OXGN_COOK_NDAPI static auto LoadBatch(
    std::span<const std::filesystem::path> manifest_paths,
    const ImportManifestLoadOptions& options,
    std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<ImportManifest>;

  //! Parses the same native manifest schema with an explicit path base.
  /*!
   The diagnostics have no `source_path`: the text has no file.
  */
  OXGN_COOK_NDAPI static auto Parse(std::string_view text,
    const std::filesystem::path& manifest_directory,
    const ImportManifestLoadOptions& options,
    std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<ImportManifest>;
};

} // namespace oxygen::content::import
