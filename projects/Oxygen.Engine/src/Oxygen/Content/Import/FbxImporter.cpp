//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Import/CookedContentWriter.h>
#include <Oxygen/Content/Import/ImageDecode.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportFormat.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/ImportedGeometry.h>
#include <Oxygen/Content/Import/Importer.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/Import/emit/GeometryEmitter.h>
#include <Oxygen/Content/Import/emit/ResourceAppender.h>
#include <Oxygen/Content/Import/emit/SceneEmitter.h>
#include <Oxygen/Content/Import/emit/TextureEmitter.h>
#include <Oxygen/Content/Import/util/Constants.h>
#include <Oxygen/Content/Import/util/CoordTransform.h>
#include <Oxygen/Content/Import/util/ImportNaming.h>
#include <Oxygen/Content/Import/util/Signature.h>
#include <Oxygen/Content/Import/util/StringUtils.h>
#include <Oxygen/Content/Import/util/TangentGen.h>
#include <Oxygen/Content/Import/util/TextureRepack.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

#include <Oxygen/Content/Import/fbx/UfbxUtils.h>
#include <Oxygen/Content/Import/fbx/ufbx.h>

namespace oxygen::content::import {

namespace {

  using std::string_view_literals::operator""sv;

  using oxygen::data::AssetKey;
  using oxygen::data::AssetType;

  // Import utilities from refactored modules
  using util::Clamp01;
  using util::MakeDeterministicAssetKey;
  using util::MakeRandomAssetKey;
  using util::StartsWithIgnoreCase;
  using util::ToFloat;
  using util::TruncateAndNullTerminate;

  using coord::ApplySwapYZDirIfEnabled;
  using coord::ApplySwapYZIfEnabled;
  using coord::EngineCameraTargetAxes;
  using coord::EngineWorldTargetAxes;
  using coord::ToGlmMat4;

  [[nodiscard]] constexpr auto LightTypeName(
    const ufbx_light_type type) noexcept -> const char*
  {
    switch (type) {
    case UFBX_LIGHT_POINT:
      return "POINT";
    case UFBX_LIGHT_DIRECTIONAL:
      return "DIRECTIONAL";
    case UFBX_LIGHT_SPOT:
      return "SPOT";
    case UFBX_LIGHT_AREA:
      return "AREA";
    case UFBX_LIGHT_VOLUME:
      return "VOLUME";
    default:
      return "UNKNOWN";
    }
  }

  [[nodiscard]] constexpr auto LightDecayName(
    const ufbx_light_decay decay) noexcept -> const char*
  {
    switch (decay) {
    case UFBX_LIGHT_DECAY_NONE:
      return "NONE";
    case UFBX_LIGHT_DECAY_LINEAR:
      return "LINEAR";
    case UFBX_LIGHT_DECAY_QUADRATIC:
      return "QUADRATIC";
    case UFBX_LIGHT_DECAY_CUBIC:
      return "CUBIC";
    default:
      return "UNKNOWN";
    }
  }

  auto LogUfbxLights(const ufbx_scene& scene) -> void
  {
    if (scene.lights.count == 0) {
      return;
    }

    constexpr size_t kMaxInstancesToLog = 16;

    for (size_t i = 0; i < scene.lights.count; ++i) {
      const auto* light = scene.lights.data[i];
      if (light == nullptr) {
        continue;
      }

      const auto light_name = fbx::ToStringView(light->name);
      LOG_F(INFO,
        "UFBX light[{}]: name='{}' type={} decay={} intensity={} "
        "color=({}, {}, {}) cast_light={} cast_shadows={} instances={}",
        i, std::string(light_name).c_str(), LightTypeName(light->type),
        LightDecayName(light->decay), util::ToFloat(light->intensity),
        util::ToFloat(light->color.x), util::ToFloat(light->color.y),
        util::ToFloat(light->color.z), light->cast_light, light->cast_shadows,
        light->instances.count);

      const auto count = static_cast<size_t>(light->instances.count);
      const auto to_log = (std::min)(count, kMaxInstancesToLog);
      for (size_t j = 0; j < to_log; ++j) {
        const auto* node = light->instances.data[j];
        const auto node_name = (node != nullptr) ? fbx::ToStringView(node->name)
                                                 : std::string_view {};
        LOG_F(INFO, "  - instance[{}]: node='{}'", j,
          std::string(node_name).c_str());
      }
      if (count > to_log) {
        LOG_F(INFO, "  - ... ({} more instances)", count - to_log);
      }
    }
  }

  /// Returns a human-readable name for a ufbx shader type.
  [[nodiscard]] constexpr auto ShaderTypeName(ufbx_shader_type type) noexcept
    -> const char*
  {
    switch (type) {
    case UFBX_SHADER_FBX_LAMBERT:
      return "FBX_LAMBERT";
    case UFBX_SHADER_FBX_PHONG:
      return "FBX_PHONG";
    case UFBX_SHADER_OSL_STANDARD_SURFACE:
      return "OSL_STANDARD_SURFACE";
    case UFBX_SHADER_ARNOLD_STANDARD_SURFACE:
      return "ARNOLD_STANDARD_SURFACE";
    case UFBX_SHADER_3DS_MAX_PHYSICAL_MATERIAL:
      return "3DS_MAX_PHYSICAL";
    case UFBX_SHADER_3DS_MAX_PBR_METAL_ROUGH:
      return "3DS_MAX_PBR_METAL_ROUGH";
    case UFBX_SHADER_3DS_MAX_PBR_SPEC_GLOSS:
      return "3DS_MAX_PBR_SPEC_GLOSS";
    case UFBX_SHADER_GLTF_MATERIAL:
      return "GLTF_MATERIAL";
    case UFBX_SHADER_OPENPBR_MATERIAL:
      return "OPENPBR_MATERIAL";
    case UFBX_SHADER_SHADERFX_GRAPH:
      return "SHADERFX_GRAPH";
    case UFBX_SHADER_BLENDER_PHONG:
      return "BLENDER_PHONG";
    case UFBX_SHADER_WAVEFRONT_MTL:
      return "WAVEFRONT_MTL";
    default:
      return "UNKNOWN";
    }
  }

  /// Returns true if the shader type supports PBR metalness/roughness workflow.
  [[nodiscard]] constexpr auto IsPbrShader(const ufbx_material& mat) noexcept
    -> bool
  {
    return mat.shader_type >= UFBX_SHADER_OSL_STANDARD_SURFACE
      || mat.features.pbr.enabled;
  }

  // Geometry and scene emission, FBX helpers, and naming helpers live in
  // dedicated modules under Import/emit, Import/fbx, and Import/util.

  // TruncateAndNullTerminate is now imported from util::
  // AlignUp is now in util::TextureRepack.h
  // Sha256ToHex is now in util::Signature.h

  // NormalizeTexturePathId is now in emit/TextureEmitter.h
  // RepackRgba8ToRowPitchAligned is now in util/TextureRepack.h

  // TextureEmission struct is replaced by emit::TextureEmissionState
  // MakeTextureSignature is now in util/Signature.h
  // TryReadWholeFileBytes is now in emit/ResourceAppender.cpp

  // Texture loading functions moved to emit/ResourceAppender.cpp:
  // - LoadExistingTextureResourcesIfPresent -> emit::InitTextureEmissionState
  // - LoadExistingBufferResourcesIfPresent -> emit::InitBufferEmissionState

  // Texture selection functions moved to emit/TextureEmitter.h:
  // - ResolveFileTexture
  // - TextureIdString
  // - SelectBaseColorTexture/Normal/Metallic/Roughness/AmbientOcclusion
  // - EnsureFallbackTexture
  // - GetOrCreateTextureResourceIndex

  // Coordinate transform functions moved to util/CoordTransform.h:
  // - EngineWorldTargetAxes
  // - EngineCameraTargetAxes
  // - ComputeTargetUnitMeters
  // - SwapYZMatrix
  // - ApplySwapYZIfEnabled (all overloads)
  // - ToGlmMat4

  class FbxImporter final : public Importer {
  public:
    [[nodiscard]] auto Name() const noexcept -> std::string_view override
    {
      return "FbxImporter";
    }

    [[nodiscard]] auto Supports(const ImportFormat format) const noexcept
      -> bool override
    {
      return format == ImportFormat::kFbx;
    }

    auto Import(const ImportRequest& request, CookedContentWriter& out)
      -> void override
    {
      const auto source_path_str = request.source_path.string();
      LOG_SCOPE_F(INFO, "FbxImporter::Import {}", source_path_str.c_str());

      const auto cooked_root = request.cooked_root.value_or(
        std::filesystem::absolute(request.source_path.parent_path()));

      ufbx_load_opts opts {};
      ufbx_error error {};

      // Always normalize coordinate system to Oxygen engine space.
      opts.target_axes = EngineWorldTargetAxes();
      opts.target_camera_axes = EngineCameraTargetAxes();

      // FBX nodes may contain "geometry transforms" that affect only the
      // attached attribute (mesh/camera/light), not children. Our cooked scene
      // representation does not currently model these separately, so request
      // ufbx to represent them using helper nodes.
      opts.geometry_transform_handling
        = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;

      // Prefer modifying geometry to ensure vertex positions (and compatible
      // linear terms) are scaled/rotated as required by import policy.
      opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

      // When converting between left-handed and right-handed conventions ufbx
      // mirrors the scene along a chosen axis.
      //
      // Oxygen world is Z-up (Oxygen/Core/Constants.h). Mirroring along Z is
      // therefore the most destructive choice as it flips "up" and can make
      // imported scenes appear upside-down. Prefer mirroring along the world
      // forward/back axis instead.
      opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Y;

      const auto& coordinate_policy = request.options.coordinate;
      if (coordinate_policy.unit_normalization
        == UnitNormalizationPolicy::kApplyCustomFactor) {
        if (!(coordinate_policy.custom_unit_scale > 0.0F)) {
          ImportDiagnostic diag {
            .severity = ImportSeverity::kError,
            .code = "fbx.invalid_custom_unit_scale",
            .message = "custom_unit_scale must be > 0 when using "
                       "UnitNormalizationPolicy::kApplyCustomFactor",
            .source_path = source_path_str,
            .object_path = {},
          };
          out.AddDiagnostic(std::move(diag));
          throw std::runtime_error(
            "FBX import invalid custom_unit_scale (must be > 0)");
        }
      }

      if (const auto target_unit_meters
        = coord::ComputeTargetUnitMeters(coordinate_policy);
        target_unit_meters.has_value()) {
        opts.target_unit_meters = *target_unit_meters;
      }

      opts.generate_missing_normals = true;

      ufbx_scene* scene
        = ufbx_load_file(source_path_str.c_str(), &opts, &error);
      if (scene == nullptr) {
        const auto desc = fbx::ToStringView(error.description);
        ImportDiagnostic diag {
          .severity = ImportSeverity::kError,
          .code = "fbx.parse_failed",
          .message = std::string(desc),
          .source_path = source_path_str,
          .object_path = {},
        };
        out.AddDiagnostic(std::move(diag));
        throw std::runtime_error("FBX parse failed: " + std::string(desc));
      }

      const auto material_count = static_cast<uint32_t>(scene->materials.count);
      const auto mesh_count = static_cast<uint32_t>(scene->meshes.count);
      const auto node_count = static_cast<uint32_t>(scene->nodes.count);
      // ufbx also keeps direct lists of attribute objects (camera/light);
      // logging helps distinguish "not exported" from "exists but unattached".
      const auto camera_count = static_cast<uint32_t>(scene->cameras.count);
      const auto light_count = static_cast<uint32_t>(scene->lights.count);
      LOG_F(INFO,
        "FBX scene loaded: {} materials, {} meshes, {} nodes, {} cameras, "
        "{} lights. SwapYZ={}",
        material_count, mesh_count, node_count, camera_count, light_count,
        request.options.coordinate.swap_yz_axes);

      LogUfbxLights(*scene);

      const auto want_materials
        = (request.options.import_content & ImportContentFlags::kMaterials)
        != ImportContentFlags::kNone;

      const auto want_geometry
        = (request.options.import_content & ImportContentFlags::kGeometry)
        != ImportContentFlags::kNone;

      const auto want_scene
        = (request.options.import_content & ImportContentFlags::kScene)
        != ImportContentFlags::kNone;

      const auto want_textures
        = (request.options.import_content & ImportContentFlags::kTextures)
        != ImportContentFlags::kNone;

      if (want_scene && !want_geometry) {
        ImportDiagnostic diag {
          .severity = ImportSeverity::kError,
          .code = "fbx.scene.requires_geometry",
          .message = "FBX scene import currently requires geometry emission",
          .source_path = source_path_str,
          .object_path = {},
        };
        out.AddDiagnostic(std::move(diag));
        throw std::runtime_error("FBX scene import requires geometry");
      }

      emit::TextureEmissionState textures;
      if (want_textures) {
        const auto textures_table_path = cooked_root
          / std::filesystem::path(
            request.loose_cooked_layout.TexturesTableRelPath());
        const auto textures_data_path = cooked_root
          / std::filesystem::path(
            request.loose_cooked_layout.TexturesDataRelPath());
        textures = emit::InitTextureEmissionState(
          textures_table_path, textures_data_path);
        emit::BuildTextureSignatureIndex(textures, textures_data_path);
        emit::EnsureFallbackTexture(textures);
      }

      uint32_t written_materials = 0;
      std::vector<AssetKey> material_keys;
      if (want_materials) {
        WriteMaterials_(*scene, request, out, textures, want_textures,
          written_materials, material_keys);
      }

      if (written_materials > 0) {
        out.OnMaterialsWritten(written_materials);
      }

      if (want_textures) {
        WriteTextures_(request, out, textures);
      }

      uint32_t written_geometry = 0;
      std::vector<ImportedGeometry> imported_geometry;
      if (want_geometry) {
        emit::WriteGeometryAssets(*scene, request, out, material_keys,
          imported_geometry, written_geometry, want_textures);
      }

      if (written_geometry > 0) {
        out.OnGeometryWritten(written_geometry);
      }

      uint32_t written_scenes = 0;
      if (want_scene) {
        emit::WriteSceneAsset(
          *scene, request, out, imported_geometry, written_scenes);
      }

      if (written_scenes > 0) {
        out.OnScenesWritten(written_scenes);
      }

      ufbx_free_scene(scene);
    }

  private:
    static auto WriteMaterials_(const ufbx_scene& scene,
      const ImportRequest& request, CookedContentWriter& out,
      emit::TextureEmissionState& textures, const bool want_textures,
      uint32_t& written_materials, std::vector<AssetKey>& out_keys) -> void
    {
      const auto count = static_cast<uint32_t>(scene.materials.count);
      if (count == 0) {
        const auto name = util::BuildMaterialName("M_Default", request, 0);
        const auto key = WriteOneMaterial_(
          request, out, name, 0, nullptr, textures, want_textures);
        out_keys.push_back(key);
        written_materials += 1;
        return;
      }

      for (uint32_t i = 0; i < count; ++i) {
        const auto* mat = scene.materials.data[i];
        const auto authored_name = (mat != nullptr)
          ? fbx::ToStringView(mat->name)
          : std::string_view {};
        const auto name = util::BuildMaterialName(authored_name, request, i);

        const auto key = WriteOneMaterial_(
          request, out, name, i, mat, textures, want_textures);
        out_keys.push_back(key);
        written_materials += 1;
      }
    }

    [[nodiscard]] static auto WriteOneMaterial_(const ImportRequest& request,
      CookedContentWriter& out, std::string_view material_name,
      const uint32_t ordinal, const ufbx_material* material,
      emit::TextureEmissionState& textures, const bool want_textures)
      -> AssetKey
    {
      const auto storage_name
        = util::NamespaceImportedAssetName(request, material_name);
      const auto virtual_path
        = request.loose_cooked_layout.MaterialVirtualPath(storage_name);

      const auto relpath
        = request.loose_cooked_layout.DescriptorDirFor(AssetType::kMaterial)
        + "/" + LooseCookedLayout::MaterialDescriptorFileName(storage_name);

      AssetKey key {};
      switch (request.options.asset_key_policy) {
      case AssetKeyPolicy::kDeterministicFromVirtualPath:
        key = MakeDeterministicAssetKey(virtual_path);
        break;
      case AssetKeyPolicy::kRandom:
        key = MakeRandomAssetKey();
        break;
      }

      oxygen::data::pak::MaterialAssetDesc desc {};
      desc.header.asset_type = static_cast<uint8_t>(AssetType::kMaterial);
      TruncateAndNullTerminate(
        desc.header.name, std::size(desc.header.name), material_name);
      desc.material_domain
        = static_cast<uint8_t>(oxygen::data::MaterialDomain::kOpaque);
      desc.flags = oxygen::data::pak::kMaterialFlag_NoTextureSampling;

      if (material != nullptr) {
        // Scalar PBR factors (used even when texture sampling is disabled).
        ufbx_vec4 base = { 1.0, 1.0, 1.0, 1.0 };
        if (material->pbr.base_color.has_value
          && material->pbr.base_color.value_components >= 3) {
          base = material->pbr.base_color.value_vec4;
        } else if (material->fbx.diffuse_color.has_value
          && material->fbx.diffuse_color.value_components >= 3) {
          const auto dc = material->fbx.diffuse_color.value_vec3;
          base = { dc.x, dc.y, dc.z, 1.0 };
        }

        float base_factor = 1.0F;
        if (material->pbr.base_factor.has_value) {
          base_factor = Clamp01(ToFloat(material->pbr.base_factor.value_real));
        } else if (material->fbx.diffuse_factor.has_value) {
          base_factor
            = Clamp01(ToFloat(material->fbx.diffuse_factor.value_real));
        }

        desc.base_color[0] = Clamp01(ToFloat(base.x) * base_factor);
        desc.base_color[1] = Clamp01(ToFloat(base.y) * base_factor);
        desc.base_color[2] = Clamp01(ToFloat(base.z) * base_factor);
        desc.base_color[3] = Clamp01(ToFloat(base.w) * base_factor);

        if (material->pbr.metalness.has_value) {
          desc.metalness = oxygen::data::Unorm16 { Clamp01(
            ToFloat(material->pbr.metalness.value_real)) };
        }

        float specular_factor = 1.0F;

        const auto shading_model
          = fbx::ToStringView(material->shading_model_name);
        const auto fbx_material_name = fbx::ToStringView(material->name);

        bool is_lambert = (material->shader_type == UFBX_SHADER_FBX_LAMBERT);
        if (!is_lambert) {
          // Fallback check for string name if ufbx didn't classify it
          if (shading_model == "Lambert" || shading_model == "lambert") {
            is_lambert = true;
          }
          // Heuristic: if the material name starts with "lambert" (e.g.
          // "lambert1"), treat it as Lambert even if the shading model says
          // Phong. This fixes issues with default materials in FBX exported
          // from Maya/etc.
          else if (StartsWithIgnoreCase(fbx_material_name, "lambert")) {
            is_lambert = true;
          }
        }

        LOG_F(INFO, "Material '{}': shader={}{}", fbx_material_name,
          ShaderTypeName(material->shader_type),
          IsPbrShader(*material) ? " (PBR)" : "");

        // Lambert materials in FBX often have garbage/default specular values.
        // UE5 imports them as 0.5 (default PBR specular).
        if (is_lambert) {
          specular_factor = 0.5F;
        } else {
          if (material->pbr.specular_factor.has_value) {
            specular_factor
              = Clamp01(ToFloat(material->pbr.specular_factor.value_real));
          } else if (material->fbx.specular_factor.has_value) {
            specular_factor
              = Clamp01(ToFloat(material->fbx.specular_factor.value_real));
          }

          // Modulate by specular color intensity if present.
          // This handles cases where specular is defined by color instead of
          // factor, or both.
          if (material->pbr.specular_color.has_value) {
            const auto& c = material->pbr.specular_color.value_vec4;
            const float intensity
              = (std::max)({ ToFloat(c.x), ToFloat(c.y), ToFloat(c.z) });
            specular_factor *= intensity;
          } else if (material->fbx.specular_color.has_value) {
            const auto& c = material->fbx.specular_color.value_vec4;
            const float intensity
              = (std::max)({ ToFloat(c.x), ToFloat(c.y), ToFloat(c.z) });
            specular_factor *= intensity;
          }
        }

        desc.specular_factor
          = oxygen::data::Unorm16 { Clamp01(specular_factor) };

        float roughness = 1.0F;
        if (material->pbr.roughness.has_value) {
          roughness = Clamp01(ToFloat(material->pbr.roughness.value_real));
        }
        if (material->features.roughness_as_glossiness.enabled) {
          roughness = 1.0F - roughness;
        }
        desc.roughness = oxygen::data::Unorm16 { Clamp01(roughness) };

        if (material->pbr.ambient_occlusion.has_value) {
          desc.ambient_occlusion = oxygen::data::Unorm16 { Clamp01(
            ToFloat(material->pbr.ambient_occlusion.value_real)) };
        }

        // Extract emissive factor from PBR or FBX properties.
        // Emissive is HDR-capable (not clamped to 0-1).
        {
          ufbx_vec4 emission = { 0.0, 0.0, 0.0, 0.0 };
          float emission_factor = 1.0F;

          if (material->pbr.emission_color.has_value
            && material->pbr.emission_color.value_components >= 3) {
            emission = material->pbr.emission_color.value_vec4;
          } else if (material->fbx.emission_color.has_value
            && material->fbx.emission_color.value_components >= 3) {
            const auto ec = material->fbx.emission_color.value_vec3;
            emission = { ec.x, ec.y, ec.z, 0.0 };
          }

          if (material->pbr.emission_factor.has_value) {
            emission_factor = ToFloat(material->pbr.emission_factor.value_real);
          } else if (material->fbx.emission_factor.has_value) {
            emission_factor = ToFloat(material->fbx.emission_factor.value_real);
          }

          // Emissive values can exceed 1.0 for HDR glow effects.
          desc.emissive_factor[0]
            = oxygen::data::HalfFloat { ToFloat(emission.x) * emission_factor };
          desc.emissive_factor[1]
            = oxygen::data::HalfFloat { ToFloat(emission.y) * emission_factor };
          desc.emissive_factor[2]
            = oxygen::data::HalfFloat { ToFloat(emission.z) * emission_factor };
        }

        if (material->pbr.normal_map.has_value) {
          desc.normal_scale
            = (std::max)(0.0F, ToFloat(material->pbr.normal_map.value_real));
        } else if (material->fbx.bump_factor.has_value) {
          desc.normal_scale
            = (std::max)(0.0F, ToFloat(material->fbx.bump_factor.value_real));
        }

        if (material->features.double_sided.enabled) {
          desc.flags |= oxygen::data::pak::kMaterialFlag_DoubleSided;
        }
        if (material->features.unlit.enabled) {
          desc.flags |= oxygen::data::pak::kMaterialFlag_Unlit;
        }
      }

      if (want_textures && material != nullptr) {
        const auto base_color_tex = emit::SelectBaseColorTexture(*material);
        const auto normal_tex = emit::SelectNormalTexture(*material);
        const auto metallic_tex = emit::SelectMetallicTexture(*material);
        const auto roughness_tex = emit::SelectRoughnessTexture(*material);
        const auto ao_tex = emit::SelectAmbientOcclusionTexture(*material);
        const auto emissive_tex = emit::SelectEmissiveTexture(*material);

        // If metallic and roughness reference the same underlying file
        // texture, treat it as a packed map using glTF semantics
        // (R=AO, G=roughness, B=metalness). This is common for assets authored
        // in glTF workflows and exported through FBX.
        if (emit::ResolveFileTexture(metallic_tex) != nullptr
          && emit::ResolveFileTexture(metallic_tex)
            == emit::ResolveFileTexture(roughness_tex)) {
          desc.flags |= oxygen::data::pak::kMaterialFlag_GltfOrmPacked;
        }

        const auto base_color_index = emit::GetOrCreateTextureResourceIndex(
          request, out, textures, base_color_tex);
        const auto normal_index = emit::GetOrCreateTextureResourceIndex(
          request, out, textures, normal_tex);
        const auto metallic_index = emit::GetOrCreateTextureResourceIndex(
          request, out, textures, metallic_tex);
        const auto roughness_index = emit::GetOrCreateTextureResourceIndex(
          request, out, textures, roughness_tex);
        const auto ao_index = emit::GetOrCreateTextureResourceIndex(
          request, out, textures, ao_tex);
        const auto emissive_index = emit::GetOrCreateTextureResourceIndex(
          request, out, textures, emissive_tex);

        desc.base_color_texture = base_color_index;
        desc.normal_texture = normal_index;
        desc.metallic_texture = metallic_index;
        desc.roughness_texture = roughness_index;
        desc.ambient_occlusion_texture = ao_index;
        desc.emissive_texture = emissive_index;

        if (base_color_index != 0 || normal_index != 0 || metallic_index != 0
          || roughness_index != 0 || ao_index != 0 || emissive_index != 0) {
          desc.flags &= ~oxygen::data::pak::kMaterialFlag_NoTextureSampling;
        }
      }

      oxygen::serio::MemoryStream stream;
      oxygen::serio::Writer<oxygen::serio::MemoryStream> writer(stream);
      (void)writer.WriteBlob(std::as_bytes(
        std::span<const oxygen::data::pak::MaterialAssetDesc, 1>(&desc, 1)));

      const auto bytes = stream.Data();

      LOG_F(INFO, "Emit material {} '{}' -> {}", ordinal,
        std::string(material_name).c_str(), relpath.c_str());

      out.WriteAssetDescriptor(
        key, AssetType::kMaterial, virtual_path, relpath, bytes);

      return key;
    }

    static auto WriteTextures_(const ImportRequest& request,
      CookedContentWriter& out, emit::TextureEmissionState& textures) -> void
    {
      using oxygen::data::loose_cooked::v1::FileKind;
      using oxygen::data::pak::TextureResourceDesc;

      // Close the data file appender (flushes any pending writes)
      emit::CloseAppender(textures.appender);

      if (textures.table.empty()) {
        return;
      }

      LOG_F(INFO, "Emit textures table: count={} data_file='{}' -> table='{}'",
        textures.table.size(),
        request.loose_cooked_layout.TexturesDataRelPath().c_str(),
        request.loose_cooked_layout.TexturesTableRelPath().c_str());

      oxygen::serio::MemoryStream table_stream;
      oxygen::serio::Writer<oxygen::serio::MemoryStream> table_writer(
        table_stream);
      const auto pack = table_writer.ScopedAlignment(1);
      (void)table_writer.WriteBlob(
        std::as_bytes(std::span<const TextureResourceDesc>(textures.table)));

      out.WriteFile(FileKind::kTexturesTable,
        request.loose_cooked_layout.TexturesTableRelPath(),
        table_stream.Data());

      // Register the externally-written data file
      out.RegisterExternalFile(FileKind::kTexturesData,
        request.loose_cooked_layout.TexturesDataRelPath());
    }
  };

} // namespace

auto CreateFbxImporter() -> std::unique_ptr<Importer>
{
  return std::make_unique<FbxImporter>();
}

} // namespace oxygen::content::import
