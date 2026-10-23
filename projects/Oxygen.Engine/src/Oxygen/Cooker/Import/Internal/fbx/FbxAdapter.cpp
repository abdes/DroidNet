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
#include <filesystem>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/ImportedLightSemantics.h>
#include <Oxygen/Cooker/Import/Internal/ImportedPropertyCommit.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/MeshTransformBake.h>
#include <Oxygen/Cooker/Import/Internal/ModelGeometrySource.h>
#include <Oxygen/Cooker/Import/Internal/ModelMaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/ModelTextureSource.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/Internal/SceneBuild.h>
#include <Oxygen/Cooker/Import/Internal/SceneNodeImportDefaults.h>
#include <Oxygen/Cooker/Import/Internal/SourceLayoutHash.h>
#include <Oxygen/Cooker/Import/Internal/StaticSourceValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/Internal/fbx/CoordTransform.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxMaterialTextures.h>
#include <Oxygen/Cooker/Import/Internal/fbx/ufbx.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Import/SceneSourceInspection.h>
#include <Oxygen/Cooker/Import/TextureImportPresets.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Core/Transforms/Decompose.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/LightValidation.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>

namespace oxygen::content::import::adapters {

namespace {

  struct FbxSceneSource final {
    std::shared_ptr<const ufbx_scene> scene;
    std::vector<base::Sha256Digest> source_layout_witnesses;
  };

} // namespace

struct FbxAdapter::Impl final {
  FbxSceneSource source;
};

FbxAdapter::FbxAdapter()
  : impl_(std::make_unique<Impl>())
{
}

FbxAdapter::~FbxAdapter() = default;

namespace {

  //! Checks if a string starts with a prefix, case-insensitive.
  [[nodiscard]] inline auto StartsWithIgnoreCase(
    std::string_view str, std::string_view prefix) -> bool
  {
    if (str.size() < prefix.size()) {
      return false;
    }
    return std::equal(
      prefix.begin(), prefix.end(), str.begin(), [](char a, char b) -> bool {
        return std::tolower(static_cast<unsigned char>(a))
          == std::tolower(static_cast<unsigned char>(b));
      });
  }

  //! Clamps a float to the [0, 1] range.
  [[nodiscard]] inline auto Clamp01(const float v) noexcept -> float
  {
    return std::clamp(v, 0.0F, 1.0F);
  }

  /*!
   FBX light `intensity` is a unitless authoring control. We map it to
   lumens for Oxygen's PBR conventions using a two-segment rule:

   - For values in [0, 15], treat the value as a multiplier on a 15 lm
     reference. This yields a range of [0, 225] lm, where 15 lm corresponds
     to a single candle.
   - For values > 15, treat the value directly as lumens.

   This keeps low-range authoring intuitive while allowing explicit lumens
   for higher intensities.
  */
  //! Views a ufbx list as a span.
  template <typename List> [[nodiscard]] auto Items(const List& list) noexcept
  {
    return std::span(list.data, list.count);
  }

  //! Element `index` of a ufbx list, bounds-checked.
  template <typename List>
  [[nodiscard]] auto ItemAt(const List& list, const size_t index)
    -> decltype(auto)
  {
    return base::CheckedAt(Items(list), index);
  }

  [[nodiscard]] auto FbxIntensityToLumens(const float intensity) noexcept
    -> float
  {
    constexpr float kMultiplierRangeEnd = 15.0F;
    constexpr float kCandleLumens = 15.0F;
    if (intensity <= kMultiplierRangeEnd) {
      return intensity * kCandleLumens;
    }
    return intensity;
  }

  [[nodiscard]] auto ToStringView(const ufbx_string& s) -> std::string_view
  {
    return { s.data, s.length };
  }

  [[nodiscard]] auto ResolveFileTexture(const ufbx_texture* texture)
    -> const ufbx_texture*
  {
    if (texture == nullptr) {
      return nullptr;
    }

    if (texture->file_textures.count > 0) {
      return ItemAt(texture->file_textures, 0);
    }

    return texture;
  }

  [[nodiscard]] auto NormalizeTexturePathId(std::filesystem::path p)
    -> std::string
  {
    if (p.empty()) {
      return {};
    }

    p = p.lexically_normal();
    auto out = p.generic_string();

#ifdef _WIN32
    std::ranges::transform(out, out.begin(), [](const char c) -> char {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
#endif

    return out;
  }

  using data::pak::world::DirectionalLightRecord;
  using data::pak::world::MakeDefaultRecord;
  using data::pak::world::NodeRecord;
  using data::pak::world::OrthographicCameraRecord;
  using data::pak::world::PerspectiveCameraRecord;
  using data::pak::world::PointLightRecord;
  using data::pak::world::RenderableRecord;
  using data::pak::world::SpotLightRecord;

  [[nodiscard]] auto TryReadFbxBoolOverride(
    const ufbx_props* props, const std::string_view name) -> std::optional<bool>
  {
    if (props == nullptr
      || ufbx_find_prop_len(props, name.data(), name.size()) == nullptr) {
      return std::nullopt;
    }
    return ufbx_find_bool_len(props, name.data(), name.size(), false);
  }

  [[nodiscard]] auto ReadImportedLightSemantics(const ufbx_props* props)
    -> detail::ImportedLightSemantics
  {
    return detail::ImportedLightSemantics {
      .affects_world = TryReadFbxBoolOverride(
        props, detail::kImportedLightAffectsWorldFbxProp),
      .casts_shadows = TryReadFbxBoolOverride(
        props, detail::kImportedLightCastsShadowsFbxProp),
    };
  }

  struct UfbxCancelContext final {
    std::stop_token stop_token;
  };

  auto UfbxProgressCallback(void* user, const ufbx_progress* /*unused*/)
    -> ufbx_progress_result
  {
    const auto* ctx = static_cast<const UfbxCancelContext*>(user);
    if (ctx != nullptr && ctx->stop_token.stop_requested()) {
      return UFBX_PROGRESS_CANCEL;
    }
    return UFBX_PROGRESS_CONTINUE;
  }

  [[nodiscard]] auto MakeErrorDiagnostic(std::string code, std::string message,
    std::string_view source_id, std::string_view object_path)
    -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

  [[nodiscard]] auto MakeCancelDiagnostic(std::string_view source_id)
    -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = "import.canceled",
      .message = "Import canceled",
      .source_path = std::string(source_id),
      .object_path = {},
    };
  }

  [[nodiscard]] auto MakeWarningDiagnostic(std::string code,
    std::string message, std::string_view source_id,
    std::string_view object_path) -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kWarning,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

  [[nodiscard]] auto ToVec3(const ufbx_vec3 v) -> glm::vec3
  {
    return glm::vec3 {
      static_cast<float>(v.x),
      static_cast<float>(v.y),
      static_cast<float>(v.z),
    };
  }

  [[nodiscard]] auto ToVec2(const ufbx_vec2 v) -> glm::vec2
  {
    return glm::vec2 {
      static_cast<float>(v.x),
      static_cast<float>(v.y),
    };
  }

  [[nodiscard]] auto ToVec4(const ufbx_vec4 v) -> glm::vec4
  {
    return glm::vec4 {
      static_cast<float>(v.x),
      static_cast<float>(v.y),
      static_cast<float>(v.z),
      static_cast<float>(v.w),
    };
  }

  [[nodiscard]] auto ToMat4(const ufbx_matrix matrix) -> glm::mat4
  {
    const auto c0 = matrix.cols[0];
    const auto c1 = matrix.cols[1];
    const auto c2 = matrix.cols[2];
    const auto c3 = matrix.cols[3];

    return glm::mat4 {
      glm::vec4 { static_cast<float>(c0.x), static_cast<float>(c0.y),
        static_cast<float>(c0.z), 0.0F },
      glm::vec4 { static_cast<float>(c1.x), static_cast<float>(c1.y),
        static_cast<float>(c1.z), 0.0F },
      glm::vec4 { static_cast<float>(c2.x), static_cast<float>(c2.y),
        static_cast<float>(c2.z), 0.0F },
      glm::vec4 { static_cast<float>(c3.x), static_cast<float>(c3.y),
        static_cast<float>(c3.z), 1.0F },
    };
  }

  struct AxisVec final {
    int x = 0;
    int y = 0;
    int z = 0;
  };

  [[nodiscard]] auto AxisToVec(const ufbx_coordinate_axis axis) -> AxisVec
  {
    switch (axis) {
    case UFBX_COORDINATE_AXIS_POSITIVE_X:
      return AxisVec { .x = 1, .y = 0, .z = 0 };
    case UFBX_COORDINATE_AXIS_NEGATIVE_X:
      return AxisVec { .x = -1, .y = 0, .z = 0 };
    case UFBX_COORDINATE_AXIS_POSITIVE_Y:
      return AxisVec { .x = 0, .y = 1, .z = 0 };
    case UFBX_COORDINATE_AXIS_NEGATIVE_Y:
      return AxisVec { .x = 0, .y = -1, .z = 0 };
    case UFBX_COORDINATE_AXIS_POSITIVE_Z:
      return AxisVec { .x = 0, .y = 0, .z = 1 };
    case UFBX_COORDINATE_AXIS_NEGATIVE_Z:
      return AxisVec { .x = 0, .y = 0, .z = -1 };
    case UFBX_COORDINATE_AXIS_UNKNOWN:
    case UFBX_COORDINATE_AXIS_FORCE_32BIT:
      break;
    }

    return AxisVec {};
  }

  [[nodiscard]] auto IsLeftHandedAxes(const ufbx_coordinate_axes& axes)
    -> std::optional<bool>
  {
    if (axes.right == UFBX_COORDINATE_AXIS_UNKNOWN
      || axes.up == UFBX_COORDINATE_AXIS_UNKNOWN
      || axes.front == UFBX_COORDINATE_AXIS_UNKNOWN) {
      return std::nullopt;
    }

    const auto right = AxisToVec(axes.right);
    const auto up = AxisToVec(axes.up);
    const auto forward = AxisToVec(axes.front);

    const AxisVec cross_ru {
      .x = (right.y * up.z) - (right.z * up.y),
      .y = (right.z * up.x) - (right.x * up.z),
      .z = (right.x * up.y) - (right.y * up.x),
    };

    const int det = (cross_ru.x * forward.x) + (cross_ru.y * forward.y)
      + (cross_ru.z * forward.z);
    return det < 0;
  }

  template <typename T> [[nodiscard]] auto HasAttribute(const T& stream) -> bool
  {
    return stream.exists && stream.values.data != nullptr
      && stream.indices.data != nullptr;
  }

  struct TriangleMeshBuffers {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;
    std::vector<glm::vec3> tangents;
    std::vector<glm::vec3> bitangents;
    std::vector<glm::vec4> colors;
    std::vector<glm::uvec4> joint_indices;
    std::vector<glm::vec4> joint_weights;
    std::vector<glm::mat4> inverse_bind_matrices;
    std::vector<uint32_t> joint_remap;
    std::vector<uint32_t> indices;
    std::vector<TriangleRange> ranges;
  };

  [[nodiscard]] auto FindSkinDeformer(const ufbx_mesh& mesh)
    -> const ufbx_skin_deformer*
  {
    auto find_in_connections
      = [](const ufbx_connection_list& list) -> const ufbx_skin_deformer* {
      for (size_t i = 0; i < list.count; ++i) {
        const auto& conn = ItemAt(list, i);
        if (conn.src != nullptr
          && conn.src->type == UFBX_ELEMENT_SKIN_DEFORMER) {
          return ufbx_as_skin_deformer(conn.src);
        }
        if (conn.dst != nullptr
          && conn.dst->type == UFBX_ELEMENT_SKIN_DEFORMER) {
          return ufbx_as_skin_deformer(conn.dst);
        }
      }
      return nullptr;
    };

    auto is_connected_to = [](const ufbx_connection_list& list,
                             const ufbx_element* element) -> bool {
      if (element == nullptr) {
        return false;
      }
      for (size_t i = 0; i < list.count; ++i) {
        const auto& conn = ItemAt(list, i);
        if (conn.src == element || conn.dst == element) {
          return true;
        }
      }
      return false;
    };

    for (size_t i = 0; i < mesh.skin_deformers.count; ++i) {
      const auto* deformer = ItemAt(mesh.skin_deformers, i);
      if (deformer != nullptr) {
        return deformer;
      }
    }

    for (size_t i = 0; i < mesh.all_deformers.count; ++i) {
      const auto* element = ItemAt(mesh.all_deformers, i);
      if (element != nullptr && element->type == UFBX_ELEMENT_SKIN_DEFORMER) {
        return ufbx_as_skin_deformer(element);
      }
    }

    for (size_t i = 0; i < mesh.element.connections_dst.count; ++i) {
      const auto& conn = ItemAt(mesh.element.connections_dst, i);
      if (conn.src != nullptr && conn.src->type == UFBX_ELEMENT_SKIN_DEFORMER) {
        return ufbx_as_skin_deformer(conn.src);
      }
    }

    for (size_t i = 0; i < mesh.element.connections_src.count; ++i) {
      const auto& conn = ItemAt(mesh.element.connections_src, i);
      if (conn.dst != nullptr && conn.dst->type == UFBX_ELEMENT_SKIN_DEFORMER) {
        return ufbx_as_skin_deformer(conn.dst);
      }
    }

    for (size_t i = 0; i < mesh.instances.count; ++i) {
      const auto* node = ItemAt(mesh.instances, i);
      if (node == nullptr) {
        continue;
      }

      if (const auto* deformer
        = find_in_connections(node->element.connections_dst);
        deformer != nullptr) {
        return deformer;
      }
      if (const auto* deformer
        = find_in_connections(node->element.connections_src);
        deformer != nullptr) {
        return deformer;
      }
    }

    if (mesh.element.scene != nullptr) {
      const auto& scene = *mesh.element.scene;
      for (size_t i = 0; i < scene.skin_deformers.count; ++i) {
        const auto* deformer = ItemAt(scene.skin_deformers, i);
        if ((deformer != nullptr)
          && (is_connected_to(deformer->element.connections_dst, &mesh.element)
            || is_connected_to(
              deformer->element.connections_src, &mesh.element))) {
          return deformer;
        }
      }
    }

    return nullptr;
  }

  [[nodiscard]] auto NormalizeWeights(const glm::vec4 weights) -> glm::vec4
  {
    const auto sum = weights.x + weights.y + weights.z + weights.w;
    if (sum <= 0.0F) {
      return glm::vec4(0.0F);
    }
    return weights / sum;
  }

  auto CleanSkinWeights(std::vector<glm::vec4>& weights,
    std::vector<glm::uvec4>& joints, std::vector<ImportDiagnostic>& diagnostics,
    std::string_view source_id, std::string_view object_path) -> void
  {
    constexpr float kMinWeight = 1.0e-4F;

    if (weights.size() != joints.size()) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.skinning_buffers",
        "Skinning buffers must have matching sizes", source_id, object_path));
      return;
    }

    for (size_t i = 0; i < weights.size(); ++i) {
      auto w = weights.at(i);
      auto j = joints.at(i);

      std::array influences = {
        std::make_pair(w.x, j.x),
        std::make_pair(w.y, j.y),
        std::make_pair(w.z, j.z),
        std::make_pair(w.w, j.w),
      };

      std::ranges::sort(influences,
        [](const auto& a, const auto& b) -> auto { return a.first > b.first; });

      size_t kept = 0;
      for (auto& influence : influences) {
        if (influence.first < kMinWeight) {
          influence.first = 0.0F;
        }
        if (influence.first > 0.0F) {
          ++kept;
        }
      }

      if (kept == 0) {
        w = glm::vec4(0.0F);
        j = glm::uvec4(0U);
      } else {
        w = NormalizeWeights(
          glm::vec4 { influences.at(0).first, influences.at(1).first,
            influences.at(2).first, influences.at(3).first });
        j = glm::uvec4 { influences.at(0).second, influences.at(1).second,
          influences.at(2).second, influences.at(3).second };
      }

      weights.at(i) = w;
      joints.at(i) = j;
    }
  }

  [[nodiscard]] auto BuildTriangleBuffers(const ufbx_mesh& mesh,
    const glm::mat4& basis, const ufbx_node* material_node,
    const std::unordered_map<const ufbx_material*, uint32_t>&
      scene_material_index_by_ptr,
    const uint32_t material_key_count,
    std::vector<ImportDiagnostic>& diagnostics, std::string_view source_id,
    std::string_view object_path) -> std::optional<TriangleMeshBuffers>
  {
    if (mesh.num_indices == 0 || mesh.num_faces == 0) {
      diagnostics.push_back(MakeErrorDiagnostic(
        "mesh.no_faces", "FBX mesh contains no faces", source_id, object_path));
      return std::nullopt;
    }

    if (!HasAttribute(mesh.vertex_position)) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_positions",
        "FBX mesh missing vertex positions", source_id, object_path));
      return std::nullopt;
    }

    TriangleMeshBuffers out;
    out.positions.reserve(mesh.num_indices);
    out.normals.reserve(mesh.num_indices);
    out.texcoords.reserve(mesh.num_indices);
    out.tangents.reserve(mesh.num_indices);
    out.bitangents.reserve(mesh.num_indices);
    out.colors.reserve(mesh.num_indices);
    const auto estimated_tris
      = mesh.num_triangles > 0 ? mesh.num_triangles : mesh.num_indices;
    out.indices.reserve(estimated_tris * 3U);

    struct MaterialRange {
      TriangleRange range {};
      std::vector<uint32_t> indices;
    };

    std::unordered_map<uint32_t, MaterialRange> range_map;
    std::vector<uint32_t> tri_indices;
    tri_indices.resize(static_cast<size_t>(mesh.max_face_triangles) * 3U);
    size_t triangulated_faces = 0;

    for (size_t idx = 0; idx < mesh.num_indices; ++idx) {
      out.positions.push_back(
        ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_position, idx)));

      if (HasAttribute(mesh.vertex_normal)) {
        out.normals.push_back(
          ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_normal, idx)));
      }
      if (HasAttribute(mesh.vertex_uv)) {
        out.texcoords.push_back(
          ToVec2(ufbx_get_vertex_vec2(&mesh.vertex_uv, idx)));
      }
      if (HasAttribute(mesh.vertex_tangent)) {
        out.tangents.push_back(
          ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_tangent, idx)));
      }
      if (HasAttribute(mesh.vertex_bitangent)) {
        out.bitangents.push_back(
          ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_bitangent, idx)));
      }
      if (HasAttribute(mesh.vertex_color)) {
        out.colors.push_back(
          ToVec4(ufbx_get_vertex_vec4(&mesh.vertex_color, idx)));
      }
    }

    const ufbx_material_list* material_list = &mesh.materials;
    if (material_node != nullptr && material_node->materials.count > 0) {
      material_list = &material_node->materials;
    }

    std::vector face_surfaces(mesh.num_faces, UFBX_NO_INDEX);
    if (mesh.material_parts.data != nullptr && mesh.material_parts.count > 0) {
      for (size_t part_i = 0; part_i < mesh.material_parts.count; ++part_i) {
        const auto& part = ItemAt(mesh.material_parts, part_i);
        const auto slot = part.index;
        if (part.face_indices.data == nullptr) {
          continue;
        }
        const auto face_count = part.face_indices.count;
        for (size_t fi = 0; fi < face_count; ++fi) {
          const auto face_index = ItemAt(part.face_indices, fi);
          if (face_index < mesh.num_faces) {
            face_surfaces.at(face_index) = slot;
          }
        }
      }
    } else if (mesh.face_material.data != nullptr
      && mesh.face_material.count >= mesh.num_faces) {
      for (size_t face_i = 0; face_i < mesh.num_faces; ++face_i) {
        face_surfaces.at(face_i) = ItemAt(mesh.face_material, face_i);
      }
    } else {
      diagnostics.push_back(MakeWarningDiagnostic("mesh.face_material_missing",
        "FBX face material list missing; defaulting to single material",
        source_id, object_path));
    }

    for (size_t face_i = 0; face_i < mesh.num_faces; ++face_i) {
      const auto face
        = base::CheckedAt(std::span(mesh.faces.data, mesh.faces.count), face_i);
      if (face.num_indices < 3) {
        diagnostics.push_back(MakeWarningDiagnostic("mesh.invalid_face",
          "FBX mesh contains face with fewer than 3 indices; skipping",
          source_id, object_path));
        continue;
      }

      uint32_t surface = material_key_count;
      const uint32_t source_slot = face_surfaces.at(face_i);
      if (material_list != nullptr && material_list->data != nullptr
        && source_slot != UFBX_NO_INDEX && source_slot < material_list->count) {
        const auto* material = ItemAt(*material_list, source_slot);
        if (material != nullptr) {
          if (const auto it = scene_material_index_by_ptr.find(material);
            it != scene_material_index_by_ptr.end()) {
            surface = it->second;
          }
        }
      }

      auto it = range_map.find(source_slot);
      if (it == range_map.end()) {
        it = range_map
               .emplace(source_slot,
                 MaterialRange {
                   .range = TriangleRange {
                     .surface = surface,
                     .source_slot = source_slot,
                     .first_index = 0,
                     .index_count = 0,
                   },
                   .indices = {},
                 })
               .first;
      }

      const auto tri_count = ufbx_triangulate_face(
        tri_indices.data(), tri_indices.size(), &mesh, face);
      if (tri_count == 0) {
        diagnostics.push_back(MakeWarningDiagnostic("mesh.triangulate_failed",
          "FBX face triangulation produced no triangles; skipping face",
          source_id, object_path));
        continue;
      }

      const auto tri_index_count = static_cast<size_t>(tri_count) * 3U;
      for (size_t i = 0; i < tri_index_count; ++i) {
        const auto idx = tri_indices.at(i);
        if (idx >= mesh.num_indices) {
          diagnostics.push_back(MakeErrorDiagnostic("mesh.index_oob",
            "FBX mesh contains out-of-range indices", source_id, object_path));
          return std::nullopt;
        }
      }

      const auto used = std::span(tri_indices).first(tri_index_count);
      it->second.indices.insert(
        it->second.indices.end(), used.begin(), used.end());
      it->second.range.index_count += static_cast<uint32_t>(tri_index_count);
      if (face.num_indices != 3) {
        ++triangulated_faces;
      }
    }

    if (triangulated_faces > 0) {
      DLOG_F(INFO, "FBX mesh '{}' triangulated {} faces", object_path,
        triangulated_faces);
    }

    if (const auto* skin_deformer = FindSkinDeformer(mesh);
      skin_deformer != nullptr) {
      out.joint_indices.reserve(mesh.num_vertices);
      out.joint_weights.reserve(mesh.num_vertices);

      for (size_t i = 0; i < mesh.num_vertices; ++i) {
        if (i >= skin_deformer->vertices.count) {
          out.joint_indices.emplace_back(0U);
          out.joint_weights.emplace_back(0.0F);
          continue;
        }

        const auto vertex = ItemAt(skin_deformer->vertices, i);
        std::array<uint32_t, 4> joints {};
        std::array<float, 4> weights {};

        const size_t count
          = (std::min)(static_cast<size_t>(vertex.num_weights), size_t { 4 });
        for (size_t w = 0; w < count; ++w) {
          const auto weight_index = vertex.weight_begin + w;
          if (weight_index >= skin_deformer->weights.count) {
            continue;
          }
          const auto weight = ItemAt(skin_deformer->weights, weight_index);
          const auto slot = static_cast<glm::uvec4::length_type>(w);
          joints.at(slot) = static_cast<uint32_t>(weight.cluster_index);
          weights.at(slot) = static_cast<float>(weight.weight);
        }

        out.joint_indices.push_back(glm::make_vec4(joints.data()));
        out.joint_weights.push_back(glm::make_vec4(weights.data()));
      }

      CleanSkinWeights(out.joint_weights, out.joint_indices, diagnostics,
        source_id, object_path);
    }

    std::vector<std::pair<uint32_t, MaterialRange>> sorted_ranges;
    sorted_ranges.reserve(range_map.size());
    for (auto& [slot, range] : range_map) {
      sorted_ranges.emplace_back(slot, std::move(range));
    }

    std::ranges::sort(sorted_ranges,
      [](const auto& a, const auto& b) -> auto { return a.first < b.first; });

    out.ranges.reserve(sorted_ranges.size());
    for (auto& entry : sorted_ranges) {
      auto& range = entry.second;
      range.range.first_index = static_cast<uint32_t>(out.indices.size());
      out.indices.insert(
        out.indices.end(), range.indices.begin(), range.indices.end());
      out.ranges.push_back(range.range);
    }

    if (out.ranges.empty()) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.no_ranges",
        "FBX mesh emitted no triangle ranges", source_id, object_path));
      return std::nullopt;
    }

    if (!out.normals.empty() && out.normals.size() != out.positions.size()) {
      out.normals.clear();
      diagnostics.push_back(MakeWarningDiagnostic("mesh.normals.mismatch",
        "FBX normals count does not match positions", source_id, object_path));
    }
    if (!out.texcoords.empty()
      && out.texcoords.size() != out.positions.size()) {
      out.texcoords.clear();
      diagnostics.push_back(MakeWarningDiagnostic("mesh.texcoords.mismatch",
        "FBX texcoords count does not match positions", source_id,
        object_path));
    }
    if (!out.tangents.empty() && out.tangents.size() != out.positions.size()) {
      out.tangents.clear();
      out.bitangents.clear();
      diagnostics.push_back(MakeWarningDiagnostic("mesh.tangents.mismatch",
        "FBX tangents count does not match positions", source_id, object_path));
    }
    if (!out.colors.empty() && out.colors.size() != out.positions.size()) {
      out.colors.clear();
      diagnostics.push_back(MakeWarningDiagnostic("mesh.colors.mismatch",
        "FBX colors count does not match positions", source_id, object_path));
    }

    if (!out.joint_indices.empty()
      && out.joint_indices.size() != out.positions.size()) {
      out.joint_indices.clear();
      out.joint_weights.clear();
      diagnostics.push_back(MakeWarningDiagnostic("mesh.skinning.mismatch",
        "FBX skinning buffers count does not match positions", source_id,
        object_path));
    }

    // The geometry in Oxygen axes, matching its nodes (see LocalTransform). A
    // rotation keeps lengths and winding.
    if (basis != glm::mat4 { 1.0F }) {
      const auto rotation = glm::mat3(basis);
      for (auto* stream :
        { &out.positions, &out.normals, &out.tangents, &out.bitangents }) {
        for (auto& value : *stream) {
          value = rotation * value;
        }
      }
    }

    if (!out.joint_indices.empty()) {
      const auto* skin_deformer = FindSkinDeformer(mesh);
      if (skin_deformer != nullptr) {
        const size_t cluster_count = skin_deformer->clusters.count;
        out.inverse_bind_matrices.reserve(cluster_count);
        out.joint_remap.reserve(cluster_count);
        for (size_t i = 0; i < cluster_count; ++i) {
          const auto* cluster = ItemAt(skin_deformer->clusters, i);
          if (cluster == nullptr) {
            continue;
          }
          out.inverse_bind_matrices.push_back(
            basis * ToMat4(cluster->geometry_to_bone) * glm::transpose(basis));
          out.joint_remap.push_back(static_cast<uint32_t>(i));
        }
      }
    }

    return out;
  }

  [[nodiscard]] auto BuildSourceId(std::string_view prefix,
    std::string_view name, uint32_t ordinal) -> std::string
  {
    std::string id;
    if (!prefix.empty()) {
      id = std::string(prefix);
      id.append("::");
    }
    if (!name.empty()) {
      id.append(name.begin(), name.end());
    } else {
      id.append("mesh_");
      id.append(std::to_string(ordinal));
    }
    id.append("::");
    id.append(std::to_string(ordinal));
    return id;
  }

  enum class TextureUsage : uint8_t {
    kBaseColor,
    kNormal,
    kMetallic,
    kRoughness,
    kMetallicRoughness,
    kOcclusion,
    kEmissive,
    kSpecular,
    kSheenColor,
    kClearcoat,
    kClearcoatNormal,
    kTransmission,
    kThickness,
  };

  [[nodiscard]] auto UsageLabel(const TextureUsage usage) -> std::string_view
  {
    switch (usage) {
    case TextureUsage::kBaseColor:
      return "base_color";
    case TextureUsage::kNormal:
      return "normal";
    case TextureUsage::kMetallic:
      return "metallic";
    case TextureUsage::kRoughness:
      return "roughness";
    case TextureUsage::kMetallicRoughness:
      return "metallic_roughness";
    case TextureUsage::kOcclusion:
      return "occlusion";
    case TextureUsage::kEmissive:
      return "emissive";
    case TextureUsage::kSpecular:
      return "specular";
    case TextureUsage::kSheenColor:
      return "sheen_color";
    case TextureUsage::kClearcoat:
      return "clearcoat";
    case TextureUsage::kClearcoatNormal:
      return "clearcoat_normal";
    case TextureUsage::kTransmission:
      return "transmission";
    case TextureUsage::kThickness:
      return "thickness";
    }
    return "texture";
  }

  [[nodiscard]] auto PresetForUsage(const TextureUsage usage) -> TexturePreset
  {
    switch (usage) {
    case TextureUsage::kBaseColor:
      return TexturePreset::kAlbedo;
    case TextureUsage::kNormal:
      return TexturePreset::kNormal;
    case TextureUsage::kMetallic:
      return TexturePreset::kMetallic;
    case TextureUsage::kRoughness:
      return TexturePreset::kRoughness;
    case TextureUsage::kMetallicRoughness:
      return TexturePreset::kORMPacked;
    case TextureUsage::kOcclusion:
      return TexturePreset::kAO;
    case TextureUsage::kEmissive:
      return TexturePreset::kEmissive;
    case TextureUsage::kSpecular:
      return TexturePreset::kRoughness;
    case TextureUsage::kSheenColor:
      return TexturePreset::kAlbedo;
    case TextureUsage::kClearcoat:
      return TexturePreset::kRoughness;
    case TextureUsage::kClearcoatNormal:
      return TexturePreset::kNormal;
    case TextureUsage::kTransmission:
    case TextureUsage::kThickness:
      return TexturePreset::kRoughness;
    }
    return TexturePreset::kData;
  }

  [[nodiscard]] constexpr auto IsBc7Format(const Format format) noexcept -> bool
  {
    return format == Format::kBC7UNorm || format == Format::kBC7UNormSRGB;
  }

  [[nodiscard]] auto BuildTextureSourceId(std::string_view prefix,
    std::string_view texture_id, const TextureUsage usage) -> std::string
  {
    std::string id;
    if (!prefix.empty()) {
      id = std::string(prefix);
      id.append("::");
    }
    id.append("tex::");
    if (!texture_id.empty()) {
      id.append(texture_id.begin(), texture_id.end());
    } else {
      id.append("texture");
    }
    id.append("::");
    id.append(UsageLabel(usage));
    return id;
  }

  struct TextureIdentity final {
    std::string texture_id;
    const ufbx_texture* file_texture = nullptr;
    std::filesystem::path resolved_path;
    bool embedded = false;
  };

  [[nodiscard]] auto ResolveTextureIdentity(const ufbx_texture* texture,
    const ImportRequest& request) -> std::optional<TextureIdentity>
  {
    const auto* file_tex = ResolveFileTexture(texture);
    if (file_tex == nullptr) {
      return std::nullopt;
    }

    TextureIdentity identity {};
    identity.file_texture = file_tex;
    identity.embedded
      = (file_tex->content.data != nullptr && file_tex->content.size > 0);

    // An embedded texture is keyed by its element: two embedded images may
    // share a name.
    if (identity.embedded) {
      identity.texture_id
        = "embedded:fbx_texture_" + std::to_string(file_tex->element_id);
      return identity;
    }

    std::string_view rel = ToStringView(file_tex->relative_filename);
    std::string_view abs = ToStringView(file_tex->filename);

    if (rel.empty() && abs.empty()) {
      const auto rel_prop
        = ufbx_find_string(&file_tex->props, "RelativeFilename", {});
      const auto abs_prop = ufbx_find_string(&file_tex->props, "FileName", {});
      if (rel_prop.length > 0) {
        rel = ToStringView(rel_prop);
      }
      if (abs_prop.length > 0) {
        abs = ToStringView(abs_prop);
      }
    }

    if (!rel.empty()) {
      identity.resolved_path
        = request.source_path.parent_path() / std::filesystem::path(rel);
    } else if (!abs.empty()) {
      const auto abs_path = std::filesystem::path(abs);
      identity.resolved_path = abs_path.is_absolute()
        ? abs_path
        : (request.source_path.parent_path() / abs_path);
    }

    if (!identity.resolved_path.empty()) {
      identity.texture_id = NormalizeTexturePathId(identity.resolved_path);
    }

    if (identity.texture_id.empty()) {
      identity.texture_id
        = "fbx_texture_" + std::to_string(file_tex->element_id);
    }

    return identity;
  }

  //! The texture's authored name, or its image file's stem.
  [[nodiscard]] auto TextureAuthoredName(const ufbx_texture& texture)
    -> std::string
  {
    if (const auto name = ToStringView(texture.name); !name.empty()) {
      return std::string(name);
    }
    auto file = ToStringView(texture.relative_filename);
    if (file.empty()) {
      file = ToStringView(texture.filename);
    }
    return std::filesystem::path(file).stem().string();
  }

  struct ResolvedTextureSource final {
    TexturePipeline::SourceBytes bytes;
    std::filesystem::path source_path {};
  };

  [[nodiscard]] auto ResolveTextureSourceBytes(const TextureIdentity& identity,
    std::string_view source_id, const std::shared_ptr<const ufbx_scene>& owner,
    std::vector<ImportDiagnostic>& diagnostics,
    std::span<const AdapterInput::ExternalTextureBytes> external_texture_bytes)
    -> std::optional<ResolvedTextureSource>
  {
    if (identity.file_texture == nullptr) {
      return std::nullopt;
    }

    const auto make_placeholder = [] -> ResolvedTextureSource {
      auto bytes = std::make_shared<std::vector<std::byte>>();
      return ResolvedTextureSource {
        .bytes = TexturePipeline::SourceBytes {
          .bytes = std::span<const std::byte>(bytes->data(), bytes->size()),
          .owner = std::static_pointer_cast<const void>(bytes),
        },
      };
    };

    auto find_external_bytes = [&](std::string_view texture_id)
      -> std::shared_ptr<std::vector<std::byte>> {
      for (const auto& entry : external_texture_bytes) {
        if (entry.texture_id == texture_id) {
          return entry.bytes;
        }
      }
      return {};
    };

    if (identity.embedded) {
      const auto bytes = std::span(
        static_cast<const std::byte*>(identity.file_texture->content.data),
        identity.file_texture->content.size);
      if (bytes.empty()) {
        diagnostics.push_back(MakeWarningDiagnostic("fbx.texture.empty",
          "FBX embedded texture payload is empty", source_id, ""));
        return make_placeholder();
      }
      return ResolvedTextureSource {
        .bytes = TexturePipeline::SourceBytes {
          .bytes = bytes,
          .owner = std::static_pointer_cast<const void>(owner),
        },
      };
    }

    if (!external_texture_bytes.empty()) {
      const auto external_bytes = find_external_bytes(identity.texture_id);
      if (external_bytes) {
        return ResolvedTextureSource {
          .bytes = TexturePipeline::SourceBytes {
            .bytes = std::span<const std::byte>(
              external_bytes->data(), external_bytes->size()),
            .owner = std::static_pointer_cast<const void>(external_bytes),
          },
        };
      }
    }

    if (identity.resolved_path.empty()) {
      diagnostics.push_back(MakeWarningDiagnostic("fbx.texture.path_missing",
        "FBX texture has no resolved file path", source_id, ""));
      return make_placeholder();
    }

    auto placeholder = make_placeholder();
    placeholder.source_path = identity.resolved_path;
    return placeholder;
  }

  [[nodiscard]] auto IsLambertMaterial(const ufbx_material& material) -> bool
  {
    if (material.shader_type == UFBX_SHADER_FBX_LAMBERT) {
      return true;
    }

    const auto shading_model = ToStringView(material.shading_model_name);
    if (shading_model == "Lambert" || shading_model == "lambert") {
      return true;
    }

    const auto name = ToStringView(material.name);
    return StartsWithIgnoreCase(name, "lambert");
  }

  [[nodiscard]] auto HasMaterialTextures(const ufbx_material* material) -> bool
  {
    if (material == nullptr) {
      return false;
    }

    return FbxMaterialTextures::From(*material).Any();
  }

  [[nodiscard]] auto BuildSceneSourceId(
    std::string_view prefix, const ImportRequest& request) -> std::string
  {
    if (!prefix.empty()) {
      return std::string(prefix);
    }
    return request.GetSceneName();
  }

  struct NodeInput final {
    std::string authored_name;
    std::string base_name;
    uint32_t parent_index = 0;
    glm::mat4 local_matrix { 1.0F };
    glm::mat4 world_matrix { 1.0F };
    bool has_renderable = false;
    bool has_camera = false;
    bool has_light = false;
    bool visible = true;
    const void* source_node = nullptr;
  };

  [[nodiscard]] auto MakeNodeKey(const std::string_view node_virtual_path)
    -> data::AssetKey
  {
    return oxygen::data::AssetKey::FromVirtualPath(node_virtual_path);
  }

  [[nodiscard]] auto MakeLocalTransformMatrix(const ufbx_transform& transform)
    -> glm::mat4
  {
    const auto matrix = ufbx_transform_to_matrix(&transform);
    return coord::ToGlmMat4(matrix);
  }

  //! The rotation from the source's axes to Oxygen's, as ufbx found it.
  /*!
   ufbx converts units in the geometry but leaves the axis change as a rotation
   on the top-level nodes (`metadata.root_rotation`). The adapter moves it into
   the geometry, as the glTF adapter does, so a geometry is in Oxygen axes on
   its own, such as when a scene places it without its model's nodes.
  */
  [[nodiscard]] auto OxygenBasis(const ufbx_scene& scene) -> glm::mat4
  {
    const auto& rotation = scene.metadata.root_rotation;
    return glm::mat4_cast(
      glm::quat(static_cast<float>(rotation.w), static_cast<float>(rotation.x),
        static_cast<float>(rotation.y), static_cast<float>(rotation.z)));
  }

  //! A node's local transform in Oxygen axes, for geometry rotated by `basis`.
  /*!
   With `C` the basis and `L` ufbx's local transform, a top-level node, whose
   `L` ends with `C`, takes `L * inverse(C)` and any deeper node
   `C * L * inverse(C)`. Each node's world transform is then ufbx's times
   `inverse(C)`, so geometry rotated by `C` lands where ufbx places it.
  */
  [[nodiscard]] auto LocalTransform(
    const ufbx_node& node, const glm::mat4& basis) -> glm::mat4
  {
    if (node.is_root) {
      return glm::mat4 { 1.0F };
    }
    const auto local = MakeLocalTransformMatrix(node.local_transform);
    const auto to_source = glm::transpose(basis);
    return node.node_depth <= 1 ? local * to_source : basis * local * to_source;
  }

  [[nodiscard]] auto MakeSceneLoadError(std::string_view source_id,
    std::string_view error_message) -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = "fbx.parse_failed",
      .message = std::string(error_message),
      .source_path = std::string(source_id),
      .object_path = {},
    };
  }

  auto FbxSourceLayoutWitness(const ufbx_mesh& mesh) -> base::Sha256Digest
  {
    SourceLayoutHash hash("oxygen.fbx-source-layout/v1");
    hash.AddCount(mesh.vertices.count);
    for (const auto& position :
      std::span(mesh.vertices.data, mesh.vertices.count)) {
      hash.AddFloat(position.x);
      hash.AddFloat(position.y);
      hash.AddFloat(position.z);
    }
    hash.AddCount(mesh.vertex_indices.count);
    for (const auto index :
      std::span(mesh.vertex_indices.data, mesh.vertex_indices.count)) {
      hash.Add(index);
    }
    hash.AddCount(mesh.faces.count);
    for (const auto& face : std::span(mesh.faces.data, mesh.faces.count)) {
      hash.Add(face.index_begin);
      hash.Add(face.num_indices);
    }
    hash.AddCount(mesh.face_material.count);
    for (const auto declaration :
      std::span(mesh.face_material.data, mesh.face_material.count)) {
      hash.Add(declaration);
    }
    hash.AddCount(mesh.material_parts.count);
    for (const auto& part :
      std::span(mesh.material_parts.data, mesh.material_parts.count)) {
      hash.Add(part.index);
      hash.AddCount(part.face_indices.count);
      for (const auto face :
        std::span(part.face_indices.data, part.face_indices.count)) {
        hash.Add(face);
      }
    }
    return hash.Finish();
  }

  auto CaptureFbxSourceLayouts(const ufbx_scene& scene)
    -> std::vector<base::Sha256Digest>
  {
    std::vector<base::Sha256Digest> witnesses;
    witnesses.reserve(scene.meshes.count);
    for (const auto* mesh : std::span(scene.meshes.data, scene.meshes.count)) {
      witnesses.push_back(FbxSourceLayoutWitness(*mesh));
    }
    return witnesses;
  }

  [[nodiscard]] auto LoadSceneFromFile(const std::filesystem::path& path,
    const AdapterInput& input, std::vector<ImportDiagnostic>& diagnostics,
    const ModelParseMode mode = ModelParseMode::kGeometry) -> FbxSceneSource
  {
    if (input.stop_token.stop_requested()) {
      DLOG_F(
        WARNING, "FBX load canceled: source_id='{}'", input.source_id_prefix);
      diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
      return {};
    }

    const auto& coordinate_policy = input.request.options.coordinate;
    if (coordinate_policy.unit_normalization
        == UnitNormalizationPolicy::kApplyCustomFactor
      && !(coordinate_policy.unit_scale > 0.0F)) {
      DLOG_F(ERROR, "FBX invalid custom unit scale: source_id='{}' scale={} ",
        input.source_id_prefix, coordinate_policy.unit_scale);
      diagnostics.push_back(MakeErrorDiagnostic("fbx.invalid_custom_unit_scale",
        "unit_scale must be > 0 when using custom unit normalization",
        input.source_id_prefix, input.object_path_prefix));
      return {};
    }

    FbxSceneSource loaded;
    ufbx_load_opts opts {};
    ufbx_error error {};

    UfbxCancelContext cancel_ctx { .stop_token = input.stop_token };
    opts.progress_cb.fn = &UfbxProgressCallback;
    opts.progress_cb.user = &cancel_ctx;

    opts.target_axes = coord::EngineWorldTargetAxes();
    opts.target_camera_axes = coord::EngineCameraTargetAxes();
    opts.geometry_transform_handling
      = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    // Default to mirroring along engine forward/back, then refine using
    // FBX axis metadata if available.
    opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Y;
    if (const auto target_unit_meters
      = coord::ComputeTargetUnitMeters(coordinate_policy);
      target_unit_meters.has_value()) {
      opts.target_unit_meters = *target_unit_meters;
    }
    opts.generate_missing_normals = mode == ModelParseMode::kGeometry;
    opts.skip_skin_vertices = false;
    opts.clean_skin_weights = true;

    {
      ufbx_load_opts probe_opts = opts;
      probe_opts.target_axes = ufbx_coordinate_axes {
        .right = UFBX_COORDINATE_AXIS_UNKNOWN,
        .up = UFBX_COORDINATE_AXIS_UNKNOWN,
        .front = UFBX_COORDINATE_AXIS_UNKNOWN,
      };
      probe_opts.target_camera_axes = probe_opts.target_axes;
      probe_opts.target_unit_meters = 0.0;
      probe_opts.generate_missing_normals = false;
      probe_opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_NONE;
      probe_opts.handedness_conversion_retain_winding = false;
      probe_opts.reverse_winding = false;

      ufbx_error probe_error {};
      std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> probe_scene(
        ufbx_load_file(path.string().c_str(), &probe_opts, &probe_error),
        &ufbx_free_scene);
      if (probe_scene != nullptr) {
        const auto handedness = IsLeftHandedAxes(probe_scene->settings.axes);
        if (mode == ModelParseMode::kGeometry) {
          loaded.source_layout_witnesses
            = CaptureFbxSourceLayouts(*probe_scene);
        }

        if (!handedness.has_value()) {
          diagnostics.push_back(MakeWarningDiagnostic("fbx.axis_unknown",
            "FBX axis metadata is incomplete; using default handedness "
            "conversion",
            input.source_id_prefix, input.object_path_prefix));
        } else if (*handedness) {
          opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Y;
        } else {
          opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_NONE;
        }
      }
    }

    ufbx_scene* scene = ufbx_load_file(path.string().c_str(), &opts, &error);
    if (scene == nullptr) {
      if (error.type == UFBX_ERROR_CANCELLED
        || input.stop_token.stop_requested()) {
        DLOG_F(WARNING, "FBX load canceled: path='{}'", path.string());
        diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
        return {};
      }
      const auto desc = ToStringView(error.description);
      DLOG_F(
        ERROR, "FBX load failed: path='{}' error='{}'", path.string(), desc);
      diagnostics.push_back(MakeSceneLoadError(input.source_id_prefix, desc));
      return {};
    }

    loaded.scene = std::shared_ptr<const ufbx_scene>(
      scene, [](ufbx_scene* value) -> void { ufbx_free_scene(value); });
    if (mode == ModelParseMode::kGeometry
      && loaded.source_layout_witnesses.size() != scene->meshes.count) {
      diagnostics.push_back(MakeErrorDiagnostic(
        "mesh.source_layout_unavailable",
        "FBX source layout could not be captured before coordinate conversion",
        input.source_id_prefix, input.object_path_prefix));
      return {};
    }
    return loaded;
  }

  [[nodiscard]] auto LoadSceneFromMemory(const std::span<const std::byte> bytes,
    const AdapterInput& input, std::vector<ImportDiagnostic>& diagnostics,
    const ModelParseMode mode = ModelParseMode::kGeometry) -> FbxSceneSource
  {
    if (input.stop_token.stop_requested()) {
      DLOG_F(WARNING, "FBX load canceled (memory): source_id='{}'",
        input.source_id_prefix);
      diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
      return {};
    }

    const auto& coordinate_policy = input.request.options.coordinate;
    if (coordinate_policy.unit_normalization
        == UnitNormalizationPolicy::kApplyCustomFactor
      && !(coordinate_policy.unit_scale > 0.0F)) {
      DLOG_F(ERROR, "FBX invalid custom unit scale: source_id='{}' scale={} ",
        input.source_id_prefix, coordinate_policy.unit_scale);
      diagnostics.push_back(MakeErrorDiagnostic("fbx.invalid_custom_unit_scale",
        "unit_scale must be > 0 when using custom unit normalization",
        input.source_id_prefix, input.object_path_prefix));
      return {};
    }

    FbxSceneSource loaded;
    ufbx_load_opts opts {};
    ufbx_error error {};

    UfbxCancelContext cancel_ctx { .stop_token = input.stop_token };
    opts.progress_cb.fn = &UfbxProgressCallback;
    opts.progress_cb.user = &cancel_ctx;

    opts.target_axes = coord::EngineWorldTargetAxes();
    opts.target_camera_axes = coord::EngineCameraTargetAxes();
    opts.geometry_transform_handling
      = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    // Default to mirroring along engine forward/back, then refine using
    // FBX axis metadata if available.
    opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Y;
    if (const auto target_unit_meters
      = coord::ComputeTargetUnitMeters(coordinate_policy);
      target_unit_meters.has_value()) {
      opts.target_unit_meters = *target_unit_meters;
    }
    opts.generate_missing_normals = mode == ModelParseMode::kGeometry;
    opts.skip_skin_vertices = false;
    opts.clean_skin_weights = true;

    {
      ufbx_load_opts probe_opts = opts;
      probe_opts.target_axes = ufbx_coordinate_axes {
        .right = UFBX_COORDINATE_AXIS_UNKNOWN,
        .up = UFBX_COORDINATE_AXIS_UNKNOWN,
        .front = UFBX_COORDINATE_AXIS_UNKNOWN,
      };
      probe_opts.target_camera_axes = probe_opts.target_axes;
      probe_opts.target_unit_meters = 0.0;
      probe_opts.generate_missing_normals = false;
      probe_opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_NONE;
      probe_opts.handedness_conversion_retain_winding = false;
      probe_opts.reverse_winding = false;

      ufbx_error probe_error {};
      std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> probe_scene(
        ufbx_load_memory(bytes.data(), bytes.size(), &probe_opts, &probe_error),
        &ufbx_free_scene);
      if (probe_scene != nullptr) {
        const auto handedness = IsLeftHandedAxes(probe_scene->settings.axes);
        if (mode == ModelParseMode::kGeometry) {
          loaded.source_layout_witnesses
            = CaptureFbxSourceLayouts(*probe_scene);
        }

        if (!handedness.has_value()) {
          diagnostics.push_back(MakeWarningDiagnostic("fbx.axis_unknown",
            "FBX axis metadata is incomplete; using default handedness "
            "conversion",
            input.source_id_prefix, input.object_path_prefix));
        } else if (*handedness) {
          opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Y;
        } else {
          opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_NONE;
        }
      }
    }

    const void* data = bytes.data();
    const auto size = bytes.size();
    ufbx_scene* scene = ufbx_load_memory(data, size, &opts, &error);
    if (scene == nullptr) {
      if (error.type == UFBX_ERROR_CANCELLED
        || input.stop_token.stop_requested()) {
        DLOG_F(WARNING, "FBX load canceled (memory): source_id='{}'",
          input.source_id_prefix);
        diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
        return {};
      }
      const auto desc = ToStringView(error.description);
      DLOG_F(ERROR, "FBX load failed (memory): error='{}'", desc);
      diagnostics.push_back(MakeSceneLoadError(input.source_id_prefix, desc));
      return {};
    }

    loaded.scene = std::shared_ptr<const ufbx_scene>(
      scene, [](ufbx_scene* value) -> void { ufbx_free_scene(value); });
    if (mode == ModelParseMode::kGeometry
      && loaded.source_layout_witnesses.size() != scene->meshes.count) {
      diagnostics.push_back(MakeErrorDiagnostic(
        "mesh.source_layout_unavailable",
        "FBX source layout could not be captured before coordinate conversion",
        input.source_id_prefix, input.object_path_prefix));
      return {};
    }
    return loaded;
  }

  //! One LOD level of a group: the node holding its mesh and the transform
  //! that takes that mesh into the group's geometry space.
  struct FbxLodLevel final {
    const ufbx_node* mesh_node = nullptr;
    glm::mat4 transform { 1.0F };
  };

  //! Source levels that form one geometry with several LODs: an FBX LodGroup,
  //! shown by its own node, or sibling meshes named `<base>_LOD<n>`, shown by
  //! the `_LOD0` node.
  struct FbxLodGroup final {
    size_t host_node = internal::kNoBakeIndex;
    std::string name;
    std::vector<FbxLodLevel> levels;
  };

  //! Why a LOD level, or a whole group, is not imported as authored.
  struct FbxLodNote final {
    std::string code;
    std::string message;
    size_t node = internal::kNoBakeIndex;
  };

  //! The LOD groups of an FBX scene and the nodes they take over.
  struct FbxLodGroups final {
    std::vector<FbxLodGroup> groups;
    //! Per node: the group it hosts, or `kNoBakeIndex`.
    std::vector<size_t> host_group;
    //! Per node: 1 when the node is a LOD level or helper the group replaces,
    //! so the scene imports neither it nor its subtree.
    std::vector<uint8_t> member;
    //! Per node: 1 when a group owns the node's mesh, so it makes no geometry
    //! of its own.
    std::vector<uint8_t> grouped;
    std::vector<FbxLodNote> notes;
  };

  //! The geometry plan of an FBX scene: the shared mesh variants, then one
  //! variant per LOD group, in group order.
  struct FbxBakePlan final {
    internal::MeshBakePlan plan;
    FbxLodGroups lods;
  };

  //! The mesh of a LOD level and the transform from the level node to it.
  struct FbxLodLevelSource final {
    const ufbx_node* mesh_node = nullptr;
    glm::mat4 transform { 1.0F };
    std::string problem;
  };

  //! Reads a LOD level: a node with a static mesh, directly or on its
  //! geometry transform helper, and nothing else beneath it.
  [[nodiscard]] auto ReadLodLevel(
    const ufbx_node& level, const glm::mat4& basis) -> FbxLodLevelSource
  {
    const auto* helper = level.geometry_transform_helper;
    const ufbx_node* mesh_node = nullptr;
    if (level.mesh != nullptr) {
      mesh_node = &level;
    } else if (helper != nullptr && helper->mesh != nullptr) {
      mesh_node = helper;
    } else {
      return {
        .mesh_node = nullptr, .transform = {}, .problem = "has no mesh"
      };
    }
    const auto own_children = std::ranges::count_if(Items(level.children),
      [helper](const ufbx_node* child) -> bool { return child != helper; });
    if (own_children != 0
      || (helper != nullptr && helper->children.count != 0)) {
      return {
        .mesh_node = nullptr,
        .transform = {},
        .problem = "has child nodes",
      };
    }
    if (level.camera != nullptr || level.light != nullptr
      || mesh_node->camera != nullptr || mesh_node->light != nullptr) {
      return {
        .mesh_node = nullptr,
        .transform = {},
        .problem = "has a camera or a light",
      };
    }
    if (mesh_node->mesh->all_deformers.count != 0) {
      return {
        .mesh_node = nullptr,
        .transform = {},
        .problem = "is skinned or deformed",
      };
    }
    return {
      .mesh_node = mesh_node,
      .transform = mesh_node == &level ? glm::mat4 { 1.0F }
                                       : LocalTransform(*mesh_node, basis),
      .problem = {},
    };
  }

  //! Splits `<base>_LOD<n>`, with `LOD` in any case and `n` of one or two
  //! digits.
  [[nodiscard]] auto ParseLodSuffix(const std::string_view name)
    -> std::optional<std::pair<std::string_view, size_t>>
  {
    constexpr auto kTag = std::string_view { "LOD" };
    constexpr auto kMaxDigits = size_t { 2 };
    constexpr auto kRadix = size_t { 10 };
    const auto separator = name.rfind('_');
    if (separator == std::string_view::npos || separator == 0) {
      return std::nullopt;
    }
    const auto suffix = name.substr(separator + 1);
    if (suffix.size() <= kTag.size() || suffix.size() > kTag.size() + kMaxDigits
      || !StartsWithIgnoreCase(suffix, kTag)) {
      return std::nullopt;
    }
    size_t level = 0;
    for (const auto digit : suffix.substr(kTag.size())) {
      if (std::isdigit(static_cast<unsigned char>(digit)) == 0) {
        return std::nullopt;
      }
      level = level * kRadix + static_cast<size_t>(digit - '0');
    }
    return std::pair { name.substr(0, separator), level };
  }

  /*!
   Collects the scene's LOD groups. A group keeps its levels from LOD0 up to the
   first level that is missing or unusable, at most `kMaxLodCount`; it imports
   none of the levels after that one and notes each. A LodGroup without a
   usable LOD0, and named siblings without usable LOD0 and LOD1, are no group:
   their nodes import as authored.
  */
  [[nodiscard]] auto FindFbxLodGroups(const ufbx_scene& scene) -> FbxLodGroups
  {
    const auto basis = OxygenBasis(scene);
    auto result = FbxLodGroups {
      .groups = {},
      .host_group = std::vector(scene.nodes.count, internal::kNoBakeIndex),
      .member = std::vector<uint8_t>(scene.nodes.count, 0),
      .grouped = std::vector<uint8_t>(scene.nodes.count, 0),
      .notes = {},
    };
    auto node_indices = std::unordered_map<const ufbx_node*, size_t> {};
    for (size_t index = 0; index < scene.nodes.count; ++index) {
      node_indices.emplace(ItemAt(scene.nodes, index), index);
    }
    const auto index_of = [&node_indices](const ufbx_node* node) -> size_t {
      return node_indices.at(node);
    };
    auto problems = std::unordered_map<size_t, std::string> {};

    // Adds a usable level while the group has room for it.
    const auto try_add_level = [&](FbxLodGroup& lods, const ufbx_node& level,
                                 const glm::mat4& to_group) -> bool {
      if (lods.levels.size() == data::pak::geometry::kMaxLodCount) {
        problems.emplace(index_of(&level),
          "is beyond the " + std::to_string(data::pak::geometry::kMaxLodCount)
            + " LODs a geometry has");
        return false;
      }
      const auto source = ReadLodLevel(level, basis);
      const auto transform = to_group * source.transform;
      if (source.problem.empty()
        && !internal::IsBakeTransformInvertible(transform)) {
        problems.emplace(index_of(&level), "has a non-invertible transform");
        return false;
      }
      if (!source.problem.empty()) {
        problems.emplace(index_of(&level), source.problem);
        return false;
      }
      lods.levels.push_back({
        .mesh_node = source.mesh_node,
        .transform = transform,
      });
      return true;
    };

    // Notes each level the group leaves out, with why for the first one.
    const auto commit = [&](FbxLodGroup lods, std::span<const size_t> skipped,
                          std::span<const size_t> members) -> void {
      for (const auto node : skipped) {
        const auto problem = problems.find(node);
        result.notes.push_back({
          .code = "fbx.lod.level_skipped",
          .message = "LOD level '"
            + std::string(ToStringView(ItemAt(scene.nodes, node)->name))
            + "' of '" + lods.name + "' is not imported: "
            + (problem != problems.end()
                ? "it " + problem->second
                : std::string("a level before it is not imported")),
          .node = node,
        });
      }
      // A replaced node's subtree goes with it, meshes included.
      const auto replace = [&](auto&& self, const size_t node) -> void {
        result.member.at(node) = 1;
        result.grouped.at(node) = 1;
        for (const auto* child : Items(ItemAt(scene.nodes, node)->children)) {
          self(self, index_of(child));
        }
      };
      for (const auto node : members) {
        replace(replace, node);
      }
      result.grouped.at(lods.host_node) = 1;
      for (const auto& level : lods.levels) {
        result.grouped.at(index_of(level.mesh_node)) = 1;
      }
      result.host_group.at(lods.host_node) = result.groups.size();
      result.groups.push_back(std::move(lods));
    };

    const auto ignore
      = [&](const ufbx_node& node, const std::string& message) -> void {
      result.notes.push_back({
        .code = "fbx.lod_group.ignored",
        .message = message,
        .node = index_of(&node),
      });
    };

    const auto add_lod_group = [&](const ufbx_node& group) -> void {
      auto lods = FbxLodGroup {
        .host_node = index_of(&group),
        .name = std::string(ToStringView(group.name)),
        .levels = {},
      };
      auto skipped = std::vector<size_t> {};
      auto members = std::vector<size_t> {};
      for (const auto* child : Items(group.children)) {
        members.push_back(index_of(child));
        if (child->geometry_transform_helper != nullptr) {
          members.push_back(index_of(child->geometry_transform_helper));
        }
        if (!skipped.empty()
          || !try_add_level(lods, *child, LocalTransform(*child, basis))) {
          skipped.push_back(index_of(child));
        }
      }
      if (lods.levels.empty()) {
        ignore(group,
          "LOD group '" + lods.name
            + "' has no usable LOD0; its children import as separate nodes");
        return;
      }
      commit(std::move(lods), skipped, members);
    };

    const auto add_named_levels = [&](const ufbx_node& parent) -> void {
      auto bases
        = std::map<std::string_view, std::map<size_t, const ufbx_node*>> {};
      for (const auto* child : Items(parent.children)) {
        if (const auto parsed = ParseLodSuffix(ToStringView(child->name));
          child != parent.geometry_transform_helper && parsed.has_value()) {
          bases[parsed->first].try_emplace(parsed->second, child);
        }
      }
      for (const auto& [base, named] : bases) {
        const auto first = named.find(0);
        if (first == named.end() || !named.contains(1)) {
          continue;
        }
        const auto& host = *first->second;
        const auto host_local = LocalTransform(host, basis);
        if (!internal::IsBakeTransformInvertible(host_local)) {
          ignore(host,
            "LOD0 of '" + std::string(base)
              + "' has a non-invertible transform; its levels import as "
                "separate nodes");
          continue;
        }
        const auto to_host = glm::inverse(host_local);
        auto lods = FbxLodGroup {
          .host_node = index_of(&host),
          .name = std::string(base),
          .levels = {},
        };
        auto skipped = std::vector<size_t> {};
        auto members = std::vector<size_t> {};
        auto expected = size_t { 0 };
        for (const auto& [level, node] : named) {
          if (level != 0) {
            members.push_back(index_of(node));
          }
          if (node->geometry_transform_helper != nullptr) {
            members.push_back(index_of(node->geometry_transform_helper));
          }
          const auto to_group = level == 0
            ? glm::mat4 { 1.0F }
            : to_host * LocalTransform(*node, basis);
          if (!skipped.empty() || level != expected
            || !try_add_level(lods, *node, to_group)) {
            skipped.push_back(index_of(node));
          }
          ++expected;
        }
        if (lods.levels.size() < 2) {
          ignore(host,
            "'" + std::string(base)
              + "' has no usable LOD0 and LOD1; its levels import as separate "
                "nodes");
          continue;
        }
        commit(std::move(lods), skipped, members);
      }
    };

    for (const auto* node : Items(scene.nodes)) {
      // A Null node may carry the LodGroup beside its own attribute.
      const auto is_lod_group = std::ranges::any_of(
        Items(node->all_attribs), [](const ufbx_element* attribute) -> bool {
          return attribute->type == UFBX_ELEMENT_LOD_GROUP;
        });
      if (is_lod_group) {
        add_lod_group(*node);
      } else {
        add_named_levels(*node);
      }
    }
    return result;
  }

  [[nodiscard]] auto BuildFbxBakePlan(const ufbx_scene& scene,
    const CoordinateConversionPolicy& policy) -> FbxBakePlan
  {
    auto lods = FindFbxLodGroups(scene);
    const auto basis = OxygenBasis(scene);
    std::vector<internal::MeshBakeNode> nodes(scene.nodes.count);
    std::vector<uint8_t> emit_mesh(scene.meshes.count, 1);
    std::unordered_map<const ufbx_mesh*, size_t> mesh_indices;
    std::vector<std::vector<std::vector<const ufbx_material*>>> material_groups(
      scene.meshes.count);
    for (size_t index = 0; index < scene.meshes.count; ++index) {
      mesh_indices.emplace(ItemAt(scene.meshes, index), index);
    }
    std::unordered_set<const ufbx_element*> animated;
    for (const auto* layer : scene.anim_layers) {
      for (const auto& property : layer->anim_props) {
        animated.insert(property.element);
      }
    }
    // A mesh only LOD groups use makes no geometry of its own.
    std::vector<uint8_t> grouped_use(scene.meshes.count, 0);
    std::vector<uint8_t> own_use(scene.meshes.count, 0);
    for (size_t index = 0; index < scene.nodes.count; ++index) {
      const auto* node = ItemAt(scene.nodes, index);
      if (node != nullptr && node->mesh != nullptr) {
        auto& use = lods.grouped.at(index) != 0 ? grouped_use : own_use;
        use.at(mesh_indices.at(node->mesh)) = 1;
      }
    }
    for (size_t mesh = 0; mesh < scene.meshes.count; ++mesh) {
      if (grouped_use.at(mesh) != 0 && own_use.at(mesh) == 0) {
        emit_mesh.at(mesh) = 0;
      }
    }
    for (size_t index = 0; index < scene.nodes.count; ++index) {
      const auto* node = ItemAt(scene.nodes, index);
      if (node == nullptr || node->mesh == nullptr
        || lods.grouped.at(index) != 0) {
        continue;
      }
      auto& input = nodes.at(index);
      input.mesh_index = mesh_indices.at(node->mesh);
      input.local_transform = LocalTransform(*node, basis);
      const auto& materials
        = node->materials.count != 0 ? node->materials : node->mesh->materials;
      const auto material_items = Items(materials);
      const std::vector<const ufbx_material*> bindings(
        material_items.begin(), material_items.end());
      auto& groups = material_groups.at(input.mesh_index);
      const auto match = std::ranges::find(groups, bindings);
      input.material_binding = static_cast<size_t>(match - groups.begin());
      if (match == groups.end()) {
        groups.push_back(bindings);
      }
      if (node->children.count != 0 || node->camera != nullptr
        || node->light != nullptr) {
        input.retain_reason = "attachment or camera/light transform";
      } else if (animated.contains(&node->element) || node->bone != nullptr
        || node->bind_pose != nullptr || node->mesh->all_deformers.count != 0) {
        input.retain_reason = "animation, skinning, or deformation transform";
      }
    }
    auto plan = internal::BuildMeshBakePlan(
      nodes, emit_mesh, policy.bake_transforms_into_meshes);
    // Each group is one geometry, shown by its host node. Its levels carry
    // their transforms into the group's space per LOD; the host's own
    // transform bakes into the whole geometry as another mesh's node would,
    // its levels being part of it rather than attachments.
    for (const auto& group : lods.groups) {
      const auto* host = ItemAt(scene.nodes, group.host_node);
      const auto local = LocalTransform(*host, basis);
      auto transform = std::optional<glm::mat4> {};
      if (policy.bake_transforms_into_meshes && local != glm::mat4(1.0F)) {
        auto& reason = plan.retained_reasons.at(group.host_node);
        if (host->camera != nullptr || host->light != nullptr) {
          reason = "attachment or camera/light transform";
        } else if (animated.contains(&host->element)) {
          reason = "animation, skinning, or deformation transform";
        } else if (!internal::IsBakeTransformInvertible(local)) {
          reason = "non-invertible transform";
        } else {
          transform = local;
        }
      }
      plan.node_variant.at(group.host_node) = plan.variants.size();
      plan.variants.push_back(internal::MeshBakeVariant {
        .mesh_index = mesh_indices.at(group.levels.front().mesh_node->mesh),
        .representative_node = group.host_node,
        .material_binding = 0,
        .transform = transform,
        .name_suffix = {},
      });
    }
    return { .plan = std::move(plan), .lods = std::move(lods) };
  }

  //! The LOD group a plan variant stands for, or null.
  [[nodiscard]] auto LodGroupOf(const FbxBakePlan& bake,
    const internal::MeshBakeVariant& variant) -> const FbxLodGroup*
  {
    if (variant.representative_node == internal::kNoBakeIndex) {
      return nullptr;
    }
    const auto group = bake.lods.host_group.at(variant.representative_node);
    return group == internal::kNoBakeIndex ? nullptr
                                           : &bake.lods.groups.at(group);
  }

  //! The materials a mesh binds on a node: the node's, or the mesh's own.
  [[nodiscard]] auto MaterialsOf(const ufbx_mesh& mesh, const ufbx_node* node)
    -> const ufbx_material_list&
  {
    return node != nullptr && node->materials.count > 0 ? node->materials
                                                        : mesh.materials;
  }

  /*!
   Renumbers a coarser LOD's material slots to the geometry's: each takes
   the slot its material has in `slots`, which starts as LOD0's list, or the
   next new slot. A surface is identified by its slot, so a material used in
   several LODs is one surface with a binding in each.
  */
  auto RemapLodSlots(std::vector<TriangleRange>& ranges,
    const ufbx_material_list& materials,
    std::vector<const ufbx_material*>& slots) -> void
  {
    for (auto& range : ranges) {
      if (range.source_slot >= materials.count) {
        range.source_slot = UFBX_NO_INDEX;
        continue;
      }
      const auto* material = ItemAt(materials, range.source_slot);
      const auto match = std::ranges::find(slots, material);
      range.source_slot = static_cast<uint32_t>(match - slots.begin());
      if (match == slots.end()) {
        slots.push_back(material);
      }
    }
    std::ranges::stable_sort(ranges, {}, &TriangleRange::source_slot);
  }

  /*!
   The layout witness of a LOD group's geometry: its levels' mesh witnesses
   and transforms, in LOD order, so a change to any level changes it.
  */
  [[nodiscard]] auto LodGroupWitness(const FbxLodGroup& group,
    const std::unordered_map<const ufbx_mesh*, size_t>& mesh_indices,
    const std::span<const base::Sha256Digest> source_witnesses)
    -> base::Sha256Digest
  {
    SourceLayoutHash hash("oxygen.fbx-lod-group-layout/v1");
    hash.AddCount(group.levels.size());
    for (const auto& level : group.levels) {
      const auto& witness = oxygen::base::CheckedAt(
        source_witnesses, mesh_indices.at(level.mesh_node->mesh));
      for (const auto byte : witness) {
        hash.Add(static_cast<uint32_t>(byte));
      }
      for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
          hash.AddFloat(level.transform[column][row]);
        }
      }
    }
    return hash.Finish();
  }

  auto PrepareGeometryNames(const ufbx_scene& scene, const AdapterInput& input)
    -> ModelGeometryPreparation
  {
    auto result = ModelGeometryPreparation {};
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }
    CHECK_F(input.naming_service != nullptr, "NamingService must not be null");
    const auto bake = BuildFbxBakePlan(scene, input.request.options.coordinate);
    const auto& bake_plan = bake.plan;
    for (size_t index = 0; index < bake_plan.retained_reasons.size(); ++index) {
      if (!bake_plan.retained_reasons.at(index).empty()) {
        result.diagnostics.push_back(
          MakeWarningDiagnostic("mesh.transform_bake_retained",
            "Retained authored node transform: "
              + bake_plan.retained_reasons.at(index),
            input.source_id_prefix, "/nodes/" + std::to_string(index)));
      }
    }
    for (const auto& note : bake.lods.notes) {
      result.diagnostics.push_back(
        MakeWarningDiagnostic(note.code, note.message, input.source_id_prefix,
          "/nodes/" + std::to_string(note.node)));
    }
    result.sources.reserve(bake_plan.variants.size());
    for (size_t variant_index = 0; variant_index < bake_plan.variants.size();
      ++variant_index) {
      if (input.stop_token.stop_requested()) {
        result.success = false;
        result.diagnostics.push_back(
          MakeCancelDiagnostic(input.source_id_prefix));
        return result;
      }
      const auto& variant = bake_plan.variants.at(variant_index);
      const auto* mesh = ItemAt(scene.meshes, variant.mesh_index);
      if (mesh == nullptr) {
        continue;
      }
      // A LOD group's geometry takes the group's name.
      const auto* group = LodGroupOf(bake, variant);
      const auto authored_name = group != nullptr
        ? std::string_view(group->name)
        : ToStringView(mesh->name);
      const auto context = NamingContext {
        .kind = ImportNameKind::kMesh,
        .ordinal = static_cast<uint32_t>(variant.mesh_index),
        .variant = variant.name_suffix,
      };
      auto name = input.naming_service->MakeUniqueName(authored_name, context);
      auto source_id = BuildSourceId(
        input.source_id_prefix, name, static_cast<uint32_t>(variant_index));
      result.sources.push_back({
        .variant = variant,
        .name = std::move(name),
        .source_id = std::move(source_id),
      });
    }
    return result;
  }

  [[nodiscard]] auto StreamWorkItemsFromScene(const ufbx_scene& scene,
    const std::span<const base::Sha256Digest> source_witnesses,
    const AdapterInput& input, GeometryWorkItemSink& sink)
    -> WorkItemStreamResult
  {
    WorkItemStreamResult result;
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }

    CHECK_F(input.naming_service != nullptr, "NamingService must not be null");

    std::unordered_map<const ufbx_material*, uint32_t>
      scene_material_index_by_ptr;
    scene_material_index_by_ptr.reserve(scene.materials.count);
    for (uint32_t mat_i = 0; mat_i < scene.materials.count; ++mat_i) {
      const auto* mat = ItemAt(scene.materials, mat_i);
      if (mat == nullptr) {
        continue;
      }
      scene_material_index_by_ptr.emplace(mat, mat_i);
    }
    DLOG_F(2, "FBX scene meshes={} skin_deformers={}", scene.meshes.count,
      scene.skin_deformers.count);

    auto prepared = PrepareGeometryNames(scene, input);
    result.success = prepared.success;
    result.diagnostics = std::move(prepared.diagnostics);
    if (!result.success) {
      return result;
    }
    const auto bake = BuildFbxBakePlan(scene, input.request.options.coordinate);
    const auto basis = OxygenBasis(scene);
    std::unordered_map<const ufbx_mesh*, size_t> mesh_indices;
    mesh_indices.reserve(scene.meshes.count);
    for (size_t index = 0; index < scene.meshes.count; ++index) {
      mesh_indices.emplace(ItemAt(scene.meshes, index), index);
    }
    for (const auto& source : prepared.sources) {
      const auto& variant = source.variant;
      const auto mesh_i = variant.mesh_index;
      const auto* mesh = ItemAt(scene.meshes, variant.mesh_index);
      const auto& mesh_name = source.name;
      if (input.stop_token.stop_requested()) {
        result.success = false;
        result.diagnostics.push_back(
          MakeCancelDiagnostic(input.source_id_prefix));
        return result;
      }

      // The meshes of the geometry's LODs: a variant's one mesh, or a LOD
      // group's levels, each with the transform into the group's space.
      struct LodInput final {
        const ufbx_mesh* mesh = nullptr;
        const ufbx_node* material_node = nullptr;
        std::optional<glm::mat4> transform;
      };
      const auto* group = LodGroupOf(bake, variant);
      std::vector<LodInput> inputs;
      if (group != nullptr) {
        for (const auto& level : group->levels) {
          inputs.push_back({
            .mesh = level.mesh_node->mesh,
            .material_node = level.mesh_node,
            .transform = level.transform == glm::mat4 { 1.0F }
              ? std::nullopt
              : std::optional(level.transform),
          });
        }
      } else {
        inputs.push_back({
          .mesh = mesh,
          .material_node = variant.representative_node != internal::kNoBakeIndex
            ? ItemAt(scene.nodes, variant.representative_node)
            : nullptr,
          .transform = std::nullopt,
        });
      }

      MeshBuildPipeline::WorkItem item;
      item.source_layout_witness = group != nullptr && group->levels.size() > 1
        ? LodGroupWitness(*group, mesh_indices, source_witnesses)
        : oxygen::base::CheckedAt(source_witnesses, mesh_i);
      item.source_id = source.source_id;
      item.mesh_name = mesh_name;
      item.storage_mesh_name = mesh_name;
      item.source_key = variant.representative_node != internal::kNoBakeIndex
        ? static_cast<const void*>(
            ItemAt(scene.nodes, variant.representative_node))
        : static_cast<const void*>(mesh);
      item.bake_transform = variant.transform;
      item.material_keys.assign(
        input.material_keys.begin(), input.material_keys.end());
      item.default_material_key = input.default_material_key;
      item.want_textures = true;
      item.request = input.request;
      item.naming_service = input.naming_service;
      item.stop_token = input.stop_token;

      // LOD0's material list numbers the slots; coarser LODs renumber theirs
      // to it, so each material is one surface across the LODs.
      const auto& lod0_materials
        = MaterialsOf(*inputs.front().mesh, inputs.front().material_node);
      std::vector<const ufbx_material*> slot_materials(
        Items(lod0_materials).begin(), Items(lod0_materials).end());
      std::set<uint32_t> surfaces_used;
      bool built = true;
      for (const auto& [lod_index, lod_input] : std::views::enumerate(inputs)) {
        const auto& materials
          = MaterialsOf(*lod_input.mesh, lod_input.material_node);
        if (std::ranges::any_of(Items(materials), HasMaterialTextures)) {
          item.has_material_textures = true;
        }

        std::vector<ImportDiagnostic> diagnostics;
        auto buffers = BuildTriangleBuffers(*lod_input.mesh, basis,
          lod_input.material_node, scene_material_index_by_ptr,
          static_cast<uint32_t>(scene_material_index_by_ptr.size()),
          diagnostics, item.source_id, item.mesh_name);
        if (!buffers.has_value()) {
          result.diagnostics.insert(
            result.diagnostics.end(), diagnostics.begin(), diagnostics.end());
          built = false;
          break;
        }
        if (lod_index != 0) {
          RemapLodSlots(buffers->ranges, materials, slot_materials);
        }
        for (const auto& range : buffers->ranges) {
          const auto* material = range.source_slot < slot_materials.size()
            ? slot_materials.at(range.source_slot)
            : nullptr;
          item.surface_names.try_emplace(range.source_slot,
            material != nullptr ? std::string(ToStringView(material->name))
                                : "Default");
          surfaces_used.insert(range.surface);
        }

        DLOG_F(2,
          "FBX mesh[{}] LOD{} skin_deformer_found={} joints={} weights={}",
          mesh_i, lod_index, FindSkinDeformer(*lod_input.mesh) != nullptr,
          buffers->joint_indices.size(), buffers->joint_weights.size());

        const bool is_skinned = !buffers->joint_indices.empty()
          && buffers->joint_weights.size() == buffers->joint_indices.size();

        auto owner = std::make_shared<TriangleMeshBuffers>(std::move(*buffers));
        TriangleMesh triangle_mesh {
          .mesh_type = is_skinned ? data::MeshType::kSkinned
                                  : data::MeshType::kStandard,
          .streams = MeshStreamView {
            .positions = std::span<const glm::vec3>(
              owner->positions.data(), owner->positions.size()),
            .normals = std::span<const glm::vec3>(
              owner->normals.data(), owner->normals.size()),
            .texcoords = std::span<const glm::vec2>(
              owner->texcoords.data(), owner->texcoords.size()),
            .tangents = std::span<const glm::vec3>(
              owner->tangents.data(), owner->tangents.size()),
            .bitangents = std::span<const glm::vec3>(
              owner->bitangents.data(), owner->bitangents.size()),
            .colors = std::span<const glm::vec4>(
              owner->colors.data(), owner->colors.size()),
            .joint_indices = std::span<const glm::uvec4>(
              owner->joint_indices.data(), owner->joint_indices.size()),
            .joint_weights = std::span<const glm::vec4>(
              owner->joint_weights.data(), owner->joint_weights.size()),
          },
          .inverse_bind_matrices = std::span<const glm::mat4>(
            owner->inverse_bind_matrices.data(),
            owner->inverse_bind_matrices.size()),
          .joint_remap = std::span<const uint32_t>(
            owner->joint_remap.data(), owner->joint_remap.size()),
          .indices = std::span<const uint32_t>(
            owner->indices.data(), owner->indices.size()),
          .ranges = std::span<const TriangleRange>(
            owner->ranges.data(), owner->ranges.size()),
          .bounds = std::nullopt,
        };

        item.lods.push_back(MeshLod {
          .lod_name = "LOD" + std::to_string(lod_index),
          .source = triangle_mesh,
          .source_owner = std::move(owner),
          .bake_transform = lod_input.transform,
        });
      }
      if (!built) {
        result.success = false;
        continue;
      }
      item.surfaces_used.assign(surfaces_used.begin(), surfaces_used.end());

      if (!sink.Consume(std::move(item))) {
        return result;
      }
      ++result.emitted;
    }

    return result;
  }

} // namespace

auto FbxAdapter::InspectSource(const std::filesystem::path& source_path,
  const AdapterInput& input) -> SceneSourceInspection
{
  SceneSourceInspection result;
  auto adapter = FbxAdapter {};
  result.format = "fbx";
  adapter.impl_->source = {};
  auto loaded = LoadSceneFromFile(
    source_path, input, result.diagnostics, ModelParseMode::kMetadata);
  const auto& scene = loaded.scene;
  if (!scene) {
    return result;
  }
  result.parsed = true;
  result.supported = internal::ValidateStaticSource(
    *scene, input.source_id_prefix, result.diagnostics);
  const auto axis_name = [](const ufbx_coordinate_axis axis) -> std::string {
    switch (axis) {
    case UFBX_COORDINATE_AXIS_POSITIVE_X:
      return "+X";
    case UFBX_COORDINATE_AXIS_NEGATIVE_X:
      return "-X";
    case UFBX_COORDINATE_AXIS_POSITIVE_Y:
      return "+Y";
    case UFBX_COORDINATE_AXIS_NEGATIVE_Y:
      return "-Y";
    case UFBX_COORDINATE_AXIS_POSITIVE_Z:
      return "+Z";
    case UFBX_COORDINATE_AXIS_NEGATIVE_Z:
      return "-Z";
    default:
      return "unknown";
    }
  };
  const auto& axes = scene->settings.axes;
  result.source_right = axis_name(axes.right);
  result.source_up = axis_name(axes.up);
  result.source_front = axis_name(axes.front);
  if (ufbx_coordinate_axes_valid(axes)) {
    result.source_left_handed = IsLeftHandedAxes(axes);
  }
  const auto* units = ufbx_find_prop(&scene->settings.props, "UnitScaleFactor");
  constexpr auto kUnauthoredProp
    = static_cast<uint32_t>(UFBX_PROP_FLAG_NOT_FOUND)
    | static_cast<uint32_t>(UFBX_PROP_FLAG_SYNTHETIC);
  if (units != nullptr
    && (static_cast<uint32_t>(units->flags) & kUnauthoredProp) == 0U
    && std::isfinite(scene->settings.unit_meters)
    && scene->settings.unit_meters > 0.0) {
    result.source_unit_meters = scene->settings.unit_meters;
  }
  result.reverses_winding
    = scene->metadata.mirror_axis != UFBX_MIRROR_AXIS_NONE;
  result.mesh_count = scene->meshes.count;
  result.material_count = scene->materials.count;
  result.node_count = scene->nodes.count;
  adapter.impl_->source = std::move(loaded);
  if (result.supported) {
    for (const auto& texture :
      adapter.CollectExternalTextureSources(input, result.diagnostics)) {
      result.external_files.push_back(
        texture.resolved_path.lexically_relative(source_path.parent_path())
          .generic_string());
    }
    result.supported = std::ranges::none_of(
      result.diagnostics, [](const auto& diagnostic) -> auto {
        return diagnostic.severity == ImportSeverity::kError;
      });
  }
  std::ranges::sort(result.external_files);
  const auto duplicates = std::ranges::unique(result.external_files);
  result.external_files.erase(duplicates.begin(), duplicates.end());
  return result;
}

auto FbxAdapter::Parse(const std::filesystem::path& source_path,
  const AdapterInput& input, const ModelParseMode mode) -> ParseResult
{
  ParseResult result;
  auto loaded = LoadSceneFromFile(source_path, input, result.diagnostics, mode);
  const auto& scene = loaded.scene;
  if (!scene) {
    DLOG_F(ERROR, "FBX parse failed: path='{}' diagnostics={}",
      source_path.string(), result.diagnostics.size());
    if (result.diagnostics.empty()) {
      result.diagnostics.push_back(MakeErrorDiagnostic("fbx.parse_failed",
        "FBX parse failed without diagnostics", input.source_id_prefix, ""));
    }
    impl_->source = {};
    result.success = false;
    return result;
  }

  if (input.request.options.scene_content_policy == SceneContentPolicy::kStatic
    && !internal::ValidateStaticSource(
      *scene, input.source_id_prefix, result.diagnostics)) {
    impl_->source = {};
    result.success = false;
    return result;
  }

  impl_->source = std::move(loaded);
  return result;
}

auto FbxAdapter::Parse(const std::span<const std::byte> source_bytes,
  const AdapterInput& input, const ModelParseMode mode) -> ParseResult
{
  ParseResult result;
  auto loaded
    = LoadSceneFromMemory(source_bytes, input, result.diagnostics, mode);
  const auto& scene = loaded.scene;
  if (!scene) {
    DLOG_F(ERROR, "FBX parse failed (memory): diagnostics={}",
      result.diagnostics.size());
    if (result.diagnostics.empty()) {
      result.diagnostics.push_back(MakeErrorDiagnostic("fbx.parse_failed",
        "FBX parse failed without diagnostics", input.source_id_prefix, ""));
    }
    impl_->source = {};
    result.success = false;
    return result;
  }

  if (input.request.options.scene_content_policy == SceneContentPolicy::kStatic
    && !internal::ValidateStaticSource(
      *scene, input.source_id_prefix, result.diagnostics)) {
    impl_->source = {};
    result.success = false;
    return result;
  }

  impl_->source = std::move(loaded);
  return result;
}

auto FbxAdapter::PrepareGeometry(const AdapterInput& input) const
  -> ModelGeometryPreparation
{
  if (!impl_->source.scene) {
    auto result = ModelGeometryPreparation {};
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("fbx.scene.not_parsed",
      "fbx adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }
  return PrepareGeometryNames(*impl_->source.scene, input);
}

auto FbxAdapter::BuildWorkItems(GeometryWorkTag /*unused*/,
  GeometryWorkItemSink& sink, const AdapterInput& input) -> WorkItemStreamResult
{
  if (!impl_->source.scene) {
    WorkItemStreamResult result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("fbx.scene.not_parsed",
      "FBX adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  if (impl_->source.source_layout_witnesses.size()
    != impl_->source.scene->meshes.count) {
    auto result = WorkItemStreamResult {};
    result.success = false;
    result.diagnostics.push_back(
      MakeErrorDiagnostic("mesh.source_layout_unavailable",
        "Geometry production requires a geometry-mode parse",
        input.source_id_prefix, input.object_path_prefix));
    return result;
  }

  return StreamWorkItemsFromScene(
    *impl_->source.scene, impl_->source.source_layout_witnesses, input, sink);
}

auto FbxAdapter::PrepareMaterials(const AdapterInput& input)
  -> ModelMaterialPreparation
{
  if (!impl_->source.scene) {
    ModelMaterialPreparation result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("fbx.scene.not_parsed",
      "FBX adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  ModelMaterialPreparation result;
  if (input.stop_token.stop_requested()) {
    result.success = false;
    result.diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
    return result;
  }

  const auto& scene = *impl_->source.scene;

  std::unordered_map<const ufbx_texture*, std::string> texture_ids;

  auto resolve_texture_id
    = [&](const ufbx_texture* texture) -> std::optional<std::string> {
    const auto* file_tex = ResolveFileTexture(texture);
    if (file_tex == nullptr) {
      return std::nullopt;
    }

    if (const auto it = texture_ids.find(file_tex); it != texture_ids.end()) {
      return it->second;
    }

    auto identity = ResolveTextureIdentity(texture, input.request);
    if (!identity.has_value()) {
      return std::nullopt;
    }

    texture_ids.emplace(file_tex, identity->texture_id);
    return identity->texture_id;
  };

  auto apply_binding = [&](MaterialTextureBinding& binding,
                         std::optional<std::string> texture_id,
                         const TextureUsage usage) -> void {
    if (!texture_id.has_value()) {
      return;
    }

    binding.assigned = true;
    binding.source_id
      = BuildTextureSourceId(input.source_id_prefix, *texture_id, usage);
    binding.index = 0;
    binding.uv_set = 0;
  };

  const auto material_count = static_cast<uint32_t>(scene.materials.count);
  result.sources.reserve(material_count == 0U ? 1U : material_count);
  if (material_count == 0) {
    CHECK_F(input.naming_service != nullptr, "NamingService must not be null");
    const NamingContext material_context {
      .kind = ImportNameKind::kMaterial,
      .ordinal = 0,
    };
    const auto material_name
      = input.naming_service->MakeUniqueName("Default", material_context);

    ModelMaterialSource item;
    item.material.name = material_name;
    item.source_id
      = BuildSourceId(input.source_id_prefix, item.material.name, 0);
    item.material.storage_name = material_name;
    item.material.domain = data::MaterialDomain::kOpaque;
    item.material.alpha_mode = MaterialAlphaMode::kOpaque;

    if (!item.material.Validate(item.source_id, result.diagnostics)) {
      result.success = false;
      return result;
    }
    result.sources.push_back(std::move(item));
    return result;
  }

  for (uint32_t i = 0; i < material_count; ++i) {
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }

    CHECK_F(input.naming_service != nullptr, "NamingService must not be null");

    const auto* material = ItemAt(scene.materials, i);
    const auto authored_name = material != nullptr
      ? ToStringView(material->name)
      : std::string_view {};
    const NamingContext material_context {
      .kind = ImportNameKind::kMaterial,
      .ordinal = i,
    };
    const auto material_name
      = input.naming_service->MakeUniqueName(authored_name, material_context);

    ModelMaterialSource item;
    item.source_id = BuildSourceId(input.source_id_prefix, material_name, i);
    item.material.name = material_name;
    item.material.storage_name = material_name;
    item.source_key = material;
    item.material.domain = data::MaterialDomain::kOpaque;
    item.material.alpha_mode = MaterialAlphaMode::kOpaque;

    if (material != nullptr) {
      auto base = glm::vec4 { 1.0F };
      if (material->pbr.base_color.has_value
        && material->pbr.base_color.value_components >= 3) {
        base = ToVec4(material->pbr.base_color.value_vec4);
      } else if (material->fbx.diffuse_color.has_value
        && material->fbx.diffuse_color.value_components >= 3) {
        base
          = glm::vec4 { ToVec3(material->fbx.diffuse_color.value_vec3), 1.0F };
      }

      float base_factor = 1.0F;
      if (material->pbr.base_factor.has_value) {
        base_factor = Clamp01(material->pbr.base_factor.value_real);
      } else if (material->fbx.diffuse_factor.has_value) {
        base_factor = Clamp01(material->fbx.diffuse_factor.value_real);
      }

      item.material.inputs.base_color[0] = Clamp01(base.x * base_factor);
      item.material.inputs.base_color[1] = Clamp01(base.y * base_factor);
      item.material.inputs.base_color[2] = Clamp01(base.z * base_factor);
      item.material.inputs.base_color[3] = Clamp01(base.w * base_factor);

      if (material->pbr.metalness.has_value) {
        item.material.inputs.metalness
          = Clamp01(material->pbr.metalness.value_real);
      }

      if (material->pbr.roughness.has_value) {
        item.material.inputs.roughness
          = Clamp01(material->pbr.roughness.value_real);
      }
      item.material.inputs.roughness_as_glossiness
        = material->features.roughness_as_glossiness.enabled;

      if (material->pbr.ambient_occlusion.has_value) {
        item.material.inputs.ambient_occlusion
          = Clamp01(material->pbr.ambient_occlusion.value_real);
      }

      {
        auto emission = glm::vec4 { 0.0F };
        if (material->pbr.emission_color.has_value
          && material->pbr.emission_color.value_components >= 3) {
          emission = ToVec4(material->pbr.emission_color.value_vec4);
        } else if (material->fbx.emission_color.has_value
          && material->fbx.emission_color.value_components >= 3) {
          emission
            = glm::vec4 { ToVec3(material->fbx.emission_color.value_vec3),
                0.0F };
        }

        float emission_factor = 1.0F;
        if (material->pbr.emission_factor.has_value) {
          emission_factor = material->pbr.emission_factor.value_real;
        } else if (material->fbx.emission_factor.has_value) {
          emission_factor = material->fbx.emission_factor.value_real;
        }

        item.material.inputs.emissive_factor[0] = emission.x * emission_factor;
        item.material.inputs.emissive_factor[1] = emission.y * emission_factor;
        item.material.inputs.emissive_factor[2] = emission.z * emission_factor;
      }

      if (material->pbr.normal_map.has_value) {
        item.material.inputs.normal_scale
          = (std::max)(0.0F, material->pbr.normal_map.value_real);
      } else if (material->fbx.bump_factor.has_value) {
        item.material.inputs.normal_scale
          = (std::max)(0.0F, material->fbx.bump_factor.value_real);
      }

      float specular_factor = 1.0F;
      if (IsLambertMaterial(*material)) {
        specular_factor = 0.5F;
      } else if (material->pbr.specular_factor.has_value) {
        specular_factor = Clamp01(material->pbr.specular_factor.value_real);
      } else if (material->fbx.specular_factor.has_value) {
        specular_factor = Clamp01(material->fbx.specular_factor.value_real);
      }

      if (material->pbr.specular_color.has_value) {
        const auto& c = material->pbr.specular_color.value_vec4;
        const float intensity = (std::max)({ c.x, c.y, c.z });
        specular_factor *= intensity;
      } else if (material->fbx.specular_color.has_value) {
        const auto& c = material->fbx.specular_color.value_vec4;
        const float intensity = (std::max)({ c.x, c.y, c.z });
        specular_factor *= intensity;
      }

      item.material.inputs.specular_factor = Clamp01(specular_factor);

      item.material.inputs.double_sided
        = material->features.double_sided.enabled;
      item.material.inputs.unlit = material->features.unlit.enabled;

      const auto textures = FbxMaterialTextures::From(*material);
      const auto* base_color_tex = textures.base_color.Texture();
      const auto* normal_tex = textures.normal.Texture();
      const auto* metallic_tex = textures.metallic.Texture();
      const auto* roughness_tex = textures.roughness.Texture();
      const auto* ao_tex = textures.occlusion.Texture();
      const auto* emissive_tex = textures.emissive.Texture();

      const auto* metallic_file = ResolveFileTexture(metallic_tex);
      const auto* roughness_file = ResolveFileTexture(roughness_tex);
      const bool orm_packed
        = metallic_file != nullptr && metallic_file == roughness_file;

      if (orm_packed) {
        const auto texture_id = resolve_texture_id(metallic_tex);
        if (texture_id.has_value()) {
          const auto source_id = BuildTextureSourceId(input.source_id_prefix,
            *texture_id, TextureUsage::kMetallicRoughness);
          item.material.textures.metallic.assigned = true;
          item.material.textures.metallic.source_id = source_id;
          item.material.textures.metallic.index = 0;
          item.material.textures.metallic.uv_set = 0;
          item.material.textures.roughness.assigned = true;
          item.material.textures.roughness.source_id = source_id;
          item.material.textures.roughness.index = 0;
          item.material.textures.roughness.uv_set = 0;

          const auto* ao_file = ResolveFileTexture(ao_tex);
          if (ao_file != nullptr && ao_file == metallic_file) {
            item.material.textures.ambient_occlusion.assigned = true;
            item.material.textures.ambient_occlusion.source_id = source_id;
            item.material.textures.ambient_occlusion.index = 0;
            item.material.textures.ambient_occlusion.uv_set = 0;
          }
        }
      }

      if (!orm_packed) {
        apply_binding(item.material.textures.metallic,
          resolve_texture_id(metallic_tex), TextureUsage::kMetallic);
        apply_binding(item.material.textures.roughness,
          resolve_texture_id(roughness_tex), TextureUsage::kRoughness);
      }

      apply_binding(item.material.textures.base_color,
        resolve_texture_id(base_color_tex), TextureUsage::kBaseColor);
      apply_binding(item.material.textures.normal,
        resolve_texture_id(normal_tex), TextureUsage::kNormal);
      if (!orm_packed
        || item.material.textures.ambient_occlusion.source_id.empty()) {
        apply_binding(item.material.textures.ambient_occlusion,
          resolve_texture_id(ao_tex), TextureUsage::kOcclusion);
      }
      apply_binding(item.material.textures.emissive,
        resolve_texture_id(emissive_tex), TextureUsage::kEmissive);

      if (material->features.specular.enabled) {
        if (material->pbr.specular_factor.has_value) {
          item.material.inputs.specular_factor
            = static_cast<float>(material->pbr.specular_factor.value_real);
        }
        if (material->pbr.specular_color.texture_enabled) {
          apply_binding(item.material.textures.specular,
            resolve_texture_id(material->pbr.specular_color.texture),
            TextureUsage::kSpecular);
        }
      }

      if (material->features.sheen.enabled) {
        if (material->pbr.sheen_color.has_value) {
          const auto sc = material->pbr.sheen_color.value_vec3;
          item.material.inputs.sheen_color_factor[0] = static_cast<float>(sc.x);
          item.material.inputs.sheen_color_factor[1] = static_cast<float>(sc.y);
          item.material.inputs.sheen_color_factor[2] = static_cast<float>(sc.z);
        }
        if (material->pbr.sheen_color.texture_enabled) {
          apply_binding(item.material.textures.sheen_color,
            resolve_texture_id(material->pbr.sheen_color.texture),
            TextureUsage::kSheenColor);
        }
      }

      if (material->features.coat.enabled) {
        if (material->pbr.coat_factor.has_value) {
          item.material.inputs.clearcoat_factor
            = static_cast<float>(material->pbr.coat_factor.value_real);
        }
        if (material->pbr.coat_roughness.has_value) {
          item.material.inputs.clearcoat_roughness
            = static_cast<float>(material->pbr.coat_roughness.value_real);
        }
        if (material->pbr.coat_color
              .texture_enabled) { // coat_color is often used for weight/factor
                                  // map
          apply_binding(item.material.textures.clearcoat,
            resolve_texture_id(material->pbr.coat_factor.texture),
            TextureUsage::kClearcoat);
        }
        if (material->pbr.coat_normal.texture_enabled) {
          apply_binding(item.material.textures.clearcoat_normal,
            resolve_texture_id(material->pbr.coat_normal.texture),
            TextureUsage::kClearcoatNormal);
        }
      }

      if (material->features.transmission.enabled) {
        if (material->pbr.transmission_factor.has_value) {
          item.material.inputs.transmission_factor
            = static_cast<float>(material->pbr.transmission_factor.value_real);
        }
        if (material->pbr.transmission_color.texture_enabled) {
          const auto* tex = material->pbr.transmission_color.texture;
          if (tex == nullptr) {
            tex = material->pbr.transmission_factor.texture;
          }
          apply_binding(item.material.textures.transmission,
            resolve_texture_id(tex), TextureUsage::kTransmission);
        }

        if (material->pbr.transmission_scatter.has_value) {
          const auto sc = material->pbr.transmission_scatter.value_vec3;
          item.material.inputs.attenuation_color[0] = static_cast<float>(sc.x);
          item.material.inputs.attenuation_color[1] = static_cast<float>(sc.y);
          item.material.inputs.attenuation_color[2] = static_cast<float>(sc.z);
        }
        if (material->pbr.transmission_depth.has_value) {
          item.material.inputs.attenuation_distance
            = static_cast<float>(material->pbr.transmission_depth.value_real);
        }
      }

      if (material->features.ior.enabled
        && material->pbr.specular_ior.has_value) {
        item.material.inputs.ior
          = static_cast<float>(material->pbr.specular_ior.value_real);
      }
    }

    if (!item.material.Validate(item.source_id, result.diagnostics)) {
      result.success = false;
      return result;
    }
    result.sources.push_back(std::move(item));
  }

  return result;
}

auto FbxAdapter::BuildWorkItems(MaterialWorkTag /*unused*/,
  MaterialWorkItemSink& sink, const AdapterInput& input) -> WorkItemStreamResult
{
  auto prepared = PrepareMaterials(input);
  auto result = WorkItemStreamResult {};
  result.success = prepared.success;
  result.diagnostics = std::move(prepared.diagnostics);
  if (!result.success) {
    return result;
  }
  for (auto& source : prepared.sources) {
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }
    auto item = MaterialPipeline::WorkItem {};
    item.source_id = std::move(source.source_id);
    item.source_key = source.source_key;
    item.material = std::move(source.material);
    item.request = input.request;
    item.naming_service = input.naming_service;
    item.stop_token = input.stop_token;
    if (!sink.Consume(std::move(item))) {
      return result;
    }
    ++result.emitted;
  }
  return result;
}

auto FbxAdapter::PrepareTextures(const AdapterInput& input) const
  -> ModelTexturePreparation
{
  if (!impl_->source.scene) {
    ModelTexturePreparation result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("fbx.scene.not_parsed",
      "FBX adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  ModelTexturePreparation result;
  if (input.stop_token.stop_requested()) {
    result.success = false;
    result.diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
    return result;
  }

  const auto& scene = *impl_->source.scene;
  // Sorted by source key, so textures are produced in a fixed order.
  std::map<std::string, ModelTextureSource> work_items;
  std::unordered_map<const ufbx_texture*, TextureIdentity> identities;

  auto get_identity
    = [&](const ufbx_texture* texture) -> std::optional<TextureIdentity> {
    const auto* file_tex = ResolveFileTexture(texture);
    if (file_tex == nullptr) {
      return std::nullopt;
    }

    if (const auto it = identities.find(file_tex); it != identities.end()) {
      return it->second;
    }

    auto identity = ResolveTextureIdentity(texture, input.request);
    if (!identity.has_value()) {
      return std::nullopt;
    }

    identities.emplace(file_tex, *identity);
    return identity;
  };

  auto register_texture
    = [&](const ufbx_texture* texture, const TextureUsage usage) -> void {
    if (texture == nullptr) {
      return;
    }

    auto identity = get_identity(texture);
    if (!identity.has_value()) {
      return;
    }

    const auto tex_source_id = BuildTextureSourceId(
      input.source_id_prefix, identity->texture_id, usage);
    if (work_items.contains(tex_source_id)) {
      return;
    }

    auto desc = MakeDescFromPreset(PresetForUsage(usage));
    desc.source_id = tex_source_id;
    desc.stop_token = input.stop_token;
    const auto& tuning = input.request.options.texture_tuning;
    if (tuning.enabled) {
      desc.flip_y_on_decode = tuning.flip_y_on_decode;
      desc.force_rgba_on_decode = tuning.force_rgba_on_decode;
      desc.mip_policy = tuning.mip_policy;
      desc.max_mip_levels = tuning.max_mip_levels;
      desc.mip_filter = tuning.mip_filter;
      desc.output_format = (usage == TextureUsage::kBaseColor
                             || usage == TextureUsage::kEmissive)
        ? tuning.color_output_format
        : tuning.data_output_format;
      desc.bc7_quality = IsBc7Format(desc.output_format) ? tuning.bc7_quality
                                                         : Bc7Quality::kNone;
    }

    ModelTextureSource item {};
    item.source_id = tex_source_id;
    item.external_texture_id = identity->texture_id;
    item.source_key = identity->file_texture;
    item.desc = std::move(desc);
    item.packing_policy_id
      = tuning.enabled ? tuning.packing_policy_id : "d3d12";
    item.output_format_policy = tuning.enabled
      ? TexturePipeline::OutputFormatPolicy::kExplicit
      : TexturePipeline::OutputFormatPolicy::kMaterialPreset;
    item.failure_policy
      = input.request.options.texture_tuning.placeholder_on_failure
      ? TexturePipeline::FailurePolicy::kPlaceholder
      : TexturePipeline::FailurePolicy::kStrict;
    item.source_path = identity->resolved_path;
    item.embedded = identity->embedded;
    item.image_key = identity->texture_id;
    item.ordinal = identity->file_texture->typed_id;
    item.usage = UsageLabel(usage);
    item.authored_name = TextureAuthoredName(*identity->file_texture);

    work_items.emplace(tex_source_id, std::move(item));
  };

  const auto material_count = static_cast<uint32_t>(scene.materials.count);
  for (uint32_t i = 0; i < material_count; ++i) {
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }

    const auto* material = ItemAt(scene.materials, i);
    if (material == nullptr) {
      continue;
    }

    const auto textures = FbxMaterialTextures::From(*material);
    const auto* base_color_tex = textures.base_color.Texture();
    const auto* normal_tex = textures.normal.Texture();
    const auto* metallic_tex = textures.metallic.Texture();
    const auto* roughness_tex = textures.roughness.Texture();
    const auto* ao_tex = textures.occlusion.Texture();
    const auto* emissive_tex = textures.emissive.Texture();

    const auto* metallic_file = ResolveFileTexture(metallic_tex);
    const auto* roughness_file = ResolveFileTexture(roughness_tex);
    const bool orm_packed
      = metallic_file != nullptr && metallic_file == roughness_file;

    register_texture(base_color_tex, TextureUsage::kBaseColor);
    register_texture(normal_tex, TextureUsage::kNormal);
    register_texture(emissive_tex, TextureUsage::kEmissive);

    if (orm_packed) {
      register_texture(metallic_tex, TextureUsage::kMetallicRoughness);
      const auto* ao_file = ResolveFileTexture(ao_tex);
      if (ao_file == nullptr || ao_file != metallic_file) {
        register_texture(ao_tex, TextureUsage::kOcclusion);
      }
    } else {
      register_texture(metallic_tex, TextureUsage::kMetallic);
      register_texture(roughness_tex, TextureUsage::kRoughness);
      register_texture(ao_tex, TextureUsage::kOcclusion);
    }
  }

  result.sources.reserve(work_items.size());
  for (auto& [_, item] : work_items) {
    result.sources.push_back(std::move(item));
  }
  NameModelTextures(result.sources);
  return result;
}

auto FbxAdapter::BuildWorkItems(TextureWorkTag /*unused*/,
  TextureWorkItemSink& sink, const AdapterInput& input) -> WorkItemStreamResult
{
  auto prepared = PrepareTextures(input);
  auto result = WorkItemStreamResult {};
  result.success = prepared.success;
  result.diagnostics = std::move(prepared.diagnostics);
  if (!result.success) {
    return result;
  }
  for (auto& source : prepared.sources) {
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }
    const auto identity = TextureIdentity {
      .texture_id = source.external_texture_id,
      .file_texture = static_cast<const ufbx_texture*>(source.source_key),
      .resolved_path = source.source_path,
      .embedded = source.embedded,
    };
    auto resolved = ResolveTextureSourceBytes(identity, source.source_id,
      impl_->source.scene, result.diagnostics, input.external_texture_bytes);
    if (!resolved.has_value()) {
      continue;
    }
    auto item = TexturePipeline::WorkItem {};
    item.source_id = std::move(source.source_id);
    item.texture_id = item.source_id;
    item.name = std::move(source.name);
    item.source_key = source.source_key;
    item.desc = std::move(source.desc);
    item.packing_policy_id = std::move(source.packing_policy_id);
    item.output_format_policy = source.output_format_policy;
    item.failure_policy = source.failure_policy;
    item.source = resolved->bytes;
    item.source_path = std::move(resolved->source_path);
    item.stop_token = input.stop_token;
    if (!sink.Consume(std::move(item))) {
      return result;
    }
    ++result.emitted;
  }
  return result;
}

auto FbxAdapter::CollectExternalTextureSources(
  const AdapterInput& input, std::vector<ImportDiagnostic>& diagnostics) const
  -> std::vector<ExternalTextureSource>
{
  auto prepared = PrepareTextures(input);
  for (auto& diagnostic : prepared.diagnostics) {
    diagnostics.push_back(std::move(diagnostic));
  }
  auto sources = std::vector<ExternalTextureSource> {};
  auto seen_ids = std::unordered_set<std::string> {};
  for (auto& source : prepared.sources) {
    if (!source.embedded && !source.source_path.empty()
      && seen_ids.insert(source.external_texture_id).second) {
      sources.push_back({
        .texture_id = std::move(source.external_texture_id),
        .resolved_path = std::move(source.source_path),
      });
    }
  }
  return sources;
}

auto FbxAdapter::BuildSceneStage(const SceneStageInput& input,
  std::vector<ImportDiagnostic>& diagnostics) const -> SceneStageResult
{
  SceneStageResult result;
  if (input.stop_token.stop_requested()) {
    diagnostics.push_back(MakeCancelDiagnostic(input.source_id));
    return result;
  }

  if (!impl_->source.scene) {
    diagnostics.push_back(MakeErrorDiagnostic("fbx.scene.not_parsed",
      "FBX adapter has no parsed scene", input.source_id, {}));
    return result;
  }

  if (input.request == nullptr) {
    diagnostics.push_back(MakeErrorDiagnostic("scene.request_missing",
      "Scene stage input is missing request data", input.source_id, {}));
    return result;
  }

  const auto& scene = *impl_->source.scene;
  const auto& request = *input.request;

  const auto bake = BuildFbxBakePlan(scene, request.options.coordinate);
  const auto& bake_plan = bake.plan;
  const auto basis = OxygenBasis(scene);
  std::unordered_map<const ufbx_node*, size_t> source_node_indices;
  source_node_indices.reserve(scene.nodes.count);
  for (size_t index = 0; index < scene.nodes.count; ++index) {
    source_node_indices.emplace(ItemAt(scene.nodes, index), index);
  }

  if (!input.geometry_keys.empty()
    && input.geometry_keys.size() < bake_plan.variants.size()) {
    diagnostics.push_back(MakeErrorDiagnostic("scene.geometry_key_missing",
      "Geometry key count does not match mesh count", input.source_id, {}));
  }

  std::vector<NodeInput> nodes;
  nodes.reserve(scene.nodes.count > 0 ? scene.nodes.count : 1U);

  auto traverse = [&](auto&& self, const ufbx_node* node, uint32_t parent,
                    uint32_t& ordinal, const glm::mat4& parent_world) -> void {
    if (node == nullptr || input.stop_token.stop_requested()) {
      return;
    }
    // A LOD level is part of its group's geometry, shown by the group's host.
    if (bake.lods.member.at(source_node_indices.at(node)) != 0) {
      return;
    }

    CHECK_F(input.naming_service != nullptr, "NamingService must not be null");

    const auto authored = ToStringView(node->name);
    const NamingContext node_context {
      .kind = ImportNameKind::kSceneNode,
      .ordinal = ordinal,
    };
    const auto base_name
      = input.naming_service->MakeUniqueName(authored, node_context);

    auto local_matrix = LocalTransform(*node, basis);
    const auto variant_index
      = bake_plan.node_variant.at(source_node_indices.at(node));
    if (variant_index < bake_plan.variants.size()
      && bake_plan.variants.at(variant_index).transform.has_value()) {
      local_matrix = glm::mat4(1.0F);
    }
    const auto world_matrix = parent_world * local_matrix;

    NodeInput node_input;
    node_input.authored_name = std::string(authored);
    node_input.base_name = base_name;
    node_input.parent_index = parent;
    node_input.local_matrix = local_matrix;
    node_input.world_matrix = world_matrix;
    node_input.visible = node->visible;
    node_input.has_camera = node->camera != nullptr;
    node_input.has_light = node->light != nullptr;
    node_input.has_renderable = false;
    node_input.source_node = node;

    // A node with a mesh, or a LOD group's host, shows its geometry.
    if (variant_index < input.geometry_keys.size()) {
      node_input.has_renderable = true;
    }

    const auto index = static_cast<uint32_t>(nodes.size());
    if (index == 0) {
      node_input.parent_index = 0;
    }

    nodes.push_back(std::move(node_input));

    ++ordinal;

    for (size_t i = 0; i < node->children.count; ++i) {
      self(self, ItemAt(node->children, i), index, ordinal, world_matrix);
    }
  };

  uint32_t ordinal = 0;
  if (scene.root_node != nullptr) {
    traverse(traverse, scene.root_node, 0, ordinal, glm::mat4 { 1.0F });
  }

  if (nodes.empty()) {
    NodeInput root;
    root.authored_name = "root";
    root.base_name = "root";
    root.parent_index = 0;
    root.local_matrix = glm::mat4 { 1.0F };
    root.world_matrix = glm::mat4 { 1.0F };
    root.visible = true;
    nodes.push_back(std::move(root));
  }

  std::vector<uint32_t> kept_indices;
  kept_indices.reserve(nodes.size());

  if (request.options.node_pruning == NodePruningPolicy::kDropEmptyNodes) {
    for (uint32_t i = 0; i < nodes.size(); ++i) {
      const auto& node = nodes.at(i);
      if (node.has_renderable || node.has_camera || node.has_light) {
        kept_indices.push_back(i);
      }
    }
  } else {
    for (uint32_t i = 0; i < nodes.size(); ++i) {
      kept_indices.push_back(i);
    }
  }

  if (kept_indices.empty()) {
    NodeInput root;
    root.authored_name = "root";
    root.base_name = "root";
    root.parent_index = 0;
    root.local_matrix = glm::mat4 { 1.0F };
    root.world_matrix = glm::mat4 { 1.0F };
    root.visible = true;
    nodes.clear();
    nodes.push_back(std::move(root));
    kept_indices.push_back(0);
  }

  std::vector old_to_new(nodes.size(), -1);
  for (uint32_t new_index = 0; new_index < kept_indices.size(); ++new_index) {
    old_to_new.at(kept_indices.at(new_index)) = static_cast<int32_t>(new_index);
  }

  std::vector<NodeInput> pruned_nodes;
  pruned_nodes.reserve(kept_indices.size());

  for (uint32_t new_index = 0; new_index < kept_indices.size(); ++new_index) {
    const auto old_index = kept_indices.at(new_index);
    auto node = nodes.at(old_index);

    uint32_t parent = node.parent_index;
    while (parent < nodes.size() && old_to_new.at(parent) < 0) {
      const auto next_parent = nodes.at(parent).parent_index;
      if (next_parent == parent) {
        break;
      }
      parent = next_parent;
    }

    uint32_t new_parent_index = new_index;
    if (parent < nodes.size() && old_to_new.at(parent) >= 0) {
      new_parent_index = static_cast<uint32_t>(old_to_new.at(parent));
    }

    // A retained parent needs only index remapping, never transform
    // reparenting.
    if (new_parent_index != new_index && parent != node.parent_index) {
      const auto parent_old_index = kept_indices.at(new_parent_index);
      const auto& parent_world = nodes.at(parent_old_index).world_matrix;

      glm::vec3 parent_translation {};
      glm::vec3 parent_scale { 1.0F, 1.0F, 1.0F };
      glm::quat parent_rotation {};
      const bool parent_decomposed = transforms::TryDecomposeTransform(
        parent_world, parent_translation, parent_rotation, parent_scale);
      const bool can_reparent = parent_decomposed
        && transforms::IsUniformScale(parent_scale)
        && transforms::IsIdentityRotation(parent_rotation);

      if (!can_reparent) {
        diagnostics.push_back(
          MakeWarningDiagnostic("scene.pruning.reparent_skipped",
            "Skipped reparenting due to non-uniform or rotated parent; "
            "preserving world transform",
            input.source_id, node.base_name));
        new_parent_index = new_index;
        node.local_matrix = node.world_matrix;
      } else {
        const auto det = glm::determinant(parent_world);
        constexpr auto kSingularDeterminant = 1e-6F;
        if (std::abs(det) > kSingularDeterminant) {
          node.local_matrix = glm::inverse(parent_world) * node.world_matrix;
        } else {
          diagnostics.push_back(MakeErrorDiagnostic("scene.pruning.singular",
            "Node pruning failed due to singular parent transform",
            input.source_id, node.base_name));
        }
      }
    }

    node.parent_index = new_parent_index;
    pruned_nodes.push_back(std::move(node));
  }

  SceneBuild build;
  build.flags = data::pak::world::SceneAssetFlags::kModelScene;
  build.nodes.reserve(pruned_nodes.size());

  const auto scene_name = request.GetSceneName();
  const auto virtual_path = internal::PlaceModelProduct(
    request, scene_name, data::kSceneDescriptorExtension)
                              .virtual_path;

  // A camera or light keeps the frame ufbx gave it. Its node's transform is
  // in Oxygen axes for geometry rotated by the basis (LocalTransform), so the
  // component sits on a child whose local transform is the basis, as in the
  // glTF adapter; the owner's mesh and children keep the owner's frame.
  std::vector<NodeRecord> component_attachments;
  const auto component_node
    = [&](const NodeInput& owner, const uint32_t owner_index,
        const std::string_view role) -> uint32_t {
    if (basis == glm::mat4 { 1.0F }) {
      return owner_index;
    }
    const auto index = static_cast<uint32_t>(
      pruned_nodes.size() + component_attachments.size());
    const NamingContext context {
      .kind = ImportNameKind::kSceneNode,
      .ordinal = index,
    };
    const auto name = input.naming_service->MakeUniqueName(
      owner.base_name + "_" + std::string(role), context);
    const auto rotation = glm::quat_cast(basis);
    NodeRecord attachment {};
    attachment.node_id = MakeNodeKey(std::string(virtual_path) + "/" + name);
    attachment.scene_name_offset = build.strings.Add(name);
    attachment.parent_index = owner_index;
    // Visibility and shadow edits on the owner still govern its component.
    attachment.inherited_flags = data::pak::world::kSceneNodeFlags_Inheritable;
    attachment.rotation[0] = rotation.x;
    attachment.rotation[1] = rotation.y;
    attachment.rotation[2] = rotation.z;
    attachment.rotation[3] = rotation.w;
    attachment.scale[0] = 1.0F;
    attachment.scale[1] = 1.0F;
    attachment.scale[2] = 1.0F;
    component_attachments.push_back(attachment);
    return index;
  };

  for (uint32_t i = 0; i < pruned_nodes.size(); ++i) {
    auto& node = pruned_nodes.at(i);
    const auto& name = node.base_name;

    glm::vec3 translation {};
    glm::vec3 scale { 1.0F, 1.0F, 1.0F };
    glm::quat rotation {};
    const bool used_fallback = transforms::DecomposeTransformOrFallback(
      node.local_matrix, translation, rotation, scale);
    if (used_fallback) {
      diagnostics.push_back(MakeWarningDiagnostic("scene.transform_sanitized",
        "Node '" + name
          + "' transform sanitized: non-finite values reset to identity TRS; "
            "invalid rotation set to identity.",
        input.source_id, name));
    }

    NodeRecord rec {};
    rec.node_id = MakeNodeKey(std::string(virtual_path) + "/" + name);
    rec.scene_name_offset = build.strings.Add(name);
    rec.parent_index = node.parent_index;
    rec.node_flags = detail::BuildImportedNodeFlags(
      node.visible, node.has_renderable, node.has_light);
    rec.translation[0] = translation.x;
    rec.translation[1] = translation.y;
    rec.translation[2] = translation.z;
    rec.rotation[0] = rotation.x;
    rec.rotation[1] = rotation.y;
    rec.rotation[2] = rotation.z;
    rec.rotation[3] = rotation.w;
    rec.scale[0] = scale.x;
    rec.scale[1] = scale.y;
    rec.scale[2] = scale.z;
    build.nodes.push_back(rec);

    const auto* ufbx_node = static_cast<const ::ufbx_node*>(node.source_node);
    if (ufbx_node != nullptr) {
      const auto variant_index
        = bake_plan.node_variant.at(source_node_indices.at(ufbx_node));
      if (variant_index < input.geometry_keys.size()) {
        build.renderables.push_back(RenderableRecord {
          .node_index = i,
          .geometry_key
          = oxygen::base::CheckedAt(input.geometry_keys, variant_index),
        });
      }
    }

    if (ufbx_node != nullptr && ufbx_node->camera != nullptr) {
      const auto& cam = *ufbx_node->camera;
      const auto camera_node
        = cam.projection_mode == UFBX_PROJECTION_MODE_PERSPECTIVE
          || cam.projection_mode == UFBX_PROJECTION_MODE_ORTHOGRAPHIC
        ? component_node(node, i, "Camera")
        : i;
      auto commit = detail::ImportedPropertyCommit {};
      if (cam.projection_mode == UFBX_PROJECTION_MODE_PERSPECTIVE) {
        namespace camera_schema = data::schema::perspective_camera;
        auto camera = MakeDefaultRecord<PerspectiveCameraRecord>();
        camera.node_index = camera_node;
        camera.fov_y = commit("fov_y", camera_schema::kFieldOfViewYRadiansRange,
          static_cast<float>(cam.field_of_view_deg.y)
            * (std::numbers::pi_v<float> / 180.0F),
          camera.fov_y);
        camera.aspect_ratio
          = commit("aspect_ratio", camera_schema::kAspectRatioRange,
            static_cast<float>(cam.aspect_ratio), camera.aspect_ratio);
        camera.near_plane = commit("near_plane", camera_schema::kNearPlaneRange,
          static_cast<float>(cam.near_plane), camera.near_plane);
        camera.far_plane = commit("far_plane", camera_schema::kFarPlaneRange,
          static_cast<float>(cam.far_plane), camera.far_plane);
        camera.aspect_mode = cam.aspect_mode == UFBX_ASPECT_MODE_WINDOW_SIZE
          ? CameraAspectMode::kAuto
          : CameraAspectMode::kFixed;
        build.perspective_cameras.push_back(camera);
      } else if (cam.projection_mode == UFBX_PROJECTION_MODE_ORTHOGRAPHIC) {
        namespace camera_schema = data::schema::orthographic_camera;
        // FBX stores orthographic clip distances unsigned and unordered.
        auto near_plane = std::abs(cam.near_plane);
        auto far_plane = std::abs(cam.far_plane);
        if (far_plane < near_plane) {
          std::swap(far_plane, near_plane);
        }
        // The volume is the schema's half height and aspect ratio.
        const auto size_x = static_cast<float>(cam.orthographic_size.x);
        const auto size_y = static_cast<float>(cam.orthographic_size.y);
        const auto half_height
          = commit("orthographic_size", camera_schema::kOrthographicSizeRange,
            size_y * 0.5F, camera_schema::kOrthographicSizeDefault);
        const auto half_width = half_height
          * commit("aspect_ratio", camera_schema::kAspectRatioRange,
            size_x / size_y, camera_schema::kAspectRatioDefault);

        auto camera = MakeDefaultRecord<OrthographicCameraRecord>();
        camera.node_index = camera_node;
        camera.left = -half_width;
        camera.right = half_width;
        camera.bottom = -half_height;
        camera.top = half_height;
        camera.near_plane = commit("near_plane", camera_schema::kNearPlaneRange,
          near_plane, camera.near_plane);
        camera.far_plane = commit("far_plane", camera_schema::kFarPlaneRange,
          far_plane, camera.far_plane);
        camera.aspect_mode = CameraAspectMode::kFixed;
        build.orthographic_cameras.push_back(camera);
      } else {
        diagnostics.push_back(MakeWarningDiagnostic("scene.camera.unsupported",
          "Unsupported camera projection type", input.source_id, name));
      }
      commit.Report(input.source_id, name, diagnostics);
    }

    if (ufbx_node != nullptr && ufbx_node->light != nullptr) {
      const auto node_light_semantics = ReadImportedLightSemantics(
        ufbx_node != nullptr ? &ufbx_node->props : nullptr);
      const auto& light = *ufbx_node->light;
      const auto light_semantics = ReadImportedLightSemantics(&light.props);
      // A point light has no direction to keep.
      const auto light_node
        = light.type == UFBX_LIGHT_DIRECTIONAL || light.type == UFBX_LIGHT_SPOT
        ? component_node(node, i, "Light")
        : i;
      const auto resolve_common
        = [&](const data::pak::world::LightCommonRecord& defaults)
        -> data::pak::world::LightCommonRecord {
        return detail::ResolveImportedLightCommon(defaults,
          node_light_semantics, light_semantics,
          /*default_affects_world=*/light.cast_light,
          /*default_casts_shadows=*/light.cast_shadows);
      };
      auto commit = detail::ImportedPropertyCommit {};
      const auto color = std::array {
        static_cast<float>(light.color.x),
        static_cast<float>(light.color.y),
        static_cast<float>(light.color.z),
      };
      const auto intensity = static_cast<float>(light.intensity);
      switch (light.type) {
      case UFBX_LIGHT_DIRECTIONAL: {
        namespace light_schema = data::schema::directional_light;
        auto rec_light = MakeDefaultRecord<DirectionalLightRecord>();
        rec_light.node_index = light_node;
        rec_light.common = resolve_common(rec_light.common);
        detail::CommitImportedLightColor(
          commit, data::kDirectionalLightCommonRanges, color, rec_light.common);
        rec_light.intensity_lux = commit("intensity_lux",
          light_schema::kIntensityLuxRange, intensity, rec_light.intensity_lux);
        build.directional_lights.push_back(rec_light);
        break;
      }
      case UFBX_LIGHT_POINT:
      case UFBX_LIGHT_AREA:
      case UFBX_LIGHT_VOLUME: {
        namespace light_schema = data::schema::point_light;
        auto rec_light = MakeDefaultRecord<PointLightRecord>();
        rec_light.node_index = light_node;
        rec_light.common = resolve_common(rec_light.common);
        detail::CommitImportedLightColor(
          commit, data::kPointLightCommonRanges, color, rec_light.common);
        rec_light.luminous_flux_lm
          = commit("luminous_flux_lm", light_schema::kLuminousFluxLmRange,
            FbxIntensityToLumens(intensity), rec_light.luminous_flux_lm);
        build.point_lights.push_back(rec_light);
        if (light.type != UFBX_LIGHT_POINT) {
          diagnostics.push_back(
            MakeWarningDiagnostic("fbx.light.unsupported_type",
              "Unsupported FBX light type converted to point light",
              input.source_id, name));
        }
        break;
      }
      case UFBX_LIGHT_SPOT: {
        namespace light_schema = data::schema::spot_light;
        auto rec_light = MakeDefaultRecord<SpotLightRecord>();
        rec_light.node_index = light_node;
        rec_light.common = resolve_common(rec_light.common);
        detail::CommitImportedLightColor(
          commit, data::kSpotLightCommonRanges, color, rec_light.common);
        rec_light.luminous_flux_lm
          = commit("luminous_flux_lm", light_schema::kLuminousFluxLmRange,
            FbxIntensityToLumens(intensity), rec_light.luminous_flux_lm);
        // FBX spot angles are full cone angles in degrees; the record holds
        // the angle from the axis in radians.
        constexpr auto kHalfConeRadiansPerDegree
          = std::numbers::pi_v<float> / 360.0F;
        rec_light.inner_cone_angle_radians = commit("inner_cone_angle_radians",
          light_schema::kInnerConeAngleRadiansRange,
          static_cast<float>(light.inner_angle) * kHalfConeRadiansPerDegree,
          rec_light.inner_cone_angle_radians);
        rec_light.outer_cone_angle_radians = commit("outer_cone_angle_radians",
          light_schema::kOuterConeAngleRadiansRange,
          static_cast<float>(light.outer_angle) * kHalfConeRadiansPerDegree,
          rec_light.outer_cone_angle_radians);
        build.spot_lights.push_back(rec_light);
        break;
      }
      default:
        diagnostics.push_back(MakeWarningDiagnostic("scene.light.unsupported",
          "Unsupported light type", input.source_id, name));
        break;
      }
      commit.Report(input.source_id, name, diagnostics);
    }
  }

  build.nodes.insert(build.nodes.end(), component_attachments.begin(),
    component_attachments.end());
  result.build = std::move(build);
  result.success = true;
  return result;
}

auto FbxAdapter::BuildWorkItems(SceneWorkTag /*unused*/,
  SceneWorkItemSink& sink, const AdapterInput& input) -> WorkItemStreamResult
{
  if (!impl_->source.scene) {
    WorkItemStreamResult result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("fbx.scene.not_parsed",
      "FBX adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  WorkItemStreamResult result;
  if (input.stop_token.stop_requested()) {
    result.success = false;
    result.diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
    return result;
  }

  auto item = ScenePipeline::WorkItem::MakeWorkItem(shared_from_this(),
    BuildSceneSourceId(input.source_id_prefix, input.request), {}, {},
    input.request, input.naming_service, input.stop_token);

  if (!sink.Consume(std::move(item))) {
    return result;
  }

  ++result.emitted;
  return result;
}

} // namespace oxygen::content::import::adapters
