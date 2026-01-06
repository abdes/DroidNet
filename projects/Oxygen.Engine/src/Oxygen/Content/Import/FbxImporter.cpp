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
#include <Oxygen/Content/Import/Importer.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/Import/emit/BufferEmitter.h>
#include <Oxygen/Content/Import/emit/ResourceAppender.h>
#include <Oxygen/Content/Import/emit/TextureEmitter.h>
#include <Oxygen/Content/Import/util/Constants.h>
#include <Oxygen/Content/Import/util/CoordTransform.h>
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

  [[nodiscard]] auto TryFindRealProp(const ufbx_props& props,
    const char* name) noexcept -> std::optional<ufbx_real>
  {
    const auto* prop = ufbx_find_prop(&props, name);
    if (prop == nullptr) {
      return std::nullopt;
    }
    if ((prop->flags & UFBX_PROP_FLAG_VALUE_REAL) == 0) {
      return std::nullopt;
    }
    return prop->value_real;
  }

  [[nodiscard]] auto TryFindBoolProp(
    const ufbx_props& props, const char* name) noexcept -> std::optional<bool>
  {
    const auto* prop = ufbx_find_prop(&props, name);
    if (prop == nullptr) {
      return std::nullopt;
    }
    if ((prop->flags & UFBX_PROP_FLAG_VALUE_INT) == 0) {
      return std::nullopt;
    }
    return prop->value_int != 0;
  }

  [[nodiscard]] auto TryFindVec3Prop(const ufbx_props& props,
    const char* name) noexcept -> std::optional<ufbx_vec3>
  {
    const auto* prop = ufbx_find_prop(&props, name);
    if (prop == nullptr) {
      return std::nullopt;
    }
    if ((prop->flags & UFBX_PROP_FLAG_VALUE_VEC3) == 0) {
      return std::nullopt;
    }
    return prop->value_vec3;
  }

  [[nodiscard]] auto ToRadiansHeuristic(const ufbx_real angle) noexcept -> float
  {
    // FBX commonly stores angles in degrees; ufbx does not annotate units for
    // `ufbx_light::inner_angle/outer_angle`. Use a conservative heuristic:
    // treat values > 2*pi as degrees.
    const auto a = static_cast<float>(angle);
    constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
    if (a > kTwoPi + 1e-3F) {
      return a * (std::numbers::pi_v<float> / 180.0F);
    }
    return a;
  }

  [[nodiscard]] auto ResolveLightRange(const ufbx_light& light) noexcept
    -> std::optional<float>
  {
    // Try a selection of commonly exported properties. If none are found,
    // preserve engine defaults.
    for (const auto* name : {
           "FarAttenuationEnd",
           "DecayStart",
           "Range",
           "Radius",
           "FalloffEnd",
         }) {
      if (const auto v = TryFindRealProp(light.props, name); v.has_value()) {
        const auto f = ToFloat(*v);
        if (f > 0.0F) {
          return f;
        }
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] auto ResolveLightSourceRadius(const ufbx_light& light) noexcept
    -> std::optional<float>
  {
    for (const auto* name : {
           "SourceRadius",
           "AreaRadius",
           "Radius",
         }) {
      if (const auto v = TryFindRealProp(light.props, name); v.has_value()) {
        const auto f = ToFloat(*v);
        if (f >= 0.0F) {
          return f;
        }
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] auto MapDecayToAttenuation(const ufbx_light_decay decay)
    -> std::pair<std::uint8_t, float>
  {
    // oxygen::scene::AttenuationModel underlying values are written into pak.
    // 0: kInverseSquare, 1: kLinear, 2: kCustomExponent
    switch (decay) {
    case UFBX_LIGHT_DECAY_LINEAR:
      return { 1, 1.0F };
    case UFBX_LIGHT_DECAY_QUADRATIC:
      return { 0, 2.0F };
    case UFBX_LIGHT_DECAY_CUBIC:
      return { 2, 3.0F };
    case UFBX_LIGHT_DECAY_NONE:
    default:
      return { 2, 0.0F };
    }
  }

  auto FillLightCommon(const ufbx_light& light,
    oxygen::data::pak::LightCommonRecord& out) noexcept -> void
  {
    out.affects_world = light.cast_light ? 1U : 0U;
    // Light colors are authored HDR in many DCCs; preserve values as-is and
    // only clamp negative inputs.
    out.color_rgb[0] = std::max(0.0F, ToFloat(light.color.x));
    out.color_rgb[1] = std::max(0.0F, ToFloat(light.color.y));
    out.color_rgb[2] = std::max(0.0F, ToFloat(light.color.z));
    out.intensity = std::max(0.0F, ToFloat(light.intensity));

    // Default to realtime mobility.
    out.mobility = 0;
    out.casts_shadows = light.cast_shadows ? 1U : 0U;

    // Try to enrich from optional properties when present.
    if (const auto v = TryFindBoolProp(light.props, "CastShadows");
      v.has_value()) {
      out.casts_shadows = v.value() ? 1U : 0U;
    }
    if (const auto v = TryFindBoolProp(light.props, "CastLight");
      v.has_value()) {
      out.affects_world = v.value() ? 1U : 0U;
    }

    if (const auto v = TryFindVec3Prop(light.props, "Color"); v.has_value()) {
      out.color_rgb[0] = std::max(0.0F, ToFloat(v->x));
      out.color_rgb[1] = std::max(0.0F, ToFloat(v->y));
      out.color_rgb[2] = std::max(0.0F, ToFloat(v->z));
    }
    if (const auto v = TryFindRealProp(light.props, "ExposureCompensation");
      v.has_value()) {
      out.exposure_compensation_ev = ToFloat(*v);
    }
    if (const auto v = TryFindRealProp(light.props, "ShadowBias");
      v.has_value()) {
      out.shadow.bias = ToFloat(*v);
    }
    if (const auto v = TryFindRealProp(light.props, "ShadowNormalBias");
      v.has_value()) {
      out.shadow.normal_bias = ToFloat(*v);
    }
    if (const auto v = TryFindBoolProp(light.props, "ContactShadows");
      v.has_value()) {
      out.shadow.contact_shadows = v.value() ? 1U : 0U;
    }
  }

  [[nodiscard]] auto ToStringView(const ufbx_string& s) -> std::string_view
  {
    return std::string_view(s.data, s.length);
  }

  // MakeDeterministicAssetKey, MakeRandomAssetKey, StartsWithIgnoreCase,
  // Clamp01, ToFloat are now imported from util::

  [[nodiscard]] auto BuildMaterialName(std::string_view authored,
    const ImportRequest& request, const uint32_t ordinal) -> std::string
  {
    if (request.options.naming_strategy) {
      const NamingContext context {
        .kind = ImportNameKind::kMaterial,
        .ordinal = ordinal,
        .parent_name = {},
        .source_id = request.source_path.string(),
      };

      if (const auto renamed
        = request.options.naming_strategy->Rename(authored, context);
        renamed.has_value()) {
        return renamed.value();
      }
    }

    if (!authored.empty()) {
      return std::string(authored);
    }

    return "M_Material_" + std::to_string(ordinal);
  }

  [[nodiscard]] auto BuildMeshName(std::string_view authored,
    const ImportRequest& request, const uint32_t ordinal) -> std::string
  {
    if (request.options.naming_strategy) {
      const NamingContext context {
        .kind = ImportNameKind::kMesh,
        .ordinal = ordinal,
        .parent_name = {},
        .source_id = request.source_path.string(),
      };

      if (const auto renamed
        = request.options.naming_strategy->Rename(authored, context);
        renamed.has_value()) {
        return renamed.value();
      }
    }

    if (!authored.empty()) {
      return std::string(authored);
    }

    return "G_Mesh_" + std::to_string(ordinal);
  }

  [[nodiscard]] auto BuildSceneNodeName(std::string_view authored,
    const ImportRequest& request, const uint32_t ordinal,
    std::string_view parent_name) -> std::string
  {
    if (request.options.naming_strategy) {
      const NamingContext context {
        .kind = ImportNameKind::kSceneNode,
        .ordinal = ordinal,
        .parent_name = parent_name,
        .source_id = request.source_path.string(),
      };

      if (const auto renamed
        = request.options.naming_strategy->Rename(authored, context);
        renamed.has_value()) {
        return renamed.value();
      }
    }

    if (!authored.empty()) {
      return std::string(authored);
    }

    return "N_Node_" + std::to_string(ordinal);
  }

  [[nodiscard]] auto BuildSceneName(const ImportRequest& request) -> std::string
  {
    const auto stem = request.source_path.stem().string();
    if (!stem.empty()) {
      return stem;
    }
    return "Scene";
  }

  [[nodiscard]] auto NamespaceImportedAssetName(
    const ImportRequest& request, const std::string_view name) -> std::string
  {
    const auto scene_name = BuildSceneName(request);
    if (scene_name.empty()) {
      return std::string(name);
    }
    if (name.empty()) {
      return scene_name;
    }
    return scene_name + "/" + std::string(name);
  }

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

  struct ImportedGeometry final {
    const ufbx_mesh* mesh = nullptr;
    AssetKey key = {};
  };

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
        const auto desc = ToStringView(error.description);
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
        WriteGeometry_(*scene, request, out, material_keys, imported_geometry,
          written_geometry, want_textures);
      }

      if (written_geometry > 0) {
        out.OnGeometryWritten(written_geometry);
      }

      uint32_t written_scenes = 0;
      if (want_scene) {
        WriteScene_(*scene, request, out, imported_geometry, written_scenes);
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
        const auto name = BuildMaterialName("M_Default", request, 0);
        const auto key = WriteOneMaterial_(
          request, out, name, 0, nullptr, textures, want_textures);
        out_keys.push_back(key);
        written_materials += 1;
        return;
      }

      for (uint32_t i = 0; i < count; ++i) {
        const auto* mat = scene.materials.data[i];
        const auto authored_name
          = (mat != nullptr) ? ToStringView(mat->name) : std::string_view {};
        const auto name = BuildMaterialName(authored_name, request, i);

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
        = NamespaceImportedAssetName(request, material_name);
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

        const auto shading_model = ToStringView(material->shading_model_name);
        const auto fbx_material_name = ToStringView(material->name);

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

        LOG_F(INFO, "Material '{}': shader_type={} model='{}' is_lambert={}",
          fbx_material_name, (int)material->shader_type, shading_model,
          is_lambert);

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

        desc.base_color_texture = base_color_index;
        desc.normal_texture = normal_index;
        desc.metallic_texture = metallic_index;
        desc.roughness_texture = roughness_index;
        desc.ambient_occlusion_texture = ao_index;

        if (base_color_index != 0 || normal_index != 0 || metallic_index != 0
          || roughness_index != 0 || ao_index != 0) {
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

    static auto WriteGeometry_(const ufbx_scene& scene,
      const ImportRequest& request, CookedContentWriter& out,
      const std::vector<AssetKey>& material_keys,
      std::vector<ImportedGeometry>& out_geometry, uint32_t& written_geometry,
      const bool want_textures) -> void
    {
      using oxygen::data::BufferResource;
      using oxygen::data::MeshType;
      using oxygen::data::Vertex;
      using oxygen::data::loose_cooked::v1::FileKind;
      using oxygen::data::pak::BufferResourceDesc;
      using oxygen::data::pak::GeometryAssetDesc;
      using oxygen::data::pak::MeshDesc;
      using oxygen::data::pak::MeshViewDesc;
      using oxygen::data::pak::SubMeshDesc;

      const auto cooked_root = request.cooked_root.value_or(
        std::filesystem::absolute(request.source_path.parent_path()));
      const auto buffers_table_path = cooked_root
        / std::filesystem::path(
          request.loose_cooked_layout.BuffersTableRelPath());
      const auto buffers_data_path = cooked_root
        / std::filesystem::path(
          request.loose_cooked_layout.BuffersDataRelPath());

      auto buffers
        = emit::InitBufferEmissionState(buffers_table_path, buffers_data_path);
      emit::BuildBufferSignatureIndex(buffers, buffers_data_path);

      auto effective_material_keys = material_keys;
      if (effective_material_keys.empty()) {
        const auto count = static_cast<uint32_t>(scene.materials.count);
        if (count == 0) {
          const auto name = BuildMaterialName("M_Default", request, 0);
          const auto storage_name = NamespaceImportedAssetName(request, name);
          const auto virtual_path
            = request.loose_cooked_layout.MaterialVirtualPath(storage_name);

          AssetKey key {};
          switch (request.options.asset_key_policy) {
          case AssetKeyPolicy::kDeterministicFromVirtualPath:
            key = MakeDeterministicAssetKey(virtual_path);
            break;
          case AssetKeyPolicy::kRandom:
            key = MakeRandomAssetKey();
            break;
          }

          effective_material_keys.push_back(key);
        } else {
          effective_material_keys.reserve(count);
          for (uint32_t i = 0; i < count; ++i) {
            const auto* mat = scene.materials.data[i];
            const auto authored_name = (mat != nullptr)
              ? ToStringView(mat->name)
              : std::string_view {};
            const auto name = BuildMaterialName(authored_name, request, i);
            const auto storage_name = NamespaceImportedAssetName(request, name);
            const auto virtual_path
              = request.loose_cooked_layout.MaterialVirtualPath(storage_name);

            AssetKey key {};
            switch (request.options.asset_key_policy) {
            case AssetKeyPolicy::kDeterministicFromVirtualPath:
              key = MakeDeterministicAssetKey(virtual_path);
              break;
            case AssetKeyPolicy::kRandom:
              key = MakeRandomAssetKey();
              break;
            }

            effective_material_keys.push_back(key);
          }
        }
      }

      // Helper to find nodes that reference a specific mesh
      auto FindNodesForMesh
        = [&](const ufbx_mesh* target_mesh) -> std::vector<const ufbx_node*> {
        std::vector<const ufbx_node*> nodes;
        for (size_t ni = 0; ni < scene.nodes.count; ++ni) {
          const auto* node = scene.nodes.data[ni];
          if (node != nullptr && node->mesh == target_mesh) {
            nodes.push_back(node);
          }
        }
        return nodes;
      };

      // Track used geometry names to detect collisions
      std::unordered_map<std::string, uint32_t> geometry_name_usage_count;

      const auto mesh_count = static_cast<uint32_t>(scene.meshes.count);
      for (uint32_t i = 0; i < mesh_count; ++i) {
        const auto* mesh = scene.meshes.data[i];
        if (mesh == nullptr || mesh->num_indices == 0 || mesh->num_faces == 0) {
          continue;
        }

        std::unordered_map<const ufbx_material*, uint32_t>
          scene_material_index_by_ptr;
        scene_material_index_by_ptr.reserve(
          static_cast<size_t>(scene.materials.count));

        std::unordered_map<const ufbx_material*, AssetKey> material_key_by_ptr;
        material_key_by_ptr.reserve(static_cast<size_t>(scene.materials.count));

        for (uint32_t mat_i = 0; mat_i < scene.materials.count; ++mat_i) {
          const auto* mat = scene.materials.data[mat_i];
          if (mat == nullptr) {
            continue;
          }
          scene_material_index_by_ptr.emplace(mat, mat_i);
          if (mat_i < effective_material_keys.size()) {
            material_key_by_ptr.emplace(mat, effective_material_keys[mat_i]);
          }
        }

        if (!mesh->vertex_position.exists
          || mesh->vertex_position.values.data == nullptr
          || mesh->vertex_position.indices.data == nullptr) {
          ImportDiagnostic diag {
            .severity = ImportSeverity::kError,
            .code = "fbx.mesh.missing_positions",
            .message = "FBX mesh is missing vertex positions",
            .source_path = request.source_path.string(),
            .object_path = std::string(ToStringView(mesh->name)),
          };
          out.AddDiagnostic(std::move(diag));
          throw std::runtime_error("FBX mesh missing positions");
        }

        const auto authored_name = ToStringView(mesh->name);
        auto mesh_name = BuildMeshName(authored_name, request, i);
        const auto original_mesh_name = mesh_name;

        // Check for name collision and disambiguate using node name if needed
        if (const auto it = geometry_name_usage_count.find(mesh_name);
          it != geometry_name_usage_count.end()) {
          // Collision detected - must rename
          const auto collision_ordinal = it->second;
          std::string new_name;

          // Try to find a node that references this mesh
          const auto nodes = FindNodesForMesh(mesh);
          if (!nodes.empty()) {
            const auto* node = nodes.front();
            const auto node_name = ToStringView(node->name);
            if (!node_name.empty()) {
              // Use pattern: NodeName_MeshName
              const auto prefix = mesh_name.starts_with("G_") ? ""sv : "G_"sv;
              new_name = std::string(prefix) + std::string(node_name) + "_"
                + std::string(authored_name.empty()
                    ? ("Mesh_" + std::to_string(i))
                    : authored_name);
            }
          }

          // Fallback: if we couldn't use a node name, append ordinal
          if (new_name.empty()) {
            new_name = mesh_name + "_" + std::to_string(collision_ordinal);
          }

          LOG_F(INFO,
            "Geometry name collision detected for '{}', renamed to '{}'",
            original_mesh_name.c_str(), new_name.c_str());
          mesh_name = std::move(new_name);
        }
        // Always track the original name to detect future collisions
        geometry_name_usage_count[original_mesh_name]++;

        const bool has_uv = mesh->vertex_uv.exists
          && mesh->vertex_uv.values.data != nullptr
          && mesh->vertex_uv.indices.data != nullptr;

        if (!has_uv && want_textures && mesh->materials.data != nullptr
          && mesh->materials.count > 0) {
          bool has_any_material_texture = false;
          for (uint32_t mi = 0; mi < mesh->materials.count; ++mi) {
            const auto* mat = mesh->materials.data[mi];
            if (mat == nullptr) {
              continue;
            }
            if (emit::SelectBaseColorTexture(*mat) != nullptr
              || emit::SelectNormalTexture(*mat) != nullptr
              || emit::SelectMetallicTexture(*mat) != nullptr
              || emit::SelectRoughnessTexture(*mat) != nullptr
              || emit::SelectAmbientOcclusionTexture(*mat) != nullptr) {
              has_any_material_texture = true;
              break;
            }
          }

          if (has_any_material_texture) {
            ImportDiagnostic diag {
              .severity = ImportSeverity::kWarning,
              .code = "fbx.mesh.missing_uvs",
              .message = "mesh has materials with textures but no UVs; "
                         "texture sampling and normal mapping may be "
                         "incorrect",
              .source_path = request.source_path.string(),
              .object_path = std::string(mesh_name),
            };
            out.AddDiagnostic(std::move(diag));
          }
        }

        std::vector<Vertex> vertices;
        vertices.reserve(mesh->num_indices);

        float bbox_min[3] = {
          (std::numeric_limits<float>::max)(),
          (std::numeric_limits<float>::max)(),
          (std::numeric_limits<float>::max)(),
        };
        float bbox_max[3] = {
          (std::numeric_limits<float>::lowest)(),
          (std::numeric_limits<float>::lowest)(),
          (std::numeric_limits<float>::lowest)(),
        };

        for (size_t idx = 0; idx < mesh->num_indices; ++idx) {
          auto p = mesh->vertex_position[idx];
          p = ApplySwapYZIfEnabled(request.options.coordinate, p);

          Vertex v {
            .position = { static_cast<float>(p.x), static_cast<float>(p.y),
              static_cast<float>(p.z) },
            .normal = { 0.0f, 1.0f, 0.0f },
            .texcoord = { 0.0f, 0.0f },
            .tangent = { 1.0f, 0.0f, 0.0f },
            .bitangent = { 0.0f, 0.0f, 1.0f },
            .color = { 1.0f, 1.0f, 1.0f, 1.0f },
          };

          if (mesh->vertex_normal.exists
            && mesh->vertex_normal.values.data != nullptr
            && mesh->vertex_normal.indices.data != nullptr) {
            auto n = mesh->vertex_normal[idx];
            n = ApplySwapYZDirIfEnabled(request.options.coordinate, n);
            v.normal = { static_cast<float>(n.x), static_cast<float>(n.y),
              static_cast<float>(n.z) };
          }

          if (has_uv) {
            const auto uv = mesh->vertex_uv[idx];
            v.texcoord = { static_cast<float>(uv.x), static_cast<float>(uv.y) };
          }

          const auto tangent_policy = request.options.tangent_policy;
          const bool preserve_authored_tangents
            = tangent_policy == GeometryAttributePolicy::kPreserveIfPresent
            || tangent_policy == GeometryAttributePolicy::kGenerateMissing;

          if (preserve_authored_tangents && mesh->vertex_tangent.exists
            && mesh->vertex_tangent.values.data != nullptr
            && mesh->vertex_tangent.indices.data != nullptr) {
            auto t = mesh->vertex_tangent[idx];
            t = ApplySwapYZDirIfEnabled(request.options.coordinate, t);
            // Validate for NaN and finite values before using
            const auto tx = static_cast<float>(t.x);
            const auto ty = static_cast<float>(t.y);
            const auto tz = static_cast<float>(t.z);
            if (std::isfinite(tx) && std::isfinite(ty) && std::isfinite(tz)) {
              v.tangent = { tx, ty, tz };
            }
            // else: keep default tangent, will be fixed in final validation
          }

          if (preserve_authored_tangents && mesh->vertex_bitangent.exists
            && mesh->vertex_bitangent.values.data != nullptr
            && mesh->vertex_bitangent.indices.data != nullptr) {
            auto b = mesh->vertex_bitangent[idx];
            b = ApplySwapYZDirIfEnabled(request.options.coordinate, b);
            // Validate for NaN and finite values before using
            const auto bx = static_cast<float>(b.x);
            const auto by = static_cast<float>(b.y);
            const auto bz = static_cast<float>(b.z);
            if (std::isfinite(bx) && std::isfinite(by) && std::isfinite(bz)) {
              v.bitangent = { bx, by, bz };
            }
            // else: keep default bitangent, will be fixed in final validation
          }

          if (mesh->vertex_color.exists
            && mesh->vertex_color.values.data != nullptr
            && mesh->vertex_color.indices.data != nullptr) {
            const auto c = mesh->vertex_color[idx];
            v.color = { static_cast<float>(c.x), static_cast<float>(c.y),
              static_cast<float>(c.z), static_cast<float>(c.w) };
          }

          vertices.push_back(v);

          bbox_min[0] = (std::min)(bbox_min[0], v.position.x);
          bbox_min[1] = (std::min)(bbox_min[1], v.position.y);
          bbox_min[2] = (std::min)(bbox_min[2], v.position.z);
          bbox_max[0] = (std::max)(bbox_max[0], v.position.x);
          bbox_max[1] = (std::max)(bbox_max[1], v.position.y);
          bbox_max[2] = (std::max)(bbox_max[2], v.position.z);
        }

        struct SubmeshBucket final {
          uint32_t scene_material_index = 0;
          AssetKey material_key {};
          std::vector<uint32_t> indices;
        };

        std::unordered_map<uint32_t, size_t> bucket_index_by_material;
        std::vector<SubmeshBucket> buckets;

        std::vector<uint32_t> tri_indices;
        tri_indices.resize(static_cast<size_t>(mesh->max_face_triangles) * 3);

        const auto default_material_key = (!effective_material_keys.empty())
          ? effective_material_keys.front()
          : AssetKey {};

        auto resolve_bucket = [&](const size_t face_i) -> SubmeshBucket& {
          uint32_t scene_material_index = 0;
          AssetKey material_key = default_material_key;

          if (mesh->face_material.data != nullptr
            && face_i < mesh->face_material.count && mesh->materials.data
            && mesh->materials.count > 0) {
            const uint32_t slot = mesh->face_material.data[face_i];
            if (slot != UFBX_NO_INDEX && slot < mesh->materials.count) {
              const auto* mat = mesh->materials.data[slot];
              if (mat != nullptr) {
                if (const auto it = scene_material_index_by_ptr.find(mat);
                  it != scene_material_index_by_ptr.end()) {
                  scene_material_index = it->second;
                }

                if (const auto it = material_key_by_ptr.find(mat);
                  it != material_key_by_ptr.end()) {
                  material_key = it->second;
                }
              }
            }
          }

          const auto found
            = bucket_index_by_material.find(scene_material_index);
          if (found != bucket_index_by_material.end()) {
            return buckets[found->second];
          }

          const auto bucket_i = buckets.size();
          bucket_index_by_material.emplace(scene_material_index, bucket_i);
          buckets.push_back(SubmeshBucket {
            .scene_material_index = scene_material_index,
            .material_key = material_key,
            .indices = {},
          });
          return buckets.back();
        };

        for (size_t face_i = 0; face_i < mesh->faces.count; ++face_i) {
          const auto face = mesh->faces.data[face_i];
          if (face.num_indices < 3) {
            continue;
          }

          auto& bucket = resolve_bucket(face_i);
          const auto tri_count = ufbx_triangulate_face(
            tri_indices.data(), tri_indices.size(), mesh, face);
          bucket.indices.insert(bucket.indices.end(), tri_indices.begin(),
            tri_indices.begin() + static_cast<ptrdiff_t>(tri_count) * 3);
        }

        buckets.erase(
          std::remove_if(buckets.begin(), buckets.end(),
            [](const SubmeshBucket& b) { return b.indices.empty(); }),
          buckets.end());

        std::sort(buckets.begin(), buckets.end(),
          [](const SubmeshBucket& a, const SubmeshBucket& b) {
            return a.scene_material_index < b.scene_material_index;
          });

        std::vector<uint32_t> indices;
        {
          size_t total = 0;
          for (const auto& b : buckets) {
            total += b.indices.size();
          }
          indices.reserve(total);
        }

        if (vertices.empty() || buckets.empty()) {
          ImportDiagnostic diag {
            .severity = ImportSeverity::kError,
            .code = "fbx.mesh.missing_buffers",
            .message = "FBX mesh does not produce valid vertex/index buffers",
            .source_path = request.source_path.string(),
            .object_path = std::string(mesh_name),
          };
          out.AddDiagnostic(std::move(diag));
          throw std::runtime_error("FBX mesh produced empty buffers");
        }

        // If tangents/bitangents were not authored, generate a consistent
        // per-vertex TBN basis from triangles (required for normal mapping).
        const auto tangent_policy = request.options.tangent_policy;
        const bool has_authored_tangents
          = mesh->vertex_tangent.exists && mesh->vertex_bitangent.exists;

        const bool should_generate_tangents
          = (tangent_policy == GeometryAttributePolicy::kGenerateMissing
              && !has_authored_tangents)
          || (tangent_policy == GeometryAttributePolicy::kAlwaysRecalculate);

        bool has_any_indices = false;
        for (const auto& b : buckets) {
          if (b.indices.size() >= 3) {
            has_any_indices = true;
            break;
          }
        }

        if (tangent_policy != GeometryAttributePolicy::kNone
          && should_generate_tangents && has_uv && has_any_indices) {
          std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0F));
          std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0F));

          for (const auto& bucket : buckets) {
            const auto tri_count = bucket.indices.size() / 3;
            for (size_t tri = 0; tri < tri_count; ++tri) {
              const auto i0 = bucket.indices[tri * 3 + 0];
              const auto i1 = bucket.indices[tri * 3 + 1];
              const auto i2 = bucket.indices[tri * 3 + 2];
              if (i0 >= vertices.size() || i1 >= vertices.size()
                || i2 >= vertices.size()) {
                continue;
              }

              const auto& v0 = vertices[i0];
              const auto& v1 = vertices[i1];
              const auto& v2 = vertices[i2];

              const glm::vec3 p0 = v0.position;
              const glm::vec3 p1 = v1.position;
              const glm::vec3 p2 = v2.position;

              const glm::vec2 w0 = v0.texcoord;
              const glm::vec2 w1 = v1.texcoord;
              const glm::vec2 w2 = v2.texcoord;

              const glm::vec3 e1 = p1 - p0;
              const glm::vec3 e2 = p2 - p0;
              const glm::vec2 d1 = w1 - w0;
              const glm::vec2 d2 = w2 - w0;

              const float denom = d1.x * d2.y - d1.y * d2.x;
              if (std::abs(denom) < 1e-8F) {
                continue;
              }
              const float r = 1.0F / denom;

              const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
              const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;

              tan1[i0] += t;
              tan1[i1] += t;
              tan1[i2] += t;

              tan2[i0] += b;
              tan2[i1] += b;
              tan2[i2] += b;
            }
          }

          for (size_t vi = 0; vi < vertices.size(); ++vi) {
            auto n = vertices[vi].normal;
            const auto n_len = glm::length(n);
            if (n_len > 1e-8F) {
              n /= n_len;
            } else {
              n = glm::vec3(0.0F, 0.0F, 1.0F); // Default up in Z-up system
            }

            glm::vec3 t = tan1[vi];
            if (glm::length(t) < 1e-8F) {
              // No accumulated tangent - generate a fallback perpendicular to
              // normal Choose an axis that isn't parallel to the normal
              const glm::vec3 axis = (std::abs(n.z) < 0.9F)
                ? glm::vec3(0.0F, 0.0F, 1.0F)
                : glm::vec3(1.0F, 0.0F, 0.0F);
              t = glm::normalize(glm::cross(n, axis));
            } else {
              // Gram-Schmidt orthonormalization
              t = glm::normalize(t - n * glm::dot(n, t));
            }

            glm::vec3 b = glm::cross(n, t);
            if (glm::dot(b, tan2[vi]) < 0.0F) {
              b = -b;
            }
            const auto b_len = glm::length(b);
            if (b_len > 1e-8F) {
              b = b / b_len;
            } else {
              // Fallback bitangent
              b = glm::normalize(glm::cross(n, t));
            }

            vertices[vi].normal = n;
            vertices[vi].tangent = t;
            vertices[vi].bitangent = b;
          }
        }

        // Final validation pass: fix any zero-length, NaN, or Inf
        // tangents/bitangents This handles cases where authored tangents exist
        // but are invalid
        for (size_t vi = 0; vi < vertices.size(); ++vi) {
          auto& v = vertices[vi];

          // Check for NaN or Inf in tangent/bitangent
          const bool t_has_nan = !std::isfinite(v.tangent.x)
            || !std::isfinite(v.tangent.y) || !std::isfinite(v.tangent.z);
          const bool b_has_nan = !std::isfinite(v.bitangent.x)
            || !std::isfinite(v.bitangent.y) || !std::isfinite(v.bitangent.z);

          const auto t_len = t_has_nan ? 0.0F : glm::length(v.tangent);
          const auto b_len = b_has_nan ? 0.0F : glm::length(v.bitangent);

          // Tangents should be normalized (length ~1.0). If too short or
          // invalid, regenerate. We accept a range of 0.5 to 2.0 to allow for
          // minor precision issues, but anything outside that is suspicious.
          constexpr float kMinValidLen = 0.5F;
          constexpr float kMaxValidLen = 2.0F;
          const bool t_invalid
            = t_has_nan || t_len < kMinValidLen || t_len > kMaxValidLen;
          const bool b_invalid
            = b_has_nan || b_len < kMinValidLen || b_len > kMaxValidLen;

          if (t_invalid || b_invalid) {
            // Generate fallback TBN from normal
            glm::vec3 n = v.normal;
            // Also check for NaN/Inf in normal
            if (!std::isfinite(n.x) || !std::isfinite(n.y)
              || !std::isfinite(n.z) || glm::length(n) < 1e-6F) {
              n = glm::vec3(0.0F, 0.0F, 1.0F); // Z-up default
            } else {
              n = glm::normalize(n);
            }

            // Choose axis not parallel to normal
            const glm::vec3 axis = (std::abs(n.z) < 0.9F)
              ? glm::vec3(0.0F, 0.0F, 1.0F)
              : glm::vec3(1.0F, 0.0F, 0.0F);

            glm::vec3 t = glm::normalize(glm::cross(n, axis));
            glm::vec3 b = glm::normalize(glm::cross(n, t));

            v.tangent = t;
            v.bitangent = b;
          } else {
            // Normalize valid tangents to ensure unit length
            v.tangent = glm::normalize(v.tangent);
            v.bitangent = glm::normalize(v.bitangent);
          }
        }

        // --- Emit buffer resources (vertex + index) ---
        const auto vb_bytes = std::as_bytes(std::span(vertices));
        constexpr uint32_t vb_stride = sizeof(Vertex);

        const auto vb_usage_flags
          = static_cast<uint32_t>(BufferResource::UsageFlags::kVertexBuffer)
          | static_cast<uint32_t>(BufferResource::UsageFlags::kStatic);
        const auto vb_index = emit::GetOrCreateBufferResourceIndex(buffers,
          vb_bytes, vb_stride, vb_usage_flags, vb_stride,
          static_cast<uint8_t>(oxygen::Format::kUnknown));

        std::vector<SubMeshDesc> submeshes;
        submeshes.reserve(buckets.size());
        std::vector<MeshViewDesc> views;
        views.reserve(buckets.size());

        MeshViewDesc::BufferIndexT index_cursor = 0;
        for (const auto& bucket : buckets) {
          float sm_bbox_min[3] = {
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(),
          };
          float sm_bbox_max[3] = {
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)(),
          };

          for (const auto vi : bucket.indices) {
            if (vi >= vertices.size()) {
              continue;
            }
            const auto& v = vertices[vi];
            sm_bbox_min[0] = (std::min)(sm_bbox_min[0], v.position.x);
            sm_bbox_min[1] = (std::min)(sm_bbox_min[1], v.position.y);
            sm_bbox_min[2] = (std::min)(sm_bbox_min[2], v.position.z);
            sm_bbox_max[0] = (std::max)(sm_bbox_max[0], v.position.x);
            sm_bbox_max[1] = (std::max)(sm_bbox_max[1], v.position.y);
            sm_bbox_max[2] = (std::max)(sm_bbox_max[2], v.position.z);
          }

          const auto name
            = "mat_" + std::to_string(bucket.scene_material_index);

          SubMeshDesc sm {};
          TruncateAndNullTerminate(sm.name, std::size(sm.name), name);
          sm.material_asset_key = bucket.material_key;
          sm.mesh_view_count = 1;
          std::copy_n(sm_bbox_min, 3, sm.bounding_box_min);
          std::copy_n(sm_bbox_max, 3, sm.bounding_box_max);
          submeshes.push_back(sm);

          const auto first_index = index_cursor;
          const auto index_count
            = static_cast<MeshViewDesc::BufferIndexT>(bucket.indices.size());
          index_cursor += index_count;

          views.push_back(MeshViewDesc {
            .first_index = first_index,
            .index_count = index_count,
            .first_vertex = 0,
            .vertex_count
            = static_cast<MeshViewDesc::BufferIndexT>(vertices.size()),
          });

          indices.insert(
            indices.end(), bucket.indices.begin(), bucket.indices.end());
        }

        const auto ib_bytes = std::as_bytes(std::span(indices));

        const auto ib_usage_flags
          = static_cast<uint32_t>(BufferResource::UsageFlags::kIndexBuffer)
          | static_cast<uint32_t>(BufferResource::UsageFlags::kStatic);
        const auto ib_index = emit::GetOrCreateBufferResourceIndex(buffers,
          ib_bytes, alignof(uint32_t), ib_usage_flags, 0,
          static_cast<uint8_t>(oxygen::Format::kR32UInt));

        // --- Emit geometry asset descriptor (desc + mesh + submesh + view) ---
        const auto storage_mesh_name
          = NamespaceImportedAssetName(request, mesh_name);

        const auto geo_virtual_path
          = request.loose_cooked_layout.GeometryVirtualPath(storage_mesh_name);
        const auto geo_relpath
          = request.loose_cooked_layout.DescriptorDirFor(AssetType::kGeometry)
          + "/"
          + LooseCookedLayout::GeometryDescriptorFileName(storage_mesh_name);

        AssetKey geo_key {};
        switch (request.options.asset_key_policy) {
        case AssetKeyPolicy::kDeterministicFromVirtualPath:
          geo_key = MakeDeterministicAssetKey(geo_virtual_path);
          break;
        case AssetKeyPolicy::kRandom:
          geo_key = MakeRandomAssetKey();
          break;
        }

        GeometryAssetDesc geo_desc {};
        geo_desc.header.asset_type = static_cast<uint8_t>(AssetType::kGeometry);
        TruncateAndNullTerminate(
          geo_desc.header.name, std::size(geo_desc.header.name), mesh_name);
        geo_desc.lod_count = 1;
        std::copy_n(bbox_min, 3, geo_desc.bounding_box_min);
        std::copy_n(bbox_max, 3, geo_desc.bounding_box_max);

        MeshDesc lod0 {};
        TruncateAndNullTerminate(lod0.name, std::size(lod0.name), mesh_name);
        lod0.mesh_type = static_cast<uint8_t>(MeshType::kStandard);
        lod0.submesh_count = static_cast<uint32_t>(submeshes.size());
        lod0.mesh_view_count = static_cast<uint32_t>(views.size());
        lod0.info.standard.vertex_buffer = vb_index;
        lod0.info.standard.index_buffer = ib_index;
        std::copy_n(bbox_min, 3, lod0.info.standard.bounding_box_min);
        std::copy_n(bbox_max, 3, lod0.info.standard.bounding_box_max);

        oxygen::serio::MemoryStream desc_stream;
        oxygen::serio::Writer<oxygen::serio::MemoryStream> writer(desc_stream);
        const auto pack = writer.ScopedAlignment(1);
        (void)writer.WriteBlob(
          std::as_bytes(std::span<const GeometryAssetDesc, 1>(&geo_desc, 1)));
        (void)writer.WriteBlob(
          std::as_bytes(std::span<const MeshDesc, 1>(&lod0, 1)));
        for (size_t sm_i = 0; sm_i < submeshes.size(); ++sm_i) {
          const auto& sm = submeshes[sm_i];
          const auto& view = views[sm_i];
          (void)writer.WriteBlob(
            std::as_bytes(std::span<const SubMeshDesc, 1>(&sm, 1)));
          (void)writer.WriteBlob(
            std::as_bytes(std::span<const MeshViewDesc, 1>(&view, 1)));
        }

        const auto geo_bytes = desc_stream.Data();

        LOG_F(INFO,
          "Emit geometry {} '{}' -> {} (vb={}, ib={}, vtx={}, idx={})",
          written_geometry, mesh_name.c_str(), geo_relpath.c_str(), vb_index,
          ib_index, vertices.size(), indices.size());

        out.WriteAssetDescriptor(geo_key, AssetType::kGeometry,
          geo_virtual_path, geo_relpath, geo_bytes);

        out_geometry.push_back(ImportedGeometry {
          .mesh = mesh,
          .key = geo_key,
        });

        written_geometry += 1;
      }

      // Close the data file appender (flushes any pending writes)
      emit::CloseAppender(buffers.appender);

      if (buffers.table.empty()) {
        return;
      }

      LOG_F(INFO, "Emit buffers table: count={} data_file='{}' -> table='{}'",
        buffers.table.size(),
        request.loose_cooked_layout.BuffersDataRelPath().c_str(),
        request.loose_cooked_layout.BuffersTableRelPath().c_str());

      oxygen::serio::MemoryStream table_stream;
      oxygen::serio::Writer<oxygen::serio::MemoryStream> table_writer(
        table_stream);
      const auto pack = table_writer.ScopedAlignment(1);
      (void)table_writer.WriteBlob(
        std::as_bytes(std::span<const BufferResourceDesc>(buffers.table)));

      out.WriteFile(FileKind::kBuffersTable,
        request.loose_cooked_layout.BuffersTableRelPath(), table_stream.Data());

      // Register the externally-written data file
      out.RegisterExternalFile(FileKind::kBuffersData,
        request.loose_cooked_layout.BuffersDataRelPath());
    }

    static auto WriteScene_(const ufbx_scene& scene,
      const ImportRequest& request, CookedContentWriter& out,
      const std::vector<ImportedGeometry>& geometry, uint32_t& written_scenes)
      -> void
    {
      using oxygen::data::AssetType;
      using oxygen::data::ComponentType;
      using oxygen::data::pak::DirectionalLightRecord;
      using oxygen::data::pak::NodeRecord;
      using oxygen::data::pak::OrthographicCameraRecord;
      using oxygen::data::pak::PerspectiveCameraRecord;
      using oxygen::data::pak::PointLightRecord;
      using oxygen::data::pak::RenderableRecord;
      using oxygen::data::pak::SceneAssetDesc;
      using oxygen::data::pak::SceneComponentTableDesc;
      using oxygen::data::pak::SceneEnvironmentBlockHeader;
      using oxygen::data::pak::SpotLightRecord;

      const auto scene_name = BuildSceneName(request);
      const auto virtual_path
        = request.loose_cooked_layout.SceneVirtualPath(scene_name);
      const auto relpath
        = request.loose_cooked_layout.SceneDescriptorRelPath(scene_name);

      AssetKey scene_key {};
      switch (request.options.asset_key_policy) {
      case AssetKeyPolicy::kDeterministicFromVirtualPath:
        scene_key = MakeDeterministicAssetKey(virtual_path);
        break;
      case AssetKeyPolicy::kRandom:
        scene_key = MakeRandomAssetKey();
        break;
      }

      std::vector<NodeRecord> nodes;
      nodes.reserve(static_cast<size_t>(scene.nodes.count));

      std::vector<std::byte> strings;
      strings.push_back(std::byte { 0 });

      std::vector<RenderableRecord> renderables;
      renderables.reserve(static_cast<size_t>(scene.nodes.count));

      std::vector<PerspectiveCameraRecord> perspective_cameras;
      perspective_cameras.reserve(static_cast<size_t>(scene.nodes.count));

      std::vector<OrthographicCameraRecord> orthographic_cameras;
      orthographic_cameras.reserve(static_cast<size_t>(scene.nodes.count));

      std::vector<DirectionalLightRecord> directional_lights;
      directional_lights.reserve(static_cast<size_t>(scene.nodes.count));

      std::vector<PointLightRecord> point_lights;
      point_lights.reserve(static_cast<size_t>(scene.nodes.count));

      std::vector<SpotLightRecord> spot_lights;
      spot_lights.reserve(static_cast<size_t>(scene.nodes.count));

      size_t camera_attr_total = 0;
      size_t camera_attr_skipped = 0;

      size_t light_attr_total = 0;
      size_t light_attr_skipped = 0;

      struct NodeRef final {
        const ufbx_node* node = nullptr;
        uint32_t index = 0;
        std::string name;
      };
      std::vector<NodeRef> node_refs;
      node_refs.reserve(static_cast<size_t>(scene.nodes.count));

      auto FindGeometryKey
        = [&](const ufbx_mesh* mesh) -> std::optional<AssetKey> {
        if (mesh == nullptr) {
          return std::nullopt;
        }
        for (const auto& g : geometry) {
          if (g.mesh == mesh) {
            return g.key;
          }
        }
        return std::nullopt;
      };

      auto AppendString =
        [&](const std::string_view s) -> oxygen::data::pak::StringTableOffsetT {
        const auto offset
          = static_cast<oxygen::data::pak::StringTableOffsetT>(strings.size());
        const auto bytes
          = std::as_bytes(std::span<const char>(s.data(), s.size()));
        strings.insert(strings.end(), bytes.begin(), bytes.end());
        strings.push_back(std::byte { 0 });
        return offset;
      };

      auto MakeNodeKey
        = [&](const std::string_view node_virtual_path) -> AssetKey {
        return MakeDeterministicAssetKey(node_virtual_path);
      };

      auto Traverse
        = [&](auto&& self, const ufbx_node* n, uint32_t parent_index,
            std::string_view parent_name, uint32_t& ordinal) -> void {
        if (n == nullptr) {
          return;
        }

        const auto authored_name = ToStringView(n->name);
        auto name
          = BuildSceneNodeName(authored_name, request, ordinal, parent_name);

        NodeRecord rec {};
        const auto node_virtual_path = virtual_path + "/" + std::string(name);
        rec.node_id = MakeNodeKey(node_virtual_path);
        rec.scene_name_offset = AppendString(name);
        rec.parent_index = parent_index;
        rec.node_flags = oxygen::data::pak::kSceneNodeFlag_Visible;

        // Use ufbx's post-conversion local TRS directly.
        //
        // Rationale: When `opts.target_axes` / `opts.target_unit_meters` is
        // set, ufbx computes a consistent local TRS for each node in the target
        // coordinate system. Reconstructing TRS from matrices (whether via
        // generic decomposition or matrix-to-TRS) can re-introduce sign/
        // reflection ambiguity and lead to flips.
        const ufbx_transform local_trs = ApplySwapYZIfEnabled(
          request.options.coordinate, n->local_transform);

        LOG_F(INFO,
          "Node '{}' (ordinal={}) local_trs: T=({:.3f}, {:.3f}, {:.3f}) "
          "R=({:.3f}, {:.3f}, {:.3f}, {:.3f}) S=({:.3f}, {:.3f}, {:.3f})",
          name, ordinal, local_trs.translation.x, local_trs.translation.y,
          local_trs.translation.z, local_trs.rotation.x, local_trs.rotation.y,
          local_trs.rotation.z, local_trs.rotation.w, local_trs.scale.x,
          local_trs.scale.y, local_trs.scale.z);

        rec.translation[0] = static_cast<float>(local_trs.translation.x);
        rec.translation[1] = static_cast<float>(local_trs.translation.y);
        rec.translation[2] = static_cast<float>(local_trs.translation.z);

        // Store quaternion as x,y,z,w in NodeRecord.
        rec.rotation[0] = static_cast<float>(local_trs.rotation.x);
        rec.rotation[1] = static_cast<float>(local_trs.rotation.y);
        rec.rotation[2] = static_cast<float>(local_trs.rotation.z);
        rec.rotation[3] = static_cast<float>(local_trs.rotation.w);

        rec.scale[0] = static_cast<float>(local_trs.scale.x);
        rec.scale[1] = static_cast<float>(local_trs.scale.y);
        rec.scale[2] = static_cast<float>(local_trs.scale.z);

        const auto index = static_cast<uint32_t>(nodes.size());
        if (index == 0) {
          rec.parent_index = 0;
        }

        nodes.push_back(rec);
        node_refs.push_back(NodeRef {
          .node = n,
          .index = index,
          .name = name,
        });

        if (const auto geo_key = FindGeometryKey(n->mesh);
          geo_key.has_value()) {
          renderables.push_back(RenderableRecord {
            .node_index = index,
            .geometry_key = *geo_key,
            .visible = 1,
            .reserved = {},
          });
        }

        if (n->camera != nullptr) {
          const auto* cam = n->camera;
          ++camera_attr_total;
          if (cam->projection_mode == UFBX_PROJECTION_MODE_PERSPECTIVE) {
            float near_plane = std::abs(static_cast<float>(cam->near_plane));
            float far_plane = std::abs(static_cast<float>(cam->far_plane));
            if (far_plane < near_plane) {
              std::swap(far_plane, near_plane);
            }
            const float fov_y_rad = static_cast<float>(cam->field_of_view_deg.y)
              * (std::numbers::pi_v<float> / 180.0f);
            perspective_cameras.push_back(PerspectiveCameraRecord {
              .node_index = index,
              .fov_y = fov_y_rad,
              .aspect_ratio = static_cast<float>(cam->aspect_ratio),
              .near_plane = near_plane,
              .far_plane = far_plane,
              .reserved = {},
            });
          } else if (cam->projection_mode
            == UFBX_PROJECTION_MODE_ORTHOGRAPHIC) {
            float near_plane = std::abs(static_cast<float>(cam->near_plane));
            float far_plane = std::abs(static_cast<float>(cam->far_plane));
            if (far_plane < near_plane) {
              std::swap(far_plane, near_plane);
            }
            const float half_w
              = static_cast<float>(cam->orthographic_size.x) * 0.5f;
            const float half_h
              = static_cast<float>(cam->orthographic_size.y) * 0.5f;
            orthographic_cameras.push_back(OrthographicCameraRecord {
              .node_index = index,
              .left = -half_w,
              .right = half_w,
              .bottom = -half_h,
              .top = half_h,
              .near_plane = near_plane,
              .far_plane = far_plane,
              .reserved = {},
            });
          } else {
            ++camera_attr_skipped;
            LOG_F(INFO,
              "Scene camera attribute skipped: node_index={} name='{}' "
              "projection_mode={}",
              index, name.c_str(), static_cast<int>(cam->projection_mode));
          }
        }

        if (n->light != nullptr) {
          const auto* light = n->light;
          ++light_attr_total;

          const auto type = light->type;
          const auto [atten_model, decay_exponent]
            = MapDecayToAttenuation(light->decay);

          switch (type) {
          case UFBX_LIGHT_DIRECTIONAL: {
            DirectionalLightRecord rec_light {};
            rec_light.node_index = index;
            FillLightCommon(*light, rec_light.common);

            // Best-effort authored properties.
            if (const auto v = TryFindRealProp(light->props, "AngularSize");
              v.has_value()) {
              rec_light.angular_size_radians = ToRadiansHeuristic(*v);
            } else if (const auto v2
              = TryFindRealProp(light->props, "AngularDiameter");
              v2.has_value()) {
              rec_light.angular_size_radians = ToRadiansHeuristic(*v2);
            }

            if (const auto v
              = TryFindBoolProp(light->props, "EnvironmentContribution");
              v.has_value()) {
              rec_light.environment_contribution = v.value() ? 1U : 0U;
            }

            directional_lights.push_back(rec_light);
            break;
          }

          case UFBX_LIGHT_POINT:
          case UFBX_LIGHT_AREA:
          case UFBX_LIGHT_VOLUME: {
            PointLightRecord rec_light {};
            rec_light.node_index = index;
            FillLightCommon(*light, rec_light.common);

            rec_light.attenuation_model = atten_model;
            rec_light.decay_exponent = decay_exponent;

            if (const auto range = ResolveLightRange(*light);
              range.has_value()) {
              rec_light.range = range.value();
            }
            if (const auto r = ResolveLightSourceRadius(*light);
              r.has_value()) {
              rec_light.source_radius = r.value();
            }

            if (type != UFBX_LIGHT_POINT) {
              ++light_attr_skipped;
              ImportDiagnostic diag {
                .severity = ImportSeverity::kWarning,
                .code = "fbx.light.unsupported_type",
                .message
                = "unsupported FBX light type converted to point light",
                .source_path = request.source_path.string(),
                .object_path = std::string(name),
              };
              out.AddDiagnostic(std::move(diag));
            }

            point_lights.push_back(rec_light);
            break;
          }

          case UFBX_LIGHT_SPOT: {
            SpotLightRecord rec_light {};
            rec_light.node_index = index;
            FillLightCommon(*light, rec_light.common);

            rec_light.attenuation_model = atten_model;
            rec_light.decay_exponent = decay_exponent;

            if (const auto range = ResolveLightRange(*light);
              range.has_value()) {
              rec_light.range = range.value();
            }
            if (const auto r = ResolveLightSourceRadius(*light);
              r.has_value()) {
              rec_light.source_radius = r.value();
            }

            const float inner = ToRadiansHeuristic(light->inner_angle);
            const float outer = ToRadiansHeuristic(light->outer_angle);
            rec_light.inner_cone_angle_radians = std::max(0.0F, inner);
            rec_light.outer_cone_angle_radians
              = std::max(rec_light.inner_cone_angle_radians, outer);

            spot_lights.push_back(rec_light);
            break;
          }

          default:
            ++light_attr_skipped;
            break;
          }
        }

        ++ordinal;

        for (size_t i = 0; i < n->children.count; ++i) {
          const auto* child = n->children.data[i];
          self(self, child, index, name, ordinal);
        }
      };

      uint32_t ordinal = 0;
      Traverse(Traverse, scene.root_node, 0, {}, ordinal);

      std::sort(renderables.begin(), renderables.end(),
        [](const RenderableRecord& a, const RenderableRecord& b) {
          return a.node_index < b.node_index;
        });
      std::sort(perspective_cameras.begin(), perspective_cameras.end(),
        [](const PerspectiveCameraRecord& a, const PerspectiveCameraRecord& b) {
          return a.node_index < b.node_index;
        });
      std::sort(orthographic_cameras.begin(), orthographic_cameras.end(),
        [](const OrthographicCameraRecord& a,
          const OrthographicCameraRecord& b) {
          return a.node_index < b.node_index;
        });

      std::sort(directional_lights.begin(), directional_lights.end(),
        [](const DirectionalLightRecord& a, const DirectionalLightRecord& b) {
          return a.node_index < b.node_index;
        });
      std::sort(point_lights.begin(), point_lights.end(),
        [](const PointLightRecord& a, const PointLightRecord& b) {
          return a.node_index < b.node_index;
        });
      std::sort(spot_lights.begin(), spot_lights.end(),
        [](const SpotLightRecord& a, const SpotLightRecord& b) {
          return a.node_index < b.node_index;
        });

      LOG_F(INFO,
        "Scene cameras: camera_attrs={} skipped_attrs={} perspective={} "
        "ortho={}",
        camera_attr_total, camera_attr_skipped, perspective_cameras.size(),
        orthographic_cameras.size());

      LOG_F(INFO,
        "Scene lights: light_attrs={} skipped_or_converted_attrs={} dir={} "
        "point={} spot={}",
        light_attr_total, light_attr_skipped, directional_lights.size(),
        point_lights.size(), spot_lights.size());
      for (const auto& cam : perspective_cameras) {
        const auto name = (cam.node_index < node_refs.size())
          ? node_refs[cam.node_index].name
          : std::string("<invalid>");
        const float fov_y_deg
          = cam.fov_y * (180.0f / std::numbers::pi_v<float>);
        LOG_F(INFO,
          "  PerspectiveCamera node_index={} name='{}' fov_y_deg={} near={} "
          "far={} aspect={}",
          cam.node_index, name.c_str(), fov_y_deg, cam.near_plane,
          cam.far_plane, cam.aspect_ratio);
      }
      for (const auto& cam : orthographic_cameras) {
        const auto name = (cam.node_index < node_refs.size())
          ? node_refs[cam.node_index].name
          : std::string("<invalid>");
        LOG_F(INFO,
          "  OrthographicCamera node_index={} name='{}' l={} r={} b={} t={} "
          "near={} far={}",
          cam.node_index, name.c_str(), cam.left, cam.right, cam.bottom,
          cam.top, cam.near_plane, cam.far_plane);
      }

      if (nodes.empty()) {
        NodeRecord root {};
        const auto root_name = std::string("root");
        root.node_id = MakeNodeKey(virtual_path + "/" + root_name);
        root.scene_name_offset = AppendString(root_name);
        root.parent_index = 0;
        root.node_flags = oxygen::data::pak::kSceneNodeFlag_Visible;
        nodes.push_back(root);
      }

      oxygen::serio::MemoryStream stream;
      oxygen::serio::Writer<oxygen::serio::MemoryStream> writer(stream);
      const auto packed = writer.ScopedAlignment(1);

      SceneAssetDesc desc {};
      desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
      TruncateAndNullTerminate(
        desc.header.name, sizeof(desc.header.name), scene_name);
      // Scene descriptor v2+ includes a trailing SceneEnvironment block.
      desc.header.version = oxygen::data::pak::kSceneAssetVersion;

      desc.nodes.offset = sizeof(SceneAssetDesc);
      desc.nodes.count = static_cast<uint32_t>(nodes.size());
      desc.nodes.entry_size = sizeof(NodeRecord);

      const auto nodes_bytes
        = std::as_bytes(std::span<const NodeRecord>(nodes));

      desc.scene_strings.offset
        = static_cast<oxygen::data::pak::StringTableOffsetT>(
          sizeof(SceneAssetDesc) + nodes_bytes.size());
      desc.scene_strings.size
        = static_cast<oxygen::data::pak::StringTableSizeT>(strings.size());

      const auto strings_bytes = std::span<const std::byte>(strings);

      std::vector<SceneComponentTableDesc> component_dir;
      component_dir.reserve(6);

      auto table_cursor = static_cast<oxygen::data::pak::OffsetT>(
        sizeof(SceneAssetDesc) + nodes_bytes.size() + strings_bytes.size());

      if (!renderables.empty()) {
        component_dir.push_back(SceneComponentTableDesc {
          .component_type = static_cast<uint32_t>(ComponentType::kRenderable),
          .table = {
            .offset = table_cursor,
            .count = static_cast<uint32_t>(renderables.size()),
            .entry_size = sizeof(RenderableRecord),
          },
        });
        table_cursor += static_cast<oxygen::data::pak::OffsetT>(
          renderables.size() * sizeof(RenderableRecord));
      }

      if (!perspective_cameras.empty()) {
        component_dir.push_back(SceneComponentTableDesc {
          .component_type
          = static_cast<uint32_t>(ComponentType::kPerspectiveCamera),
          .table = {
            .offset = table_cursor,
            .count = static_cast<uint32_t>(perspective_cameras.size()),
            .entry_size = sizeof(PerspectiveCameraRecord),
          },
        });
        table_cursor += static_cast<oxygen::data::pak::OffsetT>(
          perspective_cameras.size() * sizeof(PerspectiveCameraRecord));
      }

      if (!orthographic_cameras.empty()) {
        component_dir.push_back(SceneComponentTableDesc {
          .component_type
          = static_cast<uint32_t>(ComponentType::kOrthographicCamera),
          .table = {
            .offset = table_cursor,
            .count = static_cast<uint32_t>(orthographic_cameras.size()),
            .entry_size = sizeof(OrthographicCameraRecord),
          },
        });
        table_cursor += static_cast<oxygen::data::pak::OffsetT>(
          orthographic_cameras.size() * sizeof(OrthographicCameraRecord));
      }

      if (!directional_lights.empty()) {
        component_dir.push_back(SceneComponentTableDesc {
          .component_type
          = static_cast<uint32_t>(ComponentType::kDirectionalLight),
          .table = {
            .offset = table_cursor,
            .count = static_cast<uint32_t>(directional_lights.size()),
            .entry_size = sizeof(DirectionalLightRecord),
          },
        });
        table_cursor += static_cast<oxygen::data::pak::OffsetT>(
          directional_lights.size() * sizeof(DirectionalLightRecord));
      }

      if (!point_lights.empty()) {
        component_dir.push_back(SceneComponentTableDesc {
          .component_type = static_cast<uint32_t>(ComponentType::kPointLight),
          .table = {
            .offset = table_cursor,
            .count = static_cast<uint32_t>(point_lights.size()),
            .entry_size = sizeof(PointLightRecord),
          },
        });
        table_cursor += static_cast<oxygen::data::pak::OffsetT>(
          point_lights.size() * sizeof(PointLightRecord));
      }

      if (!spot_lights.empty()) {
        component_dir.push_back(SceneComponentTableDesc {
          .component_type = static_cast<uint32_t>(ComponentType::kSpotLight),
          .table = {
            .offset = table_cursor,
            .count = static_cast<uint32_t>(spot_lights.size()),
            .entry_size = sizeof(SpotLightRecord),
          },
        });
        table_cursor += static_cast<oxygen::data::pak::OffsetT>(
          spot_lights.size() * sizeof(SpotLightRecord));
      }

      desc.component_table_directory_offset = table_cursor;
      desc.component_table_count = static_cast<uint32_t>(component_dir.size());

      (void)writer.WriteBlob(
        std::as_bytes(std::span<const SceneAssetDesc, 1>(&desc, 1)));
      (void)writer.WriteBlob(nodes_bytes);
      (void)writer.WriteBlob(strings_bytes);
      if (!renderables.empty()) {
        (void)writer.WriteBlob(
          std::as_bytes(std::span<const RenderableRecord>(renderables)));
      }
      if (!perspective_cameras.empty()) {
        (void)writer.WriteBlob(std::as_bytes(
          std::span<const PerspectiveCameraRecord>(perspective_cameras)));
      }
      if (!orthographic_cameras.empty()) {
        (void)writer.WriteBlob(std::as_bytes(
          std::span<const OrthographicCameraRecord>(orthographic_cameras)));
      }
      if (!directional_lights.empty()) {
        (void)writer.WriteBlob(std::as_bytes(
          std::span<const DirectionalLightRecord>(directional_lights)));
      }
      if (!point_lights.empty()) {
        (void)writer.WriteBlob(
          std::as_bytes(std::span<const PointLightRecord>(point_lights)));
      }
      if (!spot_lights.empty()) {
        (void)writer.WriteBlob(
          std::as_bytes(std::span<const SpotLightRecord>(spot_lights)));
      }
      if (!component_dir.empty()) {
        (void)writer.WriteBlob(std::as_bytes(
          std::span<const SceneComponentTableDesc>(component_dir)));
      }

      SceneEnvironmentBlockHeader env_header {};
      env_header.byte_size = sizeof(SceneEnvironmentBlockHeader);
      env_header.systems_count = 0;
      (void)writer.WriteBlob(std::as_bytes(
        std::span<const SceneEnvironmentBlockHeader, 1>(&env_header, 1)));

      const auto bytes = stream.Data();

      LOG_F(INFO, "Emit scene '{}' -> {} (nodes={}, renderables={})",
        scene_name.c_str(), relpath.c_str(), nodes.size(), renderables.size());

      out.WriteAssetDescriptor(
        scene_key, AssetType::kScene, virtual_path, relpath, bytes);
      written_scenes += 1;
    }
  };

} // namespace

auto CreateFbxImporter() -> std::unique_ptr<Importer>
{
  return std::make_unique<FbxImporter>();
}

} // namespace oxygen::content::import
