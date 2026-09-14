//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <set>
#include <span>
#include <string>

#include <Oxygen/Cooker/Import/Internal/StaticScalarSourceValidation.h>
#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>
#include <Oxygen/Cooker/Import/Internal/gltf/cgltf.h>

namespace oxygen::content::import::internal {
namespace {

  auto Reject(const bool present, const std::string_view feature,
    const std::string_view source_path, const std::string_view object_path,
    std::vector<ImportDiagnostic>& diagnostics) -> void
  {
    if (present) {
      diagnostics.push_back({ .severity = ImportSeverity::kError,
        .code = "import.static_scalar.unsupported",
        .message
        = "Static/scalar import does not support " + std::string(feature) + ".",
        .source_path = std::string(source_path),
        .object_path = std::string(object_path) });
    }
  }

  auto HasAuthoredProperty(const ufbx_props& properties, const char* name)
    -> bool
  {
    const auto* property = ufbx_find_prop(&properties, name);
    return property != nullptr
      && (property->flags
           & (UFBX_PROP_FLAG_NOT_FOUND | UFBX_PROP_FLAG_SYNTHETIC))
      == 0;
  }

} // namespace

auto ValidateStaticScalarSource(const cgltf_data& source,
  const std::string_view source_path,
  std::vector<ImportDiagnostic>& diagnostics) -> bool
{
  const auto previous_count = diagnostics.size();
  Reject(source.animations_count != 0U, "animation", source_path, "/animations",
    diagnostics);
  Reject(
    source.skins_count != 0U, "skinning", source_path, "/skins", diagnostics);
  Reject(source.textures_count != 0U || source.images_count != 0U,
    "texture-bearing materials", source_path, "/textures", diagnostics);
  for (const auto& [index, camera] :
    std::views::enumerate(std::span(source.cameras, source.cameras_count))) {
    Reject(camera.type != cgltf_camera_type_perspective,
      "non-perspective camera components", source_path,
      "/cameras/" + std::to_string(index), diagnostics);
  }
  for (const auto& [index, light] :
    std::views::enumerate(std::span(source.lights, source.lights_count))) {
    Reject(light.type != cgltf_light_type_directional,
      "non-directional light components", source_path,
      "/extensions/KHR_lights_punctual/lights/" + std::to_string(index),
      diagnostics);
  }
  Reject(source.variants_count != 0U, "material variants", source_path,
    "/extensions/KHR_materials_variants", diagnostics);

  const auto known_extensions
    = std::to_array<std::string_view>({ "KHR_materials_unlit",
      "KHR_materials_ior", "KHR_materials_specular", "KHR_materials_sheen",
      "KHR_materials_clearcoat", "KHR_materials_transmission",
      "KHR_materials_volume", "KHR_lights_punctual", "KHR_materials_variants",
      "KHR_texture_transform", "KHR_mesh_quantization" });
  auto extensions = std::set<std::string_view> {};
  for (const auto* extension :
    std::span(source.extensions_used, source.extensions_used_count)) {
    extensions.emplace(extension);
  }
  for (const auto* extension :
    std::span(source.extensions_required, source.extensions_required_count)) {
    extensions.emplace(extension);
  }
  for (const auto extension : extensions) {
    Reject(
      std::ranges::find(known_extensions, extension) == known_extensions.end(),
      extension, source_path, "/extensions/" + std::string(extension),
      diagnostics);
  }

  for (const auto& [mesh_index, mesh] :
    std::views::enumerate(std::span(source.meshes, source.meshes_count))) {
    const auto path = "/meshes/" + std::to_string(mesh_index);
    for (const auto& [index, primitive] : std::views::enumerate(
           std::span(mesh.primitives, mesh.primitives_count))) {
      const auto primitive_path = path + "/primitives/" + std::to_string(index);
      Reject(primitive.targets_count != 0U, "morph targets", source_path,
        primitive_path + "/targets", diagnostics);
      Reject(primitive.type != cgltf_primitive_type_triangles,
        "non-triangle primitives", source_path, primitive_path + "/mode",
        diagnostics);
      Reject(primitive.has_draco_mesh_compression != 0,
        "Draco-compressed geometry", source_path, primitive_path, diagnostics);
    }
  }
  for (const auto& [index, buffer_view] : std::views::enumerate(
         std::span(source.buffer_views, source.buffer_views_count))) {
    Reject(buffer_view.has_meshopt_compression != 0,
      "meshopt-compressed geometry", source_path,
      "/bufferViews/" + std::to_string(index), diagnostics);
  }
  for (const auto& [index, node] :
    std::views::enumerate(std::span(source.nodes, source.nodes_count))) {
    Reject(node.has_mesh_gpu_instancing != 0, "GPU-instancing extensions",
      source_path, "/nodes/" + std::to_string(index), diagnostics);
  }
  for (const auto& [index, material] : std::views::enumerate(
         std::span(source.materials, source.materials_count))) {
    const auto path = "/materials/" + std::to_string(index);
    Reject(material.has_specular != 0
        && std::ranges::any_of(material.specular.specular_color_factor,
          [](const auto value) -> bool { return value != 1.0F; }),
      "coloured specular factors", source_path,
      path + "/extensions/KHR_materials_specular", diagnostics);
    Reject(
      material.has_sheen != 0 && material.sheen.sheen_roughness_factor != 0.0F,
      "sheen roughness", source_path, path + "/extensions/KHR_materials_sheen",
      diagnostics);
    Reject(material.has_pbr_specular_glossiness != 0
        || material.has_iridescence != 0
        || material.has_diffuse_transmission != 0
        || material.has_anisotropy != 0 || material.has_dispersion != 0
        || material.has_emissive_strength != 0,
      "material extensions without a preserved scalar mapping", source_path,
      path, diagnostics);
  }
  return diagnostics.size() == previous_count;
}

auto ValidateStaticScalarSource(const ufbx_scene& source,
  const std::string_view source_path,
  std::vector<ImportDiagnostic>& diagnostics) -> bool
{
  const auto previous_count = diagnostics.size();
  const auto& settings = source.settings;
  const auto axis_properties = std::array { "UpAxis", "UpAxisSign", "FrontAxis",
    "FrontAxisSign", "CoordAxis", "CoordAxisSign" };
  const auto has_axes = ufbx_coordinate_axes_valid(settings.axes)
    && std::ranges::all_of(axis_properties, [&](const char* name) -> bool {
         return HasAuthoredProperty(settings.props, name);
       });
  const auto has_units = std::isfinite(settings.unit_meters)
    && settings.unit_meters > 0.0
    && HasAuthoredProperty(settings.props, "UnitScaleFactor");
  if (!has_axes || !has_units) {
    diagnostics.push_back({ .severity = ImportSeverity::kError,
      .code = "import.static_scalar.coordinate_metadata",
      .message
      = "FBX import requires explicit valid source axes and units. Export the "
        "file with its coordinate-system and unit metadata.",
      .source_path = std::string(source_path),
      .object_path = "/GlobalSettings" });
  }

  Reject(source.anim_curves.count != 0U, "animation", source_path,
    "/AnimationCurves", diagnostics);
  Reject(source.skin_deformers.count != 0U || source.bones.count != 0U,
    "skinning or skeletons", source_path, "/Deformers/Skin", diagnostics);
  Reject(source.blend_deformers.count != 0U || source.blend_shapes.count != 0U,
    "morph targets", source_path, "/Deformers/BlendShape", diagnostics);
  Reject(source.cache_deformers.count != 0U || source.cache_files.count != 0U,
    "vertex caches", source_path, "/Deformers/Cache", diagnostics);
  Reject(source.textures.count != 0U || source.videos.count != 0U,
    "texture-bearing materials", source_path, "/Textures", diagnostics);
  Reject(source.stereo_cameras.count != 0U, "stereo camera components",
    source_path, "/Cameras", diagnostics);
  for (const auto* camera :
    std::span(source.cameras.data, source.cameras.count)) {
    Reject(camera->projection_mode != UFBX_PROJECTION_MODE_PERSPECTIVE,
      "non-perspective camera components", source_path, "/Cameras",
      diagnostics);
  }
  for (const auto* light : std::span(source.lights.data, source.lights.count)) {
    Reject(light->type != UFBX_LIGHT_DIRECTIONAL,
      "non-directional light components", source_path, "/Lights", diagnostics);
  }
  Reject(source.unknowns.count != 0U, "unknown FBX scene elements", source_path,
    "/Objects", diagnostics);
  Reject(source.constraints.count != 0U || source.characters.count != 0U,
    "constraints or characters", source_path, "/Constraints", diagnostics);
  Reject(source.line_curves.count != 0U || source.nurbs_curves.count != 0U
      || source.nurbs_surfaces.count != 0U
      || source.nurbs_trim_surfaces.count != 0U
      || source.procedural_geometries.count != 0U,
    "curve, NURBS or procedural geometry", source_path, "/Geometry",
    diagnostics);
  return diagnostics.size() == previous_count;
}

} // namespace oxygen::content::import::internal
