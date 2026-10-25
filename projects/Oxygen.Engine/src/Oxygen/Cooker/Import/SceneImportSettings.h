//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

struct ModelImportRecordContent;

struct SceneImportSettings {
  //! Converts persisted native options to the canonical model-recipe
  //! vocabulary.
  OXGN_COOK_NDAPI static auto FromOptions(const ImportOptions& options)
    -> SceneImportSettings;
  //! Interpret the settings without file I/O. The resulting request has no
  //! cooked destination; execution attaches it.
  OXGN_COOK_NDAPI auto Prepare(ImportFormat expected_format,
    std::ostream& error_stream) const -> std::optional<ImportRequest>;

  std::string source_path;
  //! The model's identity, the virtual path of its source file
  //! (`/Content/Ship/Ship.glb`); its products are named under it.
  std::string virtual_path;
  std::string cooked_root;
  std::string job_name;
  std::string report_path;
  std::string model_record_path;
  std::string recipe_path;
  std::string content_root;
  //! The model import record the settings come from: its source identity,
  //! surface provenance, node continuity and published nodes. Without one,
  //! the import has a fresh identity on every run.
  std::shared_ptr<const ModelImportRecordContent> model_record;
  bool verbose = false;

  bool import_textures = true;
  bool import_materials = true;
  bool import_geometry = true;
  bool import_scene = true;

  bool with_content_hashing = true;

  std::string unit_policy;
  float unit_scale = 1.0F;
  bool unit_scale_set = false;
  bool bake_transforms = false;
  float gltf_omitted_light_range_m = 4096.0F;
  std::string gltf_scene;
  std::string up_axis;
  bool import_lights = true;
  bool import_cameras = true;
  float light_intensity_scale = 1.0F;
  float normals_smoothing_angle_deg = 60.0F;
  std::map<std::string, std::string> material_remap;
  std::map<std::string, GeometryImportDefaults> geometry_defaults;

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
