//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <Oxygen/Content/Import/ImportConcurrency.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/SceneImportSettings.h>
#include <Oxygen/Content/Import/TextureImportSettings.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

struct ImportManifestJob {
  std::string job_type;
  TextureImportSettings texture;
  SceneImportSettings fbx;
  SceneImportSettings gltf;

  OXGN_CNTT_NDAPI auto BuildRequest(std::ostream& error_stream) const
    -> std::optional<ImportRequest>;
};

struct ImportManifestDefaults {
  TextureImportSettings texture;
  SceneImportSettings fbx;
  SceneImportSettings gltf;
};

struct ImportManifest {
  uint32_t version = 1;
  std::optional<uint32_t> thread_pool_size;
  std::optional<uint32_t> max_in_flight_jobs;
  std::optional<ImportConcurrency> concurrency;
  ImportManifestDefaults defaults;
  std::vector<ImportManifestJob> jobs;

  OXGN_CNTT_NDAPI auto BuildRequests(std::ostream& error_stream) const
    -> std::vector<ImportRequest>;

  //! Load a manifest from a JSON file.
  /*!
   Parses the manifest, validates it against the schema, and returns a
   populated ImportManifest object if successful.

   @param manifest_path Path to the JSON manifest file.
   @param root_override Optional override for the root directory used to resolve
          relative source paths. If unset, paths are resolved relative to the
          manifest file's parent directory.
   @param error_stream Stream for reporting parsing and validation errors.
   @return A populated ImportManifest on success, or std::nullopt on failure.
  */
  OXGN_CNTT_NDAPI static auto Load(const std::filesystem::path& manifest_path,
    const std::optional<std::filesystem::path>& root_override = std::nullopt,
    std::ostream& error_stream = std::cerr) -> std::optional<ImportManifest>;
};

} // namespace oxygen::content::import
