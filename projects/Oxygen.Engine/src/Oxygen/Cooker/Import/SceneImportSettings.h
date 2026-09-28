//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <map>
#include <string>

#include <Oxygen/Cooker/Import/TextureImportSettings.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

struct ImportOptions;

struct SceneImportSettings {
  //! Converts persisted native options to the canonical model-recipe
  //! vocabulary. Naming policy is explicit because arbitrary strategy objects
  //! are not recipes.
  OXGN_COOK_NDAPI static auto FromOptions(const ImportOptions& options,
    std::string naming_policy) -> SceneImportSettings;
  std::string source_path;
  std::string cooked_root;
  std::string job_name;
  std::string report_path;
  std::string retained_record_path;
  std::string recipe_path;
  std::string content_root;
  //! Retained UUIDv7 namespace for native material-slot allocation.
  std::string material_slot_source_identity;
  //! Retained provenance supplied by a manifest or an API caller.
  std::string material_slot_provenance_json;
  //! Optional CLI input file, mutually exclusive with inline provenance.
  std::string material_slot_provenance_path;
  bool verbose = false;

  bool import_textures = true;
  bool import_materials = true;
  bool import_geometry = true;
  bool import_scene = true;

  bool with_content_hashing = true;

  //! Optional source validation policy: default or static-scalar.
  std::string content_policy;

  std::string unit_policy;
  float unit_scale = 1.0F;
  bool unit_scale_set = false;
  bool bake_transforms = true;
  float gltf_omitted_light_range_m = 4096.0F;

  std::string normals_policy;
  std::string tangents_policy;
  std::string node_pruning;
  std::string naming_policy;

  //! Default tuning for all textures in the scene.
  TextureImportSettings texture_defaults;

  //! Overrides for specific textures found in the scene file.
  //! The key is the original filename or path as authored in the scene.
  std::map<std::string, TextureImportSettings> texture_overrides;
};

} // namespace oxygen::content::import
