//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <ostream>
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
#include <Oxygen/Cooker/Import/TextureImportSettings.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::import {

struct ImportSourceAnalysis;

struct ImportManifestJob {
  std::string job_type;
  std::optional<data::SourceKey> source_key {};
  LooseCookedLayout loose_cooked_layout;
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

  //! Declared primary input, available even when preparation fails or is
  //! unsupported.
  OXGN_COOK_NDAPI auto SourcePath() const -> std::filesystem::path;

  OXGN_COOK_NDAPI auto BuildRequest(std::ostream& error_stream) const
    -> std::optional<ImportRequest>;
};

struct ImportManifestDefaults {
  std::optional<data::SourceKey> source_key {};
  LooseCookedLayout loose_cooked_layout;
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

struct ImportManifest {
  uint32_t version = 1;
  std::optional<uint32_t> thread_pool_size;
  std::optional<uint32_t> max_in_flight_jobs;
  std::optional<ImportConcurrency> concurrency;
  ImportManifestDefaults defaults;
  std::vector<ImportManifestJob> jobs;

  //! Discover native dependencies and declared outputs without cooking.
  OXGN_COOK_NDAPI auto AnalyzeSources(std::stop_token stop_token = {}) const
    -> ImportSourceAnalysis;

  OXGN_COOK_NDAPI auto BuildRequests(std::ostream& error_stream) const
    -> std::vector<ImportRequest>;

  //! Load a manifest from a JSON file.
  /*!
   Parses the manifest, validates it against the schema, and returns a
   populated ImportManifest object if successful.

   @param manifest_path Path to the JSON manifest file.
     @param root_override Optional override for the root directory used to
   resolve relative source paths. If unset, source paths are resolved relative
       to the manifest file's parent directory. Relative output paths are
       always resolved relative to the manifest file's parent directory.
   @param error_stream Stream for reporting parsing and validation errors.
   @return A populated ImportManifest on success, or std::nullopt on failure.
  */
  OXGN_COOK_NDAPI static auto Load(const std::filesystem::path& manifest_path,
    const std::optional<std::filesystem::path>& root_override = std::nullopt,
    std::ostream& error_stream = std::cerr) -> std::optional<ImportManifest>;

  //! Parses the same native manifest schema with an explicit path base.
  OXGN_COOK_NDAPI static auto Parse(std::string_view text,
    const std::filesystem::path& manifest_directory,
    const std::optional<std::filesystem::path>& root_override,
    std::ostream& error_stream) -> std::optional<ImportManifest>;
};

} // namespace oxygen::content::import
