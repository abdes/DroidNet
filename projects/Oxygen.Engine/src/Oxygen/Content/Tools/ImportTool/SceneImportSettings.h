//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace oxygen::content::import::tool {

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

  std::string unit_policy;
  float unit_scale = 1.0F;
  bool unit_scale_set = false;
  bool bake_transforms = true;

  std::string normals_policy;
  std::string tangents_policy;
  std::string node_pruning;
};

} // namespace oxygen::content::import::tool
