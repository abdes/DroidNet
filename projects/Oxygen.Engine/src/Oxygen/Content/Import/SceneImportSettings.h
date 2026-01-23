//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <map>
#include <string>

#include <Oxygen/Content/Import/TextureImportSettings.h>

namespace oxygen::content::import {

struct SceneImportSettings {
  std::string source_path;
  std::string cooked_root;
  std::string job_name;
  std::string report_path;
  bool verbose = false;

  bool import_textures = true;
  bool import_materials = true;
  bool import_geometry = true;
  bool import_scene = true;

  bool with_content_hashing = true;

  std::string unit_policy;
  float unit_scale = 1.0F;
  bool unit_scale_set = false;
  bool bake_transforms = true;

  std::string normals_policy;
  std::string tangents_policy;
  std::string node_pruning;

  //! Default tuning for all textures in the scene.
  TextureImportSettings texture_defaults;

  //! Overrides for specific textures found in the scene file.
  //! The key is the original filename or path as authored in the scene.
  std::map<std::string, TextureImportSettings> texture_overrides;
};

} // namespace oxygen::content::import
