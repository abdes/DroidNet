//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/DroppedSourceFeatures.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxMaterialTextures.h>
#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>
#include <Oxygen/Cooker/Import/Internal/gltf/cgltf.h>

namespace oxygen::content::import::internal {
namespace {

  struct TextureCoordinates {
    cgltf_int set = 0;
    std::array<cgltf_float, 2> scale { 1.0F, 1.0F };
    std::array<cgltf_float, 2> offset {};
    cgltf_float rotation = 0.0F;

    auto operator==(const TextureCoordinates&) const -> bool = default;
  };

  auto Coordinates(const cgltf_texture_view& view) -> TextureCoordinates
  {
    const bool transformed = view.has_transform != 0;
    const bool texcoord_set = transformed && view.transform.has_texcoord != 0;
    return {
      .set = texcoord_set ? view.transform.texcoord : view.texcoord,
      .scale = transformed ? std::to_array(view.transform.scale)
                           : std::array { 1.0F, 1.0F },
      .offset = transformed ? std::to_array(view.transform.offset)
                            : std::array<cgltf_float, 2> {},
      .rotation = transformed ? view.transform.rotation : 0.0F,
    };
  }

  //! Reports a source feature the import leaves out. The message says what is
  //! left out and what the imported content is instead; the object name is the
  //! source's name of the material, mesh or node it is about.
  auto Drop(const bool present, std::string message,
    const std::string_view source_path, const std::string_view object_path,
    std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view object_name = {}) -> void
  {
    if (present) {
      diagnostics.push_back({
        .severity = ImportSeverity::kWarning,
        .code = "import.source_feature_dropped",
        .message = std::move(message),
        .source_path = std::string(source_path),
        .object_path = std::string(object_path),
        .object_name = std::string(object_name),
      });
    }
  }

  auto NameOf(const char* name) -> std::string_view
  {
    return name != nullptr ? std::string_view(name) : std::string_view {};
  }

  auto NameOf(const ufbx_string& name) -> std::string_view
  {
    return { name.data, name.length };
  }

  auto HasAuthoredProperty(const ufbx_props& properties, const char* name)
    -> bool
  {
    constexpr auto kNotAuthored
      = static_cast<uint32_t>(UFBX_PROP_FLAG_NOT_FOUND)
      | static_cast<uint32_t>(UFBX_PROP_FLAG_SYNTHETIC);
    const auto* property = ufbx_find_prop(&properties, name);
    return property != nullptr
      && (static_cast<uint32_t>(property->flags) & kNotAuthored) == 0U;
  }

  //! Extensions the glTF adapter reads.
  constexpr auto kReadExtensions = std::to_array<std::string_view>({
    "KHR_materials_unlit",
    "KHR_materials_ior",
    "KHR_materials_specular",
    "KHR_materials_sheen",
    "KHR_materials_clearcoat",
    "KHR_materials_transmission",
    "KHR_materials_volume",
    "KHR_lights_punctual",
    "KHR_materials_variants",
    "KHR_texture_transform",
    "KHR_mesh_quantization",
    "EXT_meshopt_compression",
  });

  auto ReportGltfExtensions(const cgltf_data& source,
    const std::string_view source_path,
    std::vector<ImportDiagnostic>& diagnostics) -> void
  {
    const auto read = [](const std::string_view extension) -> bool {
      return std::ranges::find(kReadExtensions, extension)
        != kReadExtensions.end();
    };
    auto required = std::set<std::string_view> {};
    for (const auto* extension :
      std::span(source.extensions_required, source.extensions_required_count)) {
      required.emplace(extension);
      if (!read(extension)) {
        diagnostics.push_back({
          .severity = ImportSeverity::kWarning,
          .code = "import.source_extension_required",
          .message = "The source requires glTF extension '"
            + std::string(extension)
            + "', which import does not read; content that depends on it "
              "imports as far as it can be read.",
          .source_path = std::string(source_path),
          .object_path = "/extensions/" + std::string(extension),
        });
      }
    }
    for (const auto* extension :
      std::span(source.extensions_used, source.extensions_used_count)) {
      Drop(!required.contains(extension) && !read(extension),
        "glTF extension '" + std::string(extension)
          + "' is not read; what it adds to the content is left out.",
        source_path, "/extensions/" + std::string(extension), diagnostics);
    }
  }

  auto ReportGltfMaterial(const cgltf_material& material,
    const std::string& path, const std::string_view source_path,
    std::vector<ImportDiagnostic>& diagnostics) -> void
  {
    const auto name = NameOf(material.name);
    const auto drop = [&](const bool present, std::string message,
                        const std::string& object_path) -> void {
      Drop(present, std::move(message), source_path, object_path, diagnostics,
        name);
    };
    const auto core_textures = std::array {
      &material.pbr_metallic_roughness.base_color_texture,
      &material.pbr_metallic_roughness.metallic_roughness_texture,
      &material.normal_texture,
      &material.occlusion_texture,
      &material.emissive_texture,
    };
    auto coordinates = std::optional<TextureCoordinates> {};
    auto conflicting = false;
    auto custom_sampling = false;
    for (const auto* view : core_textures) {
      if (view->texture == nullptr) {
        continue;
      }
      const auto current = Coordinates(*view);
      if (!coordinates.has_value()) {
        coordinates = current;
      } else {
        conflicting = conflicting || *coordinates != current;
      }
      if (const auto* sampler = view->texture->sampler; sampler != nullptr) {
        custom_sampling = custom_sampling
          || sampler->wrap_s != cgltf_wrap_mode_repeat
          || sampler->wrap_t != cgltf_wrap_mode_repeat
          || (sampler->mag_filter != cgltf_filter_type_undefined
            && sampler->mag_filter != cgltf_filter_type_linear)
          || (sampler->min_filter != cgltf_filter_type_undefined
            && sampler->min_filter != cgltf_filter_type_linear_mipmap_linear);
      }
    }
    drop(conflicting,
      "The material's textures use different UV sets or transforms, which are "
      "not imported; every texture uses those of the first one.",
      path);
    drop(custom_sampling,
      "The material's texture wrap and filter modes are not imported; its "
      "textures repeat and use trilinear filtering.",
      path);
    drop(material.has_specular != 0
        && std::ranges::any_of(material.specular.specular_color_factor,
          [](const auto value) -> bool { return value != 1.0F; }),
      "The material's specular colour is not imported; its specular "
      "reflection is white.",
      path + "/extensions/KHR_materials_specular");
    drop(material.specular.specular_color_texture.texture != nullptr,
      "The material's specular colour texture is not imported; its specular "
      "reflection is white.",
      path + "/extensions/KHR_materials_specular");
    drop(
      material.has_sheen != 0 && material.sheen.sheen_roughness_factor != 0.0F,
      "The material's sheen roughness is not imported; its sheen is smooth.",
      path + "/extensions/KHR_materials_sheen");
    drop(material.sheen.sheen_roughness_texture.texture != nullptr,
      "The material's sheen roughness texture is not imported; its sheen "
      "roughness is uniform.",
      path + "/extensions/KHR_materials_sheen");
    drop(material.clearcoat.clearcoat_roughness_texture.texture != nullptr,
      "The material's clearcoat roughness texture is not imported; its "
      "clearcoat roughness is uniform.",
      path + "/extensions/KHR_materials_clearcoat");

    // Extensions the material format has no field for, named as the file
    // uses them.
    auto unsupported = std::vector<std::string_view> {};
    const auto note = [&](const bool present, const std::string_view what) {
      if (present) {
        unsupported.push_back(what);
      }
    };
    note(material.has_pbr_specular_glossiness != 0, "specular-glossiness");
    note(material.has_iridescence != 0, "iridescence");
    note(material.has_diffuse_transmission != 0, "diffuse transmission");
    note(material.has_anisotropy != 0, "anisotropy");
    note(material.has_dispersion != 0, "dispersion");
    note(material.has_emissive_strength != 0, "emissive strength");
    if (!unsupported.empty()) {
      const auto separator
        = [&unsupported](const std::size_t at) -> std::string_view {
        if (at == 0) {
          return "";
        }
        return at + 1 == unsupported.size() ? " and " : ", ";
      };
      auto list = std::string {};
      for (const auto& [index, what] : std::views::enumerate(unsupported)) {
        list += separator(static_cast<std::size_t>(index));
        list += what;
      }
      drop(true,
        "The material's " + list
          + " are not imported; the material format has no field for them, "
            "and the material renders without them.",
        path);
    }
  }

} // namespace

auto ReportDroppedSourceFeatures(const cgltf_data& source,
  const std::string_view source_path,
  std::vector<ImportDiagnostic>& diagnostics) -> void
{
  Drop(source.animations_count != 0U,
    "Animations are not imported; the model imports in its rest pose.",
    source_path, "/animations", diagnostics);
  Drop(source.skins_count != 0U,
    "Skins are not imported; skinned geometry imports in its bind pose.",
    source_path, "/skins", diagnostics);
  Drop(source.variants_count != 0U,
    "Material variants are not imported; every mesh keeps its default "
    "material.",
    source_path, "/extensions/KHR_materials_variants", diagnostics);
  ReportGltfExtensions(source, source_path, diagnostics);

  for (const auto& [mesh_index, mesh] :
    std::views::enumerate(std::span(source.meshes, source.meshes_count))) {
    const auto path = "/meshes/" + std::to_string(mesh_index);
    for (const auto& [index, primitive] : std::views::enumerate(
           std::span(mesh.primitives, mesh.primitives_count))) {
      Drop(primitive.targets_count != 0U,
        "Morph targets are not imported; the mesh imports in its base shape.",
        source_path, path + "/primitives/" + std::to_string(index) + "/targets",
        diagnostics, NameOf(mesh.name));
    }
  }
  for (const auto& [index, material] : std::views::enumerate(
         std::span(source.materials, source.materials_count))) {
    ReportGltfMaterial(material, "/materials/" + std::to_string(index),
      source_path, diagnostics);
  }
}

auto ReportDroppedSourceFeatures(const ufbx_scene& source,
  const std::string_view source_path,
  std::vector<ImportDiagnostic>& diagnostics) -> void
{
  const auto& settings = source.settings;
  if (!std::isfinite(settings.unit_meters) || settings.unit_meters <= 0.0
    || !HasAuthoredProperty(settings.props, "UnitScaleFactor")) {
    diagnostics.push_back({
      .severity = ImportSeverity::kWarning,
      .code = "fbx.units_unknown",
      .message = "The FBX file states no unit scale; import assumes "
                 "centimetres.",
      .source_path = std::string(source_path),
      .object_path = "/GlobalSettings",
    });
  }

  Drop(source.anim_curves.count != 0U,
    "Animations are not imported; the model imports in its rest pose.",
    source_path, "/AnimationCurves", diagnostics);
  Drop(source.skin_deformers.count != 0U || source.bones.count != 0U,
    "Skins are not imported; skinned geometry imports in its bind pose.",
    source_path, "/Deformers/Skin", diagnostics);
  Drop(source.blend_deformers.count != 0U || source.blend_shapes.count != 0U,
    "Morph targets are not imported; meshes import in their base shape.",
    source_path, "/Deformers/BlendShape", diagnostics);
  Drop(source.cache_deformers.count != 0U || source.cache_files.count != 0U,
    "Vertex caches are not imported; meshes import in their base shape.",
    source_path, "/Deformers/Cache", diagnostics);
  Drop(source.stereo_cameras.count != 0U,
    "Stereo cameras are not imported; the model imports without them.",
    source_path, "/Cameras", diagnostics);
  Drop(source.unknowns.count != 0U,
    "Scene elements the FBX importer does not know are not imported; the "
    "model imports without them.",
    source_path, "/Objects", diagnostics);
  Drop(source.constraints.count != 0U || source.characters.count != 0U,
    "Constraints and characters are not imported; their nodes import as they "
    "stand in the file.",
    source_path, "/Constraints", diagnostics);
  Drop(source.line_curves.count != 0U || source.nurbs_curves.count != 0U
      || source.nurbs_surfaces.count != 0U
      || source.nurbs_trim_surfaces.count != 0U
      || source.procedural_geometries.count != 0U,
    "Curves, NURBS and procedural geometry are not imported; only polygon "
    "meshes import.",
    source_path, "/Geometry", diagnostics);
  for (const auto& [index, material] : std::views::enumerate(
         std::span(source.materials.data, source.materials.count))) {
    const auto path = "/Materials/" + std::to_string(index);
    const auto material_name = NameOf(material->name);
    const auto& pbr = material->pbr;
    const auto mapped = adapters::FbxMaterialTextures::From(*material);
    // A file binds one texture to several channels, such as a legacy FBX
    // channel and the PBR channel ufbx derives from it. A texture a slot
    // imports is carried; any other is reported once, at its first channel.
    auto reported = std::set<const ufbx_texture*> {};
    const auto report_maps
      = [&](const auto& maps, const std::string_view group) -> auto {
      for (const auto& [slot_index, map] :
        std::views::enumerate(std::span(maps))) {
        if (!adapters::FbxTextureSlot::Active(map)
          || mapped.Carries(*map.texture)
          || !reported.insert(map.texture).second) {
          continue;
        }
        const auto name
          = std::string(map.texture->name.data, map.texture->name.length);
        Drop(true,
          "Texture '" + name + "' is on a " + std::string(group)
            + " material channel the material format has no slot for; the "
              "material imports without it.",
          source_path,
          path + "/" + std::string(group) + "/" + std::to_string(slot_index),
          diagnostics, material_name);
      }
    };
    report_maps(material->pbr.maps, "pbr");
    report_maps(material->fbx.maps, "fbx");
    for (const auto slot : mapped.Slots()) {
      const auto* texture = slot.Texture();
      if (texture == nullptr) {
        continue;
      }
      Drop(texture->type != UFBX_TEXTURE_FILE,
        "The material's layered or procedural textures are not imported; the "
        "material imports without them.",
        source_path, path, diagnostics, material_name);
      Drop(texture->has_uv_transform,
        "The material's texture UV transforms are not imported; its textures "
        "map without them.",
        source_path, path, diagnostics, material_name);
      Drop(texture->wrap_u != UFBX_WRAP_REPEAT
          || texture->wrap_v != UFBX_WRAP_REPEAT,
        "The material's texture wrap modes are not imported; its textures "
        "repeat.",
        source_path, path, diagnostics, material_name);
    }
    Drop(material->features.sheen.enabled && pbr.sheen_roughness.has_value
        && pbr.sheen_roughness.value_real != 0.0,
      "The material's sheen roughness is not imported; its sheen is smooth.",
      source_path, path, diagnostics, material_name);
  }
  for (const auto& [index, node] :
    std::views::enumerate(std::span(source.nodes.data, source.nodes.count))) {
    if (node->mesh == nullptr) {
      continue;
    }
    const auto& mesh = *node->mesh;
    const auto uv_sets = std::span(mesh.uv_sets.data, mesh.uv_sets.count);
    const auto emitted_uv = uv_sets.empty()
      ? std::string_view {}
      : std::string_view(
          uv_sets.front().name.data, uv_sets.front().name.length);
    const auto& materials
      = node->materials.count != 0U ? node->materials : mesh.materials;
    for (const auto* material : std::span(materials.data, materials.count)) {
      for (const auto slot :
        adapters::FbxMaterialTextures::From(*material).Slots()) {
        const auto* texture = slot.Texture();
        if (texture == nullptr || texture->uv_set.length == 0U) {
          continue;
        }
        const auto requested
          = std::string_view(texture->uv_set.data, texture->uv_set.length);
        Drop(requested != emitted_uv,
          "Texture UV set '" + std::string(requested)
            + "' is not imported; the mesh carries only '"
            + std::string(emitted_uv) + "', which the texture uses.",
          source_path, "/Nodes/" + std::to_string(index), diagnostics,
          NameOf(node->name));
      }
    }
  }
}

} // namespace oxygen::content::import::internal
