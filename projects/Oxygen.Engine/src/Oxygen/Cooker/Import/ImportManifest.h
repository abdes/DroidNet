//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/BufferContainerImportSettings.h>
#include <Oxygen/Cooker/Import/CollisionShapeDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/GeometryDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/ImportConcurrency.h>
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

namespace oxygen::content::import {

struct ImportSourceAnalysis;

struct ImportManifestJob {
  std::string job_type;
  std::optional<data::SourceKey> source_key {};
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

  OXGN_COOK_NDAPI auto BuildRequest(std::ostream& error_stream,
    std::shared_ptr<const CapturedInputSet> captured_inputs = {}) const
    -> std::optional<ImportRequest>;

  //! Cooks the job into `cooked_root`, replacing the root it names.
  OXGN_COOK_API auto SetCookedRoot(const std::string& cooked_root) -> void;
};

struct ImportManifestDefaults {
  std::optional<data::SourceKey> source_key {};
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

  OXGN_COOK_NDAPI auto BuildRequests(std::ostream& error_stream,
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

   @param manifest_path Path to the JSON manifest file.
   @param options What the command line gives: the root, the mounts file.
   @param error_stream Stream for reporting parsing and validation errors.
   @return A populated ImportManifest on success, or std::nullopt on failure.
  */
  OXGN_COOK_NDAPI static auto Load(const std::filesystem::path& manifest_path,
    const ImportManifestLoadOptions& options = {},
    std::ostream& error_stream = std::cerr) -> std::optional<ImportManifest>;

  //! Loads several manifests as one batch with one mount table.
  /*!
   Every manifest is loaded with `options`. Their mount tables may declare the
   same mounts or different ones, never one name for two folders
   (`mount.name_conflict`, before any job runs). The jobs are merged in order;
   a job that cooks the same source to the same identity with the same type as
   an earlier one is cooked once, and the dependencies on it follow the kept
   job. Two different jobs with one id fail (`batch.job_id_duplicate`).

   The first manifest gives the batch's scheduling settings.
  */
  OXGN_COOK_NDAPI static auto LoadBatch(
    std::span<const std::filesystem::path> manifest_paths,
    const ImportManifestLoadOptions& options = {},
    std::ostream& error_stream = std::cerr) -> std::optional<ImportManifest>;

  //! Parses the same native manifest schema with an explicit path base.
  OXGN_COOK_NDAPI static auto Parse(std::string_view text,
    const std::filesystem::path& manifest_directory,
    const ImportManifestLoadOptions& options, std::ostream& error_stream)
    -> std::optional<ImportManifest>;
};

} // namespace oxygen::content::import
