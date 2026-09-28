//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>

namespace oxygen::content::import {
namespace {

  auto RecipeSchema() -> const nlohmann::json&
  {
    static const auto schema = nlohmann::json::parse(kImportManifestSchema);
    return schema;
  }

  template <typename Value, typename Parser>
  auto TokenFor(const Value value, const char* definition, Parser parse)
    -> std::string
  {
    for (const auto& candidate :
      RecipeSchema().at("definitions").at(definition).at("enum")) {
      const auto token = candidate.template get<std::string>();
      const auto parsed = parse(token);
      if (parsed && parsed.value() == value) {
        return token;
      }
    }
    throw std::invalid_argument(
      std::string("Unsupported model recipe option: ") + definition);
  }

  auto GeometryPolicy(const GeometryAttributePolicy policy) -> std::string
  {
    switch (policy) {
    case GeometryAttributePolicy::kNone:
      return "none";
    case GeometryAttributePolicy::kPreserveIfPresent:
      return "preserve";
    case GeometryAttributePolicy::kGenerateMissing:
      return "generate";
    case GeometryAttributePolicy::kAlwaysRecalculate:
      return "recalculate";
    }
    throw std::invalid_argument("Invalid geometry attribute policy");
  }

  auto TextureSettings(const ImportOptions::TextureTuning& tuning)
    -> TextureImportSettings
  {
    TextureImportSettings settings;
    if (!tuning.enabled) {
      return settings;
    }
    if (tuning.placeholder_on_failure) {
      throw std::invalid_argument(
        "Retained model imports require successful texture cooking");
    }
    settings.intent
      = TokenFor(tuning.intent, "texture_intent", internal::ParseIntent);
    settings.color_space = TokenFor(
      tuning.source_color_space, "color_space", internal::ParseColorSpace);
    settings.output_format = TokenFor(
      tuning.color_output_format, "texture_format", internal::ParseFormat);
    settings.data_format = TokenFor(
      tuning.data_output_format, "texture_format", internal::ParseFormat);
    settings.mip_policy
      = TokenFor(tuning.mip_policy, "mip_policy", internal::ParseMipPolicy);
    settings.mip_filter
      = TokenFor(tuning.mip_filter, "mip_filter", internal::ParseMipFilter);
    settings.bc7_quality
      = TokenFor(tuning.bc7_quality, "bc7_quality", internal::ParseBc7Quality);
    settings.hdr_handling = TokenFor(
      tuning.hdr_handling, "hdr_handling", internal::ParseHdrHandling);
    settings.max_mip_levels = tuning.max_mip_levels;
    settings.packing_policy = tuning.packing_policy_id;
    settings.exposure_ev = tuning.exposure_ev;
    settings.flip_y = tuning.flip_y_on_decode;
    settings.force_rgba = tuning.force_rgba_on_decode;
    settings.flip_normal_green = tuning.flip_normal_green;
    settings.renormalize_normals = tuning.renormalize_normals_in_mips;
    settings.bake_hdr_to_ldr = tuning.bake_hdr_to_ldr;
    settings.cubemap = tuning.import_cubemap;
    settings.equirect_to_cube = tuning.equirect_to_cubemap;
    settings.cube_face_size = tuning.cubemap_face_size;
    if (tuning.cubemap_layout != CubeMapImageLayout::kUnknown) {
      settings.cube_layout = TokenFor(
        tuning.cubemap_layout, "cube_layout", internal::ParseCubeLayout);
    }
    return settings;
  }

} // namespace

auto SceneImportSettings::FromOptions(const ImportOptions& options,
  std::string naming_policy) -> SceneImportSettings
{
  if (options.naming_strategy) {
    throw std::invalid_argument(
      "Model recipe conversion requires an explicit naming policy");
  }
  SceneImportSettings settings;
  settings.naming_policy = std::move(naming_policy);
  settings.normals_policy = GeometryPolicy(options.normal_policy);
  settings.tangents_policy = GeometryPolicy(options.tangent_policy);
  switch (options.node_pruning) {
  case NodePruningPolicy::kKeepAll:
    settings.node_pruning = "keep";
    break;
  case NodePruningPolicy::kDropEmptyNodes:
    settings.node_pruning = "drop-empty";
    break;
  default:
    throw std::invalid_argument("Invalid node pruning policy");
  }
  switch (options.scene_content_policy) {
  case SceneContentPolicy::kStaticScalar:
    settings.content_policy = "static-scalar";
    break;
  case SceneContentPolicy::kDefault:
    settings.content_policy = "default";
    break;
  default:
    throw std::invalid_argument("Invalid scene content policy");
  }
  settings.with_content_hashing = options.with_content_hashing;
  settings.bake_transforms = options.coordinate.bake_transforms_into_meshes;
  settings.gltf_omitted_light_range_m = options.gltf_omitted_light_range_m;
  switch (options.coordinate.unit_normalization) {
  case UnitNormalizationPolicy::kNormalizeToMeters:
    settings.unit_policy = "normalize";
    break;
  case UnitNormalizationPolicy::kPreserveSource:
    settings.unit_policy = "preserve";
    break;
  case UnitNormalizationPolicy::kApplyCustomFactor:
    settings.unit_policy = "custom";
    settings.unit_scale = options.coordinate.unit_scale;
    settings.unit_scale_set = true;
    break;
  default:
    throw std::invalid_argument("Invalid unit normalization policy");
  }
  if ((options.import_content & ~ImportContentFlags::kAll)
    != ImportContentFlags::kNone) {
    throw std::invalid_argument("Invalid model import content flags");
  }
  const auto selected = [&](ImportContentFlags flag) -> bool {
    return (options.import_content & flag) != ImportContentFlags::kNone;
  };
  settings.import_textures = selected(ImportContentFlags::kTextures);
  settings.import_materials = selected(ImportContentFlags::kMaterials);
  settings.import_geometry = selected(ImportContentFlags::kGeometry);
  settings.import_scene = selected(ImportContentFlags::kScene);
  settings.texture_defaults = TextureSettings(options.texture_tuning);
  for (const auto& [name, tuning] : options.texture_overrides) {
    settings.texture_overrides.emplace(name, TextureSettings(tuning));
  }
  return settings;
}

} // namespace oxygen::content::import
