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

#include <Oxygen/Content/Tools/ImportTool/SceneImportSettings.h>
#include <Oxygen/Content/Tools/ImportTool/TextureImportSettings.h>

namespace oxygen::content::import::tool {

struct ImportManifestJob {
  std::string job_type;
  TextureImportSettings texture;
  SceneImportSettings fbx;
  SceneImportSettings gltf;
};

struct ImportManifestDefaults {
  std::string job_type;
  TextureImportSettings texture;
  SceneImportSettings fbx;
  SceneImportSettings gltf;
};

struct ImportManifest {
  uint32_t version = 1;
  ImportManifestDefaults defaults;
  std::vector<ImportManifestJob> jobs;
};

class ImportManifestLoader {
public:
  [[nodiscard]] static auto Load(const std::filesystem::path& manifest_path,
    const std::optional<std::filesystem::path>& root_override,
    std::ostream& error_stream) -> std::optional<ImportManifest>;
};

} // namespace oxygen::content::import::tool
